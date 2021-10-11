/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)bp_map.c	1.18	95/01/16 SMI"

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
#include <vm/page.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>
#include <vm/hat_srmmu.h>
#include <sys/iommu.h>
#include <sys/bt.h>
#include <sys/debug.h>

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
 * Common routine for bp_map() and sbus_dma_map() which will read in the
 * pte's from the hardware mapping registers into the pte array given.
 * sun4m version returns bustype, sun4d version returns flag:
 *
 *	if(cknonobmem)
 *	-1:	invalid
 *	 0:	(BT_DRAM || BT_NVRAM)
 *	 1:	BT_SBUS
 *	else
 *	-1:	invalid
 *	 1:	valid
 */
int
impl_read_hwmap(as, addr, npf, pte, cknonobmem)
	struct as *as;
	caddr_t addr;
	int npf;
	register struct pte *pte;
	int cknonobmem;
{
	struct pte *hpte, tpte;
	int r = 0;
	int mybustype = -1, level = 0;
	int off, span;
	struct ptbl *ptbl;
	kmutex_t *mtx;

	hpte = (struct pte *)NULL;

	while (npf > 0) {

		/*
		 * If it's a L3 pte and it's not at L3 page table boundary,
		 * then we know the pte is still in the same page table.
		 * a hpte++ would be faster than a srmmu_ptefind. Unfortunately,
		 * for L2, L1 ptes, we cannot use the same trick since they
		 * can have a smaller (eg. L3) pte right after it.
		 */
		if (level != 3 || ((u_int)MMU_L2_OFF(addr) < MMU_L3_SIZE)) {
			hpte = srmmu_ptefind_nolock(as, addr, &level);
		} else {
			hpte++;
		}

		ASSERT(hpte != NULL);
		tpte = *hpte;

		if (!pte_valid(&tpte)) {
			/*
			 * Even locked ptes can turn up invalid
			 * if we call ptefind without the hat lock
			 * if another cpu is sync'ing the rm bits
			 * at the same time.  We avoid this race
			 * by retrying ptefind with the lock.
			 */
			hpte = srmmu_ptefind(as, addr, &level, &ptbl, &mtx,
				LK_PTBL_SHARED);
			tpte = *hpte;
			unlock_ptbl(ptbl, mtx);

			if (!pte_valid(&tpte)) {
				cmn_err(CE_CONT,
					"impl_read_hwmap invalid pte\n");
				r = -1;
				break;
			}
		}

		switch (level) {
		case 1:
			off = MMU_L1_OFF(addr);
			span = MIN(npf, mmu_btopr(MMU_L1_SIZE - off));
			break;

		case 2:
			off = MMU_L2_OFF(addr);
			span = MIN(npf, mmu_btopr(MMU_L2_SIZE - off));
			break;

		case 3:
			off = 0;
			span = 1;
			break;

		default:
#ifdef DEBUG
			cmn_err(CE_CONT, "impl_read_hwmap bad level\n");
#endif DEBUG
			return (-1);
		}

		off = mmu_btop(off);
		tpte.PhysicalPageNumber += off;

		if (cknonobmem) {
			if (mybustype == -1) {
				mybustype = impl_bustype(MAKE_PFNUM(&tpte));
				/*
				 * DVMA can happen to memory or Sbus.
				 */
				if (mybustype != BT_DRAM &&
				    mybustype != BT_NVRAM &&
				    mybustype != BT_SBUS) {
					cmn_err(CE_CONT,
					    "impl_read_hwmap:bustype err\n");
					r = -1;
					break;
				}
			} else {
				if (impl_bustype(MAKE_PFNUM(&tpte))
				    != mybustype) {
					/*
					 * we don't allow mixing bus types.
					 */
					cmn_err(CE_CONT,
					    "impl_read_hwmap:mixed bustype\n");
					r = -1;
					break;
				}
			}
		}

		/*
		 * We make the translation writable, even if the current
		 * mapping is read only.  This is necessary because the
		 * new pte is blindly used in other places where it needs
		 * to be writable.
		 */
		tpte.AccessPermissions = MMU_STD_SRWX;	/* XXX -  generous? */

		/*
		 * Copy the hw ptes to the sw array.
		 */
		npf -= span;
		addr += mmu_ptob(span);
		while (span--) {
			*pte = tpte;

			tpte.PhysicalPageNumber++;
			pte++;
		}
	}

	if (cknonobmem && r == 0 && mybustype != BT_DRAM &&
	    mybustype != BT_NVRAM)
		r = 1;
	return (r);
}

