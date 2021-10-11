/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#ident	"@(#)syscall.c	1.51	95/04/04 SMI"

#include <sys/param.h>
#include <sys/vmparam.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/signal.h>
#include <sys/stack.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/var.h>
#include <sys/inline.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <sys/cpuvar.h>
#include <sys/siginfo.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/sysinfo.h>
#include <sys/procfs.h>
#include <sys/prsystm.h>
#include <sys/fpu/fpusystm.h>
#include <sys/modctl.h>
#include <sys/aio_impl.h>
#include <c2/audit.h>
#include <sys/tnf.h>
#include <sys/tnf_probe.h>
#include <sys/privregs.h>

extern int lwp_cleaningwins();


#ifdef SYSCALLTRACE
int syscalltrace = 0;
static kmutex_t	systrace_lock;		/* syscall tracing lock */
#endif /* SYSCALLTRACE */

static krwlock_t *lock_syscall(uint_t code);
static void	psig_shared(struct regs *rp);

/*
 * Arrange for the real time profiling signal to be dispatched.
 */
void
realsigprof(int sysnum, int error)
{
	register proc_t *p;
	register klwp_t *lwp;

	if (curthread->t_rprof->rp_anystate == 0)
		return;
	p = ttoproc(curthread);
	lwp = ttolwp(curthread);
	mutex_enter(&p->p_lock);
	if (sigismember(&p->p_ignore, SIGPROF) ||
	    sigismember(&curthread->t_hold, SIGPROF)) {
		mutex_exit(&p->p_lock);
		return;
	}
	lwp->lwp_siginfo.si_signo = SIGPROF;
	lwp->lwp_siginfo.si_code = PROF_SIG;
	lwp->lwp_siginfo.si_errno = error;
	hrt2ts(gethrtime(), &lwp->lwp_siginfo.si_tstamp);
	lwp->lwp_siginfo.si_syscall = sysnum;
	lwp->lwp_siginfo.si_nsysarg =
	    (sysnum > 0 && sysnum < NSYSCALL) ? sysent[sysnum].sy_narg : 0;
	lwp->lwp_siginfo.si_fault = lwp->lwp_lastfault;
	lwp->lwp_siginfo.si_faddr = lwp->lwp_lastfaddr;
	lwp->lwp_lastfault = 0;
	lwp->lwp_lastfaddr = NULL;
	sigtoproc(p, curthread, SIGPROF, 0);
	mutex_exit(&p->p_lock);
	ASSERT(lwp->lwp_cursig == 0);
	if (issig(FORREAL))
		psig_shared(lwp->lwp_regs);
	mutex_enter(&p->p_lock);
	lwp->lwp_siginfo.si_signo = 0;
	bzero((caddr_t)curthread->t_rprof, sizeof (*curthread->t_rprof));
	mutex_exit(&p->p_lock);
}

/*
 * Called to restore the lwp's register window just before
 * returning to user level (only if the registers have been
 * fetched or modified through /proc).
 */
void
xregrestore(register klwp_id_t lwp, int shared)
{
	/*
	 * If locals+ins were modified by /proc copy them out.
	 * Also copy to the shared window, if necessary.
	 */
	if (lwp->lwp_pcb.pcb_xregstat == XREGMODIFIED) {
		(void) copyout((caddr_t)&lwp->lwp_pcb.pcb_xregs,
			(caddr_t)lwptoregs(lwp)->r_sp, sizeof (struct rwindow));

#ifndef	__sparcv9		/* v9 doesn't use shared window */
		if (shared) {
			bcopy((caddr_t)&lwp->lwp_pcb.pcb_xregs,
			    (caddr_t)lwptot(lwp)->t_stk,
			    sizeof (struct rwindow));
		}
#endif
	}
	lwp->lwp_pcb.pcb_xregstat = XREGNONE;
}


/*
 * Get the arguments to the current system call.
 *	lwp->lwp_ap normally points to the out regs in the reg structure.
 *	If the user is going to change the out registers and might want to
 *	get the args (for /proc tracing), it must copy the args elsewhere
 *	via save_syscall_args().
 */
