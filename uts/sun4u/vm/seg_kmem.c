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
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1993  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident   "@(#)seg_kmem.c 1.24     95/07/21 SMI"

/*
 * VM - kernel segment routines
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/user.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/vtrace.h>
#include <sys/map.h>
#include <sys/tuneable.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

#include <vm/seg_kmem.h>
#include <vm/hat.h>
#include <vm/hat_sfmmu.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/anon.h>
#include <vm/rm.h>
#include <vm/page.h>
#include <vm/faultcode.h>

#include <sys/promif.h>
#include <sys/obpdefs.h>
#include <sys/bootconf.h>
#include <sys/memlist.h>
#include <sys/machsystm.h>

#include <sys/prom_debug.h>

/*
 * Private seg op routines.
 */
#if defined(__STDC__)

static int	segkmem_fault(struct hat *, struct seg *, caddr_t, u_int,
		    enum fault_type, enum seg_rw);
static int	segkmem_faulta(struct seg *, caddr_t);
static int	segkmem_setprot(struct seg *, caddr_t, u_int, u_int);
static int	segkmem_checkprot(struct seg *, caddr_t, u_int, u_int);
static int	segkmem_getprot(struct seg *, caddr_t, u_int, u_int *);
static off_t	segkmem_getoffset(struct seg *, caddr_t);
static int	segkmem_gettype(struct seg *, caddr_t);
static int	segkmem_getvp(struct seg *, caddr_t, struct vnode **);
static void	segkmem_dump(struct seg *);
static int	segkmem_badop();
static void 	boot_getpages(caddr_t, u_int, u_int);
void 	boot_alloc(caddr_t, u_int, u_int);

#else

static int	segkmem_fault(/* hat, seg, addr, len, type, rw */);
static int	segkmem_faulta(/* seg, addr */);
static int	segkmem_setprot(/* seg, addr, len, prot */);
static int	segkmem_checkprot(/* seg, addr, len, prot */);
static int	segkmem_getprot(/* seg, addr, len, protv */);
static off_t	segkmem_getoffset(/* seg, caddr_t */);
static int	segkmem_gettype(/* seg, addr */);
static int	segkmem_getvp(/* seg, addr, vpp */);
static void	segkmem_dump(/* struct seg * */);
static int	segkmem_badop();
static void	boot_getpages(/* base, size, align */);
void	boot_alloc(/* base, size, align */);

#endif /* __STDC__ */

struct as kas;

extern struct bootops *bootops;

/*
 * Machine specific public segments.
 */
struct seg ktextseg;
struct seg kvseg;
struct seg kdvmaseg;

int segkmem_ready = 0;

/*
 * All kmem alloc'ed kernel pages are associated
 * with a special kernel vnode.
 */
struct vnode kvp;

struct	seg_ops segkmem_ops = {
	segkmem_badop,			/* dup */
	segkmem_badop,			/* unmap */
	(void(*)())segkmem_badop,	/* free */
	segkmem_fault,
	segkmem_faulta,
	segkmem_setprot,
	segkmem_checkprot,
	segkmem_badop,			/* kluster */
	(u_int (*)())segkmem_badop,	/* swapout */
	segkmem_badop,			/* sync */
	segkmem_badop,			/* incore */
	segkmem_badop,			/* lockop */
	segkmem_getprot,
	segkmem_getoffset,
	segkmem_gettype,
	segkmem_getvp,
	segkmem_badop,			/* advise */
	segkmem_dump,
};

/*
 * XXX Eventually these should become hat_reserve and hat_getprot
 * respectively
 */
extern void sfmmu_reserve(struct as *, caddr_t, u_int, uint);
extern u_int sfmmu_getprot(struct hat *, struct as *, caddr_t);
extern int pf_is_memory(u_int);

/*
 * segkmem_create in fusion does not support maintaining an array of
 * ptes so it expects all callers to have argsp be NULL.
 */
