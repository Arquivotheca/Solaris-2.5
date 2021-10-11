/*
 * Copyright (c) 1990, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)lock_prim.s	1.5	93/08/17 SMI"

#if defined(lint)
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#else	/* lint */
#include "assym.s"
#endif	/* lint */

#include <sys/t_lock.h>
#include <sys/mutex.h>
#include <sys/mutex_impl.h>
#include <sys/asm_linkage.h>
#include <sys/machlock.h>
#include <sys/machthread.h>

/* #define DEBUG */

#ifdef DEBUG
#include <sys/machparam.h>
#endif DEBUG

/************************************************************************
 *		ATOMIC OPERATIONS
 */

/*
 * uint8_t	ldstub(uint8_t *cp)
 *
 * Store 0xFF at the specified location, and return its previous content.
 */

#if defined(lint)
uint8_t
ldstub(uint8_t *cp)
{
	uint8_t	rv;
	rv = *cp;
	*cp = 0xFF;
	return rv;
}
#else	/* lint */

	ENTRY(ldstub)
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	retl
	ldstub	[%o0], %o0
	SET_SIZE(ldstub)

#endif	/* lint */

/*
 * uint32_t	swapl(uint32_t *lp, uint32_t nv)
 *
 * store a new value into a 32-bit cell, and return the old value.
 */

#if defined(lint)
uint32_t
swapl(uint32_t *lp, uint32_t nv)
{
	uint32_t	rv;
	rv = *lp;
	*lp = nv;
	return rv;
}
#else	/* lint */

	ENTRY(swapl)
	swap	[%o0], %o1
	retl
	mov	%o1, %o0
	SET_SIZE(swapl)

#endif	/* lint */

/*
 * uint32_t	cas(uint32_t *lp, uint32_t cv, uint32_t nv)
 *
 * store a new value into a 32-bit cell, and return the old value.
 */

#if defined(lint)
uint32_t
cas(uint32_t *lp, uint32_t cv, uint32_t nv)
{
	uint32_t	rv;
	rv = *lp;
	if (rv == cv)
		*lp = nv;
	return rv;
}
#else	/* lint */

	ENTRY(cas)
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	cas	[%o0], %o1, %o2
	retl
	mov	%o2, %o0
	SET_SIZE(cas)

#endif	/* lint */

/*
 * uint64_t	casx(uint64_t *lp, uint64_t cv, uint64_t nv)
 *
 * store a new value into a 64-bit cell, and return the old value.
 */

#if defined(lint)
uint64_t
casx(uint64_t *lp, uint64_t cv, uint64_t nv)
{
	uint64_t	rv;
	rv = *lp;
	if (rv == cv)
		*lp = nv;
	return rv;
}
#else	/* lint */

	ENTRY(casx)
	sllx	%o1, 32, %g1
	srl	%o2, 0, %o2
	sllx	%o3, 32, %g2
	srl	%o4, 0, %o4
	or	%g1, %o2, %g1
	or	%g2, %o4, %g2
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	casx	[%o0], %g1, %g2
	srlx	%g2, 32, %o0
	retl
	srl	%g2, 0, %o1
	SET_SIZE(casx)

#endif	/* lint */

/************************************************************************
 *		MEMORY BARRIERS
 */

/*
 * void		stbar(void)
 *
 * store barrier: guarantee that no stores issued after this call
 * become visible before stores issued before this call.
 */

#if defined(lint)

void
stbar(void)
{
	;
}

#else	/* lint */

	ENTRY(stbar)
	retl
	membar	#StoreStore
	SET_SIZE(stbar)

#endif	/* lint */

/*
 * void		mem_sync(void)
 * void		lock_mutex_flush(void)
 *
 * Existing entry point for logically stalling until all stores from the current
 * processor have reached global visibility. It would apparently be acceptable for
 * this to be a simple memory barrier as opposed to a full issue barrier. These
 * entry points are for backward compatibility only, nd may go away if they are
 * not really used outside current code specific to Sun-4M and Sun-4D platforms.
 */