uint_t
get_syscall_args(klwp_t *lwp, int *argp, int *nargsp)
{
	kthread_id_t	t = lwptot(lwp);
	uint_t	code = t->t_sysnum;
	int	*ap;
	int	nargs;

	if (code != 0) {
		nargs = sysent[code].sy_narg;
		ASSERT(nargs <= MAXSYSARGS);

		*nargsp = nargs;
		ap = lwp->lwp_ap;
		while (nargs-- > 0)
			*argp++ = *ap++;
	} else {
		*nargsp = 0;
	}
	return (code);
}

/*
 * Save the system call arguments in a safe place.
 *	lwp->lwp_ap normally points to the out regs in the reg structure.
 *	If the user is going to change the out registers, g1, or the stack,
 *	and might want to get the args (for /proc tracing), it must copy
 *	the args elsewhere via save_syscall_args().
 *
 *	This may be called from stop() even when we're not in a system call.
 *	Since there's no easy way to tell, this must be safe (not panic).
 *	If the copyins get data faults, return non-zero.
 */
int
save_syscall_args()
{
	kthread_id_t	t = curthread;
	klwp_t		*lwp = ttolwp(t);
	struct regs	*rp = lwp->lwp_regs;
	u_int		code = t->t_sysnum;
	u_int		nargs;

	if (lwp->lwp_argsaved || code == 0)
		return (0);		/* args already saved or not needed */

	if (code >= NSYSCALL) {
		nargs = 0;		/* illegal syscall */
	} else {
		register struct sysent *callp;

		callp = &sysent[code];
		nargs = callp->sy_narg;
		if (LOADABLE_SYSCALL(callp) && nargs == 0) {
			krwlock_t	*module_lock;

			/*
			 * Find out how many arguments the system
			 * call uses.
			 *
			 * We have the property that loaded syscalls
			 * never change the number of arguments they
			 * use after they've been loaded once.  This
			 * allows us to stop for /proc tracing without
			 * holding the module lock.
			 * /proc is assured that sy_narg is valid.
			 */
			module_lock = lock_syscall(code);
			nargs = callp->sy_narg;
			rw_exit(module_lock);
		}
	}

	/*
	 * Fetch the system call arguments.
	 */
	if (nargs != 0) {

		ASSERT(nargs <= MAXSYSARGS);

		if (rp->r_g1 == 0) {		/* indirect syscall */
			lwp->lwp_arg[0] = rp->r_o1;
			lwp->lwp_arg[1] = rp->r_o2;
			lwp->lwp_arg[2] = rp->r_o3;
			lwp->lwp_arg[3] = rp->r_o4;
			lwp->lwp_arg[4] = rp->r_o5;
			if (nargs > 5 &&
			    copyin((caddr_t)rp->r_sp + MINFRAME,
				    (caddr_t)&lwp->lwp_arg[5],
				    (nargs - 5) * sizeof (int)))
				return (-1);
		} else {
#ifndef	__sparcv9	/* first 6 args already saved in v9 */
			lwp->lwp_arg[0] = rp->r_o0;
			lwp->lwp_arg[1] = rp->r_o1;
			lwp->lwp_arg[2] = rp->r_o2;
			lwp->lwp_arg[3] = rp->r_o3;
			lwp->lwp_arg[4] = rp->r_o4;
			lwp->lwp_arg[5] = rp->r_o5;
#endif
			if (nargs > 6 &&
			    copyin((caddr_t)rp->r_sp + MINFRAME,
				    (caddr_t)&lwp->lwp_arg[6],
				    (nargs - 6) * sizeof (int)))
				return (-1);
		}
	}
	lwp->lwp_ap = lwp->lwp_arg;
	lwp->lwp_argsaved = 1;
	t->t_post_sys = 1;	/* so lwp_ap will be reset */
	return (0);
}

/*
 * nonexistent system call-- signal lwp (may want to handle it)
 * flag error if lwp won't see signal immediately
 * This works for old or new calling sequence.
 */
longlong_t
nosys()
{
	psignal(ttoproc(curthread), SIGSYS);
	return ((longlong_t) set_errno(ENOSYS));
}

