/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma	ident	"@(#)seg_map.c	1.80	95/07/17	SMI"
/*	From:	SVr4.0	"kernel:vm/seg_map.c	1.33"		*/

/*
 * VM - generic vnode mapping segment.
 *
 * The segmap driver is used only by the kernel to get faster (than seg_vn)
 * mappings [lower routine overhead; more persistent cache] to random
 * vnode/offsets.  Note than the kernel may (and does) use seg_vn as well.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/thread.h>

#include <vm/seg_kmem.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/rm.h>

/*
 * Private seg op routines.
 */
static void	segmap_free(struct seg *);
faultcode_t segmap_fault(struct hat *, struct seg *, caddr_t, u_int,
		    enum fault_type, enum seg_rw);
static faultcode_t segmap_faulta(struct seg *, caddr_t);
static int	segmap_checkprot(struct seg *, caddr_t, u_int, u_int);
static int	segmap_kluster(struct seg *, caddr_t, int);
static int	segmap_getprot(struct seg *, caddr_t, u_int, u_int *);
static off_t	segmap_getoffset(struct seg *, caddr_t);
static int	segmap_gettype(struct seg *, caddr_t);
static int	segmap_getvp(struct seg *, caddr_t, struct vnode **);
static void	segmap_dump(struct seg *);
static void	segmap_badop(void);

#define	SEGMAP_BADOP(t)	(t(*)())segmap_badop

static struct seg_ops segmap_ops = {
	SEGMAP_BADOP(int),	/* dup */
	SEGMAP_BADOP(int),	/* unmap */
	segmap_free,
	segmap_fault,
	segmap_faulta,
	SEGMAP_BADOP(int),	/* setprot */
	segmap_checkprot,
	segmap_kluster,
	(u_int (*)()) NULL,	/* swapout */
	SEGMAP_BADOP(int),	/* sync */
	SEGMAP_BADOP(int),	/* incore */
	SEGMAP_BADOP(int),	/* lockop */
	segmap_getprot,
	segmap_getoffset,
	segmap_gettype,
	segmap_getvp,
	SEGMAP_BADOP(int),	/* advise */
	segmap_dump,
};

/*
 * Private segmap routines.
 */
static void	segmap_unlock(struct hat *, struct seg *, caddr_t, u_int,
		    enum seg_rw, struct smap *);
static void	segmap_smapadd(struct smcolor *, struct smap *);
static void	segmap_smapsub(struct smcolor *, struct smap *);
static void	segmap_hashin(struct smcolor *, struct smap *,
		    struct vnode *, u_int);
static void	segmap_hashout(struct smcolor *, struct smap *);


/*
 * Statistics for segmap operations.
 *
 * No explicit locking to protect these stats.
 */
struct segmapcnt segmapcnt = {
	{ "fault",		KSTAT_DATA_ULONG },
	{ "faulta",		KSTAT_DATA_ULONG },
	{ "getmap",		KSTAT_DATA_ULONG },
	{ "get_use",		KSTAT_DATA_ULONG },
	{ "get_reclaim",	KSTAT_DATA_ULONG },
	{ "get_reuse",		KSTAT_DATA_ULONG },
	{ "get_unused",		KSTAT_DATA_ULONG },
	{ "get_nofree",		KSTAT_DATA_ULONG },
	{ "rel_async",		KSTAT_DATA_ULONG },
	{ "rel_write",		KSTAT_DATA_ULONG },
	{ "rel_free",		KSTAT_DATA_ULONG },
	{ "rel_abort",		KSTAT_DATA_ULONG },
	{ "rel_dontneed",	KSTAT_DATA_ULONG },
	{ "release",		KSTAT_DATA_ULONG },
	{ "pagecreate",		KSTAT_DATA_ULONG }
};

kstat_named_t *segmapcnt_ptr = (kstat_named_t *)&segmapcnt;
ulong_t segmapcnt_ndata = sizeof (segmapcnt) / sizeof (kstat_named_t);

/*
 * Return number of map pages in segment.
 */
#define	MAP_PAGES(seg)		((seg)->s_size >> MAXBSHIFT)

/*
 * Translate addr into smap number within segment.
 */
#define	MAP_PAGE(seg, addr)	(((addr) - (seg)->s_base) >> MAXBSHIFT)

/*
 * Translate addr in seg into struct smap pointer.
 */
#define	GET_SMAP(seg, addr)	\
	&(((struct segmap_data *)((seg)->s_data))->smd_sm[MAP_PAGE(seg, addr)])

/*
 * Bit in map (16 bit bitmap).
 */
#define	SMAP_BIT_MASK(bitindex)	(1 << ((bitindex) & 0xf))

