/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ppage.c	1.7	93/11/11 SMI"

#include <sys/t_lock.h>
#include <sys/map.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/vm.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/cmn_err.h>
#include <sys/machsystm.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/hat_sunm.h>
#include <sys/debug.h>

/*
 * A quick way to generate a cache consistent address
 * to map in a page for sun4 type machines.
 *
 * The 4/110  uses this even though it doesn't need to pick
 * vac consistent addresses.  Thus the constants for this piece
 * of code instead of using vac_size and vac_mask which
 * have a mainingful values for all sun4s except the 4/110.
 *
 * users: ppcopy, pagezero, /proc, dev/mem
 *
 * VACSZBY	- size of vac in bytes
 * VACSZPG	- size of vac in pages
 * VACMASK	- page offset in cache
 * NSETS 	- number of multiple pages that are can be mapped
 *			consistently, must be a power of two
 * The PPMAP area is 256 k in size so it allows two sets.
 */
#define	VACSZBY		0x20000
#define	VACSZPG		(VACSZBY/PAGESIZE)
#define	VACMASK		((VACSZBY-1) & MMU_PAGEMASK)
#define	VACSHIFT	17
#define	NSETS		2

static caddr_t	ppmap_vaddrs[VACSZPG][NSETS];
static int	ppmap_last = 0;

static kmutex_t	ppmap_lock;