#if defined(lint)

void
mem_sync(void)
{
	;
}

void
lock_mutex_flush(void)
{
	;
}

#else	/* lint */

	ENTRY(mem_sync)
	ALTENTRY(lock_mutex_flush)
	retl
	membar	#StoreStore|#StoreLoad
	SET_SIZE(lock_mutex_flush)
	SET_SIZE(mem_sync)

#endif	/* lint */

/*
 * void		membar_enter(void)
 *
 * Generic memory barrier used during lock entry, placed after the
 * memory operation that acquires the lock to guarantee that the lock
 * protects its data. No stores from after the memory barrier will
 * reach visibility, and no loads from after the barrier will be
 * resolved, before the lock acquisition reaches global visibility.
 */

#if defined(lint)

void
membar_enter(void)
{
	;
}

#else	/* lint */

	ENTRY(membar_enter)
	retl
	membar	#StoreLoad|#StoreStore
	SET_SIZE(membar_enter)

#endif	/* lint */

/*
 * void		membar_exit(void)
 *
 * Generic memory barrier used during lock exit, placed before the
 * memory operation that releases the lock to guarantee that the lock
 * protects its data. All loads and stores issued before the barrier
 * will be resolved before the subsequent lock update reaches visibility.
 */

#if defined(lint)

void
membar_exit(void)
{
	;
}

#else	/* lint */

	ENTRY(membar_exit)
	retl
	membar	#LoadStore|#StoreStore
	SET_SIZE(membar_exit)

#endif	/* lint */

/*
 * void		membar_producer(void)
 *
 * Arranges that all stores issued before this point in the code reach
 * global visibility before any stores that follow this point; useful
 * in producer modules that update a data item, then set a flag that
 * it is now available; the memory barrier guarantees that the
 * available flag is not visible earlier than the updated data.
 */

#if defined(lint)

void
membar_producer(void)
{
	;
}

#else	/* lint */

	ENTRY(membar_producer)
	retl
	membar	#StoreStore
	SET_SIZE(membar_producer)

#endif	/* lint */

/*
 * void		membar_consumer(void)
 *
 * Arranges that all loads issued before this point in the code are
 * completed before the loads following this point in the code; useful
 * in consumer modules that check to see if data is available and read
 * the data; the mnemory barrier guarantees that the data is not
 * sampled until after the available flag has been observed.
 */

#if defined(lint)

void
membar_consumer(void)
{
	;
}

#else	/* lint */

	ENTRY(membar_consumer)
	retl
	membar	#LoadLoad
	SET_SIZE(membar_consumer)

#endif	/* lint */

/************************************************************************
 *		MINIMUM LOCKS
 */

/*
 * void
 * lock_set(lock_t *lp)
 */

#if defined(lint)

void
lock_set(lock_t *lp)
{
	extern char *panicstr;
	extern void panic(char *, ...);
	extern char *lock_panic_msg;

	if (ldstub(lp)) {
		if (panicstr)
			goto out;
		if (ncpus == 1)
			panic(lock_panic_msg);
		do {
			while (LOCK_HELD(lp))
				;
		} while (ldstub(lp));
	}
out:
	membar_enter();
}

#else	/* lint */

	ENTRY(lock_set)
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%o0], %g1
	brz,pt	%g1, 3f
	  sethi	%hi(panicstr), %o5
	ld	[%o5+%lo(panicstr)],%g1
	brnz,pn	%g1, 3f			/* if panicing just return */
	  sethi	%hi(ncpus), %o5
	ld	[%o5+%lo(ncpus)], %g1
	dec	%g1
	brnz,a	%g1, 2f			/* if ncpus = 1 then panic */
	  ldub	[%o0], %g1

	save	%sp, -SA(MINFRAME), %sp
	set	lock_panic_msg, %o0
	call	panic,1
	restore
2:
	brnz,a	%g1, 2b
	  ldub	[%o0], %g1
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%o0], %g1
	brnz,a	%g1, 2b
	  ldub	[%o0], %g1
