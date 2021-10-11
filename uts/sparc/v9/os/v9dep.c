/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)v9dep.c	1.28	95/09/27 SMI"
#include <sys/param.h>
#include <sys/types.h>
#include <sys/vmparam.h>
#include <sys/systm.h>
#include <sys/stack.h>
#include <sys/frame.h>
#include <sys/proc.h>
#include <sys/ucontext.h>
#include <sys/cpuvar.h>
#include <sys/asm_linkage.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/bootconf.h>
#include <sys/archsystm.h>
#include <sys/fpu/fpusystm.h>
#include <sys/debug.h>
#include <sys/privregs.h>
#include <sys/machpcb.h>
#include <sys/psr_compat.h>
#include <sys/cmn_err.h>
#include <sys/asi.h>

extern int fpdispr;

/*
 * modify the lower 32bits of a u_longlong_t
 */
#define	SET_LOWER_32(all, lower)	\
	(((u_longlong_t)(all) & 0xffffffff00000000) | (u_long)(lower))

/*
 * indicator to v8tov9regs() of whether upper 32-bits of
 * 64-bit registers should be preserved
 */
#define	SETBITS_LOWER_32	1	/* set only lower 32-bits of regs */
#define	SETBITS_ALL_64		2	/* set all 64-bits of regs */
#define	MEMCPY_FPU_EN		2	/* fprs on and fpu_en == 0 */

static void	v8tov9regs(gregset_t grp, struct regs *rp, int set);
static void	v9tov8regs(struct regs *rp, gregset_t grp, u_int fprs);
static u_int	mkpsr(long long tstate, u_int fprs);

/*
 * Set floating-point registers.
 * NOTE:  'lwp' might not correspond to 'curthread' since this is
 * called from code in /proc to set the registers of another lwp.
 */
void
setfpregs(
	register klwp_id_t lwp,
	register fpregset_t *fp)
{

	register struct machpcb *mpcb;
	register kfpu_t *pfp;
	unsigned fprs = (FPRS_FEF|FPRS_DU|FPRS_DL);
	extern void _fp_write_fprs(unsigned);
	extern unsigned _fp_read_fprs();
	extern void fp_v8_load(kfpu_t *);

	mpcb = lwptompcb(lwp);
	pfp = lwptofpu(lwp);

	/*
	 * This is always true for both "real fp" programs and memcpy fp
	 * programs, because we force fpu_en to MEMCPY_FPU_EN in getfpregs,
	 * for the memcpy case where (fpu_en == 0) && (fpu_fprs & FPRS_FEF).
	 */
	if (fp->fpu_en) {
		if (!(pfp->fpu_en) && (!(pfp->fpu_fprs & FPRS_FEF)) &&
		    fpu_exists) {
			/*
			 * He's not currently using the FPU but wants to in his
			 * new context - arrange for this on return to userland.
			 */
			pfp->fpu_fprs = (V9_FPU_FPRS_TYPE)fprs;
		}
		/*
		 * Get setfpregs to restore fpu_en to zero
		 * for the memcpy case (where pfp->fpu_en == 0 &&
		 * (pfp->fp_fprs & FPRS_FEF) == FPRS_FEF).
		 */
		if (fp->fpu_en == MEMCPY_FPU_EN)
			fp->fpu_en = 0;

		/*
		 * Load up a user's floating point context.
		 */
		if (fp->fpu_qcnt > MAXFPQ) 	/* plug security holes */
			fp->fpu_qcnt = MAXFPQ;

		fp->fpu_q_entrysize = sizeof (struct fq);
		/* copy v8 fp regs (lower half of v9 fp regs) */
		kcopy((caddr_t)fp, (caddr_t)pfp, sizeof (fp->fpu_fr));
		pfp->fpu_fsr = SET_LOWER_32(pfp->fpu_fsr, fp->fpu_fsr);
		pfp->fpu_qcnt = fp->fpu_qcnt;
		pfp->fpu_q_entrysize = fp->fpu_q_entrysize;
		pfp->fpu_en = fp->fpu_en;
		pfp->fpu_q = mpcb->mpcb_fpu_q;
		if (fp->fpu_qcnt)
			kcopy((caddr_t)fp->fpu_q, (caddr_t)pfp->fpu_q,
				fp->fpu_qcnt * fp->fpu_q_entrysize);
		/* FSR ignores these bits on load, so they can not be set */
		pfp->fpu_fsr &= ~(FSR_QNE|FSR_FTT);

		kpreempt_disable();

		/*
		 * If not the current process then resume() will handle it
		 * Don't check cpu_fpowner, for memcpy's sake.
		 */
		if (lwp != ttolwp(curthread)) {
			/* force resume to reload fp regs */
			if (CPU->cpu_fpowner == lwp)
				CPU->cpu_fpowner = NULL;
			kpreempt_enable();
			return;
		}

		/*
		 * Load up FPU with new floating point context.
		 */
		if (fpu_exists) {
			if ((_fp_read_fprs() & FPRS_FEF) != FPRS_FEF) {
				_fp_write_fprs(fprs);
				pfp->fpu_fprs = (V9_FPU_FPRS_TYPE)fprs;
				if (fpdispr)
				    printf("setfpregs with fp disabled!\n");
			}

			fp_v8_load(pfp);
		}

		kpreempt_enable();
	} else {
		if ((pfp->fpu_en) ||	/* threads or normal fp case */
		    (pfp->fpu_fprs & FPRS_FEF)) {	/* memcpy case */
			/*
			 * Currently the lwp has floating point enabled.
			 * Turn off FPRS_FEF in user's fprs, saved and
			 * real copies thereof.
			 */
			pfp->fpu_en = 0;
			if (fpu_exists) {
				fprs = 0;
				if (lwp == ttolwp(curthread))
					_fp_write_fprs(fprs);
				pfp->fpu_fprs = (V9_FPU_FPRS_TYPE)fprs;
			}
		}
	}
}