int
segkmem_create(seg, argsp)
	struct seg *seg;
	caddr_t argsp;
{
	ASSERT(argsp == NULL);
	ASSERT(seg->s_as == &kas && RW_WRITE_HELD(&seg->s_as->a_lock));
	seg->s_ops = &segkmem_ops;
	seg->s_data = NULL;
	kas.a_size += seg->s_size;
	return (0);
}

/*ARGSUSED*/
static faultcode_t
segkmem_fault(hat, seg, addr, len, type, rw)
	struct hat *hat;
	struct seg *seg;
	caddr_t addr;
	u_int len;
	enum fault_type type;
	enum seg_rw rw;
{
	register faultcode_t retval;

	ASSERT(seg->s_as && RW_READ_HELD(&seg->s_as->a_lock));

	/*
	 * For now the only `faults' supported by this driver are
	 * F_SOFTLOCK/F_SOFTUNLOCK for the S_READ/S_WRITE case (caused
	 * during physio by RFS servers) and F_SOFTLOCK for the
	 * S_OTHER case used during UNIX startup.
	 *
	 * These types of faults are used to denote "lock down already
	 * loaded translations".
	 */
	switch (type) {
	case F_SOFTLOCK:
		if (rw == S_READ || rw == S_WRITE)
			retval = 0;
		else if (rw == S_OTHER) {
			/*
			 * XXX FIXME  Eventually this should go away or
			 * within an ifdef DEBUG.
			 */
			extern void sfmmu_reserve_check();

			sfmmu_reserve_check(seg->s_as, addr, len);
			retval = 0;
		} else
			retval = FC_NOSUPPORT;
		break;
	case F_SOFTUNLOCK:
		if (rw == S_READ || rw == S_WRITE)
			retval = 0;
		else
			retval = FC_NOSUPPORT;
		break;
	default:
		retval = FC_NOSUPPORT;
		break;
	}
	return (retval);
}

/*ARGSUSED*/
static faultcode_t
segkmem_faulta(seg, addr)
	struct seg *seg;
	caddr_t addr;
{
	return (FC_NOSUPPORT);
}

static int
segkmem_setprot(seg, addr, len, prot)
	struct seg *seg;
	caddr_t addr;
	u_int len, prot;
{
	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(RW_READ_HELD(&seg->s_as->a_lock));

	if (addr < seg->s_base || (addr + len) > (seg->s_base + seg->s_size))
		cmn_err(CE_PANIC, "segkmem_setprot -- out of segment");

	if (prot == 0) {
		hat_unload(seg->s_as, addr, len, HAT_UNLOAD);
	} else {
		hat_chgprot(seg->s_as, addr, len, prot);
	}
	return (0);
}

static int
segkmem_checkprot(seg, addr, len, prot)
	register struct seg *seg;
	register caddr_t addr;
	u_int len;
	register u_int prot;
{
	caddr_t eaddr;

	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(RW_READ_HELD(&seg->s_as->a_lock));

	/* DEBUG */
	if (addr < seg->s_base || (addr + len) > (seg->s_base + seg->s_size))
		cmn_err(CE_PANIC, "segkmem_checkprot");
	/* End DEBUG */

	for (eaddr = addr + len; addr < eaddr; addr += PAGESIZE) {
		/*
		 * XXX Eventually this should become hat_getprot
		 */
		if (sfmmu_getprot(kas.a_hat, seg->s_as, addr) != prot)
			return (EACCES);
	}
	return (0);
}

static int
segkmem_getprot(seg, addr, len, protv)
	register struct seg *seg;
	register caddr_t addr;
	register u_int len, *protv;
{
	u_int pgno = seg_page(seg, addr + len) - seg_page(seg, addr) + 1;
	register int i;

	ASSERT(seg->s_as == &kas);

	if (seg->s_data == NULL)
		return (EACCES);


	for (i = 0; i < pgno; i++, addr += PAGESIZE) {
		/*
		 * XXX Eventually this should become hat_getprot
		 */
		protv[i] = sfmmu_getprot(kas.a_hat, seg->s_as, addr);
	}
	return (0);
}

/*ARGSUSED*/
static off_t
segkmem_getoffset(seg, addr)
	struct seg *seg;
	caddr_t addr;
{
	return (0);
}

