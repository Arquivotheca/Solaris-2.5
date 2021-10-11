/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)syscall_trap.s	1.9	95/03/29 SMI"

/*
 * System call trap handler.
 */
#include <sys/asm_linkage.h>
#include <sys/machpcb.h>
#include <sys/machthread.h>
#include <sys/syscall.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/pcb.h>

#if !defined(lint) && !defined(__lint)
#include "assym.s"
#endif

#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

#if defined(lint) || defined(__lint)

void
syscall_trap(void)	/* for tags only - trap handler - not called from C */
{}

#else /* lint */

/*
 * System call trap handler.
 *
 * We branch here from sys_trap when a system call occurs.
 *
 * Entry:
 *	%o0 = regs
 *
 * Usage:
 *	%l0 = saved return address
 *	%l1 = saved regs
 *	%l2 = lwp
 *
 */
	ENTRY_NP(syscall_trap)
	ld	[THREAD_REG + T_CPU], %g1	! get cpu pointer
	mov	%o7, %l0			! save return addr
	ld	[%g1 + CPU_SYSINFO_SYSCALL], %g2
	mov	%o0, %l1			! save reg pointer
	inc	%g2				! pesky stats
	mov	%i0, %o0			! copy 1st arg
	st	%g2, [%g1 + CPU_SYSINFO_SYSCALL]
	mov	%i1, %o1			! copy 2nd arg

	!
	! Set new state for LWP
	!
	ld	[THREAD_REG + T_LWP], %l2
	mov	LWP_SYS, %g3
	mov	%i2, %o2			! copy 3rd arg
	stb	%g3, [%l2 + LWP_STATE]
	mov	%i3, %o3			! copy 4th arg
	ld	[%l2 + LWP_RU_SYSC], %g3	! pesky statistics
	mov	%i4, %o4			! copy 5th arg
	inc	%g3
	st	%g3, [%l2 + LWP_RU_SYSC]
	mov	%i5, %o5			! copy 6th arg
	! args for direct syscalls now set up

#ifdef TRAPTRACE
	!
	! make trap trace entry - helps in debugging
	!
	rdpr	%pstate, %l3
	andn	%l3, PSTATE_IE | PSTATE_AM, %g3
	wrpr	%g0, %g3, %pstate		! disable interrupt
	TRACE_PTR(%g3, %g2)			! get trace pointer
	rdpr	%tick, %g1
	stxa	%g1, [%g3 + TRAP_ENT_TICK]%asi
	ldx	[%l1 + G1*4], %g1		! get syscall code
	stha	%g1, [%g3 + TRAP_ENT_TL]%asi
	set	TT_SC_ENTR, %g2
	stha	%g2, [%g3 + TRAP_ENT_TT]%asi
	stxa	%g7, [%g3 + TRAP_ENT_TSTATE]%asi ! save thread in tstate space
	sta	%sp, [%g3 + TRAP_ENT_SP]%asi
	sta	%o0, [%g3 + TRAP_ENT_F1]%asi
	sta	%o1, [%g3 + TRAP_ENT_F2]%asi
	sta	%o2, [%g3 + TRAP_ENT_F3]%asi
	sta	%o3, [%g3 + TRAP_ENT_F4]%asi
	sta	%o4, [%g3 + TRAP_ENT_TPC]%asi
	sta	%o5, [%g3 + TRAP_ENT_TR]%asi
	TRACE_NEXT(%g3, %g2, %g1)		! set new trace pointer
	wrpr	%g0, %l3, %pstate		! enable interrupt
#endif /* TRAPTRACE */
#ifdef	FP_DEFERRED
	!
	! If floating-point is enabled, handle possible exceptions here.
	!
	rd	%fprs, %g3
	st	%o0, [%l2 + LWP_ARG + 0]	! save 1st arg
	btst	FPRS_FEF, %g3
	st	%o1, [%l2 + LWP_ARG + 4]	! save 2nd arg
	bz,pt	1f
	st	%o2, [%l2 + LWP_ARG + 8]	! save 3rd arg
	ld	[%sp + MPCB_FLAGS], %l3
	andn	%l3, FP_TRAPPED, %l3
	st	%l3, [%sp + MPCB_FLAGS]
	st	%fsr, [%sp + 64]
	ld	[%sp + MPCB_FLAGS], %l3
	btst	FP_TRAPPED, %l3
	bnz,pn	_syscall_fp
	.empty
