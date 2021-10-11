/*
 * Copyright (c) 1990, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)lock_prim.s	1.44	95/09/01 SMI"

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
#include <sys/psr.h>
#include <sys/machlock.h>
#include <sys/machthread.h>

/* #define DEBUG */

#ifdef DEBUG
#include <sys/machparam.h>
#endif DEBUG

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
{}

#else	/* lint */

	ENTRY(disp_lock_enter)
	mov	%psr, %o2		! get the %psr (contains PIL)
	andn	%o2, PSR_PIL, %o4	! clear the PIL field to %o4
	wr	%o4, (LOCK_LEVEL << PSR_PIL_BIT), %psr ! block disp activity
	nop; nop			! psr delay 
	ldstub	[%o0], %o3		! try to set lock
	tst	%o3			! test old lock value
	bnz	1f			! lock was already set: spin
	nop
#ifdef DISP_DEBUG
	save	%sp, -SA(MINFRAME), %sp
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
	restore
#endif
	retl
	sth	%o2, [THREAD_REG + T_OLDSPL]	! delay - save old spl in thread

	/*
	 * lock failed - spin.  But first, drop spl and check for panicking or
	 *	spinning on a uniprocessor.
	 */
1:
	save	%sp, -SA(MINFRAME), %sp
	call	spldown			! drop spl
	mov	%i2, %o0

	sethi	%hi(panicstr), %o3	! see if we're panicking
	ld	[%o3 + %lo(panicstr)], %o4
	tst 	%o4
	bnz	5f			! panicking, just return.
	sethi	%hi(ncpus), %o4		! delay

	!
	! Debug - see if there is only one CPU.  This should never happen.
	!
	ld	[%o4 + %lo(ncpus)], %o4
	cmp	%o4, 1
	bne,a	2f			! multiprocessor - enter tight spin
	ldub	[%i0], %o1		! delay - reload
	sethi	%hi(.lock_panic_msg), %o0
	call	panic			! panic - will not return
	or	%o0, %lo(.lock_panic_msg), %o0	! delay

2:
	tst	%o1
	bz	4f			! lock now appears free
	mov	%i0, %o0		! delay - setup arg for lock_set_spl
	ld	[%o3 + %lo(panicstr)], %o4  ! here with %hi(panicstr) in %o3
	tst 	%o4
	bz	2b			! not panicking, continue spin
	ldub	[%i0], %o1		! delay - reload
	ret				! panicking - return
	restore
	
4:
	call	lock_set_spl
	mov	LOCK_LEVEL << PSR_PIL_BIT, %o1
	sth	%o0, [THREAD_REG + T_OLDSPL]	! save old spl in thread
#ifdef DISP_DEBUG
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
#endif
5:
	ret				! return
	restore
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
#ifdef DISP_DEBUG
	save	%sp, -SA(MINFRAME), %sp
	call	disp_lock_trace
	mov	%i0, %o0		! delay - lock address
	restore
#endif
#ifdef	DEBUG
	ldub	[%o0], %o3		! load lock byte
	tst	%o3
	bz	.disp_lock_exit_panic		! lock not held - panic
	ld      [THREAD_REG + T_CPU], %o3       ! delay - get CPU pointer
#else
	ld      [THREAD_REG + T_CPU], %o3       ! get CPU pointer
#endif
	ldub    [%o3 + CPU_KPRUNRUN], %o3       ! get CPU->cpu_kprunrun
	tst     %o3
	bnz     1f                      ! preemption needed
	clrb    [%o0]			! delay - drop lock 
	b       spldown                 ! let splx reset priority and return
	lduh	[THREAD_REG + T_OLDSPL], %o0	! delay - get old spl
	/*
	 * Attempt to preempt
	 */
1:
	save    %sp, -SA(MINFRAME), %sp
	call    spldown
	lduh	[THREAD_REG + T_OLDSPL], %o0	! delay - get old spl
	call    kpreempt
	restore	%g0, -1, %o0		! delay - pass -1 to indicate sync call