/*ARGSUSED*/
static int
segkmem_gettype(seg, addr)
	struct seg *seg;
	caddr_t addr;
{
	return (MAP_SHARED);
}

/*ARGSUSED*/
static int
segkmem_getvp(seg, addr, vpp)
	register struct seg *seg;
	caddr_t addr;
	struct vnode **vpp;
{
	*vpp = NULL;
	return (-1);
}

static int
segkmem_badop()
{
	cmn_err(CE_PANIC, "segkmem_badop");
	/*NOTREACHED*/

	/*
	 * return for lint
	 */
	return (0);
}

/*
 * Special public segkmem routines.
 */

/*
 * Allocate physical pages for the given kernel virtual address.
 * Performs most of the work of the old memall/vmaccess combination.
 */
int
segkmem_alloc(seg, addr, len, canwait)
	struct seg *seg;
	caddr_t addr;
	u_int len;
	int canwait;
{
	page_t *pp;
	register int val = 0;		/* assume failure */
	struct as *as = seg->s_as;

	ASSERT(as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & PAGEOFFSET) == 0);

	PRM_INFO2("segkmem_alloc addr %x len %x\n", addr, len);

	pp = page_create_va(&kvp, (off_t)addr, len,
		PG_EXCL | ((canwait) ? PG_WAIT : 0), seg->s_as, addr);
	if (pp != (page_t *)NULL) {
		page_t *ppcur;

		TRACE_5(TR_FAC_VM, TR_SEGKMEM_ALLOC,
			"segkmem_alloc:seg %x addr %x pp %x kvp %x off %x",
			seg, addr, pp, &kvp, (u_long) addr & (PAGESIZE-1));

		while (pp != (page_t *)NULL) {
			ppcur = pp;
			page_sub(&pp, ppcur);
			ASSERT(se_assert(&ppcur->p_selock) &&
			    page_iolock_assert(ppcur));
			page_io_unlock(ppcur);
			page_downgrade(ppcur);

			hat_memload(as->a_hat, as, (caddr_t)ppcur->p_offset,
				ppcur, PROT_ALL & ~PROT_USER,
				HAT_LOCK | HAT_NOSYNCLOAD);
		}
		val = 1;		/* success */
	}
	return (val);
}

/*
 * This routine works like segkem_alloc() except XXX
 */
int
segkmem_alloc_le(seg, addr, len, canwait)
	struct seg *seg;
	caddr_t addr;
	u_int len;
	int canwait;
{
	page_t *pp;
	register int val = 0;		/* assume failure */
	struct as *as = seg->s_as;

	ASSERT(as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & PAGEOFFSET) == 0);

	pp = page_create_va(&kvp, (off_t)addr, len,
		PG_EXCL | ((canwait) ? PG_WAIT : 0), seg->s_as, addr);
	if (pp != (page_t *)NULL) {
		page_t *ppcur;

		while (pp != (page_t *)NULL) {
			ppcur = pp;
			page_sub(&pp, ppcur);
			ASSERT(se_assert(&ppcur->p_selock) &&
			    page_iolock_assert(ppcur));
			page_io_unlock(ppcur);
			page_downgrade(ppcur);

			hat_memload(as->a_hat, as, (caddr_t)ppcur->p_offset,
				ppcur, PROT_ALL & ~PROT_USER,
				HAT_LOCK | HAT_NOSYNCLOAD | HAT_STRUCTURE_LE);
		}
		val = 1;		/* success */
	}
	return (val);
}


