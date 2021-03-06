/*
 * Copyright (c) 1986 by Sun Microsystems, Inc.
 */

#ident	"@(#)fpu.c	1.29	95/09/25 SMI"	/* SunOS-4.1 1.9 89/08/07 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/fault.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/core.h>
#include <sys/pcb.h>
#include <sys/cpuvar.h>
#include <sys/thread.h>
#include <sys/disp.h>
#include <sys/stack.h>
#include <sys/cmn_err.h>
#include <sys/privregs.h>
#include <sys/debug.h>

#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>
#include <sys/fpu/fpusystm.h>

int fpdispr = 0;

void
fp_core(corep)
	struct core *corep;
{
	unsigned int i;
	register klwp_id_t lwp = ttolwp(curthread);
	struct v9_fpu *fp;

	fp = lwptofpu(lwp);
	for (i = 0; i < 32; i++)	/* make a fake V8 struct */
		corep->c_fpu.fpu_fr.fpu_regs[i] = fp->fpu_fr.fpu_regs[i];
	corep->c_fpu.fpu_fsr = (u_int)fp->fpu_fsr;
	corep->c_fpu.fpu_qcnt = fp->fpu_qcnt;
	corep->c_fpu.fpu_q_entrysize = fp->fpu_q_entrysize;
	corep->c_fpu.fpu_en = fp->fpu_en;
	corep->c_fpu.fpu_q = corep->c_fpu_q;
	for (i = 0; i < fp->fpu_qcnt; i++)
		corep->c_fpu_q[i] = fp->fpu_q[i];
}

/*
 * For use by procfs to save the floating point context of the thread.
 */
void
fp_prsave(fp)
	struct v9_fpu *fp;
{
	extern void _fp_write_fprs(unsigned);

	if ((fp->fpu_en) || (fp->fpu_fprs & FPRS_FEF))  {
		kpreempt_disable();
		if (fpu_exists) {
			if ((fp->fpu_fprs & FPRS_FEF) != FPRS_FEF) {
				unsigned fprs = (FPRS_FEF|FPRS_DU|FPRS_DL);

				_fp_write_fprs(fprs);
				fp->fpu_fprs = fprs;
				if (fpdispr)
				    printf("fp_procsave with fp disabled!\n");
			}
			fp_fksave(fp);
			CPU->cpu_fpowner = NULL;
		}
		kpreempt_enable();
	}
}

/*
 * Copy the floating point context of the forked thread.
 */
void
fp_fork(lwp, clwp, pfprs)
	klwp_t *lwp, *clwp;
	unsigned pfprs;
{
	struct v9_fpu *cfp, *pfp;
	unsigned char i;

	cfp = lwptofpu(clwp);
	pfp = lwptofpu(lwp);

	/*
	 * copy the parents fpq
	 */
	cfp->fpu_qcnt = pfp->fpu_qcnt;
	for (i = 0; i < pfp->fpu_qcnt; i++)
		cfp->fpu_q[i] = pfp->fpu_q[i];

	/*
	 * save the context of the parent into the childs fpu structure
	 */
	cfp->fpu_fprs = pfp->fpu_fprs = pfprs;
	fp_fksave(cfp);
	cfp->fpu_en = 1;
}

/*
 * Free any state associated with floating point context.
 * Fp_free can be called in two cases:
 * 1) from reaper -> thread_free -> lwp_freeregs -> fp_free
 *	fp context belongs to a thread on deathrow
 *	nothing to do,  thread will never be resumed
 *	thread calling ctxfree is reaper
 *
 * 2) from exec -> lwp_freeregs -> fp_free
 *	fp context belongs to the current thread
 *	must disable fpu, thread calling ctxfree is curthread
 */
void
fp_free(fp)
	struct v9_fpu *fp;
{
	register int s;
	unsigned fprs = 0;
	extern void _fp_write_fprs(unsigned);