/*
 * NOTE:  'lwp' might not correspond to 'curthread' since this is
 * called from code in /proc to set the registers of another lwp.
 */
void
run_fpq(
	register klwp_id_t lwp,
	register fpregset_t *fp)
{
	/*
	 * If the context being loaded up includes a floating queue,
	 * we need to simulate those instructions (since we can't reload
	 * the fpu) and pass the process any appropriate signals
	 */

	if (lwp == ttolwp(curthread)) {
		if (fpu_exists) {
			if (fp->fpu_qcnt)
				fp_runq(lwp->lwp_regs);
		}
	}
}

/*
 * Get floating-point registers.
 * NOTE:  'lwp' might not correspond to 'curthread' since this is
 * called from code in /proc to set the registers of another lwp.
 */
void
getfpregs(
	register klwp_id_t lwp,
	register fpregset_t *fp)
{
	register kfpu_t *pfp;
	unsigned fprs = (FPRS_FEF|FPRS_DU|FPRS_DL);
	extern int fpu_exists;
	extern unsigned _fp_read_fprs();
	extern void _fp_write_fprs(unsigned);
	extern void fp_v8_fksave(kfpu_t *);

	pfp = lwptofpu(lwp);
	if (ttolwp(curthread) == lwp)
		pfp->fpu_fprs = _fp_read_fprs();
	kpreempt_disable();
	/*
	 * First check the fpu_en case, for normal fp programs.
	 * Next check the fprs case, for fp use by memcpy.
	 */
	if (((fp->fpu_en = pfp->fpu_en) != 0) ||
	    (pfp->fpu_fprs & FPRS_FEF)) {
		/*
		 * Force setfpregs to restore the fp context in
		 * setfpregs for the memcpy case (where pfp->fpu_en == 0 &&
		 * (pfp->fp_fprs & FPRS_FEF) == FPRS_FEF).
		 */
		if (pfp->fpu_en == 0)
			fp->fpu_en = MEMCPY_FPU_EN;
		/*
		 * If we have an fpu and the current thread owns the fp
		 * context (ie, we are not calling from /proc), flush fp
		 * registers into the pcb. Don't check cpu_fpowner,
		 * for memcpy's sake.
		 */
		if (fpu_exists && ttolwp(curthread) == lwp) {
			if ((pfp->fpu_fprs & FPRS_FEF) != FPRS_FEF) {
				_fp_write_fprs(fprs);
				pfp->fpu_fprs = fprs;
				if (fpdispr)
				    printf("getfpregs with fp disabled!\n");
			}
			fp_v8_fksave(pfp);
		}
		kcopy((caddr_t)pfp, (caddr_t)fp, sizeof (fp->fpu_fr));
		fp->fpu_q = pfp->fpu_q;
		fp->fpu_fsr = (V7_FPU_FSR_TYPE)pfp->fpu_fsr;
		fp->fpu_qcnt = pfp->fpu_qcnt;
		fp->fpu_q_entrysize = pfp->fpu_q_entrysize;
	}
	kpreempt_enable();
}