/*ARGSUSED*/
void
segkmem_free(seg, addr, len)
	register struct seg *seg;
	caddr_t addr;
	u_int len;
{
	page_t *pp;
	struct as *as = seg->s_as;
	caddr_t eaddr;

	ASSERT(as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & PAGEOFFSET) == 0);

	for (eaddr = addr + len; addr < eaddr; addr += PAGESIZE) {
		/*
		 * Use page_find() instead of page_lookup() to
		 * find the page since we know that it is hashed
		 * and has an "exclusive" lock.
		 */
		pp = page_find(&kvp, (off_t)addr);
		if (pp == NULL)
			cmn_err(CE_PANIC, "segkmem_free");

		if (! page_tryupgrade(pp)) {
			/*
			 * Other thread has it locked shared too. Most
			 * likely is some /dev/mem reader threads.
			 */
			page_unlock(pp);

			/* We should be the only one to free this page! */
			pp = page_lookup(&kvp, (off_t)addr, SE_EXCL);
			if (pp == NULL) {
				cmn_err(CE_PANIC, "segkmem_free: page freed");
			}
		}

		hat_unload(as, addr, PAGESIZE, HAT_UNLOCKUNLOAD);

		/*
		 * Destroy identity of the page and put it
		 * back on the free list.
		 */
		/*LINTED: constant in conditional context*/
		VN_DISPOSE(pp, B_INVAL, 0, kcred);
	}
}

/*
 * segkmem_mapin() and segkmem_mapout() are for manipulating kernel
 * addresses only.  Since some users of segkmem_mapin() forget to unmap,
 * this is done implicitly.
 * NOTE: addr and len must always be multiples of the mmu page size.
 * Also, this routine cannot be used to set invalid translations.
 */
void
segkmem_mapin(seg, addr, len, vprot, pcookie, flags)
	struct seg *seg;
	register caddr_t addr;
	register u_int len;
	u_int vprot;
	u_int pcookie;	/* Actually the physical page number */
	int flags;
{
	caddr_t eaddr;
	page_t *pp;
	int pfn;

	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & MMU_PAGEOFFSET) == 0);

	if (vprot == PROT_NONE)
		cmn_err(CE_PANIC, "segkmem_mapin -- invalid ptes");

	pfn = pcookie;
	for (eaddr = addr + len; addr < eaddr; addr += PAGESIZE, pfn++) {

		/* implicit unlock/unload */
		hat_unload(&kas, addr, PAGESIZE, HAT_UNLOCKUNLOAD);

		/*
		 * Determine the page frame we're mapping to allow
		 * the translation layer to manage cache consistency.
		 * If we're replacing a valid mapping, then ask the
		 * translation layer to unload the mapping and update
		 * data structures (necessary to maintain the information
		 * for cache consistency).  We use page_numtopp_nowait here
		 * instead of page_numtopp to avoid a potential deadlock if
		 * the page is "exclusively locked".
		 */
		if (pf_is_memory(pfn) && !(flags & HAT_NOCONSIST)) {
			/*
			 * check for the need to attempt to acquire the
			 * "shared" lock on the page
			 */
			pp = page_numtopp_nolock(pfn);
			ASSERT(pp == NULL || se_assert(&pp->p_selock) ||
			    panicstr);
		} else {
			pp = (page_t *)NULL;
			flags |= HAT_NOCONSIST;
		}

		hat_devload(kas.a_hat, &kas, addr, pp, pfn, vprot,
			flags | HAT_LOCK);

	}
}

/*
 * Release mapping for the kernel. This quite possibly frees needed mmu
 * resources.  The segment must be backed by software ptes. The pages
 * specified are only freed if they are valid. This allows callers to
 * clear out virtual space without knowing if it's mapped or not.
 * NOTE: addr and len must always be multiples of the page size.
 */
void
segkmem_mapout(seg, addr, len)
	struct seg *seg;
	register caddr_t addr;
	register u_int len;
{
	struct as *as = seg->s_as;

	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & PAGEOFFSET) == 0);
	ASSERT((len & PAGEOFFSET) == 0);

	/* should this be an assert */
	if (as != &kas)
		cmn_err(CE_PANIC, "segkmem_mapout: bad as");

	while (len) {
		if (hat_probe(as->a_hat, as, addr)) {
			hat_unlock(as->a_hat, as, addr, PAGESIZE);
			hat_unload(as, addr, PAGESIZE, HAT_UNLOAD);
		}
		addr += PAGESIZE;
		len -= PAGESIZE;
	}
}