void
ppmapinit()
{
	register int align, nset;
	register caddr_t va;

	mutex_init(&ppmap_lock, "ppmap_lock", MUTEX_DEFAULT, NULL);
	mutex_enter(&ppmap_lock);
	va = (caddr_t)PPMAPBASE;
	for (align = 0; align < VACSZPG; align++) {
		for (nset = 0; nset < NSETS; nset++) {
			ppmap_vaddrs[align][nset] =
				va + nset * VACSZBY;
		}
		va += MMU_PAGESIZE;
	}
	mutex_exit(&ppmap_lock);
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
 */
caddr_t
ppmapin(pp, vprot, avoid)
	register struct page *pp;
	register u_int vprot;
	register caddr_t avoid;
{
	register int align, av_align, nset;
	register caddr_t va = NULL;
	union {
		struct pte u_pte;
		int u_ipte;
	} pte;
	u_long a;

	mutex_enter(&ppmap_lock);
	if (pp->p_mapping && vac) {
		/*
		 * Find an unused cache consistent slot to map the page in.
		 */
		align = pp->p_mapping->hme_impl & vac_mask >> MMU_PAGESHIFT;
		for (nset = 0; nset < NSETS; nset++) {
			if ((int)ppmap_vaddrs[align][nset] < 0) {
				va = ppmap_vaddrs[align][nset];
				break;
			}
		}
	} else {
		/*
		 * The page has no mappings, common COW case.
		 * Avoid using a mapping that collides with
		 * the avoid address parameter in the cache.
		 * We can pick any alignment except the one we are
		 * supposed to avoid, if the first pick isn't
		 * avaliable we try others.
		 */
		align = ppmap_last++;
		if (ppmap_last >= VACSZPG)
			ppmap_last = 0;

		if (avoid != (caddr_t)-1) {
			av_align = ((u_int)avoid & vac_mask) >> MMU_PAGESHIFT;
			if (align == av_align) {
				if (++align >= VACSZPG)
					align = 0;
			}
		}

		do {
			for (nset = 0; nset < NSETS; nset++) {
				if ((int)ppmap_vaddrs[align][nset] < 0) {
					va = ppmap_vaddrs[align][nset];
					break;
				}
			}
			if (va)
				break;
			/*
			 * first pick didn't succeed, try another
			 */
			if (++align >= VACSZPG)
				align = 0;
		} while (align != ppmap_last);
	}

	if (va == NULL) {
		mutex_exit(&ppmap_lock);
		/*
		 * No free slots; get a random one from kernelmap.
		 */

		a = rmalloc_wait(kernelmap, (long)CLSIZE);
		va = kmxtob(a);

		sunm_mempte(pp, vprot, &pte.u_pte);
		sunm_pteload(&kas, va, pp, pte.u_pte,
			PTELD_LOCK | PTELD_NOSYNC);
	} else if (!pp->p_mapping) {
		/*
		 * page had no mappings, short circuit everything.
		 */
		ppmap_vaddrs[align][nset] = (caddr_t)1;
		mutex_exit(&ppmap_lock);
		pte.u_ipte = 0;
		pte.u_pte.pg_prot = sunm_vtop_prot(vprot);
		pte.u_ipte |= (PG_V | PGT_OBMEM | page_pptonum(pp));
		mmu_setpte(va, pte.u_pte);
	} else {
		/*
		 * Page had mappings, use the hat layer.
		 */
		ppmap_vaddrs[align][nset] = (caddr_t)2;
		mutex_exit(&ppmap_lock);
		sunm_mempte(pp, vprot, &pte.u_pte);
		sunm_pteload(&kas, va, pp, pte.u_pte,
			PTELD_LOCK | PTELD_NOSYNC);
	}
	return (va);
}

void
ppmapout(va)
	register caddr_t va;
{
	register int align, nset;

	if (va > Sysbase) {
		/*
		 * Space came from kernelmap, flush the page and
		 * return the space.
		 */
		sunm_unload(&kas, va, PAGESIZE, HAT_UNLOAD);
		rmfree(kernelmap, (size_t)CLSIZE, (ulong_t)btokmx(va));
	} else {
		/*
		 * Space came from ppmap_vaddrs[], give it back.
		 */
		align = ((u_int)va & VACMASK) >> MMU_PAGESHIFT;

		nset = ((int)va & ((NSETS - 1) << VACSHIFT)) >> VACSHIFT;

		if (ppmap_vaddrs[align][nset] == (caddr_t)1) {
			vac_pageflush(va);
			mmu_setpte(va, mmu_pteinvalid);
		} else if (ppmap_vaddrs[align][nset] == (caddr_t)2) {
			sunm_unload(&kas, va, PAGESIZE, HAT_UNLOAD);
		} else
			cmn_err(CE_PANIC, "ppmapout");

		mutex_enter(&ppmap_lock);
		ppmap_vaddrs[align][nset] = va;
		mutex_exit(&ppmap_lock);
	}
}

/*
 * Copy the data from the physical page represented by "frompp" to
 * that represented by "topp".
 */
void
ppcopy(frompp, topp)
	page_t *frompp;
	page_t *topp;
{
	register caddr_t from_va, to_va;

	ASSERT(se_assert(&frompp->p_selock));
	ASSERT(se_assert(&topp->p_selock));

	from_va = ppmapin(frompp, PROT_READ, (caddr_t)-1);
	to_va   = ppmapin(topp, PROT_READ | PROT_WRITE, from_va);
	pgcopy(from_va, to_va, PAGESIZE);
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
	register caddr_t va;

	ASSERT((int)len > 0 && (int)off >= 0 && off + len <= PAGESIZE);
	ASSERT(se_assert(&pp->p_selock));

	va = ppmapin(pp, PROT_READ | PROT_WRITE, (caddr_t)-1);
	bzero(va + off, len);
	ppmapout(va);
}

#ifdef PPDEBUG
/*
 * Calculate the checksum for the physical page given by `pp'
 * without changing the reference and modified bits of page.
 */
int
pagesum(pp)
	struct page *pp;
{
	register caddr_t va;
	register int checksum = 0;
	register int *last, *ip;

	va = ppmapin(pp, PROT_READ, (caddr_t)-1);

	last = (int *)(va + PAGESIZE);
	for (ip = (int *)va; ip < last; ip++)
		checksum += *ip;

	ppmapout(va);

	return (checksum);
}
#endif PPDEBUG
