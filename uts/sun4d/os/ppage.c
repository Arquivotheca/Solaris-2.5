/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ppage.c	1.7	93/11/11 SMI"

#include <sys/t_lock.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/vm.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/bcopy_if.h>
#include <vm/as.h>
#include <vm/hat_srmmu.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <sys/debug.h>

/*
 * Architecures with a vac use this routine to quickly make
 * a vac-aligned mapping.  We don't have a vac, so we don't
 * care about that - just make this simple.
 */
caddr_t
ppmapin(pp, vprot, avoid)
	register struct page *pp;
	register u_int vprot;
	register caddr_t avoid;
{
	u_long a;
	caddr_t va;
#ifdef lint
	avoid = avoid;
#endif

	a = rmalloc_wait(kernelmap, (long)CLSIZE);
	va = kmxtob(a);

	hat_memload(kas.a_hat, &kas, va, pp, vprot, PTELD_LOCK | PTELD_NOSYNC);

	return (va);
}

void
ppmapout(va)
	register caddr_t va;
{
	hat_unload(&kas, va, PAGESIZE, HAT_UNLOCKUNLOAD);
	rmfree(kernelmap, (size_t)CLSIZE, (ulong_t)btokmx(va));
}

/*
 * Copy the data from the physical page represented by "frompp" to
 * that represented by "topp".
 */
void
ppcopy(page_t *frompp, page_t *topp)
{
	u_int cacheable, spfn, dpfn;

	ASSERT(se_assert(&frompp->p_selock));
	ASSERT(se_assert(&topp->p_selock));

	cacheable = (1 << (36 - MMU_STD_FIRSTSHIFT));
	spfn = frompp->p_pagenum | cacheable;
	dpfn = topp->p_pagenum | cacheable;

	ASSERT(spfn != (u_int)-1);
	ASSERT(dpfn != (u_int)-1);

	hwpage_copy(spfn, dpfn);
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

	if (off == 0 && len == PAGESIZE) {
		u_int cacheable = (1 << (36 - MMU_STD_FIRSTSHIFT));
		u_int pfn = pp->p_pagenum | cacheable;

		hwpage_zero(pfn);
	} else {
		va = ppmapin(pp, PROT_READ | PROT_WRITE, (caddr_t)-1);
		bzero(va + off, len);
		ppmapout(va);
	}
}