1:
#else
	st	%o0, [%l2 + LWP_ARG + 0]	! save 1st arg
	st	%o1, [%l2 + LWP_ARG + 4]	! save 2nd arg
	st	%o2, [%l2 + LWP_ARG + 8]	! save 3rd arg
#endif	
	st	%o3, [%l2 + LWP_ARG + 12]	! save 4th arg
	!
	! Test for pre-system-call handling
	!
	ldub	[THREAD_REG + T_PRE_SYS], %g3	! pre-syscall proc?
#ifdef SYSCALLTRACE
	sethi	%hi(syscalltrace), %g4
	ld	[%g4 + %lo(syscalltrace)], %g4
	orcc	%g3, %g4, %g0			! pre_syscall OR syscalltrace?
#else
	tst	%g3				! is pre_syscall flag set?
#endif /* SYSCALLTRACE */
	st	%o4, [%l2 + LWP_ARG + 16]	! save 5th arg
	bnz,pn	%icc, _syscall_pre		! yes - pre_syscall needed
	st	%o5, [%l2 + LWP_ARG + 20]	! save 6th arg
	! lwp_arg now set up
3:
	!
	! Call the handler.  The %o's and lwp_arg have been set up.
	!
	ldx	[%l1 + G1*4], %g1		! get code
	set	(sysent + SY_CALLC), %g3	! load address of vector table
	cmp	%g1, NSYSCALL			! check range
	sth	%g1, [THREAD_REG + T_SYSNUM]	! save syscall code
	bgeu,pn	%xcc, _syscall_ill
#if SYSENT_SIZE == 16
	sll	%g1, 4, %g4			! delay - get index 
#else
	.error	"sysent size change requires change in shift amount"
#endif
	ld	[%g3 + %g4], %g3		! load system call handler
	call	%g3				! call system call handler
	nop

#ifdef TRAPTRACE
	!
	! make trap trace entry for return - helps in debugging
	!
	rdpr	%pstate, %g5
	andn	%g5, PSTATE_IE | PSTATE_AM, %g4
	wrpr	%g0, %g4, %pstate		! disable interrupt
	TRACE_PTR(%g4, %g2)			! get trace pointer
	rdpr	%tick, %g2
	stxa	%g2, [%g4 + TRAP_ENT_TICK]%asi
	ld	[THREAD_REG + T_SYSNUM], %g2
	stha	%g2, [%g4 + TRAP_ENT_TL]%asi
	mov	TT_SC_RET, %g2			! system call return code
	stha	%g2, [%g4 + TRAP_ENT_TT]%asi
	ld	[%l1 + nPC*4], %g2		! get saved npc (new pc)
	sta	%g2, [%g4 + TRAP_ENT_TPC]%asi
	ldx	[%l1 + TSTATE*4], %g2		! get saved tstate
	stxa	%g2, [%g4 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g4 + TRAP_ENT_SP]%asi
	sta	THREAD_REG, [%g4 + TRAP_ENT_TR]%asi
	sta	%o0, [%g4 + TRAP_ENT_F1]%asi
	sta	%o1, [%g4 + TRAP_ENT_F2]%asi
	TRACE_NEXT(%g4, %g2, %g3)		! set new trace pointer
	wrpr	%g0, %g5, %pstate		! enable interrupt
#endif /* TRAPTRACE */
	!
	! Check for post-syscall processing.
	! This tests all members of the union containing t_astflag, t_post_sys,
	! and t_sig_check with one test.
	!
	ld	[THREAD_REG + T_POST_SYS_AST], %g1
