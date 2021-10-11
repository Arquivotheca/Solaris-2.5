
/*
 *      Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)interrupt.s 1.54     95/08/30 SMI"

#if defined(lint)
#include <sys/types.h>
#include <sys/thread.h>
#else	/* lint */
#include "assym.s"
#endif	/* lint */

#include <sys/param.h>
#include <sys/asm_linkage.h>
#include <sys/spitasi.h>
#include <sys/errno.h>
#include <sys/machlock.h>
#include <sys/machthread.h>
#include <sys/machcpuvar.h>
#include <sys/intreg.h>
#include <sys/intr.h>
#include <sys/privregs.h>

#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

#if defined(lint)

/* ARGSUSED */
void
pil_interrupt(int level)
{}

#else	/* lint */


/*
 * (TT 0x40..0x4F, TL>0) Interrupt Level N Handler (N == 1..15)
 * 	Register passed from LEVEL_INTERRUPT(level)
 *	%g4 - interrupt request level
 */
	ENTRY_NP(pil_interrupt)
	!
	! Register usage
	!	%g1 - cpu
	!	%g3 - intr_req
	!	%g4 - pil
	!	%g2, %g5, %g6 - temps
	!
	! grab the 1st intr_req off the list
	! if the list is empty, clear %clear_softint
	!
	CPU_ADDR(%g1, %g5)

	! We are using the tick register to use as the
	! system clock. Unfortunely, it is not reloaded
	! automatically so we need to reload it via software
	! after servicing the interrupt. For that we need
	! to take into account of the time between we got
	! the level14 and the time we re-program it.
	! We like to do this ASAP so to lose as few cycles
	! as possible.
	! 
	cmp	%g4, 0xe		! level-14
	bne	1f			! We'll check later 
	nop				! if it is indeed the tick timer
	
	! record current tick value

	add	%g1, TICK_HAPPENED, %g5	! store in per-cpu field
	rdpr	%tick, %g6
	sllx	%g6, 1, %g6
	srlx	%g6, 1, %g6	
	stx	%g6, [%g5]

1:

	sll	%g4, 2, %g5
	add	%g1, INTR_HEAD, %g6	! intr_head[0]
	add	%g6, %g5, %g6		! intr_head[pil]
	ld	[%g6], %g3		! g3 = intr_req

#ifdef DEBUG
	!
	! Verify the address of intr_req; it should be within the
	! address range of intr_pool and intr_head
	!
	! Registers passed to sys_trap()
	!	%g1 - no_intr_req
	!	%g2 - intr_req
	!	%g3 - %pil
	!	%g4 - current pil
	!
	add	%g1, INTR_POOL, %g2
	cmp	%g3, %g2
	bl,pn	%xcc, 4f
	nop
	add	%g1, INTR_HEAD, %g2
	cmp	%g2, %g3
	bge,pt	%xcc, 5f
	nop
4:
	set	no_intr_req, %g1
	mov	%g3, %g2
	mov	%g4, %g3
	mov	1, %g5
	sll	%g5, %g4, %g5
	wr	%g5, CLEAR_SOFTINT
	ba,pt	%xcc, sys_trap
	sub	%g0, 1, %g4
5:	
#endif DEBUG
	ld	[%g3 + INTR_NEXT], %g2	! 2nd entry
	brnz,pn	%g2, 1f			! branch if list not empty
	st	%g2, [%g6]
	add	%g1, INTR_TAIL, %g6	! intr_tail[0]
	st	%g0, [%g5 + %g6]	! update intr_tail[pil]
	mov	1, %g5
	sll	%g5, %g4, %g5
	wr	%g5, CLEAR_SOFTINT
1:
	!
	! put intr_req on free list
	!	%g2 - inumber
	!
	ld	[%g1 + INTR_HEAD], %g5	! current head of free list
	ld	[%g3 + INTR_NUMBER], %g2
	st	%g3, [%g1 + INTR_HEAD]
	st	%g5, [%g3 + INTR_NEXT]
#ifdef TRAPTRACE
	TRACE_PTR(%g5, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g5 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g5 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g5 + TRAP_ENT_TT]%asi
	rdpr	%tpc, %g6
	sta	%g6, [%g5 + TRAP_ENT_TPC]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g5 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g5 + TRAP_ENT_SP]%asi
	sta	%g3, [%g5 + TRAP_ENT_TR]%asi
	sta	%g2, [%g5 + TRAP_ENT_F1]%asi
	sll	%g4, 2, %g3
	add	%g1, INTR_HEAD, %g6
	ld	[%g6 + %g3], %g6		! intr_head[pil]
	sta	%g6, [%g5 + TRAP_ENT_F2]%asi
	add	%g1, INTR_TAIL, %g6
	ld	[%g6 + %g3], %g6		! intr_tail[pil]
	sta	%g4, [%g5 + TRAP_ENT_F3]%asi
	sta	%g6, [%g5 + TRAP_ENT_F4]%asi
	TRACE_NEXT(%g5, %g6, %g3)
#endif TRAPTRACE
	!
	! clear the iv_pending flag for this inum
	! 
	!
	set	intr_vector, %g5;
	sll	%g2, 0x4, %g6;
	add	%g5, %g6, %g5;			! &intr_vector[inum]
	sth	%g0, [%g5 + IV_PENDING]

	!
	! Prepare for sys_trap()
	!
	! Registers passed to sys_trap()
	!	%g1 - interrupt handler at TL==0
	!	%g2 - inumber
	!	%g3 - pil
	!	%g4 - initial pil for handler
	!
	! figure which handler to run and which %pil it starts at
	! intr_thread starts at LOCK_LEVEL to prevent preemption
	! current_thread starts at PIL_MAX to protect cpu_on_intr
	!
	mov	%g4, %g3
	cmp	%g4, LOCK_LEVEL
	bg,a,pt	%xcc, 4f		! branch if pil above LOCK_LEVEL
	mov	PIL_MAX, %g4
	bl,a,pt	%xcc, 3f		! branch if pil below LOCK_LEVEL
	mov	LOCK_LEVEL, %g4
	sethi	%hi(clk_thread), %g1
	ba,pt	%xcc, sys_trap		! else pil is LOCK_LEVEL for clock
	or	%g1, %lo(clk_thread), %g1
3:
	sethi	%hi(intr_thread), %g1
	ba,pt	%xcc, sys_trap
	or	%g1, %lo(intr_thread), %g1
4:
	sethi	%hi(current_thread), %g1
	ba,pt	%xcc, sys_trap
	or	%g1, %lo(current_thread), %g1
	SET_SIZE(pil_interrupt)

#endif	/* lint */

#if defined(lint)

void
vec_interrupt(void)
{}

#else	/* lint */