3:
	retl
	membar	#StoreLoad|#StoreStore
	SET_SIZE(lock_set)

#endif	/* lint */

/*
 * lock_try(lp), ulock_try(lp)
 *	- returns non-zero on success.
 *	- doesn't block interrupts so don't use this to spin on a lock.
 *	- uses "0xFF is busy, anything else is free" model.
 *
 *      ulock_try() is for a lock in the user address space.
 *      For all V7/V8 sparc systems they are same since the kernel and
 *      user are mapped in a user' context.
 *      For V9 platforms the lock_try and ulock_try are different impl.
 *
 */

#if defined(lint)

/* ARGSUSED */
int
lock_try(lock_t *lp)
{
	return 0xFF ^ ldstub(lp);
}

/* ARGSUSED */
int
ulock_try(lock_t *lp)
{ return (0); }

#else	/* lint */

	ENTRY(lock_try)
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%o0], %o1		! try to set lock, get value in %o1
	xor	%o1, 0xff, %o0		! delay - return non-zero if success
	retl
	membar	#StoreLoad|#StoreStore
	SET_SIZE(lock_try)

        ENTRY(ulock_try)
#ifdef DEBUG
	sethi   %hi(USERLIMIT), %o1
	cmp     %o0, %o1
	blu	1f
	nop
 
	set     2f, %o0
	call    panic
	nop
1:
#endif DEBUG
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstuba	[%o0]ASI_USER, %o1	! try to set lock, get value in %o1
	xor     %o1, 0xff, %o0		! delay - return non-zero if success
	retl
	membar	#StoreLoad|#StoreStore
	SET_SIZE(ulock_try)

	.seg	".data"
2:
	.asciz  "ulock_try: Argument is above USERLIMIT"
	.align  4
#endif	/* lint */

/*
 * lock_clear(lp), ulock_clear(lp)
 *	- unlock lock without changing interrupt priority level.
 *
 *      ulock_clear() is for a lock in the user address space.
 *      For all V7/V8 sparc systems they are same since the kernel and
 *      user are mapped in a user' context.
 *      For V9 platforms the lock_clear and ulock_clear are different impl.
 */

#if defined(lint)

/* ARGSUSED */
void
lock_clear(lock_t *lp)
{}

/* ARGSUSED */
void
ulock_clear(lock_t *lp)
{}

#else	/* lint */

	ENTRY(lock_clear)
	membar	#LoadStore|#StoreStore
	retl
	clrb	[%o0]
	SET_SIZE(lock_clear)

	ENTRY(ulock_clear)
#ifdef DEBUG
	sethi   %hi(USERLIMIT), %o1
	cmp     %o0, %o1
	blu     1f
	nop
 
	set     2f, %o0
	call    panic
	nop
1:
#endif DEBUG
 
	membar	#LoadStore|#StoreStore
	retl
	stba	%g0, [%o0]ASI_USER
	SET_SIZE(ulock_clear)

	.seg	".data"
2:
	.asciz  "ulock_clear: argument above USERLIMIT"
	.align 4
#endif	/* lint */


/*
 * lock_set_spl(lp, pl)
 * 	Returns old pl.
 */

#if defined(lint)

/* ARGSUSED */
int
lock_set_spl(lock_t *lp, int pl)
{
	extern int splr(int);
	extern int splx(int);
	extern char *panicstr;
	extern void panic(char *, ...);
	extern char *lock_panic_msg;

	int s;

	s = splr(pl);
	if (ldstub(lp)) {
		if (panicstr)
			return s;
		if (ncpus == 1)
			panic(lock_panic_msg);
		do {
			(void)splx(s);
			while (LOCK_HELD(lp))
				;
			s = splr(pl);
		} while (ldstub(lp));
	}
	return s;
}

#else	/* lint */

	ENTRY(lock_set_spl)