/*
 * Perform pre-system-call processing, including stopping for tracing,
 * auditing, microstate-accounting, etc.
 *
 * This routine is called only if the t_pre_sys flag is set.  Any condition
 * requiring pre-syscall handling must set the t_pre_sys flag.  If the
 * condition is persistent, this routine will repost t_pre_sys.
 */
int
pre_syscall(int arg0)
{
	register unsigned int code;
	register kthread_id_t	t = curthread;
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	register struct regs *rp = lwptoregs(lwp);
	register int	repost;

	t->t_pre_sys = repost = 0;	/* clear pre-syscall processing flag */

	ASSERT(t->t_schedflag & TS_DONT_SWAP);

	if (curthread->t_proc_flag & TP_MSACCT) {
		(void) new_mstate(curthread, LMS_SYSTEM);	/* in syscall */
		repost = 1;
	}

#ifdef	__sparcv9
	/*
	 * v9 can't use the lwp_ap points to &rp->r_o0 trick since
	 * r_o0 is a long long and lwp_ap is a int *.  It saves the
	 * first 6 args into lwp_arg on every syscall trap.  lwp_ap
	 * will always be &lwp_arg[0].
	 */
	ASSERT(lwp->lwp_ap == lwp->lwp_arg);
#else
	/*
	 * The syscall arguments in the out registers should be pointed to
	 * by lwp_ap.  If the args need to be copied so that the outs can
	 * be changed without losing the ability to get the args for /proc,
	 * they can be saved by save_syscall_args(), and lwp_ap will be
	 * restored by post_syscall().
	 */
	ASSERT(lwp->lwp_ap == &rp->r_o0);
#endif

	/*
	 * Make sure the thread is holding the latest credentials for the
	 * process.  The credentials in the process right now apply to this
	 * thread for the entire system call.
	 */
	if (t->t_cred != p->p_cred) {
		crfree(t->t_cred);
		t->t_cred = crgetcred();
	}

	/*
	 * Undo special arrangements to single-step the lwp
	 * so that a debugger will see valid register contents.
	 * Also so that the pc is valid for syncfpu().
	 * Also so that a syscall like exec() can be stepped.
	 */
	if (lwp->lwp_pcb.pcb_step != STEP_NONE) {
		(void) prundostep();
		repost = 1;
	}

	/*
	 * Check for indirect system call in case we stop for tracing.
	 * Don't allow multiple indirection.
	 */
	code = t->t_sysnum;
	if (code == 0 && arg0 != 0) {		/* indirect syscall */
		code = arg0;
		t->t_sysnum = arg0;
	}

	if (lwp->lwp_prof.pr_scale) {
		lwp->lwp_scall_start = lwp->lwp_stime;
		t->t_flag |= T_SYS_PROF;
		t->t_post_sys = 1;	/* make sure post_syscall runs */
		repost = 1;
	}

	/*
	 * From the proc(4) manual page:
	 * When entry to a system call is being traced, the traced process
	 * stops after having begun the call to the system but before the
	 * system call arguments have been fetched from the process.
	 * If proc changes the args we must refetch them after starting.
	 */
	if (PTOU(p)->u_systrap) {
		if (prismember(&PTOU(p)->u_entrymask, code)) {
			/*
			 * Recheck stop condition, now that lock is held.
			 */
			mutex_enter(&p->p_lock);
			if (PTOU(p)->u_systrap &&
			    prismember(&PTOU(p)->u_entrymask, code)) {
				stop(t, PR_SYSENTRY, code);
				/*
				 * Must refetch args since they were possibly
				 * modified by /proc.  Indicate that the valid
				 * copy is in the registers.
				 */
				lwp->lwp_argsaved = 0;
#ifndef	__sparcv9
				lwp->lwp_ap = &rp->r_o0;
#endif
			}
			mutex_exit(&p->p_lock);
		}
		repost = 1;
	}

	if (lwp->lwp_sysabort) {
		/*
		 * lwp_sysabort may have been set via /proc while the process
		 * was stopped on PR_SYSENTRY.  If so, abort the system call.
		 * Override any error from the copyin() of the arguments.
		 */
		lwp->lwp_sysabort = 0;
		set_errno(EINTR);	/* sets post-sys processing */
		t->t_pre_sys = 1;	/* repost anyway */
		return (1);		/* don't do system call, return EINTR */
	}

#ifdef C2_AUDIT
	if (audit_active) {	/* begin auditing for this syscall */
		audit_start(T_SYSCALL, code, 0, lwp);
		repost = 1;
	}
#endif /* C2_AUDIT */

#ifndef NPROBE
	/* Kernel probe */
	if (tnf_tracing_active) {
		TNF_PROBE_1(syscall_start, "syscall thread", /* CSTYLED */,
			tnf_sysnum,	sysnum,		t->t_sysnum);
		t->t_post_sys = 1;	/* make sure post_syscall runs */
		repost = 1;
	}
#endif /* NPROBE */

#ifdef SYSCALLTRACE
	if (syscalltrace) {
		register int i;
		int  *ap;
		char *cp;
		char *sysname;
		struct sysent *callp;

		(void) save_syscall_args();
		mutex_enter(&systrace_lock);
		printf("%d: ", p->p_pid);
		if (code >= NSYSCALL)
			printf("0x%x", code);
		else
			sysname = mod_getsysname(code);
			printf("%s[0x%x]", sysname == NULL ? "NULL" :
			    sysname, code);
		callp = &sysent[code];
		cp = "(";
		for (i = 0, ap = lwp->lwp_ap; i < callp->sy_narg; i++, ap++) {
			printf("%s%x", cp, *ap);
			cp = ", ";
		}
		if (i)
			printf(")");
		printf(" %s\n", u.u_comm);
		mutex_exit(&systrace_lock);
	}
#endif /* SYSCALLTRACE */

	/*
	 * If there was a continuing reason for pre-syscall processing,
	 * set the t_pre_sys flag for the next system call.
	 */
	if (repost)
		t->t_pre_sys = 1;
	lwp->lwp_error = 0;	/* for old drivers */
	return (0);
}

