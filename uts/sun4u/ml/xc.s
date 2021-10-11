/*
 *      Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)xc.s	1.36	95/09/07 SMI"

#if defined(lint)
#include <sys/types.h>
#include <sys/cpuvar.h>
#else	/*lint */
#include "assym.s"
#endif	/* lint */

#include <sys/param.h>
#include <sys/asm_linkage.h>
#include <sys/errno.h>
#include <sys/intreg.h>
#include <sys/intr.h>
#include <sys/x_call.h>
#include <sys/spitasi.h>
#include <sys/privregs.h>
#include <sys/machthread.h>
#include <sys/machtrap.h>
#include <sys/xc_impl.h>

#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

#if !defined(lint)

	.global _dispatch_status_busy
_dispatch_status_busy:
	.asciz	"ASI_INTR_DISPATCH_STATUS error: busy"
	.align	4

#endif	/* !lint */

#if defined(lint)

/* ARGSUSED */
void
setup_mondo(u_int func, u_int arg1, u_int arg2, u_int arg3, u_int arg4)
{}

#else

/*
 * Setup interrupt dispatch data registers
 * Entry:
 *	%o0 - function or inumber to call
 *	%o1, %o2, %o3, & %o4  - arguments
 */
	.seg "text"

	ENTRY(setup_mondo)
#ifdef DEBUG
	!
	! IDSR should not be busy at the moment
	!
	ldxa	[%g0]ASI_INTR_DISPATCH_STATUS, %g1
	btst	IDSR_BUSY, %g1
	bz,pt	%xcc, 1f
	mov	ASI_INTR_DISPATCH, %asi
	sethi	%hi(_dispatch_status_busy), %o0
	call	panic
	or	%o0, %lo(_dispatch_status_busy), %o0
#endif DEBUG
	!
	! interrupt vector dispach data reg 0
	! 	low  32bit: %o0
	! 	high 32bit: 0
	!
	mov	ASI_INTR_DISPATCH, %asi
1:
	stxa	%o0, [IDDR_0]%asi
	!
	! interrupt vector dispach data reg 1
	! 	low  32bit: %o1
	! 	high 32bit: %o3
	!
	sllx	%o3, 32, %g1
	or	%o1, %g1, %g1
	stxa	%g1, [IDDR_1]%asi
	!
	! interrupt vector dispach data reg 2
	! 	low  32bit: %o2
	! 	high 32bit: %o4
	!
	sllx	%o4, 32, %g5
	or	%o2, %g5, %g5
	stxa	%g5, [IDDR_2]%asi
	retl
	membar	#Sync			! allowed to be in the delay slot
	SET_SIZE(setup_mondo)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
send_mondo(int upaid)
{}

#else

/*
 * Send a mondo interrupt to the processor. (TL==0)
 * 	Entry:
 *		%o0 - upa port id of the processor
 * 
 * 	Register Usage:
 * 		%o0 - dispatch status
 * 		%o1 - debugging counter for NACK
 * 		%o2 - debugging counter for BUSY
 */
 
	ENTRY(send_mondo)
	!
	! construct the interrupt dispatch command register in %g1
	!
	sll	%o0, IDCR_PID_SHIFT, %g1	! IDCR<18:14> = upa port id
	or	%g1, IDCR_OFFSET, %g1		! IDCR<13:0> = 0x70
	set	XC_NACK_COUNT, %o1		! reset NACK debugging counter
1:
	set	XC_BUSY_COUNT, %o2		! reset BUSY debugging counter
	!
	! data registers are loaded already, dispatch the command
	!
	stxa	%g0, [%g1]ASI_INTR_DISPATCH	! interrupt vector dispatch
	membar	#Sync
2:
	ldxa	[%g0]ASI_INTR_DISPATCH_STATUS, %g5
	tst	%g5
	bnz,pn	%xcc, 3f
	  deccc	%o2
	retl
	nop
	!
	! update BUSY debugging counter; quit if beyond the limit
	!