!!
!! XXX4U: when we get the final version of "splr", bring it inline
!!	and revise this code so we don't require a register window.
!!
	save	%sp, -SA(MINFRAME), %sp
	call	splr, 1
	  mov	%i1, %o0
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%i0], %g1			! try the lock
	brz,pt	%g1, 3f				! if we got it, all done.
	  mov	%o0, %i5
	sethi	%hi(panicstr), %o0
	ld	[%o0+%lo(panicstr)], %g1		! check panicstr
	brnz,pn	%g1, 3f			! if nonzero, pretend we got the lock.
	  sethi	%hi(ncpus), %o1
	ld	[%o1+%lo(ncpus)], %o1		! check for uniprocessor
	cmp	%o1, 1				! if on a uni, panic.
	bne,pt	%icc, 1f
	  sethi	%hi(lock_panic_msg), %o0
	call	panic, 1
	  or	%o0, %lo(lock_panic_msg), %o0	! [internal]
1:
	call	splx, 1				! drop spl before spinning
	  mov	%i5, %o0
2:
	ldub	[%i0], %g1			! check contents of lock
	brnz,pt	%g1, 2b				! until it looks free
	  nop
	call	splr, 1				! raise SPL again
	  mov	%i1, %o0
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%i0], %g1			! try for the lock
	brnz,pn	%g1, 1b				! loop back if we fail
	  mov	%o0, %i5
3:
	membar	#StoreLoad|#StoreStore
	ret
	restore	%g0,%i5,%o0			! return SPL we raised from
	SET_SIZE(lock_set_spl)

	.seg	".data"
lock_panic_msg:
	.asciz	"lock_set: lock held and only one CPU"
	.align	4

#endif	/* lint */

/*
 * lock_clear_splx(lp, s)
 */

#if defined(lint)

void
lock_clear_splx(lock_t *lp, int s)
{
	extern int splx(int);

	lock_clear(lp);
	(void) splx(s);
}

#else	/* lint */

	.global	splx

	ENTRY(lock_clear_splx)
	membar	#LoadStore|#StoreStore
	clrb	[%o0]			! clear lock
	b	splx			! let the real splx() do the rest
	mov	%o1, %o0		! delay - set arg for splx
	SET_SIZE(lock_clear_splx)

#endif	/* lint */

#ifndef DISP_LOCK_STATS
/*
 * disp_lock_enter - get a dispatcher lock after raising spl to block other
 *	dispatcher activity.
 * - %o0 contains address of lock byte.
 */
#if defined(lint)

/* ARGSUSED */
void
disp_lock_enter(disp_lock_t *mp)
{
	curthread->t_oldspl = lock_set_spl(mp, LOCK_LEVEL);
}

#else	/* lint */

	ENTRY(disp_lock_enter)
!!
!!XXX4U: Once lock_set_spl is optimized, bring some of it inline
!!	so we can avoid buying the register window.
!!
	save	%sp, -SA(MINFRAME), %sp
	set	LOCK_LEVEL, %o1
	call	lock_set_spl
	mov	%i0, %o0
	sth	%o0, [THREAD_REG + T_OLDSPL]	! save old spl in thread
#ifdef DISP_DEBUG
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
#endif
	ret				! return
	restore
	SET_SIZE(disp_lock_enter)
#endif	/* lint */

/*
 * disp_lock_exit - drop dispatcher lock.
 */
#if defined(lint)

/* ARGSUSED */
void
disp_lock_exit(disp_lock_t *mp)
{}

#else	/* lint */

	ENTRY(disp_lock_exit)
	ALTENTRY(disp_mutex_exit)	/* XXX */
#ifdef DISP_DEBUG
	save	%sp, -SA(MINFRAME), %sp
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
	restore
#endif
#ifdef	DEBUG
	ldub	[%o0], %g1		! load lock byte
	brz	%g1, .disp_lock_exit_panic		! lock not held - panic
	ld      [THREAD_REG + T_CPU], %o3       ! delay - get CPU pointer
#else
	ld      [THREAD_REG + T_CPU], %o3       ! get CPU pointer