/*
 * Post-syscall processing.  Perform abnormal system call completion
 * actions such as /proc tracing, profiling, signals, preemption, etc.
 *
 * This routine is called only if t_post_sys, t_sig_check, or t_astflag is set.
 * Any condition requiring pre-syscall handling must set one of these.
 * If the condition is persistent, this routine will repost t_post_sys.
 */
void
post_syscall(int rval1, int rval2)
{
	kthread_id_t	t = curthread;
	proc_t	*p = curproc;
	klwp_t	*lwp = ttolwp(t);
	register struct regs *rp = lwptoregs(lwp);
	uint_t	error;
	int	code = t->t_sysnum;
	int	repost = 0;
	int	proc_stop = 0;		/* non-zero if stopping for /proc */
	int	sigprof = 0;		/* non-zero if sending SIGPROF */

	t->t_post_sys = 0;

	error = lwp->lwp_errno;

	/*
	 * Code can be zero if this is a new LWP returning after a fork(),
	 * other than the one which matches the one in the parent which called
	 * fork.  In these LWPs, skip most of post-syscall activity.
	 */
	if (code == 0)
		goto sig_check;

#ifdef C2_AUDIT
	if (audit_active) {	/* put out audit record for this syscall */
		rval_t	rval;	/* fix audit_finish() someday */

		rval.r_val1 = rval1;
		rval.r_val2 = rval2;
		audit_finish(T_SYSCALL, code, error, &rval);
		repost = 1;
	}
#endif /* C2_AUDIT */

	/*
	 * If we're going to stop for /proc tracing, set the flag and
	 * save the arguments so that the return values don't smash them.
	 */
	if (PTOU(p)->u_systrap) {
		if (prismember(&PTOU(p)->u_exitmask, code)) {
			proc_stop = 1;
			(void) save_syscall_args();
		}
		repost = 1;
	}

	/*
	 * Similarly check to see if SIGPROF might be sent.
	 */
	if (curthread->t_rprof != NULL &&
	    curthread->t_rprof->rp_anystate != 0) {
		save_syscall_args();
		sigprof = 1;
	}

	if (lwp->lwp_eosys == NORMALRETURN) {
		if (error == 0) {
#ifdef SYSCALLTRACE
			if (syscalltrace) {
				mutex_enter(&systrace_lock);
				printf("%d: r_val1=0x%x, r_val2=0x%x\n",
				    p->p_pid, rval1, rval2);
				mutex_exit(&systrace_lock);
			}
#endif /* SYSCALLTRACE */
#ifdef	__sparcv9
			rp->r_tstate &= ~TSTATE_IC;
#else
			rp->r_psr &= ~PSR_C;	/* reset carry bit */
#endif
			rp->r_o0 = rval1;
			rp->r_o1 = rval2;
		} else {
			int sig;

#ifdef SYSCALLTRACE
			if (syscalltrace) {
				mutex_enter(&systrace_lock);
				printf("%d: error=%d\n", p->p_pid, error);
				mutex_exit(&systrace_lock);
			}
#endif /* SYSCALLTRACE */
			if (error == EINTR &&
			    (sig = lwp->lwp_cursig) != 0 &&
			    sigismember(&PTOU(p)->u_sigrestart, sig) &&
			    PTOU(p)->u_signal[sig - 1] != SIG_DFL &&
			    PTOU(p)->u_signal[sig - 1] != SIG_IGN)
				error = ERESTART;
			rp->r_o0 = error;
#ifdef	__sparcv9
			rp->r_tstate |= TSTATE_IC;
#else
			rp->r_psr |= PSR_C;	/* set carry bit */
#endif
		}
		/*
		 * The default action is to redo the trap instruction.
		 * We increment the pc and npc past it for NORMALRETURN.
		 * JUSTRETURN has set up a new pc and npc already.
		 * RESTARTSYS automatically restarts by leaving pc and npc
		 * alone.
		 */
		rp->r_pc = rp->r_npc;
		rp->r_npc += 4;
	} else {
#ifndef	__sparcv9	/* nested traps handles this easier */
		/*
		 * Check user pc alignment.  This can get messed up either using
		 * /proc, or by a badly-formed executable file.
		 */
		if ((rp->r_pc & 3) != 0) {
			extern int trap();

			trap(T_ALIGNMENT, rp);
		}
#endif
		lwp->lwp_eosys = NORMALRETURN;	/* reset flag for next time */
	}

	/*
	 * From the proc(4) manual page:
	 * When exit from a system call is being traced, the traced process
	 * stops on completion of the system call just prior to checking for
	 * signals and returning to user level.  At this point all return
	 * values have been stored into the traced process's saved registers.
	 */
	if (proc_stop) {
		mutex_enter(&p->p_lock);
		if (PTOU(p)->u_systrap &&
		    prismember(&PTOU(p)->u_exitmask, code))
			stop(t, PR_SYSEXIT, code);
		mutex_exit(&p->p_lock);
	}

	/*
	 * If we are the parent returning from a successful
	 * vfork, wait for the child to exec or exit.
	 * This code must be here and not in the bowels of the system
	 * so that /proc can intercept exit from vfork in a timely way.
	 */
	if (code == SYS_vfork && rp->r_o1 == 0 && error == 0)
		vfwait((pid_t)rval1);

	/*
	 * If profiling was active at the start of this system call, and
	 * is still active, add the number of ticks spent in the call to
	 * the time spent at the current PC in user-land.
	 */
	if ((t->t_flag & T_SYS_PROF) != 0) {
		t->t_flag &= ~T_SYS_PROF;
		if (lwp->lwp_prof.pr_scale) {
			register int ticks;

			ticks = (lwp->lwp_stime - lwp->lwp_scall_start) *
				1000/HZ;
			if (ticks) {
				mutex_enter(&p->p_pflock);
				addupc((void(*)())rp->r_pc, &lwp->lwp_prof,
					ticks);
				mutex_exit(&p->p_pflock);
			}
		}
	}

	t->t_sysnum = 0;	/* no longer in a system call */

sig_check:
	if (t->t_astflag | t->t_sig_check) {
		/*
		 * Turn off the AST flag before checking all the conditions that
		 * may have caused an AST.  This flag is on whenever a signal or
		 * unusual condition should be handled after the next trap or
		 * syscall.
		 */
		astoff(t);
		t->t_sig_check = 0;

		/*
		 * for kaio requests on the special kaio poll queue,
		 * copyout their results to user memory.
		 */
		if (p->p_aio)
			aio_cleanup();

		/*
		 * If this LWP was asked to hold, call holdlwp(), which will
		 * stop.  holdlwps() sets this up and calls pokelwps() which
		 * sets the AST flag.
		 *
		 * Also check TP_EXITLWP, since this is used by fresh new LWPs
		 * through lwp_rtt().  That flag is set if the lwp_create(2)
		 * syscall failed after creating the LWP.
		 */
		if (ISHOLD(p) || (t->t_proc_flag & TP_EXITLWP))
			holdlwp();

		/*
		 * All code that sets signals and makes ISSIG_PENDING
		 * evaluate true must set t_sig_check afterwards.
		 */
		if (ISSIG_PENDING(t, lwp, p)) {
			if (issig(FORREAL))
				psig_shared(rp);
			t->t_sig_check = 1;	/* recheck next time */
		}

		if (sigprof) {
			realsigprof(code, error);
			t->t_sig_check = 1;	/* recheck next time */
		}
	}

	/*
	 * Restore register window if a debugger modified it.
	 * Set up to perform a single-step if a debugger requested it.
	 */
	if (lwp->lwp_pcb.pcb_xregstat != XREGNONE)
		xregrestore(lwp, 1);

	lwp->lwp_errno = 0;		/* clear error for next time */

#ifndef NPROBE
	/* Kernel probe */
	if (tnf_tracing_active) {
		TNF_PROBE_3(syscall_end, "syscall thread", /* CSTYLED */,
			tnf_long,	rval1,		rval1,
			tnf_long,	rval2,		rval2,
			tnf_long,	errno,		(long)error);
		repost = 1;
	}
#endif /* NPROBE */

	/*
	 * Set state to LWP_USER here so preempt won't give us a kernel
	 * priority if it occurs after this point.  Call CL_TRAPRET() to
	 * restore the user-level priority.
	 *
	 * It is important that no locks (other than spinlocks) be entered
	 * after this point before returning to user mode (unless lwp_state
	 * is set back to LWP_SYS).
	 *
	 * Sampled times past this point are charged to the user.
	 */
	lwp->lwp_state = LWP_USER;
	if (t->t_proc_flag & TP_MSACCT) {
		(void) new_mstate(t, LMS_USER);
		repost = 1;
	}
	if (t->t_trapret) {
		t->t_trapret = 0;
		thread_lock(t);
		CL_TRAPRET(t);
		thread_unlock(t);
	}
	if (CPU->cpu_runrun)
		preempt();

	/*
	 * t_post_sys will be set if pcb_step is active.
	 */
	if (lwp->lwp_pcb.pcb_step != STEP_NONE) {
		prdostep();
		repost = 1;
	}

	/*
	 * In case the args were copied to the lwp, reset the
	 * pointer so the next syscall will have the right lwp_ap pointer.
	 * v9 note: the 1st 6 args are always in lwp_arg.
	 */
#ifndef	__sparcv9
	lwp->lwp_ap = &rp->r_o0;
#endif
	lwp->lwp_argsaved = 0;

#ifndef	__sparcv9	/* v9 has hw to maintain clean windows */
	/*
	 * If clean register windows must be maintained by system calls,
	 * the slow exit path must be taken each time.  This saves checking
	 * the CLEAN_WINDOWS flag in the fast path.
	 */
	if (lwp_cleaningwins(lwp))
		t->t_post_sys = 1;
#endif

	/*
	 * If there was a continuing reason for post-syscall processing,
	 * set the t_post_sys flag for the next system call.
	 */
	if (repost)
		t->t_post_sys = 1;
}