#ifdef DEBUG
.disp_lock_exit_panic:
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(.disp_lock_exit_msg), %o0
	call	panic			! panic - will not return
	or	%o0, %lo(.disp_lock_exit_msg), %o0	! delay
#endif DEBUG
	SET_SIZE(disp_lock_exit)

#ifdef DEBUG
.disp_lock_exit_msg:
	.asciz	"disp_lock_exit: lock not held"
	.align	4
#endif /* DEBUG */
	

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
	tst	%o1
	bz	.disp_lock_exit_panic	! lock not held - panic
	nop
#endif /* DEBUG */
	clrb    [%o0]
	b       spldown                 ! let splx reset priority and return
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
	tst	%o1
	bz	.disp_lock_exit_panic	! lock not held - panic
	nop
#endif /* DEBUG */
	retl
	clrb    [%o0]      		! delay - clear lock
	SET_SIZE(disp_lock_exit_high)
#endif /* lint */
 
#endif	/* DISP_LOCK_STATS */

/*
 * lock_try(lp), ulock_try(lp)
 *	- returns non-zero on success.
 *	- doesn't block interrupts so don't use this to spin on a lock.
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
{ return (0); }

/* ARGSUSED */
int
ulock_try(lock_t *lp)
{ return (0); }

#else	/* lint */

	ENTRY(lock_try)
	ldstub	[%o0], %o1		! try to set lock, get value in %o1
	retl
	xor	%o1, 0xff, %o0		! delay - return non-zero if success
	SET_SIZE(lock_try)

        ENTRY(ulock_try)
 
#ifdef DEBUG
	sethi   %hi(KERNELBASE), %o1
	cmp     %o0, %o1
	blu     1f
	nop
 
	set     2f, %o0
	call    panic
	nop
 
2:
	.asciz  "ulock_try: Argument is above KERNELBASE"
	.align  4
 
1:
#endif DEBUG
	ldstub  [%o0], %o1              ! try to set lock, get value in %o1
	retl
	xor     %o1, 0xff, %o0          ! delay - return non-zero if success
	SET_SIZE(ulock_try)


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
	retl
	clrb	[%o0]
	SET_SIZE(lock_clear)

	ENTRY(ulock_clear)
#ifdef DEBUG
	sethi   %hi(KERNELBASE), %o1
	cmp     %o0, %o1
	blu     1f
	nop
 
	set     2f, %o0
	call    panic
	nop
 
2:
	.asciz  "ulock_clear: argument above KERNELBASE"
	.align 4
 
1:
#endif DEBUG
 
	retl
	clrb    [%o0]
	SET_SIZE(ulock_clear)

#endif	/* lint */


/*
 * lock_set_spl(lp, pl)
 * 	Returns old pl.
 */

#if defined(lint)

/* ARGSUSED */
int
lock_set_spl(lock_t *lp, int pl)
{ return (0); }

#else	/* lint */

	ENTRY(lock_set_spl)
1:
	mov	%psr, %o2
	and	%o2, PSR_PIL, %o3	! mask current PIL
	and	%o1, PSR_PIL, %o1	! mask proposed new value for PIL
	cmp	%o3, %o1		! if cur pri < new pri, set new pri
	bg,a	2f			! current pil higher than new value
	mov	%o3, %o1

2:
	cmp	%o1, (LOCK_LEVEL << PSR_PIL_BIT)
	bge	3f			! skip base spl test if above lock level
	andn	%o2, PSR_PIL, %o3	! delay - mask out old interrupt level
	wr	%o3, (LOCK_LEVEL << PSR_PIL_BIT), %psr
	nop
	ld	[THREAD_REG + T_CPU], %o4
	ld	[%o4 + CPU_BASE_SPL], %o4
	cmp	%o4, %o1		! compare base pil to new pil
	bg,a	3f			! branch and do move if base is greater
	mov	%o4, %o1		! delay - use base pil