static int vapgmask = 0;
static int *colors_used;

int
segmap_create(seg, argsp)
	struct seg *seg;
	void *argsp;
{
	register struct segmap_data *smd;
	register struct smap *smp;
	struct smcolor *smc;
	struct segmap_crargs *a = (struct segmap_crargs *)argsp;
	register u_int i;
	u_int hashsz;
	int first;

	ASSERT(seg->s_as && RW_WRITE_HELD(&seg->s_as->a_lock));

	if (((u_int)seg->s_base | seg->s_size) & MAXBOFFSET)
		cmn_err(CE_PANIC, "segkmap not MAXBSIZE aligned");

	i = MAP_PAGES(seg);

	smd = (struct segmap_data *)
	    kmem_zalloc(sizeof (struct segmap_data), KM_SLEEP);

	/*
	 * Allocate the smap strucutres now, for each virtual color desired
	 * a separate color structure is allocate for managing all objects
	 * of that color.  Each color has its own hash array and is locked
	 * separately.
	 */
	smd->smd_prot = a->prot;
	smd->smd_sm = (struct smap *)
	    kmem_zalloc((u_int)(sizeof (struct smap) * i), KM_SLEEP);

	if (a->vamask) {
		vapgmask = a->vamask / MAXBSIZE;
		smd->smd_ncolors = vapgmask + 1;
	}
	smd->smd_colors = (struct smcolor *)
		kmem_zalloc(smd->smd_ncolors * sizeof (struct smcolor),
			KM_SLEEP);

	/*
	 * Compute hash size rounding down to the next power of two.
	 */
	hashsz = (MAP_PAGES(seg) / smd->smd_ncolors) / SMAP_HASHAVELEN;
	hashsz = 1 << highbit(hashsz)-1;
	for (i = 0; i < smd->smd_ncolors; i++) {
		char lkname[20];

		smc = &smd->smd_colors[i];
		smc->smc_hashsz = hashsz;
		sprintf(lkname, "smc_lock_%d", i);

		mutex_init(&smc->smc_lock, lkname, MUTEX_DEFAULT, DEFAULT_WT);
		smc->smc_hash = (struct smap **)
		    kmem_zalloc(hashsz * sizeof (smc->smc_hash[0]), KM_SLEEP);
	}

	/*
	 * Link all the slots onto the freelist for the appropriate color.
	 */
	first = ((u_int)seg->s_base >> MAXBSHIFT) & vapgmask;
	for (smp = &smd->smd_sm[MAP_PAGES(seg) - 1];
	    smp >= smd->smd_sm; smp--) {
		smc = &smd->smd_colors[
			(first + (smp - smd->smd_sm)) & vapgmask];
		mutex_enter(&smc->smc_lock);
		segmap_smapadd(smc, smp);
		mutex_exit(&smc->smc_lock);
	}

	/*
	 * Keep track of which color colors are used more often.
	 */
	colors_used = kmem_zalloc((vapgmask+1) * sizeof (int), KM_SLEEP);

	seg->s_data = (void *)smd;
	seg->s_ops = &segmap_ops;
	return (0);
}

static void
segmap_free(seg)
	struct seg *seg;
{
	ASSERT(seg->s_as && RW_WRITE_HELD(&seg->s_as->a_lock));
}

/*
 * Do a F_SOFTUNLOCK call over the range requested.
 * The range must have already been F_SOFTLOCK'ed.
 */
static void
segmap_unlock(hat, seg, addr, len, rw, smp)
	struct hat *hat;
	struct seg *seg;
	caddr_t addr;
	u_int len;
	enum seg_rw rw;
	register struct smap *smp;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	register page_t *pp;
	register caddr_t adr;
	u_int off;
	register struct vnode *vp;

	vp = smp->sm_vp;
	off = smp->sm_off + ((u_int)addr & MAXBOFFSET);
	for (adr = addr; adr < addr + len; adr += PAGESIZE, off += PAGESIZE) {
		u_short bitmask;

		/*
		 * Use page_find() instead of page_lookup() to
		 * find the page since we know that it has
		 * "shared" lock.
		 */
		pp = page_find(vp, off);
		if (pp == NULL)
			cmn_err(CE_PANIC, "segmap_unlock");
		if (rw == S_WRITE) {
			if (!PP_ISMOD(pp)) {
				hat_setrefmod(pp);
			}
		} else if (rw != S_OTHER) {
			TRACE_3(TR_FAC_VM, TR_SEGMAP_FAULT,
				"segmap_fault:pp %x vp %x offset %x",
				pp, vp, off);
			if (!PP_ISREF(pp)) {
				hat_setref(pp);
			}
		}
		/*
		 * Clear bitmap, if the bit corresponding to "off" is set,
		 * since the page and translation are being unlocked.
		 */
		bitmask = SMAP_BIT_MASK((off - smp->sm_off) / PAGESIZE);
		if (smp->sm_bitmap & bitmask) {
			struct smcolor *smc;
			int color;

			color = ((u_int)adr >> MAXBSHIFT) & vapgmask;
			smc = &smd->smd_colors[color];
			mutex_enter(&smc->smc_lock);
			smp->sm_bitmap &= ~bitmask;
			mutex_exit(&smc->smc_lock);
		}
		hat_unlock(hat, seg->s_as, adr, PAGESIZE);
		page_unlock(pp);
	}
}