	if (curthread->t_lwp != NULL && lwptofpu(curthread->t_lwp) == fp) {
		fp->fpu_en = 0;
		fp->fpu_fprs = fprs;
		s = splhigh();
		_fp_write_fprs(fprs);
		curthread->t_cpu->cpu_fpowner = 0;
		(void) splx(s);
	}
}


#ifndef CALL2_WORKS
int	ill_fpcalls;
#endif

void
fp_disabled(rp)
	struct regs *rp;
{
	register klwp_id_t lwp;
	register struct v9_fpu *fp;
	struct cpu *cpup = curthread->t_cpu;
	unsigned fprs = (FPRS_FEF|FPRS_DL|FPRS_DU);
	int ftt;
	extern int fpu_exists;
	extern void _fp_write_fprs(unsigned);


#ifndef CALL2_WORKS
	/*
	 * This code is here because sometimes the call instruction
	 * generates an fp_disabled trap when the call offset is large.
	 */
	int instr;
	extern void trap();

	if (USERMODE(rp->r_tstate)) {
		instr = fuword((int *)rp->r_pc);
	} else {
		instr = *(int *)(rp->r_pc);
	}

	if ((instr & 0xc0000000) == 0x40000000) {
		ill_fpcalls++;
		trap(rp, T_UNIMP_INSTR, 0, 0);
		return;
	}
#endif

	lwp = ttolwp(curthread);
	ASSERT(lwp != NULL);
	fp = lwptofpu(lwp);

	if (fpu_exists) {
		kpreempt_disable();
		if (fp->fpu_en) {
			if (fpdispr) {
				printf("fpu disabled, but already enabled\n");
			}
			if ((fp->fpu_fprs & FPRS_FEF) != FPRS_FEF) {
				fp->fpu_fprs = fprs;
				if (fpdispr)
				printf("fpu disabled, saved fprs disabled\n");
			}
			_fp_write_fprs(fprs);
			cpup->cpu_fpowner = lwp;
			fp_restore(fp);
		} else {
			fp->fpu_en = 1;
			fp->fpu_fsr = 0;
			fp->fpu_fprs = fprs;
			_fp_write_fprs(fprs);
			CPU->cpu_fpowner = lwp;
			fp_enable(fp);
		}
		kpreempt_enable();
	} else {
		fp_simd_type fpsd;
		register int i;

		flush_user_windows_to_stack();
		if (!fp->fpu_en) {
			fp->fpu_en = 1;
			fp->fpu_fsr = 0;
			for (i = 0; i < 32; i++)
				fp->fpu_fr.fpu_regs[i] = (u_int)-1; /* NaN */
			for (i = 16; i < 32; i++)		/* NaN */
				fp->fpu_fr.fpu_dregs[i] = (u_longlong_t)-1;
		}
		if (ftt = fp_emulator(&fpsd, (fp_inst_type *)rp->r_pc,
				rp, (struct rwindow *)rp->r_sp,
				(struct v9_fpu *)&fp->fpu_fr.fpu_regs[0]))
			fp_traps(&fpsd, ftt, rp);
	}
}

/*
 * Process the floating point queue in lwp->lwp_pcb.
 *
 * Each entry in the floating point queue is processed in turn.
 * If processing an entry results in an exception fp_traps() is called to
 * handle the exception - this usually results in the generation of a signal
 * to be delivered to the user. There are 2 possible outcomes to this (note
 * that hardware generated signals cannot be held!):
 *
 *   1. If the signal is being ignored we continue to process the rest
 *	of the entries in the queue.
 *
 *   2. If arrangements have been made for return to a user signal handler,
 *	sendsig() will have copied the floating point queue onto the user's
 *	signal stack and zero'ed the queue count in the u_pcb. Note that
 *	this has the side effect of terminating fp_runq's processing loop.
 *	We will re-run the floating point queue on return from the user
 *	signal handler if necessary as part of normal setcontext processing.
 */
