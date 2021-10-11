/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ppage.c	1.15	95/09/21 SMI"

#include <sys/t_lock.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/vm.h>
#include <sys/cpu.h>
#include <sys/cmn_err.h>
#include <vm/as.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/hat_sfmmu.h>		/* XXX FIXME - delete */
#include <sys/debug.h>

/*
 * External Data:
 */
extern	u_int	shm_alignment;		/* vac size */
extern	char	Sysbase[], Syslimit[];

/*
 * A quick way to generate a cache consistent address to map in a page.
 * users: ppcopy, pagezero, /proc, dev/mem
 *
 * The ppmapin/ppmapout routines provide a quick way of generating a cache
 * consistent address by reserving a given amount of kernel address space.
 * The base is PPMAPBASE and its size is PPMAPSIZE.  This memory is divided
 * into x number of sets, where x is the number of colors for the virtual
 * cache. The number of colors is how many times a page can be mapped
 * simulatenously in the cache.  For direct map caches this translates to
 * the number of pages in the cache.
 * Each set will be assigned a group of virtual pages from the reserved memory
 * depending on its virtual color.
 * When trying to assign a virtual address we will find out the color for the
 * physical page in question (if applicable).  Then we will try to find an
 * available virtual page from the set of the appropiate color.
 */

#define	clsettoarray(color, set) ((color * nsets) + set)

static caddr_t	ppmap_vaddrs[PPMAPSIZE / MMU_PAGESIZE];
static kmutex_t ppmap_lock;
static int	nsets;			/* number of sets */
static int	ppmap_last = 0;
static int	ppmap_pages;		/* generate align mask */
static int	ppmap_shift;		/* set selector */

#ifdef PPDEBUG
#define		MAXCOLORS	16	/* for debug only */
static int	ppalloc_noslot = 0;	/* # of allocations from kernelmap */
static int	align_hits[MAXCOLORS];
static int	pp_allocs;		/* # of ppmapin requests */
#endif PPDEBUG

void
ppmapinit()
{
	int color, nset, setsize;
	caddr_t va;

	va = (caddr_t)PPMAPBASE;
	if (cache & CACHE_VAC) {
		ppmap_pages = mmu_btop(shm_alignment);
		nsets = PPMAPSIZE / shm_alignment;
		setsize = shm_alignment;
		ppmap_shift = MMU_PAGESHIFT + (ppmap_pages / 2);
	} else {
		/*
		 * If we do not have a virtual indexed cache we simply
		 * have only one set containing all pages.
		 */
		ppmap_pages = 1;
		nsets = mmu_btop(PPMAPSIZE);
		setsize = MMU_PAGESIZE;
		ppmap_shift = MMU_PAGESHIFT;
	}
	for (color = 0; color < ppmap_pages; color++) {
		for (nset = 0; nset < nsets; nset++) {
			ppmap_vaddrs[clsettoarray(color, nset)] =
				(caddr_t)((u_int)va + (nset * setsize));
		}
		va += MMU_PAGESIZE;
	}

	mutex_init(&ppmap_lock, "ppmap_lock", MUTEX_DEFAULT, NULL);
}

/*
 * Allocate a cache consistent virtual address to map a page, pp,
 * with protection, vprot; and map it in the MMU, using the most
 * efficient means possible.  The argument avoid is a virtual address
 * hint which when masked yields an offset into a virtual cache
 * that should be avoided when allocating an address to map in a
 * page.  An avoid arg of -1 means you don't care, for instance pagezero.
 *
 * machine dependent, depends on virtual address space layout,
 * understands that all kernel addresses have bit 31 set.
 *
 * NOTE: For sun4u platforms the meaning of the hint argument is opposite from
 * that found in other architectures.  In other architectures the hint
 * (called avoid) was used to ask ppmapin to NOT use the specified cache color.
 * This was used to avoid virtual cache trashing in the bcopy.  Unfortunately
 * in the case of a COW,  this later on caused a cache aliasing conflict.  In
 * sun4u the bcopy routine uses the block ld/st instructions so we don't have
 * to worry about virtual cache trashing.  Actually, by using the hint to choose
 * the right color we can almost guarantee a cache conflict will not occur.
 */