#ifdef SYSCALLTRACE
	sethi	%hi(syscalltrace), %g4
	ld	[%g4 + %lo(syscalltrace)], %g4
	orcc	%g4, %g1, %g0			! OR in syscalltrace
#else
	tst	%g1				! need post-processing?
#endif /* SYSCALLTRACE */
	bnz,pn	%icc, _syscall_post		! yes - post_syscall or AST set
	mov	LWP_USER, %g1
	stb	%g1, [%l2 + LWP_STATE]		! set lwp_state
	stx	%o0, [%l1 + O0*4]		! set rp->r_o0
	stx	%o1, [%l1 + O1*4]		! set rp->r_o1
	clrh	[THREAD_REG + T_SYSNUM]		! clear syscall code
	ldx	[%l1 + TSTATE*4], %g1		! get saved tstate
	ld	[%l1 + nPC*4], %g2		! get saved npc (new pc)
	mov	CCR_IC, %g3
	sllx	%g3, TSTATE_CCR_SHIFT, %g3
	add	%g2, 4, %g4			! calc new npc
	andn	%g1, %g3, %g1			! clear carry bit for no error
	st	%g2, [%l1 + PC*4]
	st	%g4, [%l1 + nPC*4]
	jmp	%l0 + 8
	stx	%g1, [%l1 + TSTATE*4]

_syscall_pre:
	ldx	[%l1 + G1*4], %g1
	call	pre_syscall			! abort = pre_syscall(arg0)
	sth	%g1, [THREAD_REG + T_SYSNUM]

	brnz,pn	%o0, _syscall_post		! did it abort?
	nop
	ldx	[%l1 + O0*4], %o0		! reload args
	ldx	[%l1 + O1*4], %o1
	ldx	[%l1 + O2*4], %o2
	ldx	[%l1 + O3*4], %o3
	ldx	[%l1 + O4*4], %o4
	ldx	[%l1 + O5*4], %o5
	st	%o0, [%l2 + LWP_ARG + 0]	! resave args
	st	%o1, [%l2 + LWP_ARG + 4]
	st	%o2, [%l2 + LWP_ARG + 8]
	st	%o3, [%l2 + LWP_ARG + 12]
	st	%o4, [%l2 + LWP_ARG + 16]
	ba,pt	%xcc, 3b
	st	%o5, [%l2 + LWP_ARG + 20]

	!
	! Floating-point trap was pending at start of system call.
	! Here with:
	!	%l3 = mpcb_flags
	!
_syscall_fp:
	andn	%l3, FP_TRAPPED, %l3
	st	%l3, [%sp + MPCB_FLAGS]		! clear FP_TRAPPED
	jmp	%l0 + 8				! return to user_rtt
	clrh	[THREAD_REG + T_SYSNUM]		! clear syscall code

	!
	! illegal system call - syscall number out of range
	!
_syscall_ill:
	call	nosys
	nop
	!
	! Post-syscall with special processing needed.
	!
_syscall_post:
	call	post_syscall			! post_syscall(rvals)
	nop
	jmp	%l0 + 8				! return to user_rtt
	nop
	SET_SIZE(syscall_trap)

#endif /* lint */


/*
 * lwp_rtt - start execution in newly created LWP.
 *	Here with t_post_sys set by lwp_create, and lwp_eosys == JUSTRETURN,
 *	so that post_syscall() will run and the registers will
 *	simply be restored.
 *	This must go out through sys_rtt instead of syscall_rtt.
 */
#if defined(lint) || defined(__lint)

void
lwp_rtt()
{}

#else	/* lint */

	ENTRY_NP(lwp_rtt)
	ld      [THREAD_REG + T_STACK], %sp	! set stack at base
	add	%sp, REGOFF, %l7
	ldx	[%l7 + O0*4], %o0
	call	post_syscall
	ldx	[%l7 + O1*4], %o1
	ba,a,pt	%xcc, user_rtt
	SET_SIZE(lwp_rtt)

#endif	/* lint */