/*
 * Set general registers.
 * NOTE:  'lwp' might not correspond to 'curthread' since this is
 * called from code in /proc to set the registers of another lwp.
 */
void
setgregs(lwp, rp)
	register klwp_id_t lwp;
	register gregset_t rp;
{
	int current = (lwp == curthread->t_lwp);

	if (current)
		save_syscall_args();	/* copy the args from the regs first */

	/*
	 * pc and npc must be word aligned on sparc.
	 * We silently make it so to avoid a watchdog reset.
	 */
	rp[REG_PC] &= ~03;
	rp[REG_nPC] &= ~03;

	v8tov9regs(rp, lwptoregs(lwp), SETBITS_LOWER_32);

	if (current) {
		/*
		 * This was called from a system call, but we
		 * do not want to return via the shared window;
		 * restoring the CPU context changes everything.
		 */
		lwp->lwp_eosys = JUSTRETURN;
		curthread->t_post_sys = 1;
	}
}

/*
 * Return the general registers.
 * NOTE:  'lwp' might not correspond to 'curthread' since this is
 * called from code in /proc to get the registers of another lwp.
 */
void
getgregs(lwp, rp)
	register klwp_id_t lwp;
	register gregset_t rp;
{
	extern u_int _fp_read_fprs();
	u_int fprs;

	kpreempt_disable();
	if (fpu_exists && ttolwp(curthread) == lwp) {
		fprs = _fp_read_fprs();
	} else {
		kfpu_t *fp = lwptofpu(lwp);
		fprs = fp->fpu_fprs;
	}
	kpreempt_enable();
	v9tov8regs(lwptoregs(lwp), rp, fprs);
}

/*
 * Set register windows.
 */
void
setgwins(klwp_t *lwp, gwindows_t *gwins)
{
	register int i;
	struct machpcb *mpcb;

	mpcb = lwptompcb(lwp);
	mpcb->mpcb_wbcnt = 0;
	for (i = 0; i < gwins->wbcnt; i++) {
		mpcb->mpcb_spbuf[i] = (caddr_t)gwins->spbuf[i];
		bcopy((caddr_t)&gwins->wbuf[i],
			(caddr_t)&mpcb->mpcb_wbuf[i],
			sizeof (struct rwindow));
		mpcb->mpcb_wbcnt++;
	}
}

/*
 * Get register windows.
 * NOTE:  'lwp' might not correspond to 'curthread' since this is
 * called from code in /proc to set the registers of another lwp.
 */
void
getgwins(klwp_t *lwp, gwindows_t *gwp)
{
	struct machpcb *mpcb = lwptompcb(lwp);
	register int wbcnt = mpcb->mpcb_wbcnt;
	register int i;

	ASSERT(wbcnt >= 0 && wbcnt <= SPARC_MAXREGWINDOW);
	gwp->wbcnt = wbcnt;
	for (i = 0; i < wbcnt; i++) {
		gwp->spbuf[i] = (int *)mpcb->mpcb_spbuf[i];
		bcopy((caddr_t)&mpcb->mpcb_wbuf[i],
		    (caddr_t)&(gwp->wbuf[i]), sizeof (struct rwindow));
	}
}

/*
 * For things that depend on register state being on the stack,
 * copy any register windows that get saved into the window buffer
 * (in the pcb) onto the stack.  This normally gets fixed up
 * before returning to a user program.  Callers of this routine
 * require this to happen immediately because a later kernel
 * operation depends on window state (like instruction simulation).
 */
void
flush_user_windows_to_stack(void)
{
	register int j, k;
	register char *sp;
	register struct machpcb *mpcb = lwptompcb(curthread->t_lwp);

	flush_user_windows();
	j = mpcb->mpcb_wbcnt;
	while (j > 0) {
		sp = mpcb->mpcb_spbuf[--j];
		if ((((int)sp & (STACK_ALIGN - 1)) == 0) &&
		    (copyout((caddr_t)&mpcb->mpcb_wbuf[j], sp,
			sizeof (struct rwindow)) == 0)) {

			/*
			 * stack was aligned and copyout succeded;
			 * move other windows down
			 */
			mpcb->mpcb_wbcnt--;
			for (k = j; k < mpcb->mpcb_wbcnt; k++) {
				mpcb->mpcb_spbuf[k] = mpcb->mpcb_spbuf[k+1];
				bcopy((caddr_t)&mpcb->mpcb_wbuf[k+1],
					(caddr_t)&mpcb->mpcb_wbuf[k],
					sizeof (struct rwindow));
			}
		}
	}
}

