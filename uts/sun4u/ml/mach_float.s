/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ident	"@(#)mach_float.s	1.13	95/07/28 SMI"

#include <sys/asm_linkage.h>
#include <sys/trap.h>
#include <sys/privregs.h>
#include <sys/regset.h>
#include <sys/spitregs.h>
#include <sys/machthread.h>
#include <sys/machtrap.h>
#include <sys/machsig.h>

/*
 * The Spitfire floating point code has been changed not to use install/
 * save/restore/fork/freectx() because of the special memcpy library
 * routines, which will lose too much performance if they have to go
 * through the fp_disabled trap (which used to call installctx()). So
 * now fp_save/fp_restore are called from resume, and they don't care
 * whether floating point was enabled from the user program via the
 * fp_enabled trap or from the memcpy library, which just turns on floating
 * point in the fprs register itself. The new routine lwp_freeregs is
 * called everywhere freectx is called, and code was added to the sun4u-
 * specific version of lwp_forkregs (which is called everywhere forkctx
 * is called) to handle forking the floating point registers.
 *
 * Note that for the fprs dirty upper/lower bits are not used for now,
 * because the #instructions to determine if we need to use them is probably
 * greater than the #insructions just using them. This is a possible future
 * optimization, only do it with very careful benchmarking!
 *
 * The fp_fksave and and fp_load were split into two routines for the
 * sake of efficiency between the getfpregs/xregs_getfpregs and
 * setfpregs/xregs_setfpregs. But note that for saving and restoring
 * context, both *must* happen. For prmachdep, aka access from [k]adb,
 * it's OK if only one part happens.
 */ 
#if !defined(lint)
#include "assym.s"
#endif	/* lint */

/*
 * fp_zero()
 */

#if defined(lint) || defined(__lint)

void
fp_zero(void)
{}

#else	/* lint */

	ENTRY_NP(fp_zero)
	fzero	%f0
	fzero	%f2
	faddd	%f0, %f2, %f4
	fmuld	%f0, %f2, %f6
	faddd	%f0, %f2, %f8
	fmuld	%f0, %f2, %f10
	faddd	%f0, %f2, %f12
	fmuld	%f0, %f2, %f14
	faddd	%f0, %f2, %f16
	fmuld	%f0, %f2, %f18
	faddd	%f0, %f2, %f20
	fmuld	%f0, %f2, %f22
	faddd	%f0, %f2, %f24
	fmuld	%f0, %f2, %f26
	faddd	%f0, %f2, %f28
	fmuld	%f0, %f2, %f30
	faddd	%f0, %f2, %f32
	fmuld	%f0, %f2, %f34
	faddd	%f0, %f2, %f36
	fmuld	%f0, %f2, %f38
	faddd	%f0, %f2, %f40
	fmuld	%f0, %f2, %f42
	faddd	%f0, %f2, %f44
	fmuld	%f0, %f2, %f46
	faddd	%f0, %f2, %f48
	fmuld	%f0, %f2, %f50
	faddd	%f0, %f2, %f52
	fmuld	%f0, %f2, %f54
	faddd	%f0, %f2, %f56
	fmuld	%f0, %f2, %f58
	faddd	%f0, %f2, %f60
	retl
	fmuld	%f0, %f2, %f62
	SET_SIZE(fp_zero)

#endif	/* lint */

/*
 * fp_save(fp)
 *	struct v9_fpu *fp;
 * fp_fksave(fp)
 *	struct v9_fpu *fp;
 * Store the floating point registers.
 */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
fp_save(struct v9_fpu *fp)
{}

/* ARGSUSED */
void
fp_fksave(struct v9_fpu *fp)
{}

#else	/* lint */

	ENTRY_NP(fp_save)
	ALTENTRY(fp_fksave)
	BSTORE_FPREGS(%o0)			! store V9 regs
	retl
	stx	%fsr, [%o0 + FPU_FSR]		! store fsr
	SET_SIZE(fp_fksave)
	SET_SIZE(fp_save)

#endif	/* lint */

/*
 * fp_v8_fksave(fp)
 *	struct v9_fpu *fp;
 *
 * This is like the above routine but only saves the lower half.
 */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
fp_v8_fksave(struct v9_fpu *fp)
{}