void
fp_runq(rp)
	register struct regs *rp;	/* ptr to regs for trap */
{
	register v9_fpregset_t *fp = 	lwptofpu(curthread->t_lwp);
	register struct fq *fqp =	fp->fpu_q;
	fp_simd_type			fpsd;

	/*
	 * don't preempt while manipulating the queue
	 */
	kpreempt_disable();

	while (fp->fpu_qcnt) {
		int fptrap;

		fptrap = fpu_simulator((fp_simd_type *)&fpsd,
					(fp_inst_type *)fqp->FQu.fpq.fpq_addr,
					(fsr_type *)&fp->fpu_fsr,
					fqp->FQu.fpq.fpq_instr);
		if (fptrap) {

			/*
			 * Instruction could not be simulated so we will
			 * attempt to deliver a signal.
			 * We may be called again upon signal exit (setcontext)
			 * and can continue to process the queue then.
			 */
			if (fqp != fp->fpu_q) {
				register int i;
				register struct fq *fqdp;

				/*
				 * We need to normalize the floating queue so
				 * the excepting instruction is at the head,
				 * so that the queue may be copied onto the
				 * user signal stack by sendsig().
				 */
				fqdp = fp->fpu_q;
				for (i = fp->fpu_qcnt; i; i--) {
					*fqdp++ = *fqp++;
				}
				fqp = fp->fpu_q;
			}
			fp->fpu_q_entrysize = sizeof (struct fpq);

			/*
			 * fpu_simulator uses the fp registers directly but it
			 * uses the software copy of the fsr. We need to write
			 * that back to fpu so that fpu's state is current for
			 * ucontext.
			 */
			if (fpu_exists)
				_fp_write_pfsr(&fp->fpu_fsr);

			/* post signal */
			fp_traps(&fpsd, fptrap, rp);

			/*
			 * Break from loop to allow signal to be sent.
			 * If there are other instructions in the fp queue
			 * they will be processed when/if the user retuns
			 * from the signal handler with a non-empty queue.
			 */
			break;
		}
		fp->fpu_qcnt--;
		fqp++;
	}

	/*
	 * fpu_simulator uses the fp registers directly, so we have
	 * to update the pcb copies to keep current, but it uses the
	 * software copy of the fsr, so we write that back to fpu
	 */
	if (fpu_exists) {
		register int i;

		for (i = 0; i < 32; i++)
			_fp_read_pfreg(&fp->fpu_fr.fpu_regs[i], i);
		for (i = 16; i < 32; i++)
			_fp_read_pdreg(&fp->fpu_fr.fpu_dregs[i], i);
		_fp_write_pfsr(&fp->fpu_fsr);
	}

	kpreempt_enable();
}

/*
 * Get the precise trapped V9 floating point instruction.
 * Fake up a queue to process. If getting the instruction results
 * in an exception fp_traps() is called to handle the exception - this
 * usually results in the generation of a signal to be delivered to the user.
 */
int	fpu_exceptions = 0;
int	fpu_exp_traps = 0;

