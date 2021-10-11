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
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1991  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident	"@(#)seg_kmem.c	1.93	95/08/08 SMI"

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
#include <sys/dumphdr.h>

#include <sys/mmu.h>
#include <sys/pte.h>

#include <vm/seg_kmem.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/anon.h>
#include <vm/rm.h>
#include <vm/page.h>

#include <vm/hat_sunm.h>

#include <sys/promif.h>
#include <sys/obpdefs.h>
#include <sys/bootconf.h>

/*
 * Private seg op routines.
 */
#if defined(__STDC__)

static faultcode_t segkmem_fault(struct hat *, struct seg *, caddr_t, u_int,
		    enum fault_type, enum seg_rw);
static faultcode_t segkmem_faulta(struct seg *, caddr_t);
static int	segkmem_setprot(struct seg *, caddr_t, u_int, u_int);
static int	segkmem_checkprot(struct seg *, caddr_t, u_int, u_int);
static int	segkmem_getprot(struct seg *, caddr_t, u_int, u_int *);
static off_t	segkmem_getoffset(struct seg *, caddr_t);
static int	segkmem_gettype(struct seg *, caddr_t);
static int	segkmem_getvp(struct seg *, caddr_t, struct vnode **);
static void	segkmem_dump(struct seg *seg);
static int	segkmem_badop();
static u_int	getprot(struct pte *);

#else

static faultcode_t segkmem_fault(/* hat, seg, addr, len, type, rw */);
static faultcode_t segkmem_faulta(/* seg, addr */);
static int	segkmem_setprot(/* seg, addr, len, prot */);
static int	segkmem_checkprot(/* seg, addr, len, prot */);
static int	segkmem_getprot(/* seg, addr, len, protv */);
static off_t	segkmem_getoffset(/* seg, caddr_t */);
static int	segkmem_gettype(/* seg, addr */);
static int	segkmem_getvp(/* seg, addr, vpp */);
static void	segkmem_dump(/* seg */);
static int	segkmem_badop();
static u_int	getprot(/* ppte */);

#endif /* __STDC__ */

struct as kas;

extern struct bootops *bootops;
/*
 * Machine specific public segments.
 */
struct seg ktextseg;
struct seg kvseg;
struct seg E_kvseg;
struct seg kdvmaseg;
int segkmem_ready = 0;

/*
 * All kmem alloc'ed kernel pages are associated
 * with a special kernel vnode.
 */
struct vnode kvp;