/*
 * (TT 0x60, TL>0) Interrupt Vector Handler
 *	Globals are the Interrupt Globals.
 */
	ENTRY_NP(vec_interrupt)
	!
	! Load the interrupt receive data register 0.
	! It could be a fast trap handler address (pc > KERNELBASE) at TL>0
	! or an interrupt number.
	!
	mov	ASI_INTR_RECEIVE, %asi
	ldxa	[IRDR_0]%asi, %g5		! %g5 = PC or Interrupt Number
	set	KERNELBASE, %g4
	cmp	%g5, %g4
	bl,a,pt	%xcc, 0f			! an interrupt number found
	nop
	!
	! Load interrupt receive data registers 1 and 2 to fetch
	! the arguments for the fast trap handler.
	!
	! Register usage:
	!	g5: TL>0 handler
	!	g1: arg1
	!	g2: arg2
	!	g3: arg3
	!	g4: arg4
	!
	ldxa	[IRDR_1]%asi, %g1		! hi: g3; lo: g1
	ldxa	[IRDR_2]%asi, %g2		! hi: g4; lo: g2
#ifdef TRAPTRACE
	TRACE_PTR(%g4, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g4 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g4 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g4 + TRAP_ENT_TT]%asi
	rdpr	%tpc, %g6
	sta	%g6, [%g4 + TRAP_ENT_TPC]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g4 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g4 + TRAP_ENT_SP]%asi
	sta	%g5, [%g4 + TRAP_ENT_TR]%asi	! pc of the TL>0 handler
	stxa	%g1, [%g4 + TRAP_ENT_F1]%asi
	stxa	%g2, [%g4 + TRAP_ENT_F3]%asi
	TRACE_NEXT(%g4, %g6, %g3)
#endif TRAPTRACE
	srlx	%g1, 32, %g3
	sllx	%g1, 32, %g1
	srlx	%g1, 32, %g1
	srlx	%g2, 32, %g4
	sllx	%g2, 32, %g2
	srlx	%g2, 32, %g2
	stxa	%g0, [%g0]ASI_INTR_RECEIVE_STATUS	! clear the BUSY bit
	jmp	%g5				! call the fast trap handler
	membar	#Sync				! and won't be back
	/* Never Reached */

0:
#ifdef DEBUG
	!
	! Verify the inumber received (should be inum < MAXIVNUM).
	!
	cmp	%g5, MAXIVNUM
	bl,pt	%xcc, 4f
	nop
	stxa	%g0, [%g0]ASI_INTR_RECEIVE_STATUS
	membar	#Sync			! need it before the ld
	ba,pt	%xcc, 5f
	nop
4:
#endif DEBUG
	!
	! We have an interrupt number.
	! Put the request on the cpu's softint list,
	! and set %set_softint.
	!
	! Register usage:
	!	%g5 - inumber
	!	%g2 - requested pil
	!	%g3 - intr_req
	!	%g4 - cpu
	!	%g1, %g6 - temps
	!
	! clear BUSY bit
	! allocate an intr_req from the free list
	!
	stxa	%g0, [%g0]ASI_INTR_RECEIVE_STATUS
	membar	#Sync			! need it before the ld
	CPU_ADDR(%g4, %g1)
	ld	[%g4 + INTR_HEAD], %g3
	!
	! if intr_req == NULL, it will cause TLB miss
	! TLB miss handler (TL>0) will call panic
	!
	! get pil from intr_vector table
	!
1:
	set	intr_vector, %g1
#if INTR_VECTOR == 0x10
	sll	%g5, 0x4, %g6
#else
	Error INTR_VECTOR has been changed
#endif
	add	%g1, %g6, %g1		! %g1 = &intr_vector[IN]
	lduh	[%g1 + IV_PIL], %g2
#ifdef DEBUG
	!
	! Verify the intr_vector[] entry according to the inumber.
	! The iv_pil field should not be zero.
	!
	! Registers passed to sys_trap()
	!	%g1 - no_ivintr
	!	%g2 - inumber
	!	%g3 - %pil
	!	%g4 - current pil
	!
	brnz,pt	%g2, 6f
	nop
5:
	set	no_ivintr, %g1
	sub	%g0, 1, %g4
	rdpr	%pil, %g3
	ba,pt	%xcc, sys_trap
	mov	%g5, %g2
6:	
#endif DEBUG
	!
	! fixup free list
	!
	ld	[%g3 + INTR_NEXT], %g6
	st	%g6, [%g4 + INTR_HEAD]
	!
	! fill up intr_req
	!
	st	%g5, [%g3 + INTR_NUMBER]
	st	%g0, [%g3 + INTR_NEXT]
	!
	! move intr_req to appropriate list
	!
	sll	%g2, 2, %g5
	add	%g4, INTR_TAIL, %g6
	ld	[%g6 + %g5], %g1	! current tail
	brz,pt	%g1, 2f			! branch if list empty
	st	%g3, [%g6 + %g5]	! make intr_req new tail
	!
	! there's pending intr_req already
	!
	ba,pt	%xcc, 3f
	st	%g3, [%g1 + INTR_NEXT]	! update old tail
2:
	!
	! no pending intr_req; make intr_req new head
	!
	add	%g4, INTR_HEAD, %g6
	st	%g3, [%g6 + %g5]
3:
#ifdef TRAPTRACE
	TRACE_PTR(%g1, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g1 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g1 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g1 + TRAP_ENT_TT]%asi
	rdpr	%tpc, %g6
	sta	%g6, [%g1 + TRAP_ENT_TPC]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g1 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g1 + TRAP_ENT_SP]%asi
	ld	[%g3 + INTR_NUMBER], %g6
	sta	%g6, [%g1 + TRAP_ENT_TR]%asi
	add	%g4, INTR_HEAD, %g6
	ld	[%g6 + %g5], %g6		! intr_head[pil]
	sta	%g6, [%g1 + TRAP_ENT_F1]%asi
	add	%g4, INTR_TAIL, %g6
	ld	[%g6 + %g5], %g6		! intr_tail[pil]
	sta	%g6, [%g1 + TRAP_ENT_F2]%asi
	sta	%g2, [%g1 + TRAP_ENT_F3]%asi
	sta	%g3, [%g1 + TRAP_ENT_F4]%asi
	TRACE_NEXT(%g1, %g6, %g5)
#endif TRAPTRACE
	!
	! Write %set_softint with (1<<pil) to cause a "pil" level trap
	!
	mov	1, %g1
	sll	%g1, %g2, %g1
	wr	%g1, SET_SOFTINT
	retry
	SET_SIZE(vec_interrupt)

#endif	/* lint */

#if defined(lint)

void
vec_intr_spurious(void)
{}

#else	/* lint */
	.seg	".data"

	.global vec_spurious_cnt
vec_spurious_cnt:
	.word	0

	.seg	".text"
	ENTRY_NP(vec_intr_spurious)
	set	vec_spurious_cnt, %g1
	ld	[%g1], %g2
	cmp	%g2, 16
	bl,a,pt	%xcc, 1f
	inc	%g2
	!
	! prepare for sys_trap()
	!	%g1 - panic
	!	%g2 - panic message
	!	%g4 - current pil
	!
	sub	%g0, 1, %g4
	set	_not_ready, %g2
	sethi	%hi(panic), %g1
	ba,pt	%xcc, sys_trap
	or	%g1, %lo(panic), %g1
	!
1:	st	%g2, [%g1]
	retry
	SET_SIZE(vec_intr_spurious)

_not_ready:	.asciz	"Interrupt Vector Receive Register not READY"
	.align 	4

#endif	/* lint */

