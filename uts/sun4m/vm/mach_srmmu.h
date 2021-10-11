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

#ifndef _VM_MACH_SRMMU_H
#define	_VM_MACH_SRMMU_H

#pragma ident	"@(#)mach_srmmu.h	1.10	95/06/05 SMI"

#include <sys/x_call.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Macros for implementing MP critical sections in the hat layer.
 */
#ifdef  MP
extern int flushes_require_xcalls;
extern u_int avoid_capture_release;

/*
 * The CAPTURE_CPUS and RELEASE_CPUS macros can be used to implement
 * critical code sections where a set of CPUs are held while ptes are
 * updated and the TLB and VAC caches are flushed.  This prevents races
 * where a pte is being updated while another CPU is accessing this
 * pte or past instances of this pte in its TLB.  The current feeling is
 * that it may be possible to avoid these races without this explicit
 * capture-release protocol.  However, keep these macros around just in
 * case they are needed.
 * flushes_require_xcalls is set during the start-up sequence once MP
 * start-up is about to begin, and once the x-call mechanisms have been
 * initialized.
 * Note that some SRMMU-based machines may not need critical section
 * support, and they will never set flushes_require_xcalls.
 *
 * Lazy TLB flushing is not implemented at this time.
 */
#define	CAPTURE_CPUS    if (flushes_require_xcalls) \
				xc_capture_cpus(-1);

#define	RELEASE_CPUS    if (flushes_require_xcalls) \
				xc_release_cpus();

/*
 * The XCALL_PROLOG and XCALL_EPILOG macros are used before and after
 * code which performs x-calls.  XCALL_PROLOG will obtain exclusive access
 * to the x-call mailbox (for the particular x-call level) and will specify
 * the set of CPUs involved in the x-call.  XCALL_EPILOG will release
 * exclusive access to the x-call mailbox.
 * Note that some SRMMU machines may not need to perform x-calls for
 * cache flush operations, so they will not set flushes_require_xcalls.
 */
#define	XCALL_PROLOG    if (flushes_require_xcalls) \
				xc_prolog(-1);

#define	XCALL_EPILOG    if (flushes_require_xcalls) \
				xc_epilog();

#define	LOCAL_PROLOG    if (flushes_require_xcalls) \
				xc_prolog(CPUSET(CPU->cpu_id));

#define	LOCAL_EPILOG    if (flushes_require_xcalls) \
				xc_epilog();

#else   /* !MP */
#define	CAPTURE_CPUS
#define	RELEASE_CPUS
#define	XCALL_PROLOG
#define	XCALL_EPILOG
#define	LOCAL_PROLOG
#define	LOCAL_EPILOG
#endif  /* MP */

#define	MOD_VALID_PTE(srflags, ppte, val, va, lvl, hat, rmflags) \
	if ((srflags) & (SR_NOFLUSH | SR_TMP_TREE)) {		\
	    (void) mod_writepte((ppte), (val), (va), (lvl), -1, (rmflags)); \
	} else {				\
	    (void) mmu_writepte((ppte), (val), (va), (lvl), (hat), (rmflags)); \
	}

#define	SET_NEW_PTE(ppte, val, va, lvl, rmflags) \
	(void) mod_writepte((ppte), (val), (va), (lvl), -1, (rmflags))

#define	MOD_VALID_PTP(srflags, pptp, val, va, lvl, hat, flags)	\
	if ((srflags) & (SR_NOFLUSH | SR_TMP_TREE)) {		\
	    (void) mod_writeptp((pptp), (val), (va), (lvl), -1, (flags)); \
	} else {				\
	    (void) mmu_writeptp((pptp), (val), (va), (lvl), (hat), (flags));\
	}

#define	SET_NEW_PTP(pptp, val, va, lvl, flags) \
	(void) mod_writeptp((pptp), (val), (va), (lvl), -1, (flags))

#define	SET_TNEW_PTP(pptp, val, va, lvl, flags) \
	(void) mod_writeptp((pptp), (val), (va), (lvl), -1, (flags))

/*
 * These routines are all MMU-SPECIFIC interfaces to the srmmu routines.
 * These routines are called from machine specific places to do the
 * dirty work for things like boot initialization, mapin/mapout and
 * first level fault handling.
 */
u_int mmu_writepte(struct pte *pte, u_int entry, caddr_t addr,
		int level, struct hat *hat, int rmkeep);

u_int mod_writepte(struct pte *pte, u_int entry, caddr_t addr,
		int level, u_int cxn, int rmkeep);

u_int mmu_writepte_locked(struct pte *pte, u_int entry, caddr_t va,
		int level, u_int cxn, int rmkeep);

void mmu_writeptp(struct ptp *, u_int, caddr_t, int, struct hat *, int);
void mmu_writeptp_locked(struct ptp *, u_int, caddr_t, int, u_int, int);

void mod_writeptp(struct ptp *, u_int, caddr_t, int, u_int, int);

extern void srmmu_tlbflush(int level, caddr_t addr, u_int cxn, u_int flags);
extern void srmmu_vacflush(int level, caddr_t addr, u_int cxn, u_int flags);
extern u_int va_to_pfn(caddr_t vaddr);
extern u_int va_to_pa(u_int vaddr);
extern void hat_kern_setup(void);
extern void mmu_flushpagectx(caddr_t, u_int, u_int);
extern void mmu_flushseg(caddr_t, u_int, u_int);
extern void mmu_flushrgn(caddr_t, u_int, u_int);
extern void mmu_flushctx(u_int, u_int);
extern void vac_pageflush(caddr_t, u_int, u_int);
extern void vac_segflush(caddr_t, u_int, u_int);
extern void vac_rgnflush(caddr_t, u_int, u_int);
extern void vac_ctxflush(u_int, u_int);
extern void vac_noxlate_pgflush(caddr_t, u_int);

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_MACH_SRMMU_H */