/*
 * Call a system call which takes a pointer to the user args struct and
 * a pointer to the return values.  This is a bit slower than the standard
 * C arg-passing method in some cases.
 */
longlong_t
syscall_ap()
{
	uint_t	error;
	struct sysent *callp;
	rval_t	rval;
	klwp_t	*lwp = ttolwp(curthread);
	struct regs *rp = lwptoregs(lwp);

	callp = &sysent[curthread->t_sysnum];

	/*
	 * If the arguments don't fit in registers %o0 - o5, make sure they
	 * have been copied to the lwp_arg array.
	 */
	if (callp->sy_narg > 6 && save_syscall_args())
		return ((longlong_t)set_errno(EFAULT));

	rval.r_val1 = 0;
	rval.r_val2 = rp->r_o1;
	lwp->lwp_error = 0;	/* for old drivers */
	error = (*(callp->sy_call))(lwp->lwp_ap, &rval);
	if (error)
		return ((longlong_t)set_errno(error));
	return (rval.r_vals);
}

/*
 * Load system call module.
 *	Returns with pointer to held read lock for module.
 */
static krwlock_t *
lock_syscall(uint_t code)
{
	krwlock_t	*module_lock;
	int 		loaded = 1;
	struct sysent	*callp;
	extern char	**syscallnames;

	callp = &sysent[code];
	module_lock = callp->sy_lock;
	rw_enter(module_lock, RW_READER);
	while (!LOADED_SYSCALL(callp) && loaded) {
		/*
		 * Give up the lock so that the module can get
		 * a write lock when it installs itself
		 */
		rw_exit(module_lock);
		/* Returns zero on success */
		loaded = (modload("sys", syscallnames[code]) != -1);
		rw_enter(module_lock, RW_READER);
	}
	return (module_lock);
}