/*
 * Map the data referred to by the buffer bp into the kernel
 * at kernel virtual address kaddr. It used to map in data for
 * DVMA, among other things. Now it isn't clear that this
 * is called for anything but mapping data into the kernel's
 * address space.
 */

/*
 * PTECHUNKSIZE is just a convenient number of pages to deal with at a time.
 * No way does it reflect anything but that.
 */

#define	PTECHUNKSIZE	16

static void
bp_map(register struct buf *bp, caddr_t kaddr)
{
	auto struct pte ptecache[PTECHUNKSIZE];
	register struct pte *pte = &ptecache[0];
	register struct pte *spte = (struct pte *)NULL;
	register struct page *pp = (struct page *)NULL;
	int npf, flags, cidx;
	struct as *as;
	unsigned int pfnum;
	caddr_t addr;

	/*
	 * If kaddr >= DVMA, that's bp_iom_map()'s job.
	 */
	if (kaddr >= DVMA)
		cmn_err(CE_PANIC, "bp_map: bad kaddr");

	/*
	 * Select where to find the pte values we need.
	 * They can either be gotten from a list of page
	 * structures hung off the bp, or are already
	 * available (in Sysmap), or must be read from
	 * the hardware.
	 */

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
		if (vaddr >= (u_int) Sysbase && vaddr < (u_int) Syslimit) {
			spte = &Sysmap[btop(vaddr - SYSBASE)];
		}
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
	 * We want to use mapin to set up the mappings now since some
	 * users of kernelmap aren't nice enough to unmap things
	 * when they are done and mapin handles this as a special case.
	 * If kaddr is in the kernelmap space, we use kseg so the
	 * software ptes will get updated.
	 *
	 * XXX - Fix this comment.
	 * Otherwise we use kdvmaseg.
	 * We should probably check to make sure it is really in
	 * one of those two segments, but it's not worth the effort.
	 */
	ASSERT(kaddr < Syslimit);
	flags = PTELD_NOSYNC;

	/* XXX - What about PTELD_INTREP */
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
			pte = spte++;
		} else if (pp) {
			/*
			 * XXX - probably should get rid of last arg.
			 * to srmmu_mempte() in vm_hat.c so that it will
			 * be the same with other machines.
			 *
			 * XXX - We stuff in KERNELBASE for now since we know
			 * PAGEIO only goes to KERNEL space.
			 */
			srmmu_mempte(pp, PROT_WRITE | PROT_READ, pte,
			    (caddr_t)KERNELBASE);
			pp = pp->p_next;
		} else {
			if (cidx == PTECHUNKSIZE) {
				int np = MIN(npf, PTECHUNKSIZE);
				if (impl_read_hwmap(as, addr, np,
				    ptecache, 0) < 0)
					cmn_err(CE_PANIC,
					    "bp_map: read_hwmap failed");
				addr += mmu_ptob(np);
				cidx = 0;
			}
			pte = &ptecache[cidx++];
		}

		/*
		 * Now map it in; segkmem_mapin() always locks the translation,
		 * i.e., calls pteload with HAT_LOCK set.
		 */
		pfnum = MAKE_PFNUM(pte);
		segkmem_mapin(&kvseg, kaddr, MMU_PAGESIZE,
		    PROT_READ | PROT_WRITE, pfnum, flags);

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
	struct page *pp = NULL;

	ASSERT(MUTEX_HELD(&maplock(map)));

#ifdef VAC
	if (vac) {
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
			pp = bp->b_pages;

			hat_mlist_enter(pp);
			if (pp->p_mapping != NULL) {
				align = mmu_btop((int)
				    hmetovaddr(pp->p_mapping) &
				    (shm_alignment - 1));
			}
			hat_mlist_exit(pp);
		} else if (bp->b_un.b_addr != NULL) {
			align = mmu_btop((int)bp->b_un.b_addr &
			    (shm_alignment - 1));
		}
	}

	if (align == -1)
#endif VAC
		return (rmalloc_locked(map, (size_t)size));

#ifdef sun4m
	/*
	 * Look for a map segment containing a request that works.
	 * If none found, return failure.
	 * Since VAC has a much stronger alignment requirement,
	 * we'll use shm_alignment even ioc is on too.
	 */

	if (vac)
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
	return (rmget(map, (long)size, (u_long)addr));
#endif sun4m
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
		hat_unload(&kas, addr, (u_int)mmu_ptob(npf), HAT_UNLOCKUNLOAD);
		a = mmu_btop(addr - Sysbase);
		bzero((caddr_t)&Usrptmap[a], (u_int)(sizeof (Usrptmap[0])*npf));
		rmfree(kernelmap, (size_t)npf, a);
		bp->b_flags &= ~B_REMAPPED;
	}
}