/*
 * Macro to service an interrupt.
 *
 * inum		- interrupt number (may be out, not preserved)
 * cpu		- cpu pointer (may be out, preserved)
 * ls1		- local scratch reg (used as &intr_vector[inum])
 * ls2		- local scratch reg (used as driver mutex)
 * os1 - os4	- out scratch reg
 */
#ifndef	lint
_spurious:
	.asciz	"interrupt level %d not serviced"
	.align	4
#if INTR_VECTOR == 0x10
#define	SERVE_INTR(inum, cpu, ls1, ls2, os1, os2, os3, os4)		\
	set	intr_vector, ls1;					\
	sll	inum, 0x4, os1;						\
	add	ls1, os1, ls1;						\
0:	SERVE_INTR_TRACE(inum, os1, os2, os3, os4);			\
	ld	[ls1 + IV_MUTEX], ls2;					\
	brz,a,pt ls2, 1f;						\
	ld	[ls1 + IV_HANDLER], os2;				\
	call	mutex_enter;						\
	mov	ls2, %o0;						\
	ld	[ls1 + IV_HANDLER], os2;				\
1:	ld	[ls1 + IV_ARG], %o0;					\
	call	os2;							\
	lduh	[ls1 + IV_PIL], ls1;					\
	brnz,pt	%o0, 2f;						\
	mov	2, %o0;							\
	set	_spurious, %o1;						\
	call	cmn_err;						\
	rdpr	%pil, %o2;						\
2:	brz,pt	ls2, 3f;						\
	ld	[THREAD_REG + T_CPU], cpu;				\
	call	mutex_exit;						\
	mov	ls2, %o0;						\
	ld	[THREAD_REG + T_CPU], cpu;				\
3:	ld	[cpu + CPU_SYSINFO_INTR], os1;				\
	inc	os1;							\
	st	os1, [cpu + CPU_SYSINFO_INTR];				\
	sll	ls1, 2, os2;						\
	add	cpu,  INTR_HEAD, os1;					\
	add	os1, os2, os1;						\
	ld	[os1], os3;						\
	brz,pt	os3, 5f;						\
	nop;								\
	rdpr	%pstate, ls2;						\
	wrpr	ls2, PSTATE_IE, %pstate;				\
	ld 	[os3 + INTR_NEXT], os2;					\
	brnz,pn	os2, 4f;						\
	st	os2, [os1];						\
	add	cpu, INTR_TAIL, os1;					\
	sll	ls1, 2, os2;						\
	st	%g0, [os1 + os2];					\
	mov	1, os1;							\
	sll	os1, ls1, os1;						\
	wr	os1, CLEAR_SOFTINT;					\
4:	ld	[cpu + INTR_HEAD], os1;					\
	ld 	[os3 + INTR_NUMBER], inum;				\
	st	os3, [cpu + INTR_HEAD];					\
	st	os1, [os3 + INTR_NEXT];					\
	set	intr_vector, ls1;					\
	sll	inum, 0x4, os1;						\
	add	ls1, os1, ls1;						\
	sth	%g0, [ls1 + IV_PENDING];				\
	wrpr	%g0, ls2, %pstate;					\
	ba,pt	%xcc, 0b;						\
5:	nop;
#else
	Error INTR_VECTOR has been changed
#endif

#ifdef TRAPTRACE
#define	SERVE_INTR_TRACE(inum, os1, os2, os3, os4)			\
	rdpr	%pstate, os4;						\
	andn	os4, PSTATE_IE | PSTATE_AM, os3;			\
	wrpr	%g0, os3, %pstate;					\
	TRACE_PTR(os1, os2);						\
	rdpr	%tick, os2;						\
	stxa	os2, [os1 + TRAP_ENT_TICK]%asi;				\
	rdpr	%tl, os2;						\
	stha	os2, [os1 + TRAP_ENT_TL]%asi;				\
	set	TT_SERVE_INTR, os2;					\
	rdpr	%pil, os3;						\
	or	os2, os3, os2;						\
	stha	os2, [os1 + TRAP_ENT_TT]%asi;				\
	ld	[%l7 + PC*4], os2;					\
	sta	os2, [os1 + TRAP_ENT_TPC]%asi;				\
	ldx	[%l7 + TSTATE*4], os2;					\
	stxa	os2, [os1 + TRAP_ENT_TSTATE]%asi;			\
	sta	%sp, [os1 + TRAP_ENT_SP]%asi;				\
	sta	inum, [os1 + TRAP_ENT_TR]%asi;				\
	stxa	%g0, [os1 + TRAP_ENT_F1]%asi;				\
	stxa	%g0, [os1 + TRAP_ENT_F3]%asi;				\
	TRACE_NEXT(os1, os2, os3);					\
	wrpr	%g0, os4, %pstate
#else	/* TRAPTRACE */
#define SERVE_INTR_TRACE(inum, os1, os2, os3, os4)
#endif	/* TRAPTRACE */

#endif	/* lint */

#if defined(lint)

void
intr_thread(void)
{}

#else	/* lint */

/*
 * Handle an interrupt in a new thread.
 *	Entry:
 *		%o0       = pointer to regs structure
 *		%o1       = inumber
 *		%o2       = pil
 *		%sp       = on current thread's kernel stack
 *		%o7       = return linkage to trap code
 *		%g7       = current thread
 *		%pstate   = normal globals, interrupts enabled, 
 *		            privileged, fp disabled
 *		%pil      = LOCK_LEVEL
 *
 *	Register Usage
 *		%l0       = return linkage
 *		%l1       = pil
 *		%l2 - %l3 = scratch
 *		%l4 - %l7 = reserved for sys_trap
 *		%o2       = cpu
 *		%o3       = intr thread
 *		%o0       = scratch
 *		%o4 - %o5 = scratch
 */
	ENTRY_NP(intr_thread)
	mov	%o7, %l0
	mov	%o2, %l1
	!
	! Get set to run interrupt thread.
	! There should always be an interrupt thread since we allocate one
	! for each level on the CPU, and if we release an interrupt, a new
	! thread gets created.
	!
	ld	[THREAD_REG + T_CPU], %o2
	ld	[%o2 + CPU_INTR_THREAD], %o3	! interrupt thread pool
	ld	[%o3 + T_LINK], %o4		! unlink thread from CPU's list
	st	%o4, [%o2 + CPU_INTR_THREAD]
	!
	! Consider the new thread part of the same LWP so that
	! window overflow code can find the PCB.
	!
	ld	[THREAD_REG + T_LWP], %o4
	st	%o4, [%o3 + T_LWP]
	!
	! Threads on the interrupt thread free list could have state already
	! set to TS_ONPROC, but it helps in debugging if they're TS_FREE
	! Could eliminate the next two instructions with a little work.
	!
	mov	ONPROC_THREAD, %o4
	st	%o4, [%o3 + T_STATE]
	!
	! Push interrupted thread onto list from new thread.
	! Set the new thread as the current one.
	! Set interrupted thread's T_SP because if it is the idle thread,
	! resume may use that stack between threads.
	!
	st	%o7, [THREAD_REG + T_PC]	! mark pc for resume
	st	%sp, [THREAD_REG + T_SP]	! mark stack for resume
	st	THREAD_REG, [%o3 + T_INTR]	! push old thread
	st	%o3, [%o2 + CPU_THREAD]		! set new thread
	mov	%o3, THREAD_REG			! set global curthread register
	ld	[%o3 + T_STACK], %sp		! interrupt stack pointer
	!
	! Initialize thread priority level from intr_pri
	!
	sethi	%hi(intr_pri), %o4