caddr_t
ppmapin(pp, vprot, hint)
	register struct page *pp;
	register u_int vprot;
	register caddr_t hint;
{
	int color,  nset, index;
	caddr_t va;
	u_long a;

	mutex_enter(&ppmap_lock);

#ifdef PPDEBUG
	pp_allocs++;
#endif PPDEBUG
	if (cache & CACHE_VAC) {
		color = sfmmu_get_ppvcolor(pp);
		if (color == -1) {
			if ((int)hint != -1) {
				color = addr_to_vcolor(hint);
			} else {
				color = addr_to_vcolor(mmu_ptob(pp->p_pagenum));
			}
		}

	} else {
		/*
		 * For physical caches, we can pick any address we want.
		*/
		color = 0;
	}

	do {
		for (nset = 0; nset < nsets; nset++) {
			index = clsettoarray(color, nset);
			va = ppmap_vaddrs[index];
			if (va != NULL) {
#ifdef PPDEBUG
				align_hits[color]++;
#endif PPDEBUG
				ppmap_last = color;
				ppmap_vaddrs[index] = NULL;
				mutex_exit(&ppmap_lock);
				hat_memload(kas.a_hat, &kas, va, pp, vprot,
					HAT_LOCK | HAT_NOSYNCLOAD);
				return (va);
			}
		}
		/*
		 * first pick didn't succeed, try another
		 */
		if (++color == ppmap_pages)
			color = 0;
	} while (color != ppmap_last);

#ifdef PPDEBUG
	ppalloc_noslot++;
#endif PPDEBUG
	mutex_exit(&ppmap_lock);

	/*
	 * No free slots; get a random one from kernelmap.
	 */

	a = rmalloc_wait(kernelmap, (long)CLSIZE);
	va = kmxtob(a);

	hat_memload(kas.a_hat, &kas, va, pp, vprot, HAT_LOCK | HAT_NOSYNCLOAD);

	return (va);

}

void
ppmapout(caddr_t va)
{
	int color, nset, index;

	if (va > Sysbase && va < Syslimit) {
		/*
		 * Space came from kernelmap, flush the page and
		 * return the space.
		 */
		hat_unload(&kas, va, PAGESIZE,
			(HAT_NOSYNCUNLOAD | HAT_UNLOCKUNLOAD));
		rmfree(kernelmap, (size_t)CLSIZE, (ulong_t)btokmx(va));
	} else {
		/*
		 * Space came from ppmap_vaddrs[], give it back.
		 */
		mutex_enter(&ppmap_lock);

		color = addr_to_vcolor(va);
		nset = ((u_int)va >> ppmap_shift) & (nsets - 1);
		if (cache & CACHE_VAC)
			ASSERT(color < ppmap_pages);
		index = clsettoarray(color, nset);
		if (ppmap_vaddrs[index] == NULL) {
			hat_unload(&kas, va, PAGESIZE,
				(HAT_NOSYNCUNLOAD | HAT_UNLOCKUNLOAD));
		} else
			panic("ppmapout");

		ppmap_vaddrs[index] = va;

		mutex_exit(&ppmap_lock);
	}
}

/*
 * Copy the data from the physical page represented by "frompp" to
 * that represented by "topp".
 */
void
ppcopy(page_t *frompp, page_t *topp)
{
	register caddr_t from_va, to_va;
	extern void bcopy();

	ASSERT(se_assert(&frompp->p_selock));
	ASSERT(se_assert(&topp->p_selock));

	from_va = ppmapin(frompp, PROT_READ, (caddr_t)-1);
	to_va   = ppmapin(topp, PROT_READ | PROT_WRITE, from_va);
	bcopy(from_va, to_va, PAGESIZE);
	ppmapout(from_va);
	ppmapout(to_va);
}

/*
 * Zero the physical page from off to off + len given by `pp'
 * without changing the reference and modified bits of page.
 */
void
pagezero(pp, off, len)
	page_t *pp;
	u_int off, len;
{
	extern void sync_icache();
	extern void doflush();
	extern int hwblkclr();
	register caddr_t va;

	ASSERT((int)len > 0 && (int)off >= 0 && off + len <= PAGESIZE);
	ASSERT(se_assert(&pp->p_selock));

	va = ppmapin(pp, PROT_READ | PROT_WRITE, (caddr_t)-1);
	if (hwblkclr(va + off, len)) {
		/*
		 * We may not have used block commit asi.
		 * So flush the I-$ manually
		 */
		sync_icache(va + off, len);
	} else {
	/*
	 * We have used blk commit, and flushed the I-$. However we still
	 * may have an instruction in the pipeline. Only a flush instruction
	 * will invalidate that.
	 */
		doflush(va);
	}
	ppmapout(va);
}
