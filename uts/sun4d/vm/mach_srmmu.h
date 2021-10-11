/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

/*
 * VM - Hardware Address Translation management.
 *
 * This file describes the contents of the sun referernce mmu (srmmu)
 * specific hat data structures and the srmmu specific hat procedures.
 * The machine independent interface is described in <vm/hat.h>.
 */

#ifndef _VM_MACH_HAT_SRMMU_H
#define	_VM_MACH_HAT_SRMMU_H

#pragma ident	"@(#)mach_srmmu.h	1.11	95/03/07 SMI"


#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	CAPTURE_CPUS
#define	RELEASE_CPUS
#define	XCALL_PROLOG
#define	XCALL_EPILOG

#define	MOD_VALID_PTE(srflags, ppte, val, va, lvl, hat, rmflags) \
	if ((srflags) & (SR_NOFLUSH)) {		\
		swapl((val), (int *) (ppte));	\
	} else {				\
		(void) mmu_writepte((ppte), (val), (va), (lvl), \
		    (hat), (rmflags)); \
	}

#define	SET_NEW_PTE(ppte, val, va, lvl, rmflags) \
	swapl((val), (int *) (ppte))

#define	MOD_VALID_PTP(srflags, pptp, val, va, lvl, hat, flags) \
	if ((srflags) & SR_NOFLUSH) {		\
		swapl((val), (int *) (pptp));	\
	} else {				\
		(void) mmu_writeptp((pptp), (val), (va), (lvl), \
		    (hat), (flags)); \
	}

#define	SET_NEW_PTP(pptp, val, va, lvl, rmflags) \
	swapl((val), (int *) (pptp))

#define	DEMAP

typedef unsigned long long pa_t;

/*
 * page table pointer to physical address (36 bits) and back.
 */
#define	ptp_to_pa(ptp)	(((pa_t)((u_int)(ptp) & ~PTE_ETYPEMASK)) << 4)
#define	pa_to_ptp(pa)	((u_int) ((pa) >> 4) | MMU_ET_PTP)

/*
 * Various macros to read ptbl fields. This is needed for a 4m h/w bug
 * workaround.
 */
#define	mmu_readpte(src, dst)	(*(struct pte *)(dst) = *(struct pte *)(src))
#define	mmu_readptp(src, dst)	(*(struct ptp *)(dst) = *(struct ptp *)(src))

/*
 * These routines are all MMU-SPECIFIC interfaces to the srmmu routines.
 * These routines are called from machine specific places to do the
 * dirty work for things like boot initialization, mapin/mapout and
 * first level fault handling.
 */

u_int 	mmu_writepte(struct pte *pte, u_int entry, caddr_t addr,
		int level, struct hat *hat, int rmkeep);

u_int	mmu_writepte_locked(struct pte *pte, u_int entry, caddr_t va,
		int level, u_int cxn, int rmkeep);

void	mmu_writeptp(struct ptp *, u_int, caddr_t, int, struct hat *, int);

void	mmu_writeptp_locked(struct ptp *, u_int, caddr_t, int, u_int, int);

int	srmmu_xlate(int, caddr_t, pa_t *, union ptpe *, int *);

int	srmmu_pte_pa(int, caddr_t, pa_t *, int *);

/*
 * conversion functions, 4m passes addresses as u_int so we'll
 * do the same to keep the code similar
 */
u_int va_to_pfn(caddr_t vaddr);	/* used in kvm_init() */
pa_t va_to_pa(caddr_t vaddr);	/* used in startup() and */
					/* mach_hat_srmmu.c */

/*
 * assembly routines used by hat_srmmu.c
 */
u_int ldphys36(pa_t paddr);
u_int stphys36(pa_t paddr, u_int val);

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_MACH_HAT_SRMMU_H */