3:
	bnz,a,pt %xcc, 4f
	btst	IDSR_BUSY, %g5
	sethi	%hi(_send_mondo_busy), %o0
	call	panic
	or	%o0, %lo(_send_mondo_busy), %o0

	!
	! wait if BUSY
	!
4:
	bnz,pn	%xcc, 2b
	nop

	!
	! update NACK debugging counter; quit if beyond the limit
	!
	deccc	%o1
	bnz,a,pt %xcc, 5f
	btst	IDSR_NACK, %g5
	sethi	%hi(_send_mondo_nack), %o0
	call	panic
	or	%o0, %lo(_send_mondo_nack), %o0
	!
	! repeat the dispatch if NACK
	!
5:
	bz,pn	%xcc, 7f
	nop
	!
	! nacked so pause for 1 usec_delay and retry
	!
	sethi	%hi(Cpudelay), %g2
	ld	[%g2 + %lo(Cpudelay)], %g2 ! microsecond countdown counter
	orcc	%g2, 0, %g2		! set cc bits to nz
6:	bnz	6b			! microsecond countdown loop
	subcc	%g2, 1, %g2		! 2 instructions in loop
	ba,pt	%xcc,1b
	nop
7:
	retl
	nop
	SET_SIZE(send_mondo)

_send_mondo_busy:
	.asciz	"send_mondo: BUSY"
	.align	4

_send_mondo_nack:
	.asciz	"send_mondo: NACK"
	.align	4
#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
send_self_xcall(struct cpu *cpu, u_int arg1, u_int arg2, u_int arg3, u_int arg4,
    u_int func)
{}

#else

/*
 * For a x-trap request to the same processor, just send a fast trap
 */
	ENTRY_NP(send_self_xcall)
	ta ST_SELFXCALL
	retl
	nop
	SET_SIZE(send_self_xcall)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
self_xcall(struct cpu *cpu, u_int arg1, u_int arg2, u_int arg3, u_int arg4,
    u_int func)
{}

#else

/*
 *	
 * Entered by the software trap (TT=ST_SELFXCALL, TL>0) thru send_self_xcall().
 * Emulate the mondo handler - vec_interrupt().
 *
 * Global registers are the Alternate Globals.
 *	Entries:
 *		%o0 - CPU
 *		%o5 - function or inumber to call
 *		%o1, %o2, %o3, %o4  - arguments
 *		
 */
	ENTRY_NP(self_xcall)
	!
	! Verify what to do -
	! It could be a fast trap handler address (pc > KERNELBASE)
	! or an interrupt number.
	!
	set	KERNELBASE, %g4
	cmp	%o5, %g4
	bl,pt	%xcc, 0f			! an interrupt number found
	mov	%o5, %g5

	!
	! TL>0 handlers are expected to do "retry"
	! prepare their return PC and nPC now
	!
	rdpr	%tnpc, %g1
	wrpr	%g1, %tpc			!  PC <- TNPC[TL]
 	add	%g1, 4, %g1
	wrpr	%g1, %tnpc			! nPC <- TNPC[TL] + 4

#ifdef TRAPTRACE
	TRACE_PTR(%g4, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g4 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g4 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g4 + TRAP_ENT_TT]%asi
	sta	%g5, [%g4 + TRAP_ENT_TR]%asi ! pc of the TL>0 handler
	rdpr	%tpc, %g6
	sta	%g6, [%g4 + TRAP_ENT_TPC]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g4 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g4 + TRAP_ENT_SP]%asi
	sta	%o1, [%g4 + TRAP_ENT_F1]%asi
	sta	%o3, [%g4 + TRAP_ENT_F2]%asi
	sta	%o2, [%g4 + TRAP_ENT_F3]%asi
	sta	%o4, [%g4 + TRAP_ENT_F4]%asi
	TRACE_NEXT(%g4, %g6, %g3)