/*
 * Loadable syscall support.
 *	If needed, load the module, then reserve it by holding a read
 * 	lock for the duration of the call.
 *	Later, if the syscall is not unloadable, it could patch the vector.
 */
longlong_t
loadable_syscall(int a0, int a1, int a2, int a3, int a4, int a5)
{
	longlong_t	rval;
	struct sysent	*callp;
	krwlock_t	*module_lock;
	int		code;

	code = curthread->t_sysnum;
	callp = &sysent[code];

	/*
	 * Try to autoload the system call if necessary.
	 */
	module_lock = lock_syscall(code);
	THREAD_KPRI_RELEASE();	/* drop priority given by rw_enter */

	/*
	 * we've locked either the loaded syscall or nosys
	 */
	if (callp->sy_flags & SE_ARGC) {
		longlong_t (*sy_call)();

		sy_call = (longlong_t (*)())callp->sy_call;
		rval = (*sy_call)(a0, a1, a2, a3, a4, a5);
	} else
		rval = syscall_ap();

	THREAD_KPRI_REQUEST();	/* regain priority from read lock */
	rw_exit(module_lock);
	return (rval);
}

/*
 * Handle indirect system calls.
 *	This interface should be deprecated.  The library can handle
 *	this more efficiently, but keep this implementation for old binaries.
 */