#define	MAXPPB	(MAXBSIZE/4096)	/* assumes minimum page size of 4k */

/*
 * This routine is called via a machine specific fault handling
 * routine.  It is also called by software routines wishing to
 * lock or unlock a range of addresses.
 */
faultcode_t
segmap_fault(hat, seg, addr, len, type, rw)
	struct hat *hat;
	struct seg *seg;
	caddr_t addr;
	u_int len;
	enum fault_type type;
	enum seg_rw rw;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	register struct smap *smp;
	register page_t *pp, **ppp;
	register struct vnode *vp;
	register u_int off;
	page_t *pl[MAXPPB + 1];
	u_int prot;
	u_int addroff;
	caddr_t adr;
	int err;
	u_int sm_off;

	segmapcnt.smc_fault.value.ul++;
	smp = GET_SMAP(seg, addr);
	vp = smp->sm_vp;
	sm_off = smp->sm_off;

	if (vp == NULL)
		return (FC_MAKE_ERR(EIO));

	addroff = (u_int)addr & MAXBOFFSET;
	if (addroff + len > MAXBSIZE)
		cmn_err(CE_PANIC, "segmap_fault length");
	off = sm_off + addroff;

	/*
	 * First handle the easy stuff
	 */
	if (type == F_SOFTUNLOCK) {
		segmap_unlock(hat, seg, addr, len, rw, smp);
		return (0);
	}

	TRACE_3(TR_FAC_VM, TR_SEGMAP_GETPAGE,
		"segmap_getpage:seg %x addr %x vp %x", seg, addr, vp);
	err = VOP_GETPAGE(vp, (offset_t)off, len, &prot, pl, MAXBSIZE,
	    seg, addr, rw, CRED());

	if (err)
		return (FC_MAKE_ERR(err));

	prot &= smd->smd_prot;

	/*
	 * Handle all pages returned in the pl[] array.
	 * This loop is coded on the assumption that if
	 * there was no error from the VOP_GETPAGE routine,
	 * that the page list returned will contain all the
	 * needed pages for the vp from [off..off + len].
	 */
	ppp = pl;
	while ((pp = *ppp++) != NULL) {
		ASSERT(pp->p_vnode == vp);
		/*
		 * Verify that the pages returned are within the range
		 * of this segmap region.  Note that it is theoretically
		 * possible for pages outside this range to be returned,
		 * but it is not very likely.  If we cannot use the
		 * page here, just release it and go on to the next one.
		 */
		if (pp->p_offset < sm_off ||
		    pp->p_offset >= sm_off + MAXBSIZE) {
			page_unlock(pp);
			continue;
		}

		adr = addr + (pp->p_offset - off);
		if (adr >= addr && adr < addr + len) {
			if (!PP_ISREF(pp)) {
				hat_setref(pp);
			}
			TRACE_3(TR_FAC_VM, TR_SEGMAP_FAULT,
				"segmap_fault:pp %x vp %x offset %x",
				pp, pp->p_vnode, pp->p_offset);
			if (type == F_SOFTLOCK) {
				/*
				 * Load up the translation keeping it
				 * locked and don't unlock the page.
				 */
				hat_memload(hat, seg->s_as, adr, pp, prot,
				    HAT_LOCK);
				continue;
			}
		}
		/*
		 * Either it was a page outside the fault range or a
		 * page inside the fault range for a non F_SOFTLOCK -
		 * load up the hat translation and release the page lock.
		 */
		hat_memload(hat, seg->s_as, adr, pp, prot, HAT_LOAD);
		page_unlock(pp);
	}
	return (0);
}

/*
 * This routine is used to start I/O on pages asynchronously.
 */