void
fp_precise(rp)
	register struct regs *rp;	/* ptr to regs for trap */
{
	fp_simd_type	fpsd;
	int		inst_ftt;
	union {
		int		i;
		fp_inst_type	inst;
	}		kluge;
	klwp_id_t lwp = ttolwp(curthread);

	/*
	 * Get the instruction to be emulated from the pc.
	 * Set lwp_state to LWP_SYS for the purposes of clock accounting.
	 * Note that the kernel is NOT prepared to handle a kernel fp
	 * exception if it can't pass successfully through the fp simulator.
	 */
	fpu_exceptions++;
	if ((USERMODE(rp->r_tstate))) {
		inst_ftt = _fp_read_word((caddr_t)rp->r_pc, &(kluge.i), &fpsd);
		lwp->lwp_state = LWP_SYS;
	} else {
		kluge.i = *(u_int *)rp->r_pc;
		inst_ftt = ftt_none;
	}

	if (inst_ftt != ftt_none) {
		/*
		 * Save the bad address and post the signal.
		 * It can only be an ftt_alignment or ftt_fault trap.
		 * XXX - How can this work w/mainsail and do_unaligned?
		 */
		fpu_exp_traps++;
		fpsd.fp_trapaddr = (char *)rp->r_pc;
		fp_traps(&fpsd, inst_ftt, rp);
	} else {
		/*
		 * Conjure up a floating point queue and advance the pc/npc
		 * to fake a deferred fp trap. We now run the fp simulator
		 * in fp_precise, while allowing setfpregs to call fp_runq,
		 * because this allows us to do the ugly machinations to
		 * inc/dec the pc depending on the trap type, as per
		 * bugid 1210159. fp_runq is still going to have the
		 * generic "how do I connect the "fp queue to the pc/npc"
		 * problem alluded to in bugid 1192883, which is only a
		 * problem for a restorecontext of a v8 fp queue on a
		 * v9 system, which seems like the .000000001% case (on v9)!
		 */
		v9_fpregset_t *fp = lwptofpu(curthread->t_lwp);
		struct fpq *pfpq = &fp->fpu_q->FQu.fpq;
		fp_simd_type	fpsd;
		int fptrap;

		pfpq->fpq_addr = (unsigned long *)rp->r_pc;
		pfpq->fpq_instr = kluge.i;
		fp->fpu_qcnt = 1;
		fp->fpu_q_entrysize = sizeof (struct fpq);
		rp->r_pc = rp->r_npc;
		rp->r_npc += 4;

		kpreempt_disable();
		fptrap = fpu_simulator((fp_simd_type *)&fpsd,
					(fp_inst_type *)pfpq,
					(fsr_type *)&fp->fpu_fsr, kluge.i);

		/* update the hardware fp fsr state for sake of ucontext */
		if (fpu_exists)
			_fp_write_pfsr(&fp->fpu_fsr);

		if (fptrap) {
			/* back up the pc if the signal needs to be precise */
			if (fptrap != ftt_ieee) {
				rp->r_npc = rp->r_pc;
				rp->r_pc -= 4;
				fp->fpu_qcnt = 0;
			}
			/* post signal */
			fp_traps(&fpsd, fptrap, rp);

			/* decrement queue count for ieee exceptions */
			if (fptrap == ftt_ieee) {
				fp->fpu_qcnt = 0;
			}
		} else {
			fp->fpu_qcnt = 0;
		}
		/* update the software pcb copies of hardware fp registers */
		if (fpu_exists) {
			register int i;

			for (i = 0; i < 32; i++)
				_fp_read_pfreg(&fp->fpu_fr.fpu_regs[i], i);
			for (i = 16; i < 32; i++)
				_fp_read_pdreg(&fp->fpu_fr.fpu_dregs[i], i);
		}
		kpreempt_enable();
	}
	/*
	 * Reset lwp_state to LWP_USER for the purposes of clock accounting.
	 */
	if ((USERMODE(rp->r_tstate)))
		lwp->lwp_state = LWP_USER;
}

/*
 * Handle floating point traps generated by simulation/emulation.
 */
void
fp_traps(pfpsd, ftt, rp)
	fp_simd_type	*pfpsd;		/* Pointer to simulator data */
	register enum ftt_type ftt;	/* trap type */
	register struct regs *rp;	/* ptr to regs fro trap */
{
	extern void fpu_trap();

	/*
	 * If we take a user's exception in kernel mode, we want to trap
	 * with the user's registers.
	 */

	switch (ftt) {
	case ftt_ieee:
		fpu_trap(rp, T_FP_EXCEPTION_IEEE, pfpsd->fp_trapaddr,
		    pfpsd->fp_trapcode, 0);
		break;
	case ftt_fault:
		fpu_trap(rp, T_DATA_EXCEPTION, pfpsd->fp_trapaddr, 0,
			pfpsd->fp_traprw);
		break;
	case ftt_alignment:
		fpu_trap(rp, T_ALIGNMENT, pfpsd->fp_trapaddr, 0, 0);
		break;
	case ftt_unimplemented:
		fpu_trap(rp, T_UNIMP_INSTR, pfpsd->fp_trapaddr, 0, 0);
		break;
	default:
		/*
		 * We don't expect any of the other types here.
		 */
		panic("fp_traps: bad ftt");
	}
}