longlong_t
indir(int code, int a0, int a1, int a2, int a3, int a4)
{
	klwp_t		*lwp = ttolwp(curthread);
	struct sysent	*callp;
	uint_t		nargs;
	int		*ap;

	if (code <= 0 || code >= NSYSCALL)
		return (nosys());

	ASSERT(lwp->lwp_ap != NULL);

	curthread->t_sysnum = code;
	callp = &sysent[code];
	nargs = callp->sy_narg;

	/*
	 * Handle argument setup, unless already done in pre_syscall().
	 */
#ifdef	__sparcv9

	if (save_syscall_args())	/* always move args to LWP array */
		return ((longlong_t)set_errno(EFAULT));
#else

	if (nargs > 5) {
		if (save_syscall_args()) 	/* move args to LWP array */
			return ((longlong_t)set_errno(EFAULT));
	} else if (!lwp->lwp_argsaved) {
		ap = lwp->lwp_ap;		/* args haven't been saved */
		lwp->lwp_ap = ap + 1;		/* advance arg pointer */
		curthread->t_post_sys = 1;	/* so lwp_ap will be reset */
	}
#endif

	return ((*callp->sy_callc)(a0, a1, a2, a3, a4, lwp->lwp_arg[5]));
}

/*
 * set_errno - set an error return from the current system call.
 *	This could be a macro.
 *	This returns the value it is passed, so that the caller can
 *	use tail-recursion-elimination and do return (set_errno(ERRNO));
 */
