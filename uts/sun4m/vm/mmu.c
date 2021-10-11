/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */
#ident	"@(#)mmu.c	1.35	95/01/16 SMI"

/*
 * VM - Sun-4c low-level routines.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/mman.h>
#include <sys/map.h>
#include <sys/proc.h>

#include <vm/page.h>
#include <vm/seg.h>
#include <vm/as.h>

#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/stack.h>
#include <vm/hat_srmmu.h>
#include <sys/machsystm.h>
#include <vm/seg_kmem.h>

/* Only here for compatibility */

void
mmu_setpte(base, pte)
	caddr_t base;
	struct pte pte;
{
	srmmu_devload(kas.a_hat, &kas, base, NULL, pte.PhysicalPageNumber,
		srmmu_ptov_prot(&pte), PTELD_NOSYNC);
}


void
mmu_getpte(base, ppte)
	caddr_t base;
	struct pte *ppte;
{
	struct pte *tmp;
	struct ptbl *ptbl;
	int level;
	union ptpe tmp2;
	struct as *pas;
	kmutex_t *mtx;
	u_int off;

	if ((u_int) base >= KERNELBASE)
		pas = &kas;
	else
		pas = curproc->p_as;

	tmp = srmmu_ptefind(pas, base, &level, &ptbl, &mtx,
		LK_PTBL_SHARED);
	mmu_readpte(tmp, (struct pte *) &tmp2.ptpe_int);
	unlock_ptbl(ptbl, mtx);

	if (pte_valid(&tmp2.pte)) {
		switch (level) {
		case 3:
			*ppte = tmp2.pte;
			return;

		case 2:
			off = (u_int)base & 0x3FFFF;
			off >>= MMU_PAGESHIFT;
			tmp2.pte.PhysicalPageNumber += off;
			*ppte = tmp2.pte;
			return;

		case 1:
			off = (u_int)base & 0xFFFFFF;
			off >>= MMU_PAGESHIFT;
			tmp2.pte.PhysicalPageNumber += off;
			*ppte = tmp2.pte;
			return;
		}
	} else {
		tmp2.ptpe_int = 0;
		*ppte = tmp2.pte;
	}
}

void
mmu_getkpte(base, ppte)
	caddr_t base;
	struct pte *ppte;
{
	mmu_getpte(base, ppte);
}