/*
 * Clear registers on exec(2).
 */
void
setregs(void)
{
	register u_long entry;
	register struct regs *rp;
	klwp_id_t lwp = ttolwp(curthread);
	kfpu_t *fpp = lwptofpu(lwp);

	entry = (u_long)u.u_exdata.ux_entloc;

	/*
	 * Initialize user registers.
	 */
	save_syscall_args();		/* copy args from registers first */
	rp = lwptoregs(lwp);
	rp->r_g1 = rp->r_g2 = rp->r_g3 = rp->r_g4 = rp->r_g5 =
	    rp->r_g6 = rp->r_g7 = rp->r_o0 = rp->r_o1 = rp->r_o2 =
	    rp->r_o3 = rp->r_o4 = rp->r_o5 = rp->r_o7 = 0;
	rp->r_tstate = TSTATE_USER;
	rp->r_pc = entry;
	rp->r_npc = entry + 4;
	curthread->t_post_sys = 1;
	lwp->lwp_eosys = JUSTRETURN;
	lwp->lwp_pcb.pcb_trap0addr = NULL;	/* no trap 0 handler */

	/*
	 * Throw out old user windows, init window buf.
	 */
	trash_user_windows();

	/*
	 * Here we initialize minimal fpu state.
	 * The rest is done at the first floating
	 * point instruction that a process executes
	 * or by the lib_psr memcpy routines.
	 */
	fpp->fpu_en = 0;
	fpp->fpu_fprs = 0;
}

/*
 * Construct the execution environment for the user's signal
 * handler and arrange for control to be given to it on return
 * to userland.  The library code now calls setcontext() to
 * clean up after the signal handler, so sigret() is no longer
 * needed.
 */
