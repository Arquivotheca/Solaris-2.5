/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)bp_map.c	1.11	94/07/06 SMI"

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/buf.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/machparam.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>
#include <vm/hat_sfmmu.h>
#include <vm/page.h>
#include <sys/iommu.h>
#include <sys/bt.h>
#include <sys/cpu_module.h>

extern	u_int	shm_alignment;	/* VAC address consistency modulus */

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
 * Map the data referred to by the buffer bp into the kernel
 * at kernel virtual address kaddr. It used to map in data for
 * DVMA, among other things. Now it isn't clear that this
 * is called for anything but mapping data into the kernel's
 * address space.
 */
static void
bp_map(register struct buf *bp, caddr_t kaddr)
{
	register page_t *pp = (page_t *)NULL;
	int npf, flags;
	struct as *as;
	unsigned int pfnum;
	caddr_t addr;

	/*
	 * Select where to find the pfn values we need.
	 * They can either be gotten from a list of page
	 * structures hung off the bp, or must be obtained
	 * by inquiring the hat.
	 */

	if (bp->b_flags & B_PAGEIO) {
		pp = bp->b_pages;
	}

	if (pp == (struct page *) NULL) {
		addr = (caddr_t) bp->b_un.b_addr;
		if (bp->b_proc == (struct proc *)NULL ||
		    (bp->b_flags & B_REMAPPED) != 0 ||
		    (as = bp->b_proc->p_as) == (struct as *)NULL) {
			as = &kas;
		}
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
	flags = HAT_NOSYNCLOAD;

	/*
	 * Loop through the number of page frames.
	 * Don't use a predecrement because we use
	 * npf in the loop.
	 */

	npf = btoc(bp->b_bcount + ((int)bp->b_un.b_addr & PAGEOFFSET));

	if (!pp) {
		/*
		 * We need to grab the as lock because hat_getpfnum expects
		 * it to be held.
		 */
		ASSERT(as);
		AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
	}
	while (npf > 0) {
		if (pp) {
			pfnum = pp->p_pagenum;
			pp = pp->p_next;
		} else {
			pfnum = hat_getpfnum(as, addr);
			addr += mmu_ptob(1);
		}
		/*
		 * Now map it in
		 */
		segkmem_mapin(&kvseg, kaddr, MMU_PAGESIZE,
		    PROT_READ | PROT_WRITE, pfnum, flags);

		/*
		 * adjust values of interest
		 */
		kaddr += MMU_PAGESIZE;
		npf--;
	}
	if (!pp) {
		AS_LOCK_EXIT(as, &as->a_lock);
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
	int vcolor = -1;
	page_t *pp = NULL;
	extern int sfmmu_get_addrvcolor(caddr_t);

	ASSERT(MUTEX_HELD(&maplock(map)));

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
			vcolor = sfmmu_get_ppvcolor(pp);
		} else if (bp->b_un.b_addr != NULL) {
			vcolor = sfmmu_get_addrvcolor(bp->b_un.b_addr);
		}
	}

	if (vcolor == -1)
		return (rmalloc_locked(map, (size_t)size));

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
		addr = (mp->m_addr & ~mask) + vcolor;
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
	int npf2;
	u_long a;
	caddr_t	addr;
	caddr_t iaddr;

	if (bp->b_flags & B_REMAPPED) {
		addr = (caddr_t)((int)bp->b_un.b_addr & PAGEMASK);
		bp->b_un.b_addr = (caddr_t)((int)bp->b_un.b_addr & PAGEOFFSET);
		npf = mmu_btopr(bp->b_bcount + (int)bp->b_un.b_addr);

		/*
		 * For now, flush I-$ here
		 * Just in case we bought in a page that we are about
		 * to execute
		 */
		npf2 = npf; iaddr = addr;
		while (npf2 > 0) {
			CPU_ICACHE_FLUSHPAGE(iaddr);
			iaddr = iaddr + MMU_PAGESIZE;
			npf2--;
		}

		hat_unload(&kas, addr, (u_int)mmu_ptob(npf),
			(HAT_NOSYNCUNLOAD | HAT_UNLOCKUNLOAD));
		a = mmu_btop(addr - Sysbase);
		rmfree(kernelmap, (size_t)npf, a);
		bp->b_flags &= ~B_REMAPPED;
	}
}