#endif
	ldub    [%o3 + CPU_KPRUNRUN], %g1       ! get CPU->cpu_kprunrun
	membar	#LoadStore|#StoreStore
	brnz	%g1, 1f			! preemption needed
	clrb    [%o0]			! delay - drop lock 
	b       splx                    ! let splx reset priority and return
	lduh	[THREAD_REG + T_OLDSPL], %o0	! delay - get old spl
	/*
	 * Attempt to preempt
	 */
1:
	save    %sp, -SA(MINFRAME), %sp
	call    splx
	lduh	[THREAD_REG + T_OLDSPL], %o0	! delay - get old spl
	call    kpreempt
	restore	%g0, -1, %o0		! delay - pass -1 to indicate sync call

#ifdef DEBUG
.disp_lock_exit_panic:
	sethi	%hi(.disp_lock_exit_msg), %o0
	call	panic			! panic - will not return
	or	%o0, %lo(.disp_lock_exit_msg), %o0	! delay
#endif DEBUG
	SET_SIZE(disp_lock_exit)

#ifdef DEBUG
	.seg	".data"
.disp_lock_exit_msg:
	.asciz	"disp_lock_exit: lock not held"
	.align	4
#endif /* DEBUG */
	

#endif	/* lint */

/*
 * disp_lock_enter_high - Get lock without changing spl level.
 */
#if defined(lint)

/* ARGSUSED */
void
disp_lock_enter_high(disp_lock_t *mp)
{}

#else	/* lint */

	ENTRY(disp_lock_enter_high)
#ifdef DISP_DEBUG
	save	%sp, -SA(MINFRAME), %sp
	call	lock_set
	mov	%i0, %o0

	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
	ret
	restore
#else /* DISP_DEBUG */
	b	lock_set
	nop
#endif /* DISP_DEBUG */
	SET_SIZE(disp_lock_enter_high)
#endif	/* lint */

/*
 * disp_lock_exit_nopreempt - Drop dispatcher lock without checking preemption.
 */
#if defined(lint)

/* ARGSUSED */    
void
disp_lock_exit_nopreempt(disp_lock_t *mp)
{}
 
#else /* lint */
 
	ENTRY(disp_lock_exit_nopreempt)
#ifdef DISP_DEBUG
	save	%sp, -SA(MINFRAME), %sp
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
	restore
#endif /* DISP_DEBUG */
#ifdef DEBUG
	ldub	[%o0], %o1		! panic if lock not already held
	brz	%o1, .disp_lock_exit_panic	! lock not held - panic
	nop
#endif /* DEBUG */
	membar	#LoadStore|#StoreStore
	clrb    [%o0]
	b       splx                    ! let splx reset priority and return
	lduh	[THREAD_REG + T_OLDSPL], %o0	! delay - get old SPL
	SET_SIZE(disp_lock_exit_nopreempt)
#endif /* lint */
 
/*
 * Clear dispatcher spinlock, but leave the spl high.
 */
#if defined(lint)
 
/* ARGSUSED */
void
disp_lock_exit_high(disp_lock_t *mp)
{}
 
#else /* lint */
 
	ENTRY(disp_lock_exit_high)
#ifdef DISP_DEBUG
	save	%sp, -SA(MINFRAME), %sp
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
	restore
#endif
#ifdef DEBUG
	ldub	[%o0], %o1		! panic if lock not already held
	brz	%o1, .disp_lock_exit_panic	! lock not held - panic
	nop
#endif /* DEBUG */
	membar	#LoadStore|#StoreStore
	retl
	clrb    [%o0]      		! delay - clear lock
	SET_SIZE(disp_lock_exit_high)
#endif /* lint */
 
#endif	/* DISP_LOCK_STATS */