#ifdef	LDSH_WORKS
	ldsh	[%o4 + %lo(intr_pri)], %o4	! grab base interrupt priority
#else
	lduh	[%o4 + %lo(intr_pri)], %o4	! grab base interrupt priority
	sll	%o4, 16, %o4
	sra	%o4, 16, %o4
#endif
	add	%l1, %o4, %o4		! convert level to dispatch priority
	sth	%o4, [THREAD_REG + T_PRI]
	wrpr	%g0, %l1, %pil			! lower %pil to new level
	!
	! call the handler
	!
	SERVE_INTR(%o1, %o2, %l2, %l3, %o4, %o5, %o3, %o0)
	!
	! update cpu_sysinfo.intrthread  - interrupts as threads (below clock)
	!
	ld	[%o2 + CPU_SYSINFO_INTRTHREAD], %o3
	inc	%o3
	st	%o3, [%o2 + CPU_SYSINFO_INTRTHREAD]
	!
	! If there is still an interrupted thread underneath this one,
	! then the interrupt was never blocked or released and the
	! return is fairly simple.  Otherwise jump to intr_thread_exit.
	!
	wrpr	%g0, LOCK_LEVEL, %pil
	ld	[THREAD_REG + T_INTR], %o4	! pinned thread
	brz,pn	%o4, intr_thread_exit		! branch if none
	nop
	!
	! link the thread back onto the interrupt thread pool
	!
	ld	[%o2 + CPU_INTR_THREAD], %o3
	st	%o3, [THREAD_REG + T_LINK]
	st	THREAD_REG, [%o2 + CPU_INTR_THREAD]
	!	
	! set the thread state to free so kadb doesn't see it
	!
	mov	FREE_THREAD, %o5
	st	%o5, [THREAD_REG + T_STATE]
	!
	! Switch back to the interrupted thread and return
	!
	st	%o4, [%o2 + CPU_THREAD]
	mov	%o4, THREAD_REG
	ld	[THREAD_REG + T_SP], %sp		! restore %sp
	jmp	%l0 + 8
	wrpr	%g0, %l1, %pil
	/* Not Reached */

	!
	! An interrupt returned on what was once (and still might be)
	! an interrupt thread stack, but the interrupted process is no longer
	! there.  This means the interrupt must've blocked or called
	! release_interrupt(). (XXX release_interrupt() is not used by anyone)
	!
	! There is no longer a thread under this one, so put this thread back 
	! on the CPU's free list and resume the idle thread which will dispatch
	! the next thread to run.
	!
	! All traps below LOCK_LEVEL are disabled here, but the mondo interrupt
	! is enabled.
	!