int
sendsig(
	int		sig,
	k_siginfo_t	*sip,
	register void	(*hdlr)())
{
	int minstacksz;		/* min stack required to catch signal */
	int newstack = 0;	/* if true, switching to altstack */
	label_t ljb;
	register int *sp;
	register struct regs *rp;
	klwp_t *lwp = ttolwp(curthread);
	int gwin_size = 0;
	int fpq_size = 0;
	int xregs_size = 0;
	struct sigframe {
		struct frame frwin;
		ucontext_t uc;
	};
	siginfo_t *sip_addr;
	register struct sigframe *fp;
	ucontext_t *tuc;
	caddr_t xregs;
	kfpu_t *fpp;
	struct machpcb *mpcb;
	extern unsigned _fp_read_fprs();
	extern void xregs_clrptr(struct ucontext *);
	extern void xregs_setptr(struct ucontext *, caddr_t);
	extern int xregs_getsize(void);
	extern void xregs_get(struct _klwp *, caddr_t);

	/*
	 * Make sure the current last user window has been flushed to
	 * the stack save area before we change the sp.
	 * Restore register window if a debugger modified it.
	 */
	flush_user_windows_to_stack();
	if (lwp->lwp_pcb.pcb_xregstat != XREGNONE)
		xregrestore(lwp, 0);

	rp = lwptoregs(lwp);

	minstacksz = sizeof (struct sigframe);

	if (sip != NULL)
		minstacksz += sizeof (siginfo_t);

	/*
	 * These two fields are pointed to by ABI structures and may
	 * be of arbitrary length. Size them now so we know how big
	 * the signal frame has to be.
	 */
	fpp = lwptofpu(lwp);
	fpp->fpu_fprs = _fp_read_fprs();
	if ((fpp->fpu_en) || (fpp->fpu_fprs & FPRS_FEF)) {
		fpq_size = fpp->fpu_q_entrysize * fpp->fpu_qcnt;
		minstacksz += fpq_size;
	}

	mpcb = lwptompcb(lwp);
	if (mpcb->mpcb_wbcnt != 0) {
		gwin_size = (mpcb->mpcb_wbcnt * sizeof (struct rwindow))
		    + SPARC_MAXREGWINDOW * sizeof (int *) + sizeof (int);
		minstacksz += gwin_size;
	}

	/*
	 * Extra registers, if support by this platform, may be of arbitrary
	 * length. Size them now so we know how big the signal frame has to be.
	 */
	xregs_size = xregs_getsize();
	minstacksz += xregs_size;

	/*
	 * Figure out whether we will be handling this signal on
	 * an alternate stack specified by the user. Then allocate
	 * and validate the stack requirements for the signal handler
	 * context. on_fault will catch any faults.
	 */
	newstack = (sigismember(&u.u_sigonstack, sig) &&
	    !(lwp->lwp_sigaltstack.ss_flags & (SS_ONSTACK|SS_DISABLE)));

	if (newstack != 0) {
		fp = (struct sigframe *)(SA((int)lwp->lwp_sigaltstack.ss_sp) +
		    SA((int)lwp->lwp_sigaltstack.ss_size) - STACK_ALIGN -
			SA(minstacksz));
	} else {
		fp = (struct sigframe *)((caddr_t)rp->r_sp - SA(minstacksz));
		/*
		 * Could call grow here, but stack growth now handled below
		 * in code protected by on_fault().
		 */
	}
	sp = (int *)((int)fp + sizeof (struct sigframe));

	/*
	 * Make sure process hasn't trashed its stack.
	 */
	if (((int)fp & (STACK_ALIGN - 1)) != 0 ||
	    (caddr_t)fp >= (caddr_t)USERLIMIT ||
	    (caddr_t)fp + SA(minstacksz) >= (caddr_t)USERLIMIT) {
#ifdef DEBUG
		printf("sendsig: bad signal stack pid=%d, sig=%d\n",
		    curproc->p_pid, sig);
		printf("sigsp = 0x%x, action = 0x%x, upc = 0x%x\n",
		    fp, hdlr, rp->r_pc);

		if (((int)fp & (STACK_ALIGN - 1)) != 0)
		    printf("bad stack alignment\n");
		else
		    printf("fp above USERLIMIT\n");
#endif
		return (0);
	}

	if (on_fault(&ljb))
		goto badstack;

	tuc = kmem_alloc((size_t)(sizeof (ucontext_t)), KM_SLEEP);
	xregs_clrptr(tuc);
	savecontext(tuc, lwp->lwp_sigoldmask);

	/*
	 * save extra register state if it exists
	 */
	if (xregs_size != 0) {
		xregs_setptr(tuc, (caddr_t)sp);
		xregs = kmem_alloc(xregs_size, KM_SLEEP);
		xregs_get(lwp, xregs);
		copyout_noerr(xregs, (caddr_t)sp, xregs_size);
		kmem_free(xregs, xregs_size);
		sp = (int *)((int) sp + xregs_size);
	}

	copyout_noerr((caddr_t)tuc, (caddr_t)&fp->uc, sizeof (ucontext_t));
	kmem_free(tuc, sizeof (ucontext_t));

	if (sip != NULL) {
		uzero((caddr_t)sp, sizeof (siginfo_t));
		copyout_noerr((caddr_t)sip, (caddr_t)sp, sizeof (*sip));
		sip_addr = (siginfo_t *) sp;
		sp = (int *)((int)sp + sizeof (siginfo_t));

		if (sig == SIGPROF &&
		    curthread->t_rprof != NULL &&
		    curthread->t_rprof->rp_anystate) {
			/*
			 * We stand on our head to deal with
			 * the real time profiling signal.
			 * Fill in the stuff that doesn't fit
			 * in a normal k_siginfo structure.
			 */
			register int i = sip->si_nsysarg;
			while (--i >= 0)
				suword_noerr((int *)&sip_addr->si_sysarg[i],
						(int)lwp->lwp_arg[i]);
			copyout_noerr((caddr_t)curthread->t_rprof->rp_state,
					(caddr_t)sip_addr->si_mstate,
					sizeof (curthread->t_rprof->rp_state));
		}
	} else
		sip_addr = (siginfo_t *) NULL;

	/*
	 * When flush_user_windows() can't save all the windows to
	 * the stack, it puts them in the u-area pcb.
	 */
	if (gwin_size != 0) {
		gwindows_t *gwp;

		gwp = kmem_alloc(gwin_size, KM_SLEEP);
		getgwins(lwp, gwp);
		suword_noerr((int *)&fp->uc.uc_mcontext.gwins, (int)sp);
		copyout_noerr((caddr_t)gwp, (caddr_t)sp, gwin_size);
		kmem_free(gwp, gwin_size);
		sp = (int *)((int)sp + gwin_size);
	} else
		suword_noerr((int *)&fp->uc.uc_mcontext.gwins, (int)NULL);

	if (fpq_size != 0) {
		register struct fq *fqp = (struct fq *) sp;
		suword_noerr((int *)&fp->uc.uc_mcontext.fpregs.fpu_q, (int)fqp);
		copyout_noerr((caddr_t)mpcb->mpcb_fpu_q, (caddr_t)fqp,
			fpp->fpu_qcnt * fpp->fpu_q_entrysize);

		/*
		 * forget the fp queue so that the signal handler can run
		 * without being harrassed--it will do a setcontext that will
		 * re-establish the queue if there still is one
		 *
		 * NOTE: fp_runq() relies on the qcnt field being zeroed here
		 *	to terminate its processing of the queue after signal
		 *	delivery.
		 */
		mpcb->mpcb_fpu->fpu_qcnt = 0;
		sp = (int *)((int) sp + fpq_size);

		/* Also, syscall needs to know about this */
		mpcb->mpcb_flags |= FP_TRAPPED;

	} else {
		suword_noerr((int *)&fp->uc.uc_mcontext.fpregs.fpu_q,
			(int)NULL);
		subyte_noerr((caddr_t)&fp->uc.uc_mcontext.fpregs.fpu_qcnt, 0);
	}


	/*
	 * Since we flushed the user's windows and we are changing his
	 * stack pointer, the window that the user will return to will
	 * be restored from the save area in the frame we are setting up.
	 * We copy in save area for old stack pointer so that debuggers
	 * can do a proper stack backtrace from the signal handler.
	 */
	if (mpcb->mpcb_wbcnt == 0) {
		ucopy((caddr_t)rp->r_sp, (caddr_t)&fp->frwin,
			sizeof (struct rwindow));
	}

	lwp->lwp_oldcontext = &fp->uc;

	if (newstack != 0)
		lwp->lwp_sigaltstack.ss_flags |= SS_ONSTACK;

	no_fault();
	mpcb->mpcb_wbcnt = 0;		/* let user go on */

	/*
	 * Set up user registers for execution of signal handler.
	 */
	rp->r_sp = (int)fp;
	rp->r_pc = (int)hdlr;
	rp->r_npc = (int)hdlr + 4;
	/* make sure %asi is ASI_PNF */
	rp->r_tstate &= ~((long long)0xff << TSTATE_ASI_SHIFT);
	rp->r_tstate |= ((long long)ASI_PNF << TSTATE_ASI_SHIFT);
	rp->r_o0 = sig;
	rp->r_o1 = (int)sip_addr;
	rp->r_o2 = (int)&fp->uc;
	/*
	 * Don't set lwp_eosys here.  sendsig() is called via psig() after
	 * lwp_eosys is handled, so setting it here would affect the next
	 * system call.
	 */
	return (1);

badstack:
	no_fault();
#ifdef DEBUG
	printf("sendsig: bad signal stack pid=%d, sig=%d\n",
	    curproc->p_pid, sig);
	printf("on fault, sigsp = 0x%x, action = 0x%x, upc = 0x%x\n",
	    fp, hdlr, rp->r_pc);
#endif
	return (0);
}