static faultcode_t
segmap_faulta(seg, addr)
	struct seg *seg;
	caddr_t addr;
{
	register struct smap *smp;
	register struct vnode *vp;
	register u_int off;
	int err;

	segmapcnt.smc_faulta.value.ul++;
	smp = GET_SMAP(seg, addr);
	vp = smp->sm_vp;
	off = smp->sm_off;

	if (vp == NULL) {
		cmn_err(CE_WARN, "segmap_faulta - no vp");
		return (FC_MAKE_ERR(EIO));
	}

	TRACE_3(TR_FAC_VM, TR_SEGMAP_GETPAGE,
		"segmap_getpage:seg %x addr %x vp %x", seg, addr, vp);
	err = VOP_GETPAGE(vp, (offset_t)(off + ((u_int)addr & MAXBOFFSET)),
	    PAGESIZE, (u_int *)NULL, (page_t **)NULL, 0,
	    seg, addr, S_READ, CRED());
	if (err)
		return (FC_MAKE_ERR(err));
	return (0);
}

/*ARGSUSED*/
static int
segmap_checkprot(seg, addr, len, prot)
	struct seg *seg;
	caddr_t addr;
	u_int len, prot;
{
	struct segmap_data *smd = (struct segmap_data *)seg->s_data;

	ASSERT(seg->s_as && RW_LOCK_HELD(&seg->s_as->a_lock));

	/*
	 * Need not acquire the segment lock since
	 * "smd_prot" is a read-only field.
	 */
	return (((smd->smd_prot & prot) != prot) ? EACCES : 0);
}

static int
segmap_getprot(seg, addr, len, protv)
	register struct seg *seg;
	register caddr_t addr;
	register u_int len, *protv;
{
	struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	u_int pgno = seg_page(seg, addr + len) - seg_page(seg, addr) + 1;

	ASSERT(seg->s_as && RW_READ_HELD(&seg->s_as->a_lock));

	if (pgno != 0) {
		do
			protv[--pgno] = smd->smd_prot;
		while (pgno != 0);
	}
	return (0);
}

/*ARGSUSED*/
static off_t
segmap_getoffset(seg, addr)
	register struct seg *seg;
	caddr_t addr;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;

	ASSERT(seg->s_as && RW_READ_HELD(&seg->s_as->a_lock));

	/* XXX - this doesn't make any sense */
	return (smd->smd_sm->sm_off);
}

/*ARGSUSED*/
static int
segmap_gettype(seg, addr)
	struct seg *seg;
	caddr_t addr;
{
	ASSERT(seg->s_as && RW_READ_HELD(&seg->s_as->a_lock));

	return (MAP_SHARED);
}

/*ARGSUSED*/
static int
segmap_getvp(seg, addr, vpp)
	register struct seg *seg;
	caddr_t addr;
	struct vnode **vpp;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;

	ASSERT(seg->s_as && RW_READ_HELD(&seg->s_as->a_lock));

	/* XXX - This doesn't make any sense */
	*vpp = smd->smd_sm->sm_vp;
	return (0);
}

/*
 * Check to see if it makes sense to do kluster/read ahead to
 * addr + delta relative to the mapping at addr.  We assume here
 * that delta is a signed PAGESIZE'd multiple (which can be negative).
 *
 * For segmap we always "approve" of this action from our standpoint.
 */
/*ARGSUSED*/
static int
segmap_kluster(seg, addr, delta)
	struct seg *seg;
	caddr_t addr;
	int delta;
{
	return (0);
}

static void
segmap_badop()
{
	cmn_err(CE_PANIC, "segmap_badop");
	/*NOTREACHED*/
}

/*
 * Special private segmap operations
 */

/*
 * Add smp to the free list on smd.  If the smp still has a vnode
 * association with it, then it is added to the end of the free list,
 * otherwise it is added to the front of the list.
 */
static void
segmap_smapadd(smc, smp)
	register struct smcolor *smc;
	register struct smap *smp;
{
	struct smap *smpfreelist;

	ASSERT(MUTEX_HELD(&smc->smc_lock));

	if (smp->sm_refcnt != 0)
		cmn_err(CE_PANIC, "segmap_smapadd");

	smpfreelist = smc->smc_free;
	if (smpfreelist == 0) {
		smp->sm_next = smp->sm_prev = smp;
	} else {
		smp->sm_next = smpfreelist;
		smp->sm_prev = smpfreelist->sm_prev;
		smpfreelist->sm_prev = smp;
		smp->sm_prev->sm_next = smp;
	}

	if (smp->sm_vp == (struct vnode *)NULL)
		smc->smc_free = smp;
	else
		smc->smc_free = smp->sm_next;

	if (smc->smc_want) {
		smc->smc_want = 0;
		cv_broadcast(&smc->smc_free_cv); /* Added to freelist */
	}
}