#else	/* lint */

	ENTRY_NP(fp_v8_fksave)
	BSTORE_V8_FPREGS(%o0)			! store V8 regs
	retl
	stx	%fsr, [%o0 + FPU_FSR]		! store fsr
	SET_SIZE(fp_v8_fksave)

#endif	/* lint */

/*
 * fp_v8p_fksave(fp)
 *	struct v9_fpu *fp;
 *
 * This is like the above routine but only saves the upper half.
 */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
fp_v8p_fksave(struct v9_fpu *fp)
{}

#else	/* lint */

	ENTRY_NP(fp_v8p_fksave)
	BSTORE_V8P_FPREGS(%o0)			! store V9 extra regs
	retl
	stx	%fsr, [%o0 + FPU_FSR]		! store fsr
	SET_SIZE(fp_v8p_fksave)

#endif	/* lint */

/*
 * fp_restore(fp)
 *	struct v9_fpu *fp;
 */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
fp_restore(struct v9_fpu *fp)
{}

#else	/* lint */

	ENTRY_NP(fp_restore)
	BLOAD_FPREGS(%o0)			! load V9 regs
	retl
	ldx	[%o0 + FPU_FSR], %fsr		! restore fsr
	SET_SIZE(fp_restore)

#endif	/* lint */

/*
 * fp_v8_load(fp)
 *	struct v9_fpu *fp;
 */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
fp_v8_load(struct v9_fpu *fp)
{}

#else	/* lint */

	ENTRY_NP(fp_v8_load)
	BLOAD_V8_FPREGS(%o0)			! load V8 regs
	retl
	ldx	[%o0 + FPU_FSR], %fsr		! restore fsr
	SET_SIZE(fp_v8_load)

#endif	/* lint */

/*
 * fp_v8p_load(fp)
 *	struct v9_fpu *fp;
 */

#if defined(lint) || defined(__lint)

/* ARGSUSED */
void
fp_v8p_load(struct v9_fpu *fp)
{}

#else	/* lint */

	ENTRY_NP(fp_v8p_load)
	BLOAD_V8P_FPREGS(%o0)			! load V9 extra regs
	retl
	ldx	[%o0 + FPU_FSR], %fsr		! restore fsr
	SET_SIZE(fp_v8p_load)

#endif	/* lint */

/*
 * Floating Point Exceptions handled according to type:
 *	2) unfinished_fpop
 *		re-execute the faulty instruction(s) using
 *		software emulation (must do every instruction in FQ)
 *	3) unimplemented_fpop
 *		an unimplemented instruction, if it is legal,
 *		will cause emulation of the instruction (and all
 *		other instuctions in the FQ)
 *	4) sequence_error
 *		panic, this should not happen, and if it does it
 *		it is the result of a kernel bug
 *
 * This code assumes the trap preamble has set up the window environment
 * for execution of kernel code.
 * Note: this code could be changed to be part of the cpu-specific
 * (ie, Spitfire-specific) module code before final release.
 */

#if defined(lint)

/* ARGSUSED */
void
_fp_exception(struct regs *rp)
{}

#else	/* lint */

	ENTRY_NP(_fp_exception)
	mov	%o7, %l0

	ld	[THREAD_REG + T_LWP], %l2 ! get lwp
	ld	[%l2 + LWP_FPU], %l3	! get lwp_fpu
	stx	%fsr, [%l3 + FPU_FSR]	! get floating point status
	ldx	[%l3 + FPU_FSR], %g1
	set	FSR_FTT, %o4
	and	%g1, %o4, %g2		! mask out trap type
	srl	%g2, FSR_FTT_SHIFT, %o4	! check ftt
	!
	! traps are already enabled to allow other
	! interrupts while emulating floating point instructions
	!
	cmp	%o4, FTT_SEQ		! sanity check for bogus exceptions
	blt,a,pt %xcc, fpeok
	nop
	!
	! Sequence error or unknown ftt exception.
	!
seq_error:
	set	.badfpexcpmsg, %o0	! panic
	call	panic
	mov	%o4, %o1		! mov ftt to o1 for stack backtrace

fpeok:
	!
	! Get the floating point instruction, and run the floating
	! point simulator. There is no floating point queue, so we fake one.
	!
	call	fp_precise		! fp_precise(&regs)
	nop

	!
	! the fsr's condition codes could have been modified
	! by emulation so reload the fsr to update them with
	! the simulator results
	!