3:
	or	%o3, %o1, %o3
	mov	%o3, %psr		! disable (some) interrupts
	nop; nop			! psr delay

	ldstub	[%o0], %o3		! try to set lock, get value in %o3
	tst	%o3
	bnz,a	.lock_set_spin		! lock already held - go spin
	sethi	%hi(panicstr), %o3	! delay - setup for test of panicstr
4:
	retl				! return
	mov	%o2, %o0		! delay - return old %psr (lock_set_spl)

	!
	! Spin on lock without using an atomic operation to prevent the caches
	! from unnecessarily moving ownership of the line around.
	! also drop the spl during the spin.
	!
.lock_set_spin:
	ld	[%o3 + %lo(panicstr)], %o3  ! here with %hi(panicstr) in %o3
	tst 	%o3
	bnz	4b
	nop

	save	%sp, -SA(MINFRAME), %sp	! get new window
	call	splx			! drop spl
	mov	%i2, %o0

	ldub	[%i0], %o3
6:
	tst	%o3
	bz,a	1b			! lock appears to be free, try again
	restore				! delay - get back to callers window

	!
	! Debug - see if there is only one CPU.  This should never happen.
	!
	sethi	%hi(ncpus), %o3
	ld	[%o3 + %lo(ncpus)], %o3
	cmp	%o3, 1
	bne	6b
	ldub	[%i0], %o3		! delay - reload

	set	.lock_panic_msg, %o0
	call	panic
	nop
	SET_SIZE(lock_set_spl)

.lock_panic_msg:
	.asciz	"lock_set: lock held and only one CPU"
	.align	4

#endif	/* lint */

/*
 * void
 * lock_set(lp)
 */

#if defined(lint)

/* ARGSUSED */
void
lock_set(lock_t *lp)
{}
/* ARGSUSED */
void
disp_lock_enter_high(disp_lock_t *mp)
{}

#else	/* lint */

	/*
	 * disp_lock_enter_high - Get lock without changing spl level.
	 */
	ALTENTRY(disp_lock_enter_high)
	ENTRY(lock_set)
	ldstub	[%o0], %o1		! try to set lock, get value in %o1
1:
	tst	%o1
	bnz	3f			! lock already held - go spin
	nop
2:
	retl				! return
	mov	%o2, %o0		! delay - return old %psr (lock_set_spl)
	!
	! Spin on lock without using an atomic operation to prevent the caches
	! from unnecessarily moving ownership of the line around.
	!
3:
	ldub	[%o0], %o1
4:
	tst	%o1
	bz,a	1b			! lock appears to be free, try again
	ldstub	[%o0], %o1		! delay slot - try to set lock

	sethi	%hi(panicstr) , %o3
	ld	[%o3 + %lo(panicstr)], %o3
	tst 	%o3
	bnz	2b			! after panic, feign success
	sethi	%hi(ncpus), %o3		! delay slot
	!
	! Debug - see if there is only one CPU.  This should never happen.
	!
	ld	[%o3 + %lo(ncpus)], %o3
	cmp	%o3, 1
	bne	4b
	ldub	[%o0], %o1		! delay - reload

	set	.lock_panic_msg, %o0
	call	panic
	nop
	SET_SIZE(disp_lock_enter_high)
	SET_SIZE(lock_set)

#endif	/* lint */

/*
 * lock_clear_splx(lp, s)
 */

#if defined(lint)

/* ARGSUSED */
void
lock_clear_splx(lock_t *lp, int s)
{}

#else	/* lint */

	.global	splx

	ENTRY(lock_clear_splx)
	clrb	[%o0]			! clear lock
	b	splx			! let the real splx() do the rest
	mov	%o1, %o0		! delay - set arg for splx
	SET_SIZE(lock_clear_splx)

#endif	/* lint */