/*
 * Remove smp from the smd free list.  The caller is responsible
 * for deleting old mappings, if any, in effect before using it.
 */
static void
segmap_smapsub(smc, smp)
	register struct smcolor *smc;
	register struct smap *smp;
{
	struct smap *smpfreelist;

	ASSERT(MUTEX_HELD(&smc->smc_lock));

	smpfreelist = smc->smc_free;
	if (smpfreelist == smp)
		smc->smc_free = smp->sm_next;	/* go to next page */

	if (smc->smc_free == smp)
		smc->smc_free = NULL;		/* smp list is gone */
	else {
		smp->sm_prev->sm_next = smp->sm_next;
		smp->sm_next->sm_prev = smp->sm_prev;
	}
	smp->sm_prev = smp->sm_next = smp;	/* make smp a list of one */
	smp->sm_refcnt = 1;
}

static void
segmap_hashin(smc, smp, vp, off)
	register struct smcolor *smc;
	register struct smap *smp;
	struct vnode *vp;
	u_int off;
{
	register struct smap **hpp;

	ASSERT(MUTEX_HELD(&smc->smc_lock));

	/*
	 * Funniness here - we don't increment the ref count on the vnode
	 * even though we have another pointer to it here.  The reason
	 * for this is that we don't want the fact that a seg_map
	 * entry somewhere refers to a vnode to prevent the vnode
	 * itself from going away.  This is because this reference
	 * to the vnode is a "soft one".  In the case where a mapping
	 * is being used by a rdwr [or directory routine?] there already
	 * has to be a non-zero ref count on the vnode.  In the case
	 * where the vp has been freed and the the smap structure is
	 * on the free list, there are no pages in memory that can
	 * refer to the vnode.  Thus even if we reuse the same
	 * vnode/smap structure for a vnode which has the same
	 * address but represents a different object, we are ok.
	 */
	smp->sm_vp = vp;
	smp->sm_off = off;

	hpp = &smc->smc_hash[SMAP_HASHFUNC(smc, vp, off)];
	smp->sm_hash = *hpp;
	*hpp = smp;
}

static void
segmap_hashout(smc, smp)
	register struct smcolor *smc;
	register struct smap *smp;
{
	register struct smap **hpp, *hp;
	struct vnode *vp;

	ASSERT(MUTEX_HELD(&smc->smc_lock));

	vp = smp->sm_vp;
	hpp = &smc->smc_hash[SMAP_HASHFUNC(smc, vp, smp->sm_off)];
	for (;;) {
		hp = *hpp;
		if (hp == NULL)
			cmn_err(CE_PANIC, "segmap_hashout");
		if (hp == smp)
			break;
		hpp = &hp->sm_hash;
	}

	*hpp = smp->sm_hash;
	smp->sm_hash = NULL;
	smp->sm_vp = NULL;
	smp->sm_off = 0;
}

/*
 * Special public segmap operations
 */

/*
 * Create pages (without using VOP_GETPAGE) and load up tranlations to them.
 * If softlock is TRUE, then set things up so that it looks like a call
 * to segmap_fault with F_SOFTLOCK.
 *
 * All fields in the generic segment (struct seg) are considered to be
 * read-only for "segmap" even though the kernel address space (kas) may
 * not be locked, hence no lock is needed to access them.
 */
