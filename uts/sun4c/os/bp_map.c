/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)bp_map.c	1.9	94/12/01 SMI"

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/buf.h>
#include <sys/cmn_err.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/machsystm.h>
#include <sys/debug.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>

/*
 * Global Routines:
 *
 * int impl_read_hwmap()
 * void bp_mapin()
 * void bp_mapout()
 */

/*
 * Static Routines
 */
static int bp_alloc(struct map *map, register struct buf *bp, int size);
static void bp_map(register struct buf *bp, caddr_t kaddr);

/*
 * Common routine for bp_map and rootnex_dma_map which will read in the
 * pte's from the hardware mapping registers into the pte array given and
 * return bustype. If asked, we also enforce some rules on non-obmem ptes.
 */
int
impl_read_hwmap(as, addr, npf, pte, cknonobmem)
	struct as *as;
	caddr_t addr;
	int npf;
	register struct pte *pte;
	int cknonobmem;
{
	register caddr_t saddr;
	struct pmgrp *pmgrp;
	int bustype = -1;
	struct sunm *sunm;

	pmgrp = (struct pmgrp *)NULL;

	if (as == &kas) {
		saddr = addr;
	}

	/*
	 * We block around use of SEGTEMP2 virtual address mapping
	 * since this routine can be called at interrupt level.
	 */
	mutex_enter(&sunm_mutex);
	while (npf-- > 0) {
		/*
		 * If we're at the beginning of a pmgrp's address range, or
		 * we're not the kernel address space and we haven't gotten
		 * the pmgrp, use SEGTEMP2 to snag a temporary address
		 * range for the purpose of being able to get the pte's
		 */
		if (as != &kas && (pmgrp == (struct pmgrp *)NULL ||
		    ((int)addr & (PMGRPSIZE - 1)) < MMU_PAGESIZE)) {
			/*
			 * Find the pmgrp in the address space.
			 */
			saddr = (caddr_t)((int)addr & ~(PMGRPSIZE - 1));
			sunm = (struct sunm *)as->a_hat->hat_data;
			for (pmgrp = sunm->sunm_pmgrps; pmgrp &&
			    pmgrp->pmg_base != saddr; pmgrp = pmgrp->pmg_next)
				;
			if (pmgrp == (struct pmgrp *)NULL) {
				bustype = -1;
				break;
			}
			mmu_settpmg(SEGTEMP2, pmgrp);
			saddr = SEGTEMP2 + ((int)addr & (PMGRPSIZE - 1));
		}

		mmu_getpte(saddr, pte);
		addr += MMU_PAGESIZE;
		saddr += MMU_PAGESIZE;
		if (pte->pg_v == 0) {
			bustype = -1;
			break;
		}
		if (bustype == -1) {
			bustype = pte->pg_type;
		} else if (cknonobmem) {
			if (bustype != pte->pg_type) {
				/*
				 * Our type shifted....
				 */
				bustype = -1;
				break;
			}
		}
		pte++;
	}
	mmu_settpmg(SEGTEMP2, pmgrp_invalid);
	mutex_exit(&sunm_mutex);
	return (bustype);
}

/*
 * Map the data referred to by the buffer bp into the kernel
 * at kernel virtual address kaddr.
 */
#define	PTECHUNKSIZE	16