/*
 * mutex_enter() and mutex_exit().
 * 
 * These routines do the simple case of mutex_enter (adaptive lock, not held)
 * and mutex_exit (adaptive lock, no waiters).  If anything complicated is
 * going on, the C version (mutex_adaptive_enter) is called.
 *
 * mutex_adaptive_tryenter() is similar to mutex_enter() but returns
 * zero if the lock cannot be acquired, and nonzero on success.
 *
 * These routines do not do lock tracing.  If that is needed,
 * a separate mutex type could be used.
 *
 * If we get preempted in the small window after grabbing the lock but
 * before setting the owner, the interrupt code will detect that the
 * trapped pc lies within a 5-instruction critical region of mutex_enter
 * or mutex_adaptive_tryenter and set the owner.  This allows priority
 * inheritance code to be sure that the owner field will soon be valid
 * if the lock is seen to be held.
 *
 * The interrupt code has the following guilty knowledge:
 *
 *	%i0 is the mutex pointer throughout the critical regions
 *
 *	%i1 is the result of the ldstub throughout the critical regions
 *
 *	the two critical regions are instructions 2-6 of mutex_enter()
 *	and mutex_adaptive_tryenter(), and these routines are contiguous
 *
 * The code below is carefully constructed to make the 5-instruction
 * critical regions of mutex_enter() and mutex_adaptive_tryenter()
 * nearly contiguous.  This simplifies testing in the interrupt path.
 * Instead of doing two checks every time, it first checks the range
 * spanning both critical regions, then rejects the hole between them.
 * Since it's very rare to be interrupted in either region, the first
 * test will almost always be negative, so the second test is bypassed.
 *
 * If you change any of these assumptions, make sure you change the
 * interrupt code as well.
 *
 * If you don't think this is tricky, look closer.
 *
 * Also see comments in sparc/sys/mutex.h.
 */
#if defined (lint)

/* ARGSUSED */
void
mutex_enter(kmutex_t *lp)
{}
/* ARGSUSED */
int
mutex_adaptive_tryenter(kmutex_t *lp)
{ return (0); }

#else

	!
	! labels:  .{B,E}{crit,hole} == {begin, end} {critical union, hole}
	!

	ENTRY(mutex_enter)
	ldstub	[%o0 + M_LOCK], %o1		! try lock
.Bcrit:	tst	%o1				! did we get it?
	bnz	mutex_adaptive_enter		! no - locked or wrong type
	sra	THREAD_REG, PTR24_LSB, %o2	! %o2 = lock+owner
	retl					! return
	st	%o2, [%o0 + M_OWNER]		! delay - set lock+owner field
.Bhole:	SET_SIZE(mutex_enter)

	ENTRY(mutex_adaptive_tryenter)
	ldstub	[%o0 + M_LOCK], %o1		! try lock
.Ehole:	tst	%o1				! did we get it?
	bnz	.Ecrit				! no - lock already held
	sra	THREAD_REG, PTR24_LSB, %o2	! %o2 = lock+owner
	retl					! return
	st	%o2, [%o0 + M_OWNER]		! delay - set lock+owner field
.Ecrit:	retl					! return
	clr	%o0				! delay - 0 indicates failure
	SET_SIZE(mutex_adaptive_tryenter)

#ifdef DEBUG
	!
	! All assumptions about the geometry of the critical region are
	! ORed together into mutex_critical_verifier such that the result
	! will be non-zero if any assumption is violated.  kern_setup1()
	! ASSERTs that mutex_critical_verifier == 0.
	!
#define	MUTEX_CRITICAL_VERIFIER	\
	(.Bcrit - MUTEX_CRITICAL_UNION_START) | \
	(.Bcrit + MUTEX_CRITICAL_REGION_SIZE - .Bhole) | \
	(.Bhole + MUTEX_CRITICAL_HOLE_SIZE - .Ehole) | \
	(.Ehole + MUTEX_CRITICAL_REGION_SIZE - .Ecrit) | \
	(.Bcrit + MUTEX_CRITICAL_UNION_SIZE - .Ecrit)

	.seg	".data"
	.align	4
	.global	mutex_critical_verifier