void
segmap_pagecreate(seg, addr, len, softlock)
	struct seg *seg;
	register caddr_t addr;
	u_int len;
	int softlock;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	register page_t *pp;
	register u_int off;
	register struct smcolor *smc;
	struct smap *smp;
	struct vnode *vp;
	caddr_t eaddr;
	u_int prot;

	ASSERT(seg->s_as == &kas);

	segmapcnt.smc_pagecreate.value.ul++;

	eaddr = addr + len;
	addr = (caddr_t)((u_int)addr & PAGEMASK);

	smp = GET_SMAP(seg, addr);
	vp = smp->sm_vp;
	off = smp->sm_off + ((u_int)addr & MAXBOFFSET);
	prot = smd->smd_prot;

	for (; addr < eaddr; addr += PAGESIZE, off += PAGESIZE) {
		pp = page_lookup(vp, off, SE_SHARED);
		if (pp == NULL) {
			u_short bitindex;

			if ((pp = page_create_va(vp, off,
			    PAGESIZE, PG_WAIT, seg->s_as, addr)) == NULL) {
				cmn_err(CE_PANIC,
				    "segmap_page_create page_create");
			}
			page_io_unlock(pp);

			/*
			 * Since pages created here do not contain valid
			 * data until the caller writes into them, the
			 * "exclusive" lock will not be dropped to prevent
			 * other users from accessing the page.  We also
			 * have to lock the translation to prevent a fault
			 * from occuring when the virtual address mapped by
			 * this page is written into.  This is necessary to
			 * avoid a deadlock since we haven't dropped the
			 * "exclusive" lock.
			 */
			bitindex = (off - smp->sm_off) / PAGESIZE;
			smc = &smd->smd_colors[
				((u_int)addr >> MAXBSHIFT) & vapgmask];
			mutex_enter(&smc->smc_lock);
			smp->sm_bitmap |= SMAP_BIT_MASK(bitindex);
			mutex_exit(&smc->smc_lock);

			hat_memload(seg->s_as->a_hat, seg->s_as,
			    addr, pp, prot, HAT_LOCK);
		} else {
			if (softlock) {
				hat_memload(seg->s_as->a_hat, seg->s_as,
				    addr, pp, prot, HAT_LOCK);
			} else {
				hat_memload(seg->s_as->a_hat, seg->s_as,
				    addr, pp, prot, HAT_LOAD);
				page_unlock(pp);
			}
		}
		TRACE_5(TR_FAC_VM, TR_SEGMAP_PAGECREATE,
		    "segmap_pagecreate:seg %x addr %x pp %x vp %x offset %x",
		    seg, addr, pp, vp, off);
	}
}


caddr_t
segmap_getmap(seg, vp, off)
	struct seg *seg;
	struct vnode *vp;
	u_int off;
{
	return (segmap_getmapflt(seg, vp, off, MAXBSIZE, 0, S_OTHER));
}

/*
 * segmap_getmap allocates a MAXBSIZE big slot to map the vnode vp
 * in the range <off, off + len). off doesn't need to be MAXBSIZE aligned.
 * The return address is  always MAXBSIZE aligned.
 *
 * If forcefault is nonzero and the MMU translations haven't yet been created,
 * segmap_getmap will call segmap_fault(..., F_INVAL, rw) to create them.
 */