/*
 * load user registers into lwp
 */
void
lwp_load(klwp_t *lwp, ucontext_t *ucp)
{
	setgregs(lwp, ucp->uc_mcontext.gregs);
	lwptoregs(lwp)->r_tstate = TSTATE_USER;
	lwp->lwp_eosys = JUSTRETURN;
}

/*
 * set syscall()'s return values for a lwp.
 */
void
lwp_setrval(klwp_id_t lwp, int v1, int v2)
{
	struct regs *rp = lwptoregs(lwp);

	rp->r_tstate &= ~TSTATE_IC;
	rp->r_o0 = v1;
	rp->r_o1 = v2;
}

u_int
get_profile_pc(void *p)
{
	struct regs *rp = (struct regs *)p;

	if (USERMODE(rp->r_tstate))
		return (0);
	return (rp->r_pc);
}

/*
 * Convert v8 gregset struct to v9 regs.
 */
static void
v8tov9regs(gregset_t grp, struct regs *rp, int set)
{
	unsigned long long icc;

	icc = grp[REG_PSR] & PSR_ICC;
	icc <<= PSR_TSTATE_CC_SHIFT;
	rp->r_tstate &= ~TSTATE_ICC;
	rp->r_tstate |= icc;
	rp->r_pc = grp[REG_PC];
	rp->r_npc = grp[REG_nPC];
	rp->r_y = grp[REG_Y];

	switch (set) {
	case SETBITS_ALL_64:
		rp->r_g1 = grp[REG_G1];
		rp->r_g2 = grp[REG_G2];
		rp->r_g3 = grp[REG_G3];
		rp->r_g4 = grp[REG_G4];
		rp->r_g5 = grp[REG_G5];
		rp->r_g6 = grp[REG_G6];
		rp->r_g7 = grp[REG_G7];

		rp->r_o0 = grp[REG_O0];
		rp->r_o1 = grp[REG_O1];
		rp->r_o2 = grp[REG_O2];
		rp->r_o3 = grp[REG_O3];
		rp->r_o4 = grp[REG_O4];
		rp->r_o5 = grp[REG_O5];
		rp->r_o6 = grp[REG_O6];
		rp->r_o7 = grp[REG_O7];
		break;
	case SETBITS_LOWER_32:
		rp->r_g1 = SET_LOWER_32(rp->r_g1, grp[REG_G1]);
		rp->r_g2 = SET_LOWER_32(rp->r_g2, grp[REG_G2]);
		rp->r_g3 = SET_LOWER_32(rp->r_g3, grp[REG_G3]);
		rp->r_g4 = SET_LOWER_32(rp->r_g4, grp[REG_G4]);
		rp->r_g5 = SET_LOWER_32(rp->r_g5, grp[REG_G5]);
		rp->r_g6 = SET_LOWER_32(rp->r_g6, grp[REG_G6]);
		rp->r_g7 = SET_LOWER_32(rp->r_g7, grp[REG_G7]);

		rp->r_o0 = SET_LOWER_32(rp->r_o0, grp[REG_O0]);
		rp->r_o1 = SET_LOWER_32(rp->r_o1, grp[REG_O1]);
		rp->r_o2 = SET_LOWER_32(rp->r_o2, grp[REG_O2]);
		rp->r_o3 = SET_LOWER_32(rp->r_o3, grp[REG_O3]);
		rp->r_o4 = SET_LOWER_32(rp->r_o4, grp[REG_O4]);
		rp->r_o5 = SET_LOWER_32(rp->r_o5, grp[REG_O5]);
		rp->r_o6 = SET_LOWER_32(rp->r_o6, grp[REG_O6]);
		rp->r_o7 = SET_LOWER_32(rp->r_o7, grp[REG_O7]);
		break;
	default:
		cmn_err(CE_PANIC, "v8tov9regs: invalid bit set %d\n", set);
		/*NOTREACHED*/
	}
}