mutex_critical_verifier:
	.word	MUTEX_CRITICAL_VERIFIER
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
	/*
	 * We must be sure this is an adaptive mutex (or call the right
	 * function to handle it), but we also want to be sure the current
	 * thread really owns the mutex.  We assume the mutex is adaptive,
	 * and if the owner isn't right, go to C and let it do the
	 * type check.  If it is adaptive and not owned, the C code will panic.
	 */
	ld	[%o0 + M_OWNER], %o2		! load owner/lock field
	sra	THREAD_REG, PTR24_LSB, %o1	! lock+curthread
	cmp	%o1, %o2			! is this the owner?
#ifndef MP
	be,a	1f				! right type and owner
	clr	[%o0 + M_OWNER]			! delay - clear owner AND lock
#else
	!
	! For multiprocessors, do an atomic operation to be sure the lock
	! clear is seen by other CPUs before anyone we check the wait bit.
	! Using swap to clear the owner and lock accomplishes this.
	!
	be,a	1f				! right type and owner
	swap	[%o0 + M_OWNER], %g0		! delay - clear owner AND lock
#endif /* MP */
	b,a	mutex_vector_exit
	nop

	!
	! Check the waiters field.
	!
	! As soon as the owner/lock was cleared above, the mutex could be
	! deallocated by another CPU, so the load of the waiters field
	! could fault.  The trap code detects data faults that occur with 
	! the PC at mutex_exit_nofault and continues at mutex_exit_fault.
	!
1:
	ALTENTRY(mutex_exit_nofault)
	lduh	[%o0 + M_WAITERS], %o1		! check for waiting threads
	tst	%o1
	bnz	mutex_adaptive_release		! lock wanted - do wakeup
	nop
	retl					! return 
	nop
	SET_SIZE(mutex_exit)

	!
	! Continue here after getting fault on checking waiters field.
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
#if defined(lint)

/* ARGSUSED */
void
asm_mutex_spin_enter(kmutex_t *lp)
{}

#else	/* lint */
	ENTRY_NP(asm_mutex_spin_enter)
	ldstub	[%l6 + M_SPINLOCK], %l5	! try to set lock, get value in %l5
1:
	tst	%l5
	bnz	3f			! lock already held - go spin
	nop
2:	
	jmp	%l7 + 8			! return
	nop
	!
	! Spin on lock without using an atomic operation to prevent the caches
	! from unnecessarily moving ownership of the line around.
	!
3:
	ldub	[%l6 + M_SPINLOCK], %l5
4:
	tst	%l5
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
#endif	/* lint */

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
#if defined(lint)

/* ARGSUSED */
void
asm_mutex_spin_exit(kmutex_t *lp)
{}

#else	/* lint */
	ENTRY_NP(asm_mutex_spin_exit)
	jmp	%l7 + 8			! return
	clrb	[%l6 + M_SPINLOCK]	! delay - clear lock
	SET_SIZE(asm_mutex_spin_exit)
#endif	/* lint */

/*
 * _get_pc()
 *
 * return the return address from a procedure call
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
 * _get_sp()
 *
 * get the current stack pointer.
 */

#if defined(lint)

greg_t
_get_sp(void)
{ return (0); }

#else	/* lint */

	ENTRY(_get_sp)
	retl
	mov	%fp, %o0
	SET_SIZE(_get_sp)

#endif	/* lint */

/*
 * lock_mutex_flush()
 *
 * guarantee writes are flushed.
 */

#if defined(lint)

void
lock_mutex_flush(void)
{ return; }

#else	/* lint */

	ENTRY(lock_mutex_flush)
	retl
	ldstub	[THREAD_REG + T_LOCK], %g0 ! dummy atomic op to flush owner
	SET_SIZE(lock_mutex_flush)

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
	stbar				! make sure stores are seen in order
	retl				! return
	st	%o3, [%o0 + T_LOCKP]	! delay - store new lock pointer

#endif	/* lint */