/*
 * Dump the pages belonging to this segkmem segment.
 * XXX Untested.
 */
void
segkmem_dump(struct seg *seg)
{
	caddr_t  addr, eaddr;
	u_int   pfn;
	extern void dump_addpage(u_int);

	eaddr = seg->s_base + seg->s_size;
	for (addr = seg->s_base; addr < eaddr; addr += PAGESIZE) {
		pfn = va_to_pfn(addr);
		if (pfn != (u_int)-1 && pfn <= physmax && pf_is_memory(pfn))
			dump_addpage(pfn);
	}

}


/*
 * Allocate "npages" (MMU) pages worth of system virtual address space, and
 * allocate wired-down page frames to back them.
 * If "flag" is KM_NOSLEEP, block until address space and page frames are
 * available.
 *
 * XXX - should a distinction be drawn between "rmalloc" failing and
 * "segkmem_alloc" failing?
 */
void *
kmem_getpages(npages, flag)
	int npages, flag;
{
	register u_long a;
	caddr_t base;
	u_int	nbytes = mmu_ptob(npages);

	TRACE_3(TR_FAC_KMEM, TR_KMEM_GETPAGES_START,
		"kmem_getpages_start:caller %K npages %d flag %x",
		caller(), npages, flag);

	/*
	 * Make sure that the number of pages requested isn't bigger
	 * than segkmem itself.
	 */
	if (segkmem_ready && (npages > (int)mmu_btop(kvseg.s_size)))
		cmn_err(CE_PANIC, "kmem_getpages: request too big");

	/*
	 * Allocate kernel virtual address space.
	 */
	if (flag & KM_NOSLEEP) {
		if ((a = rmalloc(kernelmap, (long)npages)) == 0) {
			TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
				"kmem_getpages_end:kmem addr %x", NULL);
			return (NULL);
		}
	} else {
		a = rmalloc_wait(kernelmap, (long)npages);
	}

	base = kmxtob(a);

	/*
	 * Allocate physical pages to back the address allocated.
	 */
	if (segkmem_ready) {
		if (segkmem_alloc(&kvseg, (caddr_t)base,
		    nbytes, !(flag & KM_NOSLEEP)) == 0) {
			rmfree(kernelmap, (size_t)npages, a);
			TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
				"kmem_getpages_end:kmem addr %x", NULL);
			return (NULL);
		}
	} else {
		boot_getpages(base, nbytes, BO_NO_ALIGN);
	}
	TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
		"kmem_getpages_end:kmem addr %x", base);

	return ((void *)base);
}


/*
 * This routine works like kmem_get_pages() except that pages are mapped
 * with a little-endian ordering.
 */
void *
kmem_getpages_le(int npages, int flag)
{
	register u_long a;
	caddr_t base;

	/*
	 * Make sure that the number of pages requested isn't bigger
	 * than segkmem itself.
	 */
	if (segkmem_ready && (npages > (int)mmu_btop(kvseg.s_size)))
		cmn_err(CE_PANIC, "kmem_getpages_le: request too big");

	/*
	 * Allocate kernel virtual address space.
	 */
	if (flag & KM_NOSLEEP)
		if ((a = rmalloc(kernelmap, (long)npages)) == 0)
			return (NULL);
	else
		a = rmalloc_wait(kernelmap, (long)npages);

	base = kmxtob(a);

	/*
	 * Allocate physical pages to back the address allocated.
	 */
	if (segkmem_ready) {
		if (segkmem_alloc_le(&kvseg, (caddr_t)base,
					(u_int)mmu_ptob(npages),
					!(flag & KM_NOSLEEP)) == 0) {
			rmfree(kernelmap, (long)npages, a);
			return (NULL);
		}
	} else
		cmn_err(CE_PANIC, "kmem_getpages_le: segkmem not ready");

	return ((void *)base);
}

/*
 * This is the memlist we'll keep around until segkmem is ready
 * and then we'll come through and free it.
 */
static struct memlist *kmem_garbage_list;

/*
 * Free "npages" (MMU) pages gotten with "kmem_getpages".
 */