#endif TRAPTRACE
	!
	! Load the arguments for the fast trap handler.
	!
	mov	%o1, %g1
	mov	%o2, %g2
	mov	%o3, %g3
	jmp	%g5				! call the fast trap handler
	mov	%o4, %g4			! and won't be back
	/* Not Reached */

0:
	!
	! Fetch data from intr_vector[] table according to the interrupt number.
	!
	! We have an interrupt number.
	! Put the request on the cpu's softint list,
	! and set %set_softint.
	!
	! Register usage
	!	%g5 - inumber
	!	%g2 - requested pil
	!	%g3 - intr_req
	!	%g4 - *cpu
	!	%g1, %g6 - temps
	!
	! allocate an intr_req from the free list
	!
	ld	[%o0 + INTR_HEAD], %g3
	mov	%o0, %g4		! use g4 to match vec_interrupt()'s code
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
3:
	!
	! Write %set_softint with (1<<pil) to cause a "pil" level trap
	!
	mov	1, %g1
	sll	%g1, %g2, %g1
	wr	%g1, SET_SOFTINT
	done
	SET_SIZE(self_xcall)

#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
xc_trace(int traptype, cpuset_t cpu_set, int cnt)
{}

#else
#ifdef	TRAPTRACE
	ENTRY(xc_trace)
	rdpr	%pstate, %o3
	andn	%o3, PSTATE_IE | PSTATE_AM, %o4
	wrpr	%g0, %o4, %pstate		/* disable interrupts */
	TRACE_PTR(%g1, %g2)
	rdpr	%tick, %g3
	stxa	%g3, [%g1 + TRAP_ENT_TICK]%asi
	stha	%g0, [%g1 + TRAP_ENT_TL]%asi
	set	TT_XCALL, %o4
	or	%o0, %o4, %o0
	stha	%o0, [%g1 + TRAP_ENT_TT]%asi
	sta	%o7, [%g1 + TRAP_ENT_TPC]%asi
	sta	%o1, [%g1 + TRAP_ENT_SP]%asi
	set	cpu_ready_set, %o4
	ld	[%o4], %o4
	sta	%o4, [%g1 + TRAP_ENT_TR]%asi
	stxa	%g0, [%g1 + TRAP_ENT_TSTATE]%asi
	stxa	%o2, [%g1 + TRAP_ENT_F1]%asi
	stxa	%g0, [%g1 + TRAP_ENT_F3]%asi
	TRACE_NEXT(%g1, %g2, %g3)
	retl
	wrpr	%g0, %o3, %pstate		/* enable interrupts */
	SET_SIZE(xc_trace)
#else	TRAPTRACE
#define	xc_trace(a, b, c)
#endif	TRAPTRACE

#endif	/* lint */

#if defined(lint)
void
idle_stop_xcall(void)
{}
#else
/*
 * idle or stop xcall handler.
 *
 * Called in response to an xt_some initiated by idle_other_cpus
 * and stop_other_cpus.
 *
 *	Entry:
 *		%g1    - handler at TL==0
 *		%g2    - arg1
 *		%g3    - arg2
 *		%g4    - arg3
 *
 * 	Register Usage:
 *		%g1    - handler at TL==0
 *		%g2.lo - arg1
 *		%g3    - arg2
 *		%g2.hi - arg3
 *		%g4    - pil
 *
 * %g1 will either be cpu_idle_self or cpu_stop_self and is
 * passed to sys_trap, to run at TL=0. No need to worry about
 * the regp passed to cpu_idle_self/cpu_stop_self, since
 * neither require arguments.
 */
	ENTRY_NP(idle_stop_xcall)
	sllx	%g4, 32, %g4		! high 32 bit of %g2
	or	%g4, %g2, %g2
	rdpr	%pil, %g4
	cmp	%g4, XCALL_PIL
	ba,pt	%xcc, sys_trap
	movl	%xcc, XCALL_PIL, %g4
	SET_SIZE(idle_stop_xcall)
#endif	/* lint */
