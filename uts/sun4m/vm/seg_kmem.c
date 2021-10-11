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

#pragma ident   "@(#)seg_kmem.c 1.76     95/07/21 SMI"

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

#include <sys/mmu.h>
#include <sys/iommu.h>
#include <sys/pte.h>

#include <vm/seg_kmem.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/anon.h>
#include <vm/rm.h>
#include <vm/page.h>
#include <vm/faultcode.h>
#include <vm/hat_srmmu.h>

#include <sys/promif.h>
#include <sys/obpdefs.h>
#include <sys/bootconf.h>
#include <sys/memlist.h>
#include <sys/machsystm.h>

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
static u_int	getprot(struct pte *);
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
static u_int	getprot(/* ppte */);
static void	boot_getpages(/* base, size, align */);
void	boot_alloc(/* base, size, align */);

#endif /* __STDC__ */

struct as kas;

extern int do_pg_coloring;

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
	 * For now the only `fault' supported by this driver are
	 * F_SOFTLOCK/F_SOFTUNLOCK for the S_READ/S_WRITE case (caused
	 * during physio by RFS servers).
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
					srmmu_reserve(seg->s_as, adr,
						PAGESIZE, 1);
						/* HAT_RESERVE_LOAD); */
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

/*
 * XXX - Routines which obtain information directly from the
 * MMU should acquire the hat layer lock.
 */

static int
segkmem_setprot(seg, addr, len, prot)
	struct seg *seg;
	caddr_t addr;
	u_int len, prot;
{
	register struct pte *ppte, *seg_pte;
	register caddr_t eaddr;
	struct pte new_pte;
	int level;
	struct ptbl *ptbl;
	int i;
	kmutex_t *mtx;

	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(RW_READ_HELD(&seg->s_as->a_lock));

	if (addr < seg->s_base || (addr + len) > (seg->s_base + seg->s_size))
		cmn_err(CE_PANIC, "segkmem_setprot -- out of segment");

	ppte = NULL;
	if (seg->s_data) {
		seg_pte = (struct pte *)seg->s_data;
		seg_pte += mmu_btop(addr - seg->s_base);
	} else
		seg_pte = NULL;

	if (prot == 0)
		hat_unload(seg->s_as, addr, len, HAT_UNLOAD);
	else
		hat_chgprot(seg->s_as, addr, len, prot);
	if (seg_pte == NULL)
		return (0);

	level = 0;
	eaddr = addr + len;
	while (addr < eaddr) {

		if (level != 3 || MMU_L2_OFF(addr) == 0) {
			if (ppte != NULL)
				unlock_ptbl(ptbl, mtx);
				ppte = srmmu_ptefind(seg->s_as, addr, &level,
					&ptbl, &mtx, 0);
			} else
				ppte++;

			mmu_readpte(ppte, &new_pte);
		if (level == 2) {
			addr += MMU_L2_SIZE;
			for (i = 0; i < MMU_NPTE_THREE; i++) {
				*seg_pte = new_pte;
				new_pte.PhysicalPageNumber++;
			}
		} else {
			ASSERT(level == 3);
			*seg_pte++ = new_pte;
			addr += MMU_L3_SIZE;
		}
	}

	unlock_ptbl(ptbl, mtx);
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

		switch (ppte->AccessPermissions) {
		case MMU_STD_SRUR:
			pprot = PROT_READ | PROT_USER;
			break;
		case MMU_STD_SRWURW:
			pprot = PROT_READ | PROT_WRITE | PROT_USER;
			break;
		case MMU_STD_SRXURX:
			pprot = PROT_READ | PROT_EXEC | PROT_USER;
			break;
		case MMU_STD_SRWXURWX:
			pprot = PROT_READ | PROT_WRITE | PROT_EXEC | PROT_USER;
			break;
		case MMU_STD_SXUX:
			pprot = PROT_EXEC | PROT_USER;
			break;
		case MMU_STD_SRWUR:	/* Hmmm, doesn't map nicely, demote */
			pprot = PROT_READ | PROT_USER;
			break;
		case MMU_STD_SRX:
			pprot = PROT_READ | PROT_EXEC;
			break;
		case MMU_STD_SRWX:
			pprot = PROT_READ | PROT_WRITE | PROT_EXEC;
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
	register struct pte *pte;
	register u_int pprot;
	caddr_t eaddr;
	int level;
	struct ptbl *ptbl;
	kmutex_t *mtx;

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

	pte = NULL;
	level = 0;
	eaddr = addr + len;
	while (addr < eaddr) {

		if (level != 3 || MMU_L2_OFF(addr) == 0) {
			if (pte != NULL)
				unlock_ptbl(ptbl, mtx);
				pte = srmmu_ptefind(seg->s_as, addr,
				    &level, &ptbl, &mtx, 0);
			} else
				pte++;


		if (!pte_valid(pte))
			pprot = 0;
		else {
			pprot = srmmu_ptov_prot(pte);
		}

		if ((pprot & prot) != prot) {
			unlock_ptbl(ptbl, mtx);
			return (EACCES);
		}

		switch (level) {
		case 3:
			addr += MMU_L3_SIZE;
			break;

		case 2:
			addr += MMU_L2_SIZE;
			break;

		case 1:
			addr += MMU_L1_SIZE;
			break;
		}
	}
	unlock_ptbl(ptbl, mtx);
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
	ppte += mmu_btop(addr - seg->s_base);

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
	/*NOTREACHED*/
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
	struct pte *ppte;
	struct pte tpte;
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

		TRACE_5(TR_FAC_VM, TR_SEGKMEM_ALLOC,
			"segkmem_alloc:seg %x addr %x pp %x kvp %x off %x",
			seg, addr, pp, &kvp, (u_long) addr & (PAGESIZE - 1));
		if (seg->s_data) {
			ppte = (struct pte *)seg->s_data;
		} else {
			ppte = (struct pte *)NULL;
		}

		while (pp != (page_t *)NULL) {
			ppcur = pp;
			page_sub(&pp, ppcur);
			ASSERT(se_assert(&ppcur->p_selock) &&
			    page_iolock_assert(ppcur));
			page_io_unlock(ppcur);
			page_downgrade(ppcur);

			srmmu_memload(as->a_hat, as, (caddr_t)ppcur->p_offset,
				ppcur, PROT_ALL & ~PROT_USER, HAT_LOCK);

			if (ppte != NULL) {
				srmmu_mempte(ppcur, PROT_ALL & ~PROT_USER,
					&tpte, (caddr_t)ppcur->p_offset);
				/* structure assignment */
				*(ppte + (mmu_btop((caddr_t)ppcur->p_offset -
				    seg->s_base))) = tpte;
			}
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
		ppte += mmu_btop(addr - seg->s_base);
	} else
		ppte = NULL;

	for (; (int)len > 0; len -= PAGESIZE, addr += PAGESIZE) {
		if (ppte) {
			ppte->EntryType = MMU_ET_INVALID;
			ppte++;
		}
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
		/*LINTED: constant in conditional context*/
		VN_DISPOSE(pp, B_INVAL, 0, kcred);
	}
}

#ifdef notdef

/*
 * mapin() and mapout() are for manipulating kernel addresses only.
 * They are provided for backward compaitibility. They have been
 * replaced by segkmem_mapin() and segkmem_mapout().
 */
/*ARGSUSED*/
void
mapin(ppte, v, paddr, size, access)
	struct pte *ppte;
	u_int v;
	u_int paddr;
	int size, access;
{
	mapin_page(ppte, v, paddr >> PAGESHIFT, size, access);
}

/*
 * Map a physical address range into kernel virtual addresses.
 * Just like "mapin", but the physical address is given as a
 * physical page number, not an address, so things with addresses
 * longer than 32 bits can be mapped (like sbus or vme stuff).
 */
/*ARGSUSED*/
mapin_page(ppte, v, ppage, size, access)
	struct pte *ppte;
	u_int v;
	u_int ppage;
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
		cmn_err(CE_PANIC, "mapin -- bad seg");
	if (!pte_valid((struct pte *)&access))
		cmn_err(CE_PANIC, "mapin -- invalid pte");
	vprot = srmmu_ptov_prot((struct pte *)&access);
	/* XXX check if vprot is == 0? */

	segkmem_mapin(seg, vaddr, (u_int)mmu_ptob(size), vprot,
		ppage, 0);

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
		cmn_err(CE_PANIC, "mapout -- bad pte");
	vaddr = kvseg.s_base + mmu_ptob(ppte - (struct pte *)kvseg.s_data);
	segkmem_mapout(&kvseg, vaddr, (u_int)mmu_ptob(size));
}

#endif /* notdef */

/*
 * segkmem_mapin() and segkmem_mapout() are for manipulating kernel
 * addresses only. Since some users of segkmem_mapin() forget to unmap,
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
	register struct pte *ppte, *phpte = (struct pte *)NULL;
	struct pte apte, tpte;
	page_t *pp;
	int level;
	extern int vrfy_is_mem();

	ASSERT(seg->s_as == &kas);
	ASSERT(len <= seg->s_size);
	ASSERT(addr <= addr + len);
	ASSERT(addr >= seg->s_base && addr + len <= seg->s_base + seg->s_size);
	ASSERT(seg->s_ops == &segkmem_ops);
	ASSERT(((u_int)addr & MMU_PAGEOFFSET) == 0);

	if (seg->s_data) {
		ppte = (struct pte *)seg->s_data;
		ppte += mmu_btop(addr - seg->s_base);
	} else
		ppte = (struct pte *)NULL;
	*(u_int *)&apte = MMU_STD_INVALIDPTE;
	apte.PhysicalPageNumber = pcookie;
	if (vprot == PROT_NONE)
		cmn_err(CE_PANIC, "segkmem_mapin -- invalid ptes");
	apte.AccessPermissions = srmmu_vtop_prot(addr, vprot);
	apte.EntryType = MMU_ET_PTE;

	/*
	 * Always lock the mapin'd translations.
	 */
	flags |= PTELD_LOCK;
	level = 0;

	for (; len != 0; addr += MMU_PAGESIZE, len -= MMU_PAGESIZE) {
		/*
		 * XXX - The mutex locks need to be taken care of.
		 */

		if (level != 3 || MMU_L2_OFF(addr) == 0) {
			phpte =  srmmu_ptefind_nolock(&kas, addr, &level);
		} else
			phpte++;

		mmu_readpte(phpte, &tpte);
		if (pte_valid(&tpte)) {
			ASSERT(level == 3);
			hat_unlock(kas.a_hat, &kas, addr, PAGESIZE);
			hat_unload(&kas, addr, MMU_PAGESIZE, HAT_UNLOAD);
		}
		apte.Cacheable = vrfy_is_mem(apte.PhysicalPageNumber);

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
		if (apte.Cacheable && !(flags & HAT_NOCONSIST)) {
			pp = page_numtopp_nolock(apte.PhysicalPageNumber);
			ASSERT(pp == NULL || se_assert(&pp->p_selock) ||
			    panicstr);
		} else {
			pp = (page_t *)NULL;
		}

		srmmu_devload(kas.a_hat, &kas, addr, (devpage_t *)pp,
			apte.PhysicalPageNumber, vprot, flags);

		if (ppte != (struct pte *)NULL)
			*ppte++ = *(struct pte *)&apte;

		apte.PhysicalPageNumber++;
	}
}

/*
 * Release mapping for the kernel. This frees pmegs, which are a critical
 * resource. The segment must be backed by software ptes. The pages
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

	ppte = (struct pte *)seg->s_data;
	ppte += mmu_btop(addr - seg->s_base);
	for (; len != 0; addr += MMU_PAGESIZE, len -= MMU_PAGESIZE, ppte++) {
		if (!pte_valid(ppte))
			continue;
		ppte->EntryType = MMU_ET_INVALID;
		hat_unlock(kas.a_hat, &kas, addr, PAGESIZE);
		hat_unload(&kas, addr, MMU_PAGESIZE, HAT_UNLOAD);
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
		    (u_int)mmu_ptob(npages),
		    !(flag & KM_NOSLEEP)) == 0) {
			rmfree(kernelmap, (long)npages, a);
			TRACE_1(TR_FAC_KMEM, TR_KMEM_GETPAGES_END,
				"kmem_getpages_end:kmem addr %x", NULL);
			return (NULL);
		}
	} else
		boot_getpages(base, mmu_ptob(npages), 0);

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
		struct memlist *this;
		/*
		 * If someone attempts to free memory here we must delay
		 * the action until kvseg is ready.  So lets just put
		 * the request on a list and delete it at a later time.
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

	this = kmem_garbage_list;
	while (this != NULL) {
		kmem_freepages((void *)this->address,
			(int)mmu_btopr(this->size));
		next = this->next;
		kmem_free(this, sizeof (struct memlist));
		this = next;
	}

	kmem_garbage_list = (struct memlist *)0;
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
boot_getpages(base, size, align)
	caddr_t base;
	u_int size, align;
{
	struct page *pp;
	caddr_t addr, p;
	u_int n, pfn;

	addr = BOP_ALLOC(bootops, base, size, align);
	if (addr != base)
		cmn_err(CE_PANIC, "bop_alloc failed");
	for (p = addr, n = mmu_btop(size); n; p += MMU_PAGESIZE, n--) {
		pfn = va_to_pfn(p);
		if (pfn == (u_int)-1)
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