void
kmem_freepages(addr, npages)
#ifdef lint
	caddr_t addr;
#else
	void *addr;
#endif
	int npages;
{
	TRACE_3(TR_FAC_KMEM, TR_KMEM_FREEPAGES_START,
		"kmem_freepages_start:caller %K addr %x npages %d",
		caller(), addr, npages);

	if (!segkmem_ready) {
		struct memlist *this;

		/*
		 * If someone attempts to free memory here we must delay
		 * the action until kvseg is ready.  So lets just put
		 * the request on a list and free it at a later time.
		 */
		this = (struct memlist *)
		    kmem_zalloc(sizeof (struct memlist), KM_NOSLEEP);
		if (!this) {
			cmn_err(CE_PANIC,
				"can't allocate kmem gc list element");
		}
		this->address = (u_int)addr;
		this->size = (u_int)ctob(npages);
		this->next = kmem_garbage_list;
		kmem_garbage_list = this;
	} else {
		/*
		 * Free the physical memory behind the pages.
		 */
		segkmem_free(&kvseg, (caddr_t)addr, (u_int)mmu_ptob(npages));

		/*
		 * Free the virtual addresses behind which they resided.
		 */
		rmfree(kernelmap, (long)npages, (u_long)btokmx((caddr_t)addr));
	}
	TRACE_0(TR_FAC_KMEM, TR_KMEM_FREEPAGES_END, "kmem_freepages_end");
}

/*
 * Collect and free all the unfreed garbage on the delayed free list
 * Called at the end of startup().
 */
void
kmem_gc(void)
{
	struct memlist	*this, *next;

	ASSERT(segkmem_ready);
#ifdef lint
	next = NULL;
#endif /* lint */

	for (this = kmem_garbage_list; this; this = next) {
		kmem_freepages((void *)this->address,
			(int)mmu_btopr(this->size));
		next = this->next;
		kmem_free(this, sizeof (struct memlist));
	}

	kmem_garbage_list = (struct memlist *)NULL;
}

/*
 * Return the total amount of virtual space available for kmem_alloc
 */
u_long
kmem_maxvirt()
{
	struct map *bp;
	u_long	amount = 0;

	mutex_enter(&maplock(kernelmap));
	for (bp = mapstart(kernelmap); bp->m_size; bp++)
		amount += bp->m_size;
	mutex_exit(&maplock(kernelmap));
	return (amount);
}

/*
 * Get pages from boot and hash them into the kernel's vp.
 * Used after the page structs have been allocated, but before
 * segkmem is ready.
 */

void
boot_getpages(caddr_t base, u_int size, u_int align)
{
	struct page *pp;
	caddr_t addr, p;
	u_int n, pfn;

	PRM_INFO2("boot_getpages addr %x len %x\n", base, size);

	addr = BOP_ALLOC(bootops, base, size, align);
	if ((addr == NULL) || (addr != base))
		cmn_err(CE_PANIC, "bop_alloc failed");
	for (p = addr, n = mmu_btop(size); n; p += MMU_PAGESIZE, n--) {
		pfn = va_to_pfn(p);
		if ((int)pfn == -1)
			cmn_err(CE_PANIC, "kmem_getpages: pfn is -1");

		pp = page_numtopp(pfn, SE_EXCL);
		if (pp == NULL || pp->p_free)
			cmn_err(CE_PANIC, "kmem_getpages: pp is NULL or free");
		(void) page_hashin(pp, &kvp, (u_int)p, NULL);
		page_downgrade(pp);
	}
}

/*
 * Allocate memory using boot, accounting for it in the kernel as well.
 * Use this after kmem_init() in startup if you need to allocate without
 * going through seg_kmem.
 */
void
boot_alloc(base, size, align)
	caddr_t base;
	u_int size, align;
{
	u_int n;

	n = size / MMU_PAGESIZE;
	mutex_enter(&freemem_lock);
	pages_pp_kernel += n;
	availrmem -= n;
	mutex_exit(&freemem_lock);
	boot_getpages(base, size, align);
}