uint_t
set_errno(uint_t errno)
{
	ASSERT(errno != 0);		/* must not be used to clear errno */

	curthread->t_post_sys = 1;	/* have post_syscall do error return */
	return (ttolwp(curthread)->lwp_errno = errno);
}

/*
 * set_proc_pre_sys - Set pre-syscall processing for entire process.
 */
void
set_proc_pre_sys(proc_t *p)
{
	register kthread_id_t	t;
	register kthread_id_t	first;

	ASSERT(MUTEX_HELD(&p->p_lock));

	t = first = p->p_tlist;
	do {
		t->t_pre_sys = 1;
	} while ((t = t->t_forw) != first);
}

/*
 * set_proc_post_sys - Set post-syscall processing for entire process.
 */
void
set_proc_post_sys(proc_t *p)
{
	register kthread_id_t	t;
	register kthread_id_t	first;

	ASSERT(MUTEX_HELD(&p->p_lock));

	t = first = p->p_tlist;
	do {
		t->t_post_sys = 1;
	} while ((t = t->t_forw) != first);
}

/*
 * set_proc_sys - Set pre- and post-syscall processing for entire process.
 */
void
set_proc_sys(proc_t *p)
{
	register kthread_id_t	t;
	register kthread_id_t	first;

	ASSERT(MUTEX_HELD(&p->p_lock));

	t = first = p->p_tlist;
	do {
		t->t_pre_sys = 1;
		t->t_post_sys = 1;
	} while ((t = t->t_forw) != first);
}

/*
 * set_all_proc_sys - set pre- and post-syscall processing flags for all
 * user processes.
 *
 * This is needed when auditing, tracing, or other facilities which affect
 * all processes are turned on.
 */
void
set_all_proc_sys()
{
	register kthread_id_t	t;
	register kthread_id_t	first;

	mutex_enter(&pidlock);
	t = first = curthread;
	do {
		t->t_pre_sys = 1;
		t->t_post_sys = 1;
	} while ((t = t->t_next) != first);
	mutex_exit(&pidlock);
}

/*
 * set_proc_ast - Set asynchronous service trap (AST) flag for all
 * threads in process.
 */
void
set_proc_ast(proc_t *p)
{
	register kthread_id_t	t;
	register kthread_id_t	first;

	ASSERT(MUTEX_HELD(&p->p_lock));

	t = first = p->p_tlist;
	do {
		aston(t);
	} while ((t = t->t_forw) != first);
}


/*
 * Handle a signal for the current thread.
 *
 * This is a wrapper around psig() that handles the case of a sparc register
 * window which may be shared between the kernel and the user.  The window will
 * be reloaded from the kernel stack, but the frame (locals and ins) should
 * come from the kernel stack.  This routine compensates by copying the frame
 * from the new user stack to the kernel stack.
 *
 * Could do this in sendsig(), but would require a flag to indicate syscall vs.
 * trap.  This can be simplified if traps are changed to use the shared window
 * approach as well.
 */
static void
psig_shared(struct regs *rp)
{
	int	sp = rp->r_sp;		/* stack pointer before signal */

	psig();
#ifndef	__sparcv9			/* v9 doesn't use shared window */
	if (sp != rp->r_sp) {
		kthread_id_t	t = curthread;
		klwp_t 		*lwp;
		/*
		 * Stack changed, load new contents.
		 */
		flush_windows();
		sp = rp->r_sp;
		if (copyin((caddr_t)sp, t->t_stk, sizeof (struct rwindow))) {
			proc_t	*p = curproc;

			/*
			 * Copyin failed.  Force a SIGSEGV to be taken.
			 */
			mutex_enter(&p->p_lock);
			sigdelset(&p->p_ignore, SIGSEGV);
			sigdelset(&t->t_hold, SIGSEGV);
			u.u_signal[SIGSEGV - 1] = SIG_DFL;
			mutex_exit(&p->p_lock);
			lwp = ttolwp(t);
			lwp->lwp_cursig = SIGSEGV;
			ASSERT(lwp->lwp_curinfo == NULL);
			psig();		/* send SIGSEGV */
		}
	}
#endif
}