caddr_t
segmap_getmapflt(seg, vp, off, len, forcefault, rw)
	struct seg *seg;
	struct vnode *vp;
	u_int off;
	u_int len;
	int forcefault;
	enum seg_rw rw;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	register struct smcolor *smc;
	register struct smap *smp;
	extern struct vnode *common_specvp();
	caddr_t baseaddr;
	u_int baseoff;
	int newslot;
	caddr_t vaddr;
	int color;

	ASSERT(seg->s_as == &kas);

	baseoff = off & MAXBMASK;
	if (off + len > baseoff + MAXBSIZE)
		cmn_err(CE_PANIC, "segmap_getmap bad len");

	/*
	 * If this is a block device we have to be sure to use the
	 * "common" block device vnode for the mapping.
	 */
	if (vp->v_type == VBLK)
		vp = common_specvp(vp);

	/*
	 * Function to choose a virtual address cache offset for a given
	 * mapping: f(vp, off) = coff.
	 * Given a VAC of size vacsize, and a baseaddr aligned wrt to vacsize,
	 * the address is given by f(vp, off) + baseaddr;
	 * Note the function must obey the property
	 * baseaddr + f(vp, off + n * PAGESIZE) = addr + n * PAGESIZE
	 * The original function was: f(vp, off) = off.
	 * However, this tends to overload the first cache block, as most
	 * typical usage filesystem activity tends to occur at offset 0.
	 * The new function which eliminates this problem is:
	 * f(vp, off) = (vp << 2) + off
	 * XXXXXX NOTE: map_addr() must be made to use this function as
	 * well, for the VAC consistency stuff to work.
	 */
	color = ((((u_int) vp << 2) + baseoff) >> MAXBSHIFT) & vapgmask;

	colors_used[color]++;
	smc = &smd->smd_colors[color];
	mutex_enter(&smc->smc_lock);

	segmapcnt.smc_getmap.value.ul++;

	for (smp = smc->smc_hash[SMAP_HASHFUNC(smc, vp, baseoff)];
	    smp != NULL; smp = smp->sm_hash) {
		if (smp->sm_vp == vp && smp->sm_off == baseoff)
			break;
	}


	if (smp != NULL) {
		if (vp->v_count == 0)			/* XXX - debugging */
			cmn_err(CE_WARN, "segmap_getmap vp count of zero");
		if (smp->sm_refcnt != 0) {
			segmapcnt.smc_get_use.value.ul++;
			smp->sm_refcnt++;		/* another user */

			/*
			 * Wait for old translations to be unloaded before
			 * attempting to use the slot.
			 */
			while (smp->sm_flags & SF_UNLOAD) {
				cv_wait(&smp->sm_cv, &smc->smc_lock);
			}
		} else {
			segmapcnt.smc_get_reclaim.value.ul++;
			segmap_smapsub(smc, smp);	/* reclaim */
		}
		newslot = 0;
	} else {
		/*
		 * Allocate a new slot and set it up.
		 */
		while ((smp = smc->smc_free) == NULL) {
			segmapcnt.smc_get_nofree.value.ul++;
			smc->smc_want = 1;
			cv_wait(&smc->smc_free_cv, &smc->smc_lock);
		}

		segmap_smapsub(smc, smp);

		if (smp->sm_vp != (struct vnode *)NULL) {
			/*
			 * Destroy old vnode association and unload any
			 * hardware translations to the old object.
			 */
			segmapcnt.smc_get_reuse.value.ul++;
			segmap_hashout(smc, smp);

			/*
			 * Hash the smap structure and initialize sm_flags
			 * before dropping "smd_lock".  This ensures
			 * that others users wait while the translations
			 * to the old object are being unloaded.
			 */
			segmap_hashin(smc, smp, vp, baseoff);
			smp->sm_flags = SF_UNLOAD;
			mutex_exit(&smc->smc_lock);

			hat_unload(seg->s_as, seg->s_base +
			    ((smp - smd->smd_sm) * MAXBSIZE), MAXBSIZE,
			    HAT_UNLOAD);

			mutex_enter(&smc->smc_lock);
			cv_broadcast(&smp->sm_cv);
		} else {
			segmapcnt.smc_get_unused.value.ul++;
			segmap_hashin(smc, smp, vp, baseoff);
		}
		smp->sm_flags = 0;
		newslot = 1;
	}
	baseaddr = seg->s_base + ((smp - smd->smd_sm) * MAXBSIZE);
	TRACE_4(TR_FAC_VM, TR_SEGMAP_GETMAP,
	    "segmap_getmap:seg %x addr %x vp %x offset %x",
	    seg, baseaddr, vp, baseoff);
	mutex_exit(&smc->smc_lock);

	if (smc !=
	    &smd->smd_colors[((u_int)baseaddr >> MAXBSHIFT) & vapgmask]) {
		cmn_err(CE_PANIC, "wrong color 0x%x, 0x%x", smc, baseaddr);
	}

	/*
	 * Prefault the translations
	 */
	vaddr = baseaddr + (off - baseoff);
	if (forcefault &&
	    (newslot || !hat_probe(seg->s_as->a_hat, seg->s_as, vaddr))) {
		caddr_t pgaddr = (caddr_t)((u_int)vaddr & PAGEMASK);
		(void) segmap_fault(seg->s_as->a_hat, seg, pgaddr,
		    (vaddr + len - pgaddr + PAGESIZE - 1) & PAGEMASK,
		    F_INVAL, rw);
	}

	return (baseaddr);
}