static void
bp_map(register struct buf *bp, caddr_t kaddr)
{
	auto struct pte pc[PTECHUNKSIZE];
	register struct pte *ptep = &pc[0];
	register struct pte *spte = (struct pte *)NULL;
	register struct page *pp = (struct page *)NULL;
	int npf, flags, cidx;
	struct seg *seg;
	struct as *as;
	caddr_t addr;

	/*
	 * Select where to find the pte values we need.
	 * They can either be gotten from a list of page
	 * structures hung off the bp, or are already
	 * available (in Sysmap), or must be read from
	 * the hardware.
	 */
/* XXX - do something about E_Sysmap here */

	if (bp->b_flags & B_PAGEIO) {
		/*
		 * The way to get the pte's is to traverse
		 * the page structures and convert them to ptes.
		 *
		 * The original code commented against 'having
		 * to protect against interrupts messing up
		 * the array'. I don't think that that applies.
		 */
		pp = bp->b_pages;
	} else if ((bp->b_flags & B_PHYS) == 0) {
		u_int vaddr = (u_int) bp->b_un.b_addr;

		if (vaddr > (u_int)E_SYSBASE && vaddr < (u_int)E_Syslimit)
			spte = &E_Sysmap[btop(vaddr - E_SYSBASE)];
		else if (vaddr > (u_int)SYSBASE && vaddr < (u_int)Syslimit)
			spte = &Sysmap[btop(vaddr - SYSBASE)];
	}

	/*
	 * If the pte's aren't in Sysmap, or can't be gotten from page
	 * structures, then we have to read them from the hardware.
	 * We look at stuff in the bp to try and find the right
	 * address space for which bp->b_un.b_addr is meaningful.
	 */

	if ((spte == (struct pte *)NULL) && (pp == (struct page *)NULL)) {
		if (bp->b_proc == (struct proc *)NULL ||
		    (bp->b_flags & B_REMAPPED) != 0 ||
		    (as = bp->b_proc->p_as) == (struct as *)NULL) {
			as = &kas;
		}
		addr = (caddr_t)bp->b_un.b_addr;
		cidx = PTECHUNKSIZE;
	}


	/*
	 * If kaddr is in the kernelmap space, we use kvseg so the
	 * software ptes will get updated. Otherwise we use kdvmaseg.
	 * We should probably check to make sure it is really in
	 * one of those two segments, but it's not worth the effort.
	 */
	flags = PTELD_NOSYNC;
	if (kaddr < Syslimit) {
		seg = &kvseg;
	} else {
		flags |= PTELD_INTREP;
		seg = &kdvmaseg;
	}

	/*
	 * Loop through the number of page frames.
	 * Don't use a predecrement because we use
	 * npf in the loop.
	 */

	npf = btoc(bp->b_bcount + ((int)bp->b_un.b_addr & PAGEOFFSET));

	while (npf > 0) {
		/*
		 * First, fetch the pte we're interested in.
		 */
		if (spte) {
			ptep = spte++;
		} else if (pp) {
			sunm_mempte(pp, PROT_WRITE | PROT_READ, ptep);
			pp = pp->p_next;
		} else {
			if (cidx == PTECHUNKSIZE) {
				int np = MIN(npf, PTECHUNKSIZE);
				if (impl_read_hwmap(as, addr, np, pc, 0) < 0) {
					cmn_err(CE_PANIC,
						"bp_map: read_hwmap failed");
					/*NOTREACHED*/
				}
				addr = (caddr_t)((u_long) addr + mmu_ptob(np));
				cidx = 0;
			}
			ptep = &pc[cidx++];
		}

		/*
		 * Now map it in
		 */
		segkmem_mapin(seg, kaddr, MMU_PAGESIZE,
		    PROT_READ | PROT_WRITE, MAKE_PFNUM(ptep), flags);

		/*
		 * adjust values of interest
		 */
		kaddr += MMU_PAGESIZE;
		npf--;
	}
}

/*
 * Allocate 'size' units from the given map so that
 * the vac alignment constraints for bp are maintained.
 *
 * Return 'addr' if successful, 0 if not.
 */