struct seg_ops segkmem_ops = {
	segkmem_badop,			/* dup */
	segkmem_badop,			/* unmap */
	(void (*)()) segkmem_badop,	/* free */
	segkmem_fault,
	segkmem_faulta,
	segkmem_setprot,
	segkmem_checkprot,
	segkmem_badop,			/* kluster */
	(u_int (*)()) segkmem_badop,	/* swapout */
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
 * The segkmem driver will (optional) use an array of pte's to back
 * up the mappings for compatibility reasons.  This driver treates
 * argsp as a pointer to the pte array to be used for the segment.
 */
int
segkmem_create(seg, argsp)
	struct seg *seg;
	caddr_t argsp;
{
	ASSERT(seg->s_as == &kas && RW_WRITE_HELD(&seg->s_as->a_lock));

	seg->s_ops = &segkmem_ops;
	seg->s_data = (void *)argsp;	/* actually a struct pte array */
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
	register caddr_t adr;
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
		if (rw == S_READ || rw == S_WRITE || rw == S_OTHER) {
			if (rw == S_OTHER) {
				for (adr = addr; adr < addr + len;
				    adr += PAGESIZE) {
					if (mmu_getpmg(adr) != pmgrp_invalid)
						sunm_pmgreserve(seg->s_as, adr,
							PAGESIZE);
				}
			}
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
	register struct pte *ppte;
	register caddr_t eaddr;
	register u_int pprot;

	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(RW_READ_HELD(&seg->s_as->a_lock));

	if (prot)
		pprot = sunm_vtop_prot(prot);
	if (seg->s_data) {
		ppte = (struct pte *)seg->s_data;
		ppte += btoct(addr - seg->s_base);
	} else
		ppte = (struct pte *)NULL;

	for (eaddr = addr + len; addr < eaddr; /* CSTYLED */) {
		if (prot == 0) {
			struct pmgrp *pmg = mmu_getpmg(addr);

			if ((((u_int)addr & PMGRPMASK) == (u_int)addr) &&
			    ((addr + PMGRPSIZE) <= eaddr)) {

				/* invalidate entire PMGRP */
				mmu_pmginval(addr);
				addr += PMGRPSIZE;
				continue;

			} else if (pmg != pmgrp_invalid) {

				/* invalidate PME */
				mmu_setpte(addr, mmu_pteinvalid);
			}

			/* invalidate software pte (if any) */
			if (ppte)
				*ppte++ = mmu_pteinvalid;
		} else {
			struct pte tpte;

			mmu_getpte(addr, &tpte);
			if (tpte.pg_v) {
				tpte.pg_prot = pprot;
#ifdef VAC
				/* XXX - should use hat layer */
				if (tpte.pg_type == OBMEM)
					tpte.pg_nc = 0;
#ifdef	sun4c
				{
					extern int bug1156505_enter[];
					extern int cpu_buserr_type;

					if (cpu_buserr_type == 1 &&
					    addr == (caddr_t)bug1156505_enter)
						tpte.pg_nc = 1;
				}
#endif	/* sun4c */
#endif VAC
				mmu_setpte(addr, tpte);
			}

			if (ppte)
				*ppte++ = tpte;	/* structure assignment */
		}
		addr += PAGESIZE;
	}
	return (0);
}

static u_int
getprot(ppte)
	register struct pte *ppte;
{
	register u_int pprot;

	if (!pte_valid(ppte)) {
		pprot = 0;
	} else {
		switch (ppte->pg_prot) {
		case KR:
			pprot = PROT_READ | PROT_EXEC;
			break;
		case UR:
			pprot = PROT_READ | PROT_EXEC | PROT_USER;
			break;
		case KW:
			pprot = PROT_READ | PROT_EXEC | PROT_WRITE;
			break;
		case UW:
			pprot = PROT_READ | PROT_EXEC | PROT_WRITE | PROT_USER;
			break;
		default:
			pprot = 0;
			break;
		}
	}
	return (pprot);
}

static int
segkmem_checkprot(seg, addr, len, prot)
	register struct seg *seg;
	register caddr_t addr;
	u_int len;
	register u_int prot;
{
	register struct pte *ppte;
	struct pte tpte;
	caddr_t eaddr;

	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(RW_READ_HELD(&seg->s_as->a_lock));

	if (seg->s_data) {
		ppte = (struct pte *)seg->s_data;
		ppte += btoct(addr - seg->s_base);
	} else
		ppte = (struct pte *)NULL;

	for (eaddr = addr + len; addr < eaddr; addr += PAGESIZE) {
		if (ppte) {
			tpte = *ppte++;		/* structure assignment */
		} else
			mmu_getkpte(addr, &tpte);

		if ((getprot(&tpte) & prot) != prot)
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
	register struct pte *ppte;
	register i;
	struct pte tpte;

	if (seg->s_data == NULL)
		return (EACCES);

	ppte = (struct pte *)seg->s_data;
	ppte += btoct(addr - seg->s_base);

	for (i = 0; i < pgno; i++) {
		tpte = *ppte++;
		protv[i] = getprot(&tpte);
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
	register struct pte *ppte;
	struct pte tpte;
	register int val = 0;		/* assume failure */
	struct as *as = seg->s_as;

	ASSERT(as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & PAGEOFFSET) == 0);

	pp = page_create(&kvp, (off_t)addr, len,
	    PG_EXCL | ((canwait) ? PG_WAIT : 0));
	if (pp != (page_t *)NULL) {
		page_t *ppcur;
		TRACE_5(TR_FAC_VM, TR_SEGKMEM_ALLOC,
			"segkmem_alloc:seg %x addr %x pp %x kvp %x off %x",
			seg, addr, pp, &kvp, (u_long) addr & (PAGESIZE - 1));
		if (seg->s_data)
			ppte = (struct pte *)seg->s_data;
		else
			ppte = (struct pte *)NULL;

		while (pp != (page_t *)NULL) {
			ppcur = pp;
			page_sub(&pp, ppcur);
			ASSERT(se_assert(&ppcur->p_selock) &&
			    page_iolock_assert(ppcur));
			page_io_unlock(ppcur);
			page_downgrade(ppcur);

			sunm_mempte(ppcur, PROT_ALL & ~PROT_USER, &tpte);
			sunm_pteload(as, (caddr_t)ppcur->p_offset, ppcur,
				tpte, PTELD_LOCK);	/* XXX */
			if (ppte)
				/* structure assignment */
				*(ppte + (btoct((caddr_t)ppcur->p_offset -
				    seg->s_base))) = tpte;
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
	struct pte *ppte;
	struct as *as = seg->s_as;

	ASSERT(as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & PAGEOFFSET) == 0);

	if (seg->s_data) {
		ppte = (struct pte *)seg->s_data;
		ppte += btoct(addr - seg->s_base);
	} else
		ppte = (struct pte *)NULL;

	for (; (int)len > 0; len -= PAGESIZE, addr += PAGESIZE) {
		if (ppte)
			*ppte++ = mmu_pteinvalid;	/* clear Sysmap entry */

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
			pp = page_lookup(&kvp, (off_t) addr, SE_EXCL);
			if (pp == NULL) {
				cmn_err(CE_PANIC, "segkmem_free: page freed");
			}
		}

		hat_unlock(as->a_hat, as, addr, PAGESIZE);
		hat_unload(as, addr, PAGESIZE, HAT_UNLOAD);
		/*
		 * Destroy identity of the page and put it
		 * back on the free list.
		 */
		VN_DISPOSE(pp, B_INVAL, 0, kcred);
	}
}

#ifdef notdef

/*
 * mapin() and mapout() are for manipulating kernel addresses only.
 * They are provided for backward compatibility.  They have been
 * replaced by segkmem_mapin() and segkmem_mapout().
 *
 * Drivers should use ddi_map_regs() and ddi_unmap_regs().
 */

/*
 * Map a physical address range into kernel virtual addresses.
 */
/*ARGSUSED*/
void
mapin(ppte, v, paddr, size, access)
	struct pte *ppte;
	u_int v;
	u_int paddr;
	int size, access;
{
	struct seg *seg;
	register caddr_t vaddr;
	u_int vprot;

	vaddr = (caddr_t)mmu_ptob(v);

	rw_enter(&kas.a_lock, RW_READER);
	seg = as_segat(&kas, vaddr);
	rw_exit(&kas.a_lock);
	if (seg == NULL)
		panic("mapin -- bad seg");
	if ((access & PG_V) == 0)
		panic("mapin -- invalid pte");
	switch (access & PG_PROT) {
	case PG_KR:
		vprot = PROT_READ;
		break;
	case PG_KW:
		vprot = PROT_READ | PROT_WRITE;
		break;
	case PG_UR:
		vprot = PROT_READ | PROT_USER;
		break;
	case PG_UW:
		vprot = PROT_READ | PROT_WRITE | PROT_USER;
		break;
	}
	segkmem_mapin(seg, vaddr, (u_int)mmu_ptob(size), vprot,
	    paddr & PG_PFNUM, 0);
}

/*
 * Release mapping for kernel.  This frees pmegs, which are a critical
 * resource.  ppte must be a pointer to a pte within Sysmap[].
 */
void
mapout(ppte, size)
	struct pte *ppte;
{
	caddr_t vaddr;
	extern struct pte ESysmap[];

	if (ppte < Sysmap || ppte >= ESysmap)
		panic("mapout -- bad pte");
	vaddr = kvseg.s_base + mmu_ptob(ppte - (struct pte *)kvseg.s_data);
	segkmem_mapout(&kvseg, vaddr, (u_int)mmu_ptob(size));
}

#endif /* notdef */

/*
 * segkmem_mapin() and segkmem_mapout() are for manipulating kernel
 * addresses only.  Since some users of segkmem_mapin() forget to unmap,
 * this is done implicitly.
 * XXX - NOTE: addr and len must always be multiples of the mmu page size.
 * Also, this routine cannot be used to set invalid translations.
 */
void
segkmem_mapin(seg, addr, len, vprot, pcookie, flags)
	struct seg *seg;
	register caddr_t addr;
	register u_int len;
	u_int vprot;
	u_int pcookie;
	int flags;
{
	register struct pte *ppte;
	page_t *pp;
	struct as *as = seg->s_as;
	union {
		struct pte u_pte;
		int u_ipte;
	} apte, tpte;

	ASSERT(as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & MMU_PAGEOFFSET) == 0);

	if (seg->s_data) {
		ppte = (struct pte *)seg->s_data;
		ppte += btoct(addr - seg->s_base);
	} else
		ppte = (struct pte *)NULL;

	apte.u_ipte = pcookie;
	if (vprot == PROT_NONE)
		cmn_err(CE_PANIC, "segkmem_mapin -- invalid ptes");
	apte.u_pte.pg_prot = sunm_vtop_prot(vprot);
	apte.u_pte.pg_v = 1;

	/*
	 * Always lock the mapin'd translations.
	 */
	flags |= PTELD_LOCK;

	for (; len != 0; addr += MMU_PAGESIZE, len -= MMU_PAGESIZE) {
		/*
		 * Determine the page frame we're mapping to allow
		 * the translation layer to manage cache consistency.
		 * If we're replacing a valid mapping, then ask the
		 * translation layer to unload the mapping and update
		 * data structures (necessary to maintain the information
		 * for cache consistency).  We use page_numtookpp here
		 * instead of page_numtopp so that we don't get a page
		 * struct for physical pages in transition.
		 */
		if (apte.u_pte.pg_type == OBMEM && !(flags & PTELD_NOCONSIST)) {
			/*
			 * check for the need to attempt to acquire the
			 * "shared" lock on the page
			 */
			pp = page_numtopp_nowait(apte.u_pte.pg_pfnum,
			    SE_SHARED);
			ASSERT(pp == NULL || se_assert(&pp->p_selock) ||
			    panicstr);
		} else {
			pp = (page_t *)NULL;
		}

		if ((flags & PTELD_INTREP) == 0) {
			/*
			 * Because some users of mapin don't mapout things
			 * when they are done, we check for a currently
			 * valid translation.  If we find one, then we
			 * unlock the old translation now.
			 */
			mmu_getpte(addr, &tpte.u_pte);
			if (pte_valid(&tpte.u_pte)) {
				hat_unlock(as->a_hat, as, addr, PAGESIZE);
				/*
				 * If the page is different than the one
				 * already loaded, unload it now.  We test
				 * to see if the page is different before
				 * doing this since we can blow up if this
				 * virtual page contains a page struct used
				 * by hat_unload and we are just reloading
				 * going from non-cached to cached.
				 */
				if ((tpte.u_ipte &
				    (PG_V | PG_PROT | PG_PFNUM)) !=
				    (apte.u_ipte & (PG_V | PG_PROT | PG_PFNUM)))
					hat_unload(as, addr,
						MMU_PAGESIZE, HAT_UNLOAD);
			}
		}

		sunm_pteload(as, addr, pp, apte.u_pte, flags);

		if (pp) {
			page_unlock(pp);
		}

		if (ppte != (struct pte *)NULL)
			*ppte++ = apte.u_pte;

		apte.u_pte.pg_pfnum++;
	}
}

/*
 * Release mapping for the kernel.  This frees pmegs, which are a critical
 * resource.  The segment must be backed by software ptes.  The pages
 * specified are only freed if they are valid.  This allows callers to
 * clear out virtual space without knowing if it's mapped or not.
 * XXX - NOTE: addr and len must always be multiples of the page size.
 */
void
segkmem_mapout(seg, addr, len)
	struct seg *seg;
	register caddr_t addr;
	register u_int len;
{
	register struct pte *ppte;

	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);

	/*
	 * XXX - What happens if MMU_PAGESIZE is not the
	 * same as PAGESIZE?
	 */
	ASSERT(((u_int)addr & MMU_PAGEOFFSET) == 0);

	if (seg->s_as != &kas)
		cmn_err(CE_PANIC, "segkmem_mapout: bad as");
	if (seg->s_data == NULL)
		cmn_err(CE_PANIC, "segkmem_mapout: no ptes");
	ppte = (struct pte *)(seg->s_data);
	ppte += btoct(addr - seg->s_base);
	for (; len != 0; addr += MMU_PAGESIZE, len -= MMU_PAGESIZE, ppte++) {
		if (!pte_valid(ppte))
			continue;

		ppte->pg_v = 0;
		hat_unlock(kas.a_hat, &kas, addr, PAGESIZE);
		hat_unload(&kas, addr, MMU_PAGESIZE, HAT_UNLOAD);
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
	int e = 1;

	TRACE_3(TR_FAC_KMEM, TR_KMEM_GETPAGES_START,
		"kmem_getpages_start:caller %K npages %d flag %x",
		caller(), npages, flag);

	/*
	 * Make sure that the number of pages requested isn't bigger
	 * than segkmem itself.
	 */
	if (segkmem_ready && ((npages > (int)btoct(kvseg.s_size)) ||
	    (npages > (int)btoct(E_kvseg.s_size))))
		cmn_err(CE_PANIC, "kmem_getpages: request too big");

	/*
	 * Allocate kernel virtual address space.
	 * XXX - making the ethernet's idiosyncracies visible
	 * here is debateable, so maybe this should be moved to
	 * a more machine dependent place.
	 */
	if ((a = rmalloc(ekernelmap, (long)npages)) != 0) {
		base = ekmxtob(a);
	} else if ((a = rmalloc(kernelmap, (long)npages)) != 0) {
		e = 0;
		base = kmxtob(a);
	} else if (flag & KM_NOSLEEP) {
		TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
			"kmem_getpages_end:kmem addr %x", NULL);
		return (NULL);
	} else {
		e = 1;
		a = rmalloc_wait(ekernelmap, (long)npages);
		base = ekmxtob(a);
	}

	/*
	 * Allocate physical pages to back the address allocated.
	 */
	if (segkmem_ready) {
		if (segkmem_alloc(e ? & E_kvseg : &kvseg, (caddr_t)base,
		    (u_int)mmu_ptob(npages),
		    !(flag & KM_NOSLEEP)) == 0) {
			rmfree(e ? ekernelmap : kernelmap, (size_t)npages, a);
			TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
				"kmem_getpages_end:kmem addr %x", NULL);
			return (NULL);
		}
	} else {
		struct pte tpte;
		struct page *pp;
		caddr_t addr;
		u_int n;

		/*
		 * Below we simulate segkmem before it is
		 * completely initialized by using the boot/prom
		 * memory allocator and the parts of the vm system
		 * that are operational early on.  The required parts
		 * are: initialization of locking, kernelmap,
		 * the memseg list, and the page structs.
		 * Notable parts that are not initialized:
		 * any kernel segments and the hat layer.
		 */
		addr = BOP_ALLOC(bootops, base, (u_int)mmu_ptob(npages),
				BO_NO_ALIGN);
		if ((addr == NULL) || (addr != base))
			panic("boot alloc failed");

		for (n = npages; n; addr += MMU_PAGESIZE, n--) {
			mmu_getpte(addr, &tpte);
			pp = page_numtopp(tpte.pg_pfnum, SE_EXCL);
			(void) page_hashin(pp, &kvp, (u_int)addr,
			    (kmutex_t *)NULL);
			page_downgrade(pp);
		}
	}
	TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
		"kmem_getpages_end:kmem addr %x", base);

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
		struct memlist	*this;

		/*
		 * If someone attempts to free memory here we must delay
		 * the action until kvseg is ready.  So lets just put
		 * the request on a list and free it at a later time.
		 */
		this = (struct memlist *)
		    kmem_zalloc(sizeof (struct memlist), KM_NOSLEEP);
		if (!this)
			panic("can't allocate kmem gc list element");
		this->address = (u_int)addr;
		this->size = (u_int)ctob(npages);
		this->next = kmem_garbage_list;
		kmem_garbage_list = this;
	} else {
		if ((u_int)addr >= (u_int)E_Sysbase) {
			/*
			 * Free the physical memory behind the pages.
			 */
			segkmem_free(&E_kvseg, (caddr_t)addr,
				(u_int)mmu_ptob(npages));

			/*
			 * Free the virtual addresses behind which they resided.
			 */
			rmfree(ekernelmap, (long)npages,
				(u_long)btoekmx((caddr_t)addr));
		} else {
			/*
			 * Free the physical memory behind the pages.
			 */
			segkmem_free(&kvseg, (caddr_t)addr,
				(u_int)mmu_ptob(npages));

			/*
			 * Free the virtual addresses behind which they resided.
			 */
			rmfree(kernelmap, (long)npages,
				(u_long)btokmx((caddr_t)addr));
		}
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

	/* LINTED lint thinks next may be used before set */
	for (this = kmem_garbage_list; this; this = next) {
		kmem_freepages((void *)this->address, (int)btoc(this->size));
		next = this->next;
		kmem_free(this, sizeof (struct memlist));
	}

	kmem_garbage_list = (struct memlist *) 0;
}