fp_ret:
	rd	%fprs, %g1		! read fprs, save value in %g1
	st	%g1, [%l3 + FPU_FPRS]	! save fprs
	ldx	[%l3 + FPU_FSR], %g1
	!
	! clear interrupting condition in fsr
	!
	set	(FSR_FTT), %o4		! clear ftt bits
	andn	%g1, %o4, %g2
	stx	%g2, [%l3 + FPU_FSR]
	jmp	%l0 + 8
	ldx	[%l3 + FPU_FSR], %fsr	! ld new fsr to set condition codes
	SET_SIZE(_fp_exception)

.badfpexcpmsg:
	.asciz	"unexpected floating point exception %x\n"

	.align 4

#endif	/* lint */

/*
 * Floating Point Exceptions.
 * handled according to type:
 *	1) IEEE_exception
 *		re-execute the faulty instruction(s) using
 *		software emulation (must do every instruction in FQ)
 *
 * This code assumes the trap preamble has set up the window environment
 * for execution of kernel code.
 * Note: this code could be changed to be part of the cpu-specific
 * (ie, Spitfire-specific) module code before final release.
 */

#if defined(lint)

/* ARGSUSED */
void
_fp_ieee_exception(struct regs *rp)
{}

#else	/* lint */

	ENTRY_NP(_fp_ieee_exception)
	mov	%o7, %l0

	ld	[THREAD_REG + T_LWP], %l2 ! get lwp
	ld	[%l2 + LWP_FPU], %l3	! get lwp_fpu
	stx	%fsr, [%l3 + FPU_FSR]	! get floating point status
	ldx	[%l3 + FPU_FSR], %g1
	!
	! Call fpu_trap directly, don't bother to run the fp simulator.
	! The *rp is alreay in %o0.
	!
	set	(T_FP_EXCEPTION_IEEE), %o1	! trap type

	ld	[%o0 + PC*4], %o2	! addr
	ld	[%o0 + nPC*4], %o3	! fix up deferred trap address
	st	%o3, [%o0 + PC*4]
	add	%o3, 4, %o4
	st	%o4, [%o0 + nPC*4]

	set	FSR_CEXC, %o3
	and	%g1, %o3, %g2		! mask out cexc

	andcc	%g2, FSR_CEXC_NX, %g0	! check for inexact
	bnz,a,pt %xcc, fpok
	or	%g0, FPE_FLTRES, %o3	! fp inexact code

	andcc	%g2, FSR_CEXC_DZ, %g0	! check for divide-by-zero
	bnz,a,pt %xcc, fpok
	or	%g0, FPE_FLTDIV, %o3	! fp divide by zero code

	andcc	%g2, FSR_CEXC_UF, %g0	! check for underflow
	bnz,a,pt %xcc, fpok
	or	%g0, FPE_FLTUND, %o3	! fp underflow code

	andcc	%g2, FSR_CEXC_OF, %g0	! check for overflow
	bnz,a,pt %xcc, fpok
	or	%g0, FPE_FLTOVF, %o3	! fp overflow code

	andcc	%g2, FSR_CEXC_NV, %g0	! check for invalid
	bnz,a,pn %xcc, fpok
	or	%g0, FPE_FLTINV, %o3	! fp invalid code

cexec_err:
	set	.badfpcexcmsg, %o0	! panic
	call	panic
	mov	%g1, %o1		! mov fsr to o1 for stack backtrace

fpok:
	call	fpu_trap		! fpu_trap()
	clr	%o4			! rw
	!
	! reload and clear interrupting condition in fsr
	!
	ldx	[%l3 + FPU_FSR], %g1
	set	(FSR_FTT_IEEE), %o4
	xor	%g1, %o4, %g2		! clear ftt_ieee bit
	stx	%g2, [%l3 + FPU_FSR]
	jmp	%l0 + 8
	ldx	[%l3 + FPU_FSR], %fsr	! ld new fsr to set condition codes
	SET_SIZE(_fp_ieee_exception)

.badfpcexcmsg:
	.asciz	"No floating point exception, fsr %x\n"

	.align 4

#endif	/* lint */