static int
bp_alloc(struct map *map, register struct buf *bp, int size)
{
	register struct map *mp;
	register u_long addr, mask;
	int align = -1;

	ASSERT(MUTEX_HELD(&maplock(map)));

	if (vac) {
		/*
		 * XXX: this needs to be reexamined for sun4c
		 */

		if ((bp->b_flags & B_PAGEIO) != 0) {
			/*
			 * Peek at the first page's alignment.
			 * We could work harder and check the alignment
			 * of all the pages.  If a conflict is found
			 * and the page is not kept (more than once
			 * if intrans), then try to do hat_pageunload()'s
			 * to allow the IO to be cached.  However,
			 * this is very unlikely and not worth the
			 * extra work (for now at least).
			 */
			struct page *pp = bp->b_pages;

			hat_mlist_enter(pp);
			if (pp->p_mapping != NULL) {
				struct hment *hme;

				hme = (struct hment *)pp->p_mapping;
				align = hme->hme_impl &
					vac_mask >> MMU_PAGESHIFT;
			}
			hat_mlist_exit(pp);
		} else if (bp->b_un.b_addr != NULL) {
			align = mmu_btop((int)bp->b_un.b_addr & vac_mask);
		}
	}

	if (align == -1)
		return (rmalloc_locked(map, (size_t)size));

	/*
	 * Look for a map segment containing a request that works.
	 * If none found, return failure.
	 */
	mask = mmu_btop(shm_alignment) - 1;
	for (mp = mapstart(map); mp->m_size; mp++) {
		if (mp->m_size < size)
			continue;

		/*
		 * Find first addr >= mp->m_addr that
		 * fits the alignment constraints.
		 */
		addr = (mp->m_addr & ~mask) + align;
		if (addr < mp->m_addr)
			addr += mask + 1;

		/*
		 * See if it fit within the map.
		 */
		if (addr + size <= mp->m_addr + mp->m_size)
			break;
	}

	if (mp->m_size == 0)
		return (0);

	/* let rmget() do the rest of the work */

	addr = rmget(map, (size_t)size, (ulong_t)addr);
	return (addr);
}

/*
 * Called to convert bp for pageio/physio to a kernel addressable location.
 * We allocate virtual space from the kernelmap and then use bp_map to do
 * most of the real work.
 */
void
bp_mapin(bp)
	register struct buf *bp;
{
	int npf, o;
	long a;
	caddr_t kaddr;

	if ((bp->b_flags & (B_PAGEIO | B_PHYS)) == 0 ||
	    (bp->b_flags & B_REMAPPED) != 0)
		return;		/* no pageio/physio or already mapped in */

	if ((bp->b_flags & (B_PAGEIO | B_PHYS)) == (B_PAGEIO | B_PHYS))
		cmn_err(CE_PANIC, "bp_mapin");

	o = (int)bp->b_un.b_addr & PAGEOFFSET;
	npf = btoc(bp->b_bcount + o);

	/*
	 * Allocate kernel virtual space for remapping.
	 */
	mutex_enter(&maplock(kernelmap));
	while ((a = bp_alloc(kernelmap, bp, npf)) == 0) {
		mapwant(kernelmap) = 1;
		cv_wait(&map_cv(kernelmap), &maplock(kernelmap));
	}
	mutex_exit(&maplock(kernelmap));
	kaddr = Sysbase + mmu_ptob(a);

	/* map the bp into the virtual space we just allocated */
	bp_map(bp, kaddr);

	bp->b_flags |= B_REMAPPED;
	bp->b_un.b_addr = kaddr + o;
}

/*
 * bp_mapout will release all the resources associated with a bp_mapin call.
 * We call hat_unload to release the work done by bp_map which will insure
 * that the reference and modified bits from this mapping are not OR'ed in.
 */
void
bp_mapout(bp)
	register struct buf *bp;
{
	int npf;
	u_long a;
	caddr_t	addr;

	if (bp->b_flags & B_REMAPPED) {
		addr = (caddr_t)((int)bp->b_un.b_addr & PAGEMASK);
		bp->b_un.b_addr = (caddr_t)((int)bp->b_un.b_addr & PAGEOFFSET);
		npf = mmu_btopr(bp->b_bcount + (int)bp->b_un.b_addr);
		hat_unload(&kas, addr, (u_int)mmu_ptob(npf), HAT_UNLOAD);
		a = mmu_btop(addr - Sysbase);
		bzero((caddr_t)&Usrptmap[a], (u_int)(sizeof (Usrptmap[0])*npf));
		rmfree(kernelmap, (size_t)npf, a);
		bp->b_flags &= ~B_REMAPPED;
	}
}