intr_thread_exit:
#ifdef TRAPTRACE
	rdpr	%pstate, %l2
	andn	%l2, PSTATE_IE | PSTATE_AM, %o4
	wrpr	%g0, %o4, %pstate			! cpu to known state
	TRACE_PTR(%o4, %o5)
	rdpr	%tick, %o5
	stxa	%o5, [%o4 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %o5
	stha	%o5, [%o4 + TRAP_ENT_TL]%asi
	set	TT_INTR_EXIT, %o5
	stha	%o5, [%o4 + TRAP_ENT_TT]%asi
	sta	%g0, [%o4 + TRAP_ENT_TPC]%asi
	stxa	%g0, [%o4 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%o4 + TRAP_ENT_SP]%asi
	sta	THREAD_REG, [%o4 + TRAP_ENT_TR]%asi
	ld	[%o2 + CPU_BASE_SPL], %o5
	sta	%o5, [%o4 + TRAP_ENT_F1]%asi
	sta	%g0, [%o4 + TRAP_ENT_F2]%asi
	TRACE_NEXT(%o4, %o5, %o0)
	wrpr	%g0, %l2, %pstate
#endif TRAPTRACE
        ld      [%o2 + CPU_SYSINFO_INTRBLK], %o4   ! cpu_sysinfo.intrblk++
        inc     %o4
        st      %o4, [%o2 + CPU_SYSINFO_INTRBLK]
	!
	! Put thread back on either the interrupt thread list if it is
	! still an interrupt thread, or the CPU's free thread list, if it did a
	! release interrupt.
	!
	lduh	[THREAD_REG + T_FLAGS], %o5
	btst	T_INTR_THREAD, %o5		! still an interrupt thread?
	bz,pn	%xcc, 1f			! No, so put back on free list
	mov	1, %o0				! delay
	!
	! This was an interrupt thread, so clear the pending interrupt flag
	! for this level.
	!
	ld	[%o2 + CPU_INTR_ACTV], %o5	! get mask of interrupts active
	sll	%o0, %l1, %o0			! form mask for level
	andn	%o5, %o0, %o5			! clear interrupt flag
	call	_intr_set_spl			! set CPU's base SPL level
	st	%o5, [%o2 + CPU_INTR_ACTV]	! delay - store active mask
	!
	! Set the thread state to free so kadb doesn't see it
	!
	mov	FREE_THREAD, %o4
	st	%o4, [THREAD_REG + T_STATE]
	!
	! Put thread on either the interrupt pool or the free pool and
	! call swtch() to resume another thread.
	!
	ld	[%o2 + CPU_INTR_THREAD], %o5	! get list pointer
	st	%o5, [THREAD_REG + T_LINK]
	call	swtch				! switch to best thread
	st	THREAD_REG, [%o2 + CPU_INTR_THREAD] ! delay - put thread on list
	ba,a,pt	%xcc, .				! swtch() shouldn't return
1:
	mov	TS_ZOMB, %o4			! set zombie so swtch will free
	call	swtch				! run next process - free thread
	st	%o4, [THREAD_REG + T_STATE]	! delay - set state to zombie
	ba,a,pt	%xcc, .				! swtch() shouldn't return
	SET_SIZE(intr_thread)
#endif	/* lint */

#if defined(lint)

/*
 * Handle an interrupt in the current thread
 *	Entry:
 *		%o0       = pointer to regs structure
 *		%o1       = inumber
 *		%o2       = pil
 *		%sp       = on current thread's kernel stack
 *		%o7       = return linkage to trap code
 *		%g7       = current thread
 *		%pstate   = normal globals, interrupts enabled, 
 *		            privileged, fp disabled
 *		%pil      = PIL_MAX
 *
 *	Register Usage
 *		%l0       = return linkage
 *		%l1       = old stack
 *		%l2 - %l3 = scratch
 *		%l4 - %l7 = reserved for sys_trap
 *		%o3       = cpu
 *		%o0       = scratch
 *		%o4 - %o5 = scratch
 */
void
current_thread(void)
{}

#else	/* lint */

	! %l7 cannot be modified here; contains rp for profiling

	ENTRY_NP(current_thread)
	
	mov	%o7, %l0
	ld	[THREAD_REG + T_CPU], %o3
	ld	[%o3 + CPU_ON_INTR], %o4
	!
	! Handle high_priority nested interrupt on separate interrupt stack
	!
	tst	%o4
	inc	%o4
	bnz,pn	%xcc, 1f			! already on the stack
	st	%o4, [%o3 + CPU_ON_INTR]
	mov	%sp, %l1
	ld	[%o3 + CPU_INTR_STACK], %sp	! get on interrupt stack
1:
#ifdef DEBUG
	!
	! ASSERT(%o2 > LOCK_LEVEL)
	!
	cmp	%o2, LOCK_LEVEL
	bg,pt	%xcc, 2f
	nop
	mov	3, %o0
	sethi	%hi(current_thread_wrong_pil), %o1
	call	cmn_err				! %o2 has the %pil already
	or	%o1, %lo(current_thread_wrong_pil), %o1
2:
#endif DEBUG
	wrpr	%g0, %o2, %pil			! enable interrupts

	!
	! call the handler
	!
	SERVE_INTR(%o1, %o3, %l2, %l3, %o4, %o5, %o2, %o0)
	!
	! get back on current thread's stack
	!
	rdpr	%pil, %o2
	wrpr	%g0, PIL_MAX, %pil		! disable interrupts (1-15)


	!if tickint enabled and pil is 14 enqueue tickint req 
	!then need to re-enqueue tickint intr service request

	cmp	%o2, PIL_14
	bne,pn	%xcc, 2f
	nop

	!if bit 63 is set then tickint is disabled
	rd	TICK_COMPARE, %g1
	srlx	%g1, TICKINT_DIS_SHFT, %g1
	brnz,pt	%g1, 2f
	nop

	rdpr 	%pstate, %o1
	andn	%o1, PSTATE_IE, %g1
	call	enqueue_tickint_req
	wrpr	%g0, %g1, %pstate		!disable vec interrupts
	wrpr	%g0, %o1, %pstate		!enable vec interrupts

	! for tickint intr need to clear TICKINT and reset tick_compare reg
	rd	SOFTINT, %o4
	and	%o4, TICK_INT_MASK, %o4
	brz,pn	%o4, 2f
	nop

	! clear TICKINT 
	mov	1, %o0
	wr	%o0, CLEAR_SOFTINT

	
	! reset tick_compare reg
	rdpr	%tick, %g1
	sllx	%g1, 1, %g1
	srlx	%g1, 1, %g1

	CPU_ADDR(%o4, %o5)
	add	%o4, TICKINT_INTRVL, %o5	! how many cycles
	ldx	[%o5], %g2			! to add for next tick

	add	%o4, TICK_HAPPENED, %o5		! get last time we got
	ldx	[%o5], %g3			! the tick

	! If we went into kadb or something, then it is possible
	! that last_tick + INTRVL has already past.
	! In that case, we can only base the next tick on the current
	! timer reading

	sub	%g1, %g3, %g4			! Compare the difference
	add	%g1, %g2, %g1			! current + interval 
	cmp	%g4, %g2			! difference >=  interval
	bge	7f				! next = current + interval
	nop
	sub	%g1, %g4, %g1			! interval > difference
						! next = current - difference
						!        + interval

7:

	wr	%g1, TICK_COMPARE 		! do it 

2:
	! get back on current thread's stack
	!
	ld	[%o3 + CPU_ON_INTR], %o4
	dec	%o4				! decrement on_intr
	tst	%o4
	st	%o4, [%o3 + CPU_ON_INTR]	! store new on_intr
	movz	%xcc, %l1, %sp
	jmp	%l0 + 8
	wrpr	%g0, %o2, %pil			! enable interrupts
	SET_SIZE(current_thread)

#ifdef DEBUG
current_thread_wrong_pil:
	.seg	".data"
	.align	4
	.asciz	"current_thread: unexpected %pil level: %d"
	.align	4
	.seg	".text"
#endif DEBUG
#endif	/* lint */

#if defined(lint)

void
clk_thread(void)
{}

#else	/* lint */

/*
 * Handle the clock interrupt in the clock interrupt thread.
 * May swtch to new stack.
 *
 * The clock interrupt handler invokes the clock thread only
 * if it is not already active.
 *
 *	Entry:
 *		%o0       = pointer to regs structure
 *		%o1       = inumber
 *		%o2       = pil
 *		%sp       = on current thread's kernel stack
 *		%o7       = return linkage to trap code
 *		%g7       = current thread
 *		%pstate   = normal globals, interrupts enabled, 
 *		            privileged, fp disabled
 *		%pil      = LOCK_LEVEL
 *
 *	Register Usage
 *		%l0       = return linkage
 *		%l1       = clock thread
 *		%l2 - %l3 = scratch
 *		%l4 - %l7 = reserved for sys_trap
 *		%o2       = cpu
 *		%o0       = scratch
 *		%o3 - %o5 = scratch
 */
	ENTRY_NP(clk_thread)
        mov	%o7, %l0
	ld	[THREAD_REG + T_CPU], %o2	! get CPU pointer
	!
	! call the handler
	!
	SERVE_INTR(%o1, %o2, %l2, %l3, %o4, %o5, %o3, %o0)
	!
	set	clock_started, %o4
	ldub	[%o4], %o4
	brz,pn	%o4, 0f
	sethi	%hi(clk_intr), %o4		! count clock interrupt
	ld	[%lo(clk_intr) + %o4], %o5
	inc	%o5
	st	%o5, [%lo(clk_intr)+ %o4]
	!
	! Try to activate the clock interrupt thread.  Set the t_lock first.
	!
	sethi	%hi(clock_thread), %l1
	ld	[%l1 + %lo(clock_thread)], %l1	! clock thread pointer
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	ldstub	[%l1 + T_LOCK], %o4	! try to set clock_thread->t_lock
	brnz,pn	%o4, 9f			! clock already running
	ld	[%l1 + T_STATE], %o5	! delay - load state
	!
	! Check state.  If it isn't TS_FREE (FREE_THREAD), it must be blocked
	! on a mutex or something.
	!
	cmp	%o5, FREE_THREAD
	bne,a,pn %xcc, 9f		! clock_thread not idle
	clrb	[%l1 + T_LOCK]		! delay - release the lock
	!
	! consider the clock thread part of the same LWP so that window
	! overflow code can find the PCB.
	!
	ld	[THREAD_REG + T_LWP], %o4
	mov	ONPROC_THREAD, %o5	! set running state
	st	%o4, [%l1 + T_LWP]
	st	%o5, [%l1 + T_STATE]
	!
	! Push the interrupted thread onto list from the clock thread.
	! Set the clock thread as the current one.
	!
	st	%l0, [THREAD_REG + T_PC]	! mark pc for resume
	st	%sp, [THREAD_REG + T_SP]	! mark stack for resume
	st	THREAD_REG, [%l1 + T_INTR]	! point clock at old thread
	st	%o2, [%l1 + T_CPU]		! set new thread's CPU pointer
	st	%o2, [%l1 + T_BOUND_CPU]	! set cpu binding for thread
	st	%l1, [%o2 + CPU_THREAD]		! point CPU at new thread
	add	%o2, CPU_THREAD_LOCK, %o4	! pointer to onproc thread lock
	st	%o4, [%l1 + T_LOCKP]		! set thread's disp lock ptr
	mov	%l1, THREAD_REG			! set curthread register
	ld	[%l1 + T_STACK], %sp		! set new stack pointer
	!
	! Initialize clock thread priority based on intr_pri and call clock
	!
	sethi	%hi(intr_pri), %o4
#ifdef	LDSH_WORKS
	ldsh	[%o4 + %lo(intr_pri)], %o4	! grab base interrupt priority
#else
	lduh	[%o4 + %lo(intr_pri)], %o4	! grab base interrupt priority
	sll	%o4, 16, %o4
	sra	%o4, 16, %o4
#endif
	add	%o4, LOCK_LEVEL, %o4
	call	clock
	sth	%o4, [%l1 + T_PRI]	! delay slot - set priority
	!
	! On return, we must determine whether the interrupted thread is
	! still pinned or not.  If not, just call swtch().
	! 
	ld	[%l1 + T_INTR], %o4	! is there a pinned thread?
	ld	[%l1 + T_CPU], %o2
#if FREE_THREAD != 0	/* Save instructions since FREE_THREAD is 0 */
	tst	%o4
	mov	FREE_THREAD, %o5	! use reg since FREE not 0
	bz,pt	1f			! nothing pinned - swtch
	st	%o5, [%l1 + T_STATE]	! delay - set thread free
#else
	brz,pn	%o4, 1f			! nothing pinned - swtch
	clr	[%l1 + T_STATE]		! delay - set thread free
#endif /* FREE_THREAD */
	st	%o4, [%o2 + CPU_THREAD]	! set CPU thread back to pinned one
	mov	%o4, THREAD_REG		! set curthread register
	clrb	[%l1 + T_LOCK]		! unlock clock_thread->t_lock
	ld	[THREAD_REG + T_SP], %sp ! restore %sp
	jmp	%l0 + 8			! return
	nop

1:
	!
	! No pinned (interrupted) thread to return to,
	! so clear the pending interrupt flag for this level and call swtch
	!
	ld	[%o2 + CPU_INTR_ACTV], %o5	! get mask of interrupts active
	andn	%o5, (1 << CLOCK_LEVEL), %o5	! clear clock interrupt flag
	call	set_base_spl			! set CPU's base SPL level
	st	%o5, [%o2 + CPU_INTR_ACTV]	! delay - store active mask

	ld      [%o2 + CPU_SYSINFO_INTRBLK], %o4   ! cpu_sysinfo.intrblk++
	inc     %o4
	call	swtch			! swtch() - give up CPU - won't be back
        st      %o4, [%o2 + CPU_SYSINFO_INTRBLK]

	!
	! returning from level10 without calling clock().
	! Increment clock_pend so clock() will rerun tick processing.
	! Must enable traps before returning to allow sys_rtt to call C.
	!
9:
	!
	! On a Uniprocessor, we're protected by SPL level
	! and don't need clock_lock.
	!
#ifdef MP
	set	clock_lock, %l1
	call	lock_set
	mov	%l1, %o0
#endif /* MP */
	sethi	%hi(clock_pend), %o4
	ld	[%o4 + %lo(clock_pend)], %o5
	inc	%o5			!  increment clock_pend
	st	%o5, [%o4 + %lo(clock_pend)]
#ifdef MP
	clrb	[%l1]			! clear the clock_lock
#endif /* MP */
0:
	jmp	%l0 + 8
	nop
	SET_SIZE(clk_thread)

#endif	/* lint */

/*
 * Set CPU's base SPL level, based on which interrupt levels are active.
 * 	Called at spl7 or above.
 */

#if defined(lint)

void
set_base_spl(void)
{}

#else	/* lint */

	ENTRY_NP(set_base_spl)
	ld	[THREAD_REG + T_CPU], %o2	! load CPU pointer
	ld	[%o2 + CPU_INTR_ACTV], %o5	! load active interrupts mask

/*
 * WARNING: non-standard callinq sequence; do not call from C
 *	%o2 = pointer to CPU
 *	%o5 = updated CPU_INTR_ACTV
 */
_intr_set_spl:					! intr_thread_exit enters here
	!
	! Determine highest interrupt level active.  Several could be blocked
	! at higher levels than this one, so must convert flags to a PIL
	! Normally nothing will be blocked, so test this first.
	!
	brz,pt	%o5, 3f				! nothing active
	sra	%o5, 11, %o3			! delay - set %o3 to bits 15-11
	set	_intr_flag_table, %o1
	tst	%o3				! see if any of the bits set
	ldub	[%o1 + %o3], %o3		! load bit number
	bnz,a,pn %xcc, 1f			! yes, add 10 and we're done
	add	%o3, 11-1, %o3			! delay - add bit number - 1

	sra	%o5, 6, %o3			! test bits 10-6
	tst	%o3
	ldub	[%o1 + %o3], %o3
	bnz,a,pn %xcc, 1f
	add	%o3, 6-1, %o3

	sra	%o5, 1, %o3			! test bits 5-1
	ldub	[%o1 + %o3], %o3

	!
	! highest interrupt level number active is in %l6
	!
1:
	cmp	%o3, CLOCK_LEVEL		! don't block clock interrupts,
	bz,a	3f
	sub	%o3, 1, %o3			!   instead drop PIL one level
3:
	retl
	st	%o3, [%o2 + CPU_BASE_SPL]	! delay - store base priority
	SET_SIZE(set_base_spl)

/*
 * Table that finds the most significant bit set in a five bit field.
 * Each entry is the high-order bit number + 1 of it's index in the table.
 * This read-only data is in the text segment.
 */
_intr_flag_table:
	.byte	0, 1, 2, 2,	3, 3, 3, 3,	4, 4, 4, 4,	4, 4, 4, 4
	.byte	5, 5, 5, 5,	5, 5, 5, 5,	5, 5, 5, 5,	5, 5, 5, 5
	.align	4

#endif	/* lint */

/*
 * int
 * intr_passivate(from, to)
 *	kthread_id_t	from;		interrupt thread
 *	kthread_id_t	to;		interrupted thread
 */

#if defined(lint)

/* ARGSUSED */
int
intr_passivate(kthread_id_t from, kthread_id_t to)
{ return (0); }

#else	/* lint */

	ENTRY_NP(intr_passivate)
	save	%sp, -SA(MINFRAME), %sp	! get a new window 

	flushw				! force register windows to stack
	!
	! restore registers from the base of the stack of the interrupt thread.
	!
	ld	[%i0 + T_STACK], %i2	! get stack save area pointer
	ld	[%i2 + (0*4)], %l0	! load locals
	ld	[%i2 + (1*4)], %l1
	ld	[%i2 + (2*4)], %l2
	ld	[%i2 + (3*4)], %l3
	ld	[%i2 + (4*4)], %l4
	ld	[%i2 + (5*4)], %l5
	ld	[%i2 + (6*4)], %l6
	ld	[%i2 + (7*4)], %l7
	ld	[%i2 + (8*4)], %o0	! put ins from stack in outs
	ld	[%i2 + (9*4)], %o1
	ld	[%i2 + (10*4)], %o2
	ld	[%i2 + (11*4)], %o3
	ld	[%i2 + (12*4)], %o4
	ld	[%i2 + (13*4)], %o5
	ld	[%i2 + (14*4)], %i4	! copy stack/pointer without using %sp
	ld	[%i2 + (15*4)], %i5
	!
	! put registers into the save area at the top of the interrupted
	! thread's stack, pointed to by %l7 in the save area just loaded.
	!
	ld	[%i1 + T_SP], %i3	! get stack save area pointer
	st	%l0, [%i3 + (0*4)]	! save locals
	st	%l1, [%i3 + (1*4)]
	st	%l2, [%i3 + (2*4)]
	st	%l3, [%i3 + (3*4)]
	st	%l4, [%i3 + (4*4)]
	st	%l5, [%i3 + (5*4)]
	st	%l6, [%i3 + (6*4)]
	st	%l7, [%i3 + (7*4)]
	st	%o0, [%i3 + (8*4)]	! save ins using outs
	st	%o1, [%i3 + (9*4)]
	st	%o2, [%i3 + (10*4)]
	st	%o3, [%i3 + (11*4)]
	st	%o4, [%i3 + (12*4)]
	st	%o5, [%i3 + (13*4)]
	st	%i4, [%i3 + (14*4)]	! fp, %i7 copied using %i4
	st	%i5, [%i3 + (15*4)]

	clr	[%i2 + ((8+6)*4)]	! clear frame pointer in save area

	sethi	%hi(intr_pri), %o0
#ifdef	LDSH_WORKS
	ldsh	[%o0 + %lo(intr_pri)], %o0	! grab base interrupt priority
#else
	lduh	[%o0 + %lo(intr_pri)], %o0	! grab base interrupt priority
	sll	%o0, 16, %o0
	sra	%o0, 16, %o0
#endif
#ifdef	LDSH_WORKS
	ldsh	[%i0 + T_PRI], %i4
#else
	lduh	[%i0 + T_PRI], %i4
	sll	%i4, 16, %i4
	sra	%i4, 16, %i4
#endif
	sub	%i4, %o0, %i4		! convert dispatch priority to pil
	ret
	restore	%i4, 0, %o0
	SET_SIZE(intr_passivate)

#endif	/* lint */

/*
 * Return a thread's interrupt level.
 * Since this isn't saved anywhere but in %l4 on interrupt entry, we
 * must dig it out of the save area.
 *
 * Caller 'swears' that this really is an interrupt thread.
 *
 * int
 * intr_level(t)
 *	kthread_id_t	t;
 */

#if defined(lint)

/* ARGSUSED */
int
intr_level(kthread_id_t t)
{ return (0); }

#else	/* lint */

	ENTRY_NP(intr_level)
	sethi	%hi(intr_pri), %o1
#ifdef	LDSH_WORKS
	ldsh	[%o1 + %lo(intr_pri)], %o1	! grab base interrupt priority
#else
	lduh	[%o1 + %lo(intr_pri)], %o1	! grab base interrupt priority
	sll	%o1, 16, %o1
	sra	%o1, 16, %o1
#endif
#ifdef	LDSH_WORKS
	ldsh	[%o0 + T_PRI], %o4
#else
	lduh	[%o0 + T_PRI], %o4
	sll	%o4, 16, %o4
	sra	%o4, 16, %o4
#endif
	retl
	sub	%o4, %o1, %o0		! convert dispatch priority to pil
	SET_SIZE(intr_level)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
int
disable_pil_intr()
{ return (0); }

#else	/* lint */

	ENTRY_NP(disable_pil_intr)
	rdpr	%pil, %o0
	retl
	wrpr	%g0, PIL_MAX, %pil		! disable interrupts (1-15)
	SET_SIZE(disable_pil_intr)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
enable_pil_intr(int pil_save)
{}

#else	/* lint */

	ENTRY_NP(enable_pil_intr)
	retl
	wrpr	%o0, %pil
	SET_SIZE(enable_pil_intr)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
int
disable_vec_intr()
{ return (0); }

#else	/* lint */

	ENTRY_NP(disable_vec_intr)
	rdpr	%pstate, %o0
	andn	%o0, PSTATE_IE, %g1
	retl
	wrpr	%g0, %g1, %pstate		! disable interrupt
	SET_SIZE(disable_vec_intr)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
enable_vec_intr(int pstate_save)
{}

#else	/* lint */

	ENTRY_NP(enable_vec_intr)
	retl
	wrpr	%g0, %o0, %pstate
	SET_SIZE(enable_vec_intr)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
setsoftint(u_int inum)
{}

#else	/* lint */

	ENTRY_NP(setsoftint)
	save	%sp, -SA(MINFRAME), %sp	! get a new window 
	rdpr	%pstate, %l5
	wrpr	%l5, PSTATE_IE, %pstate		! disable interrupt
	!
	! Fetch data from intr_vector[] table according to the inum.
	!
	! We have an interrupt number.
	! Put the request on the cpu's softint list,
	! and set %set_softint.
	!
	! Register usage
	!	%i0 - inumber
	!	%l2 - requested pil
	!	%l3 - intr_req
	!	%l4 - *cpu
	!	%l1, %l6 - temps
	!
	! check if a softint is pending for this inum already
	! if one is pending, don't bother queuing another
	!
	set	intr_vector, %l1
#if INTR_VECTOR == 0x10
	sll	%i0, 0x4, %l6
#else
	Error INTR_VECTOR has been changed
#endif
	add	%l1, %l6, %l1			! %l1 = &intr_vector[inum]
	lduh	[%l1 + IV_PENDING], %l6
	brnz,pn	%l6, 4f				! branch, if pending
	or	%g0, 1, %l2
	sth	%l2, [%l1 + IV_PENDING]		! intr_vector[inum].pend = 1
	!
	! allocate an intr_req from the free list
	!
	CPU_ADDR(%l4, %l2)
	ld	[%l4 + INTR_HEAD], %l3
	lduh	[%l1 + IV_PIL], %l2
	!
	! fixup free list
	!
	ld	[%l3 + INTR_NEXT], %l6
	st	%l6, [%l4 + INTR_HEAD]
	!
	! fill up intr_req
	!
	st	%i0, [%l3 + INTR_NUMBER]
	st	%g0, [%l3 + INTR_NEXT]
	!
	! move intr_req to appropriate list
	!
	sll	%l2, 2, %l0
	add	%l4, INTR_TAIL, %l6
	ld	[%l6 + %l0], %l1	! current tail
	brz,pt	%l1, 2f			! branch if list empty
	st	%l3, [%l6 + %l0]	! make intr_req new tail
	!
	! there's pending intr_req already
	!
	ba,pt	%xcc, 3f
	st	%l3, [%l1 + INTR_NEXT]	! update old tail
2:
	!
	! no pending intr_req; make intr_req new head
	!
	add	%l4, INTR_HEAD, %l6
	st	%l3, [%l6 + %l0]
3:
	!
	! Write %set_softint with (1<<pil) to cause a "pil" level trap
	!
	mov	1, %l1
	sll	%l1, %l2, %l1
	wr	%l1, SET_SOFTINT
4:
	wrpr	%g0, %l5, %pstate
	ret
	restore
	SET_SIZE(setsoftint)

#endif	/* lint */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
clear_soft_intr(u_int pil)
{}

#else	/* lint */

	ENTRY_NP(clear_soft_intr)
	mov	1, %o1
	sll	%o1, %o0, %o1
	retl
	wr	%o1, CLEAR_SOFTINT
	SET_SIZE(clear_soft_intr)

#endif	/* lint */




/* TICK_INT routines */




#if defined(lint)
/*
 *  
 * 
 */
 
/* ARGSUSED */
int
tickint_hndlr1()
{return (1);}
#else   /* lint */
        ENTRY_NP(tickint_hndlr1)
        or      %g0, %l7, %o0           ! rp
 
        save    %sp, -SA(MINFRAME), %sp ! get a new window
 
        ! make sure its a tickint; if not just return
        rd      SOFTINT, %l1
        and     %l1, TICK_INT_MASK, %l1
        brz,pn  %l1, 2f
        nop

        ! if profiling, pass rp so profiler can get PC
        CPU_ADDR(%o1, %o2)
        add     %o1, CPU_PROFILING, %o2
        ld      [%o2], %o3
        brz,pn  %o3, 1f
        add     %o3, PROF_RP, %o2
        st      %i0, [%o2]
1:
        call    tickint_hndlr2
        nop
2:
        ret
        restore %g0, 1, %o0
        SET_SIZE(tickint_hndlr1)
#endif  /* lint */




#if defined(lint)
/*
 * Softint generated when counter field of tick reg matches value field 
 * of tick_cmpr reg
 */

/* ARGSUSED */
void
tickcmpr_set(long long clock_cycles)
{}

#else   /* lint */
	ENTRY_NP(tickcmpr_set)

	! get 64-bit clock_cycles interval
	sllx	%o0, 0x20, %g3
	or	%g3, %o1, %g3
	brz,pn	%g3, 1f
	nop

	! get current tick reg count
	rdpr	%tick, %g2
	sllx	%g2, 1, %g2
	srlx	%g2, 1, %g2
	
	! set compare reg with (tickreg + interval)
	add	%g2, %g3, %g4
	ba,a,pt	%xcc, 2f
1:
	or	%g0, 1, %o0
	sllx	%o0, TICKINT_DIS_SHFT, %g4
2:
	retl
	wr	%g4, TICK_COMPARE	 ! set value in compare reg     
	SET_SIZE(tickcmpr_set)
#endif  /* lint */

#if defined(lint)
/*
 *  return 1 if disabled
 */

/* ARGSUSED */
int
tickint_disabled()
{ return (0); }
#else   /* lint */
	ENTRY_NP(tickint_disabled)
	rd	TICK_COMPARE, %g1
	retl
	srlx	%g1, TICKINT_DIS_SHFT, %o0
	SET_SIZE(tickint_disabled)
#endif  /* lint */

#if defined(lint)
/*
 * Softint generated when counter field of tick reg matches value field 
 * of tick_cmpr reg
 */

/* ARGSUSED */
void
tickcmpr_reset()
{}
#else   /* lint */
	ENTRY_NP(tickcmpr_reset)
	or	%g0, 1, %o0
	sllx	%o0, TICKINT_DIS_SHFT, %g1		
	retl
	wr	%g1, TICK_COMPARE	 ! set bit 63 to disable interrupt  
	SET_SIZE(tickcmpr_reset)
#endif  /* lint */

#if defined(lint)
/*
 *
 */

void
enqueue_tickint_req()
{}

#else   /* lint */
	ENTRY_NP(enqueue_tickint_req)
	sethi	%hi(tickint_inum), %o0
	ld	[%o0 + %lo(tickint_inum)], %g5
	mov	PIL_14, %g2

	! get intr_req free list
	CPU_ADDR(%g4, %g1)
	ld	[%g4 + INTR_HEAD], %g3  

	! take intr_req from free list
	ld	[%g3 + INTR_NEXT], %g6
	st	%g6, [%g4 + INTR_HEAD]

	! fill up intr_req
	st	%g5, [%g3 + INTR_NUMBER]
	st	%g0, [%g3 + INTR_NEXT]

	! add intr_req to proper pil list
	sll	%g2, 2, %g5
	add	%g4, INTR_TAIL, %g6
	ld	[%g6 + %g5], %g1		! current tail
	brz,pt	%g1, 2f			! branch if list is empty
	st	%g3, [%g6 + %g5]	! make intr_req the new tail

	! an intr_req was already queued so update old tail
	ba,pt	%xcc, 3f
	st	%g3, [%g1 + INTR_NEXT]
2:
	! no intr_req's queued so make intr_req the new head
	add	%g4, INTR_HEAD, %g6
	st	%g3, [%g6 + %g5]
3:
	retl
	nop
	SET_SIZE(enqueue_tickint_req)
#endif  /* lint */

#if defined(lint)
/*
 *
 */

void
dequeue_tickint_req()
{}

#else   /* lint */
	ENTRY_NP(dequeue_tickint_req)
	sethi	%hi(tickint_inum), %o0
	ld	[%o0 + %lo(tickint_inum)], %g5

	mov	PIL_14, %g2
	sll	%g2, 2, %g2

	! begin search with head of pil list
	CPU_ADDR(%g4, %g1)
	add	%g4, INTR_HEAD, %g6
	add	%g6, %g2, %g6		! pil queue head
	ld	[%g6], %g1  		! first entry
	or	%g0, %g6, %g3		! use %g3 as intr_req prev
1:
	brz,pn	%g1, 5f			! branch if list empty 
	nop
	ld	[%g1 + INTR_NUMBER], %o0		 
	cmp	%o0, %g5
	be,pt  %xcc, 2f
	nop
	or	%g0, %g1, %g3		
	add	%g3, INTR_NEXT, %g3	! %g3 is next of prev
	ba,pt	%xcc, 1b
	ld	[%g1 + INTR_NEXT], %g1
2:
	! dequeue the found entry
	ld	[%g1 + INTR_NEXT], %g5
	st	%g5, [%g3]		! prev.next <- current.next
	brnz,pn  %g5, 4f		! branch if tail not reached	
	nop
	add	%g4, INTR_TAIL, %g5
	cmp	%g3, %g6			
	bne,pn  %xcc, 3f
	st	%g0, [%g5 + %g2]
	mov	TICK_INT_MASK, %g3	! if queue now empty insure
	sll	%g3, PIL_14, %g2	! interrupt is clear
	or	%g2, %g3, %g3
	wr	%g3, CLEAR_SOFTINT
	ba,pt	%xcc, 4f
3:
	sub	%g3, INTR_NEXT, %g3
	st	%g3, [%g5 + %g2]
4:
	! move the found entry to the free list 
	ld	[%g4 + INTR_HEAD], %g5
	st	%g1, [%g4 + INTR_HEAD]
	st	%g5, [%g1 + INTR_NEXT]
5:
	retl
	nop
	SET_SIZE(dequeue_tickint_req)
#endif  /* lint */