/*
 * Convert v9 regs struct to v8 gregset.
 */
static void
v9tov8regs(struct regs *rp, gregset_t grp, u_int fprs)
{
	grp[REG_PSR] = mkpsr(rp->r_tstate, fprs);
	grp[REG_PC] = rp->r_pc;
	grp[REG_nPC] = rp->r_npc;
	grp[REG_Y] = rp->r_y;
	grp[REG_G1] = rp->r_g1;
	grp[REG_G2] = rp->r_g2;
	grp[REG_G3] = rp->r_g3;
	grp[REG_G4] = rp->r_g4;
	grp[REG_G5] = rp->r_g5;
	grp[REG_G6] = rp->r_g6;
	grp[REG_G7] = rp->r_g7;
	grp[REG_O0] = rp->r_o0;
	grp[REG_O1] = rp->r_o1;
	grp[REG_O2] = rp->r_o2;
	grp[REG_O3] = rp->r_o3;
	grp[REG_O4] = rp->r_o4;
	grp[REG_O5] = rp->r_o5;
	grp[REG_O6] = rp->r_o6;
	grp[REG_O7] = rp->r_o7;
}

static u_int
mkpsr(long long tstate, u_int fprs)
{
	u_int psr, icc;

	psr = tstate & TSTATE_CWP_MASK;
	if (tstate & TSTATE_PRIV)
		psr |= PSR_PS;
	if (fprs & FPRS_FEF)
		psr |= PSR_EF;
	icc = tstate >> PSR_TSTATE_CC_SHIFT;
	psr |= icc & PSR_ICC;
	psr |= V9_PSR_IMPLVER;
	return (psr);
}