/*
 * Return the total amount of virtual space available for kmem_alloc
 */
u_long
kmem_maxvirt()
{
	struct map *bp;
	u_long	amount = 0;

	mutex_enter(&maplock(ekernelmap));
	for (bp = mapstart(ekernelmap); bp->m_size; bp++)
		amount += bp->m_size;
	mutex_exit(&maplock(ekernelmap));

	mutex_enter(&maplock(kernelmap));
	for (bp = mapstart(kernelmap); bp->m_size; bp++)
		amount += bp->m_size;
	mutex_exit(&maplock(kernelmap));
	return (amount);
}

/*
 * Dump the pages belonging to this segkmem segment.
 * Since some architectures have devices that live in OBMEM space above
 * physical memory, we must check the page frame number as well as the
 * type.
 * (Let's hope someone doesn't architect a system that allows devices
 * in OBMEM space *between* physical memory segments!  Or, if they do,
 * that it can be dumped just like memory.)
 */
static void
segkmem_dump(seg)
	struct seg *seg;
{
	struct pte	tpte;
	caddr_t		addr, eaddr;
	u_int		pfn;
	extern struct seg kmonseg;

	/*
	 * kmonseg maps the PROM area, whose page table entries are
	 * not guaranteed to be valid. Bypass it to avoid trying to
	 * dump garbage.
	 */
	if (seg == &kmonseg)
		return;

	for (addr = seg->s_base, eaddr = addr + seg->s_size;
	    addr < eaddr; addr += MMU_PAGESIZE) {
		mmu_getpte(addr, &tpte);
		if (pte_valid(&tpte) && (tpte.pg_type == OBMEM) &&
		    ((pfn = tpte.pg_pfnum) <= physmax))
			dump_addpage(pfn);
	}
}