/*
 * mutex_enter() and mutex_exit().
 * 
 * These routines do the simple case of mutex_enter when the lock is not
 * held, and mutex_exit when no thread is waiting for the lock.
 * If anything complicated is going on, the C version (mutex_adaptive_enter) 
 * is called.
 *
 * These routines do not do lock tracing.  If that is needed,
 * a separate mutex type could be used.
 *
 * Also see comments in sparc9/sys/mutex.h.
 *
 * XXX4U: we want to change the owner code to be, precisely,
 * the actual thread pointer.
 * 
 * XXX4U: we can also invert the two words in the mutex and
 * use casx, if we can guarantee that mutexes are aligned
 * on doubleword bounds. Unfortunately, previous structure
 * definition implies that binaries may exist that place
 * the mutex on longword but not doubleword bounds. NB that
 * doing this is part of a provable solution to the race
 * case in mutex_exit vs mutex_destroy.
 */

#if defined (lint)

/* ARGSUSED */
void
mutex_enter(kmutex_t *lp)
{}

#else
	.seg	".text"

	ENTRY(mutex_enter)
	mov	THREAD_REG, %g1
.Bcrit:
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	cas	[%o0], %g0, %g1			! try to get it. XXX4U: requires M_OWNER==0
	brnz	%g1, mutex_adaptive_enter	! locked or wrong type
	nop
	retl
	membar	#StoreLoad|#StoreStore
.Bhole:
	SET_SIZE(mutex_enter)

#endif /* lint */

/*
 * Similar to mutex_enter() above, but here we return zero if the lock
 * cannot be acquired, and nonzero on success.  Same tricks here.
 * This is only in assembler because of the tricks.
 */
#if defined (lint)

/* ARGSUSED */
int
mutex_adaptive_tryenter(kmutex_t *lp)
{ return (0); }

#else
	.seg	".text"

	ENTRY(mutex_adaptive_tryenter)
	mov	THREAD_REG, %o1
.Ehole:
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	cas	[%o0], %g0, %o1			! try to get it. XXX4U: requires M_OWNER==0
	clr	%o0
	membar	#StoreLoad|#StoreStore
.Ecrit:
	retl
	movrz	%o1, 1, %o0	! if we got a zero, return true
	SET_SIZE(mutex_adaptive_tryenter)

#ifdef DEBUG

#define MUTEX_CRITICAL_VERIFIER \
	(.Bcrit - MUTEX_CRITICAL_UNION_START) | \
	(.Bcrit + MUTEX_CRITICAL_REGION_SIZE - .Bhole) | \
	(.Bhole + MUTEX_CRITICAL_HOLE_SIZE - .Ehole) | \
	(.Ehole + MUTEX_CRITICAL_REGION_SIZE - (2*4)  - .Ecrit) | \
	(.Bcrit + MUTEX_CRITICAL_UNION_SIZE - (2*4) - .Ecrit)
	.seg    ".data"
        .align  4
        .global mutex_critical_verifier
mutex_critical_verifier:
        .word   MUTEX_CRITICAL_VERIFIER
#endif


#endif /* lint */


#if defined(lint)

/* ARGSUSED */
void
mutex_exit(kmutex_t *lp)
{}

void
mutex_exit_nofault(void)		/* label where fault might occur */
{}

/* ARGSUSED */
void
mutex_exit_fault(kmutex_t *lp)		/* label where fault continues */
{}

#else
	ENTRY(mutex_exit)
	membar	#LoadStore|#StoreStore
	mov	THREAD_REG, %o1
	clr	%o2
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	cas	[%o0], %o1, %o2
	cmp	%o1, %o2
	bnz	mutex_vector_exit
	nop

	!
	! Check the waiters field.
	!
	! As soon as the owner/lock was cleared above, the mutex could be
	! deallocated by another CPU, so the load of the waiters field
	! could fault.  The trap code detects faults that occur with
	! the PC at mutex_exit_nofault and continues at mutex_exit_fault.
	!
	! XXX4U: eventually, do this with a single CASX?
	!
	ALTENTRY(mutex_exit_nofault)
	lduh	[%o0 + M_WAITERS], %o1		! check for waiting threads
	tst	%o1
	bnz	mutex_adaptive_release		! lock wanted - do wakeup
	nop
	retl					! return 
	nop
	SET_SIZE(mutex_exit)

	!
	! Continue here after getting fault on checkin waiters field.
	! Just return, since the fault indicates the mutex must've been freed.
	!
	ENTRY(mutex_exit_fault)
	retl
	nop
	SET_SIZE(mutex_exit_fault)