int
segmap_release(seg, addr, flags)
	struct seg *seg;
	caddr_t addr;
	u_int flags;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	register struct smcolor *smc;
	register struct smap *smp;
	int 		error;
	u_short		bitindex;
	struct as 	*as = seg->s_as;
	int		bflags = 0;
	struct vnode	*vp;
	u_int		offset;

	ASSERT(as == &kas);

	if (addr < seg->s_base || addr >= seg->s_base + seg->s_size ||
	    ((u_int)addr & MAXBOFFSET) != 0)
		cmn_err(CE_PANIC, "segmap_release addr");

	smp = GET_SMAP(seg, addr);
	TRACE_3(TR_FAC_VM, TR_SEGMAP_RELMAP,
		"segmap_relmap:seg %x addr %x refcnt %d",
		seg, addr, smp->sm_refcnt);

	smc = &smd->smd_colors[((u_int)addr >> MAXBSHIFT) & vapgmask];

	mutex_enter(&smc->smc_lock);
	/*
	 * Search the bitmap for pages, if any, created by
	 * segmap_pagecreate() if the smap refcnt is 1.
	 * Unlock translations, release the "exclusive" lock
	 * on such pages and clear the appropriate bit in the bitmap.
	 */
	if (smp->sm_refcnt == 1) {
		while (bitindex = highbit((u_long)smp->sm_bitmap)) {
			page_t *pp;
			caddr_t naddr;
			u_int off;

			bitindex -= 1;
			off = smp->sm_off + (bitindex * PAGESIZE);
			pp = page_find(smp->sm_vp, off);
			naddr = addr + (bitindex * PAGESIZE);
			smp->sm_bitmap &= ~SMAP_BIT_MASK(bitindex);
			hat_unlock(as->a_hat, as, naddr, PAGESIZE);
			page_unlock(pp);
		}
	}

	/*
	 * Need to call VOP_PUTPAGE() if any flags (except SM_DONTNEED)
	 * are set.
	 */
	if ((flags & ~SM_DONTNEED) != 0) {
		if (flags & SM_WRITE)
			segmapcnt.smc_rel_write.value.ul++;
		if (flags & SM_ASYNC) {
			bflags |= B_ASYNC;
			segmapcnt.smc_rel_async.value.ul++;
		}
		if (flags & SM_INVAL) {
			bflags |= B_INVAL;
			segmapcnt.smc_rel_abort.value.ul++;
		}
		if (smp->sm_refcnt == 1) {
			/*
			 * We only bother doing the FREE and DONTNEED flags
			 * if no one else is still referencing this mapping.
			 */
			if (flags & SM_FREE) {
				bflags |= B_FREE;
				segmapcnt.smc_rel_free.value.ul++;
			}
			if (flags & SM_DONTNEED) {
				bflags |= B_DONTNEED;
				segmapcnt.smc_rel_dontneed.value.ul++;
			}
		}
	} else {
		segmapcnt.smc_release.value.ul++;
		error = 0;
	}

	vp = smp->sm_vp;
	offset = smp->sm_off;

	if (--smp->sm_refcnt == 0) {
		if (flags & SM_INVAL) {
			/*
			 * The "smc_lock" can now be dropped since
			 * the smap entry is no longer hashed and is
			 * also not on the free list.
			 */
			segmap_hashout(smc, smp);	/* remove map info */
			mutex_exit(&smc->smc_lock);
			hat_unload(as, addr, MAXBSIZE, HAT_UNLOAD);
			mutex_enter(&smc->smc_lock);
		}
		segmap_smapadd(smc, smp);		/* add to free list */
	}
	mutex_exit(&smc->smc_lock);

	/*
	 * Now invoke VOP_PUTPAGE() if any flags (except SM_DONTNEED)
	 * are set.
	 */
	if ((flags & ~SM_DONTNEED) != 0) {
		error = VOP_PUTPAGE(vp, (offset_t)offset, MAXBSIZE,
		    bflags, CRED());
	}
	return (error);
}

void
segmap_flush(seg, vp)
	struct seg *seg;
	register struct vnode *vp;
{
	register struct segmap_data *smd = (struct segmap_data *)seg->s_data;
	register struct smcolor *smc;
	register struct smap *smp;
	register u_int	hashsz;
	register u_int	i;
	register int c;

	for (c = 0; c < smd->smd_ncolors; c++) {
		smc = &smd->smd_colors[c];
		hashsz = smc->smc_hashsz;
		mutex_enter(&smc->smc_lock);
		for (i = 0; i < hashsz; i++) {
			for (smp = smc->smc_hash[i]; smp != NULL;
				smp = smp->sm_hash) {
				if (smp->sm_vp == vp) {
					/*
					 * XXX - Don't hashout the smap entry,
					 * just unload it's translations.
					 */
					segmap_hashout(smc, smp);
					hat_unload(seg->s_as, seg->s_base +
					    ((smp - smd->smd_sm) * MAXBSIZE),
					    MAXBSIZE, HAT_UNLOAD);
				}
			}
		}
		mutex_exit(&smc->smc_lock);
	}
}

/*
 * Dump the pages belonging to this segmap segment.
 */
static void
segmap_dump(seg)
	struct seg *seg;
{
	struct segmap_data *smd;
	struct smap *smp, *smp_end;
	page_t *pp;
	u_int pfn, off;
	extern void dump_addpage(u_int);

	smd = (struct segmap_data *)seg->s_data;
	for (smp = smd->smd_sm, smp_end = smp + MAP_PAGES(segkmap);
	    smp < smp_end; smp++) {

		if (smp->sm_refcnt) {
			for (off = 0; off < MAXBSIZE; off += PAGESIZE) {
				int we_own_it = 0;

				/*
				 * If pp == NULL, the page either does
				 * not exist or is exclusively locked.
				 * So determine if it exists before
				 * searching for it.
				 */
				if ((pp = page_lookup_nowait(smp->sm_vp,
				    smp->sm_off + off, SE_SHARED)))
					we_own_it = 1;
				else if (page_exists(smp->sm_vp,
				    smp->sm_off + off))
					pp = page_find(smp->sm_vp,
					    smp->sm_off + off);

				if (pp) {
					pfn = page_pptonum(pp);
					dump_addpage(pfn);
					if (we_own_it)
						page_unlock(pp);
				}
			}
		}
	}
}