#endif	/* lint */

/*
 * asm_mutex_spin_enter(mutex_t *)
 *
 * For use by assembly interrupt handler only.
 * Does not change spl, since the interrupt handler is assumed to be
 * running at high level already.
 * Traps may be off, so cannot panic.
 * Does not keep statistics on the lock.
 *
 * Entry:	%l6 - points to mutex
 * 		%l7 - address of call (returns to %l7+8)
 * Uses:	%l6, %l5
 */
#ifndef lint
	ENTRY_NP(asm_mutex_spin_enter)
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%l6 + M_SPINLOCK], %l5	! try to set lock, get value in %l5
1:
	tst	%l5
	bnz	3f			! lock already held - go spin
	nop
2:	
	jmp	%l7 + 8			! return
	membar	#StoreLoad|#StoreStore
	!
	! Spin on lock without using an atomic operation to prevent the caches
	! from unnecessarily moving ownership of the line around.
	!
3:
	ldub	[%l6 + M_SPINLOCK], %l5
4:
	tst	%l5
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	bz,a	1b			! lock appears to be free, try again
	ldstub	[%l6 + M_SPINLOCK], %l5	! delay slot - try to set lock

	sethi	%hi(panicstr) , %l5
	ld	[%l5 + %lo(panicstr)], %l5
	tst 	%l5
	bnz	2b			! after panic, feign success
	nop
	b	4b
	ldub	[%l6 + M_SPINLOCK], %l5	! delay - reload lock
	SET_SIZE(asm_mutex_spin_enter)
#endif /* lint */

/*
 * asm_mutex_spin_exit(mutex_t *)
 *
 * For use by assembly interrupt handler only.
 * Does not change spl, since the interrupt handler is assumed to be
 * running at high level already.
 *
 * Entry:	%l6 - points to mutex
 * 		%l7 - address of call (returns to %l7+8)
 * Uses:	none
 */
#ifndef lint
	ENTRY_NP(asm_mutex_spin_exit)
	membar	#LoadStore|#StoreStore
	jmp	%l7 + 8			! return
	clrb	[%l6 + M_SPINLOCK]	! delay - clear lock
	SET_SIZE(asm_mutex_spin_exit)
#endif /* lint */

/*
 * _get_pc()
 *
 * return the return address from a procedure call
 *
 * XXX4U: What is this doing *here*? It is used in
 * one place and only one place: common/io/stream.c
 * in some TRACE code. Blech.
 */

#if defined(lint)

/*ARGSUSED*/
greg_t
_get_pc(void)
{ return (0); }

#else	/* lint */

	ENTRY(_get_pc)
	retl
	mov	%i7, %o0		! return address is in %i7
	SET_SIZE(_get_pc)

#endif	/* lint */

/*
 * thread_onproc()
 * Set thread in onproc state for the specified CPU.
 * Also set the thread lock pointer to the CPU's onproc lock.
 * Since the new lock isn't held, the store ordering is important.
 * If not done in assembler, the compiler could reorder the stores.
 */
#if defined(lint)

void
thread_onproc(kthread_id_t t, cpu_t *cp)
{
	t->t_state = TS_ONPROC;
	t->t_lockp = &cp->cpu_thread_lock;
}

#else	/* lint */

	ENTRY(thread_onproc)
	set	ONPROC_THREAD, %o2	! TS_ONPROC state
	st	%o2, [%o0 + T_STATE]	! store state
	add	%o1, CPU_THREAD_LOCK, %o3 ! pointer to disp_lock while running
	retl				! return
	st	%o3, [%o0 + T_LOCKP]	! delay - store new lock pointer

#endif	/* lint */
