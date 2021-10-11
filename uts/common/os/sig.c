/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sig.c	1.90	95/07/19 SMI"	/* from SVr4.0 1.52 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bitmap.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/poll.h>	/* only needed for kludge in sigwaiting_check() */
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/fault.h>
#include <sys/ucontext.h>
#include <sys/procfs.h>
#include <sys/wait.h>
#include <sys/class.h>
#include <sys/mman.h>
#include <sys/procset.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/prsystm.h>
#include <sys/debug.h>
#include <vm/as.h>
#include <sys/bitmap.h>
#include <c2/audit.h>
#include <sys/core.h>

#ifdef KPERF
#include <sys/disp.h> 		/* used when compiled with -DKPER */
#endif

				/* MUST be contiguous */
k_sigset_t nullsmask = {0, 0};

#if	((MAXSIG > 32) && (MAXSIG <= 64))
k_sigset_t fillset = {(u_long)0xffffffff, ((1 << (MAXSIG - 32)) - 1)};
#endif

k_sigset_t cantmask = {(sigmask(SIGKILL)|sigmask(SIGSTOP)), 0};

k_sigset_t cantreset = {(sigmask(SIGILL)|sigmask(SIGTRAP)|sigmask(SIGPWR)), 0};

k_sigset_t ignoredefault = {(sigmask(SIGCONT)|sigmask(SIGCLD)|sigmask(SIGPWR)
			|sigmask(SIGWINCH)|sigmask(SIGURG)|sigmask(SIGWAITING)),
			(sigmask(SIGLWP)|sigmask(SIGCANCEL)|sigmask(SIGFREEZE)
			|sigmask(SIGTHAW))};

k_sigset_t stopdefault = {(sigmask(SIGSTOP)|sigmask(SIGTSTP)
			|sigmask(SIGTTOU)|sigmask(SIGTTIN)), 0};

k_sigset_t coredefault = {(sigmask(SIGQUIT)|sigmask(SIGILL)|sigmask(SIGTRAP)
			|sigmask(SIGIOT)|sigmask(SIGEMT)|sigmask(SIGFPE)
			|sigmask(SIGBUS)|sigmask(SIGSEGV)|sigmask(SIGSYS)
			|sigmask(SIGXCPU)|sigmask(SIGXFSZ)), 0};

k_sigset_t holdvfork = {(sigmask(SIGTTOU)|sigmask(SIGTTIN)|sigmask(SIGTSTP)),
			0};

static int		isjobstop(int);
static sigqueue_t	*sigqalloc(proc_t *);
static void		sigqrel(sigqueue_t *);

/*
 * Internal variables for counting number of user thread stop requests posted.
 * They may not be accurate at some special situation such as that a virtually
 * stopped thread starts to run.
 */
static int num_utstop;
/*
 * Internal variables for broadcasting an event when all thread stop requests
 * are processed.
 */
static kcondvar_t utstop_cv;

extern kmutex_t thread_stop_lock;
void del_one_utstop();

/*
 * Send the specified signal to the specified process.
 */
void
psignal(proc_t *p, int sig)
{
	mutex_enter(&p->p_lock);
	sigtoproc(p, NULL, sig, 0);
	mutex_exit(&p->p_lock);
}

/*
 * Direct the specified signal to the specified lwp.
 */
void
tsignal(kthread_t *t, int sig)
{
	register proc_t *p = ttoproc(t);

	ASSERT(p == curproc);
	mutex_enter(&p->p_lock);
	sigtoproc(p, t, sig, 0);
	mutex_exit(&p->p_lock);
}

/*
 * return 1 when all lwps in process have signal masked otherwise
 * return 0.
 */
int
sigisheld(proc_t *p, int sig)
{
	kthread_id_t t;

	/*
	 * If the process has an "aslwp", all signals are unblocked since this
	 * aslwp will take all signals sent asynchronously to the process.
	 */
	if (p->p_flag & ASLWP)
		return (0);
	if ((t = p->p_tlist) != NULL) {
		do {
			if (!sigismember(&t->t_hold, sig))
				return (0);
		} while ((t = t->t_forw) != p->p_tlist);
	}
	return (1);
}

/*
 * Return true if this thread is going to eat this signal soon.
 */
int
eat_signal(kthread_t *t, int sig)
{
	register int rval = 0;
	ASSERT(THREAD_LOCK_HELD(t));

	/*
	 * Do not do anything if the target thread has the signal blocked.
	 * However, if it is the aslwp that is being sent the signal, ignore
	 * its t_hold mask.
	 */
	if (!sigismember(&t->t_hold, sig) || t->t_procp->p_aslwptp == t) {
		if (t->t_state == TS_SLEEP && (t->t_flag & T_WAKEABLE)) {
			setrun_locked(t);
			rval = 1;
		} else if (t->t_state == TS_STOPPED && sig == SIGKILL) {
			ttoproc(t)->p_stopsig = 0;
			t->t_schedflag |= TS_XSTART | TS_PSTART;
			setrun_locked(t);
		} else if (t != curthread && t->t_state == TS_ONPROC) {
			if ((t != curthread) && (t->t_cpu != CPU))
				poke_cpu(t->t_cpu->cpu_id);
			rval = 1;
		} else if (t->t_state == TS_RUN) {
			rval = 1;
		}
		aston(t);		/* have thread do an issig */
	}

	return (rval);
}

/*
 * Post a signal.
 * If a non-null thread pointer is passed, then post the signal
 * to the thread/lwp, otherwise post the signal to the process.
 */
/* ARGSUSED */
void
sigtoproc(proc_t *p, kthread_t *t, int sig, int fromuser)
{
	ASSERT(MUTEX_HELD(&p->p_lock));

	if (sig <= 0 || sig >= NSIG)
		return;

	/*
	 * Regardless of origin or directedness,
	 * SIGKILL kills all lwps in the process immediately
	 * and jobcontrol signals affect all lwps in the process.
	 * These signals are posted to the process, not to an individual lwp.
	 */
	if (sig == SIGKILL) {
		p->p_flag |= SKILLED;
		t = NULL;
	} else if (sig == SIGCONT) {
		t = p->p_aslwptp;
		sigdelq(p, t, SIGSTOP);
		sigdelq(p, t, SIGTSTP);
		sigdelq(p, t, SIGTTOU);
		sigdelq(p, t, SIGTTIN);
		if (t != NULL) {
			sigdiffset(&t->t_sig, &stopdefault);
			sigdiffset(&p->p_notifsigs, &stopdefault);
			if ((t = p->p_tlist) != NULL) {
				do {
					sigdelq(p, t, SIGSTOP);
					sigdelq(p, t, SIGTSTP);
					sigdelq(p, t, SIGTTOU);
					sigdelq(p, t, SIGTTIN);
					sigdiffset(&t->t_sig, &stopdefault);
				} while ((t = t->t_forw) != p->p_tlist);
			}
		} else
			sigdiffset(&p->p_sig, &stopdefault);
		p->p_stopsig = 0;
		if ((t = p->p_tlist) != NULL) {
			do {
				thread_lock(t);
				if (t->t_state == TS_STOPPED &&
				    t->t_whystop == PR_JOBCONTROL) {
					t->t_schedflag |= TS_XSTART;
					setrun_locked(t);
				}
				thread_unlock(t);
			} while ((t = t->t_forw) != p->p_tlist);
		}
		t = p->p_aslwptp;
	} else if (sigismember(&stopdefault, sig)) {
		t = p->p_aslwptp;
		sigdelq(p, t, SIGCONT);
		if (t != NULL) {
			sigdelset(&t->t_sig, SIGCONT);
			sigdelset(&p->p_notifsigs, SIGCONT);
			if ((t = p->p_tlist) != NULL) {
				do {
					sigdelq(p, t, SIGCONT);
					sigdelset(&t->t_sig, SIGCONT);
				} while ((t = t->t_forw) != p->p_tlist);
			}
			t = p->p_aslwptp;
		} else
			sigdelset(&p->p_sig, SIGCONT);
	} else if (t == NULL) {
		t = p->p_aslwptp;
	}
	/*
	 * By assigning p_aslwptp to t above, we are funneling asynchronously
	 * generated signals to the special, designated lwp, the "aslwp" if one
	 * exists. If it does not exist, and the target process is a process
	 * marked to have such an aslwp, the signal is simply put in p_sig and
	 * when the aslwp is created by the application's runtime, this signal
	 * will be taken out of p_sig and assigned to the aslwp's t_sig mask.
	 */
	if (!tracing(p, sig) && sigismember(&p->p_ignore, sig))
		return;

	if (t != (kthread_t *)NULL) {
		/*
		 * This is a directed signal, wake up the lwp. If this is SIGLWP
		 * being sent to a thread sleeping or about to sleep at a
		 * wakeable priority within an MT process, the thread will not
		 * return EINTR back to user-level - it will temporarily ignore
		 * this signal (see issig_forreal()). The signal will be taken
		 * when the lwp finishes the sleep. This is to prevent user
		 * thread preemption (via SIGLWP) from having the side-effect
		 * of interrupting threads blocked in interruptible system calls
		 */
		sigaddset(&t->t_sig, sig);
		thread_lock(t);
		(void) eat_signal(t, sig);
		thread_unlock(t);
	} else if ((t = p->p_tlist) != NULL) {
		/*
		 * Make sure some lwp that already exists in the process,
		 * fields the signal soon. Wake up an interruptibly sleeping
		 * lwp if necessary. If this is the special process with the
		 * ASLWP p_flag on, we are here because p_aslwptp has not yet
		 * been advertised. In this case, needless work is done in
		 * waking up existing lwps who will not take the signal in
		 * p_sig: however this is a very rare case, and we pay the extra
		 * penalty in favor of speeding up the common case.
		 */
		int su = 0;

		sigaddset(&p->p_sig, sig);
		do {
			thread_lock(t);
			if (eat_signal(t, sig)) {
				thread_unlock(t);
				break;
			}
			if (sig == SIGKILL && SUSPENDED(t))
				su++;
			thread_unlock(t);
		} while ((t = t->t_forw) != p->p_tlist);
		/*
		 * If the process is deadlocked, make somebody run and
		 * die.
		 */
		if (sig == SIGKILL && p->p_stat != SIDL &&
		    p->p_lwprcnt == 0 && p->p_lwpcnt == su) {
			thread_lock(t);
			t->t_schedflag |= TS_CSTART;
			setrun_locked(t);
			thread_unlock(t);
		}
	}
}

static int
isjobstop(int sig)
{
	register proc_t *p = ttoproc(curthread);

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(curthread != p->p_aslwptp);

	if (u.u_signal[sig-1] == SIG_DFL && sigismember(&stopdefault, sig)) {
		/*
		 * If SIGCONT has been posted since we promoted this signal
		 * from pending to current, then don't do a jobcontrol stop.
		 * Also discard SIGCONT, since it would not have been sent
		 * if a debugger had not been holding the process stopped.
		 */
		if (sigismember(&curthread->t_sig, SIGCONT)) {
			sigdelset(&curthread->t_sig, SIGCONT);
			sigdelq(p, curthread, SIGCONT);
		} else if (sigismember(&p->p_sig, SIGCONT)) {
			sigdelset(&p->p_sig, SIGCONT);
			sigdelq(p, NULL, SIGCONT);
		} else if ((sig == SIGSTOP || p->p_detached == 0)) {
			stop(curthread, PR_JOBCONTROL, sig);
			mutex_exit(&p->p_lock);
			mutex_enter(&pidlock);
			p->p_wcode = CLD_CONTINUED;
			p->p_wdata = SIGCONT;
			sigcld(p);
			mutex_exit(&pidlock);
			mutex_enter(&p->p_lock);
		}
		return (1);
	}
	return (0);
}

/*
 * Returns true if the current process has a signal to process, and
 * the signal is not held.  The signal to process is put in p_cursig.
 * This is asked at least once each time a process enters the system
 * (though this can usually be done without actually calling issig by
 * checking the pending signal masks).  A signal does not do anything
 * directly to a process; it sets a flag that asks the process to do
 * something to itself.
 *
 * The "why" argument indicates the allowable side-effects of the call:
 *
 * FORREAL:  Extract the next pending signal from p_sig into p_cursig;
 * stop the process if a stop has been requested or if a traced signal
 * is pending.
 *
 * JUSTLOOKING:  Don't stop the process, just indicate whether or not
 * a signal might be pending (FORREAL is needed to tell for sure).
 *
 * XXX: Changes to the logic in these routines should be propagated
 * to lm_sigispending().  See bug 1201594.
 */

static int issig_forreal(void);
static int issig_justlooking(void);

int
issig(int why)
{
	ASSERT(why == FORREAL || why == JUSTLOOKING);

	return ((why == FORREAL)? issig_forreal() : issig_justlooking());
}


static int
issig_justlooking()
{
	/*
	 * This function answers the question:
	 * "Is there any reason to call issig_forreal()?"
	 *
	 * We have to answer the question w/o grabbing any locks
	 * because we are (most likely) being called after we
	 * put ourself on the sleep queue.
	 */

	register klwp_t *lwp = ttolwp(curthread);
	register proc_t *p = ttoproc(curthread);
	k_sigset_t set;

	if ((lwp->lwp_asleep && ISHOLD(p)) ||
	    (p->p_flag & (EXITLWPS|SKILLED)) ||
	    (lwp->lwp_nostop == 0 &&
	    (p->p_stopsig | (p->p_flag & HOLDLWP2) |
	    (curthread->t_proc_flag & (TP_PRSTOP | TP_CHKPT)))) ||
	    lwp->lwp_cursig)
		return (1);

	set = p->p_sig;
	sigorset(&set, &curthread->t_sig);
	sigdiffset(&set, &curthread->t_hold);
	if (p->p_flag & SVFORK)
		sigdiffset(&set, &holdvfork);
	/*
	 * If in an MT process, and am sleeping, do not want to be woken up
	 * due to SIGLWP, which is sent to this thread only for user-level
	 * thread preemption, which is to be turned off for threads blocked
	 * in interruptible waits, as indicated by lwp_asleep being set.
	 * When this lwp eventually wakes up, it will get SIGLWP.
	 */
	if ((p->p_flag & ASLWP) && lwp->lwp_asleep != 0)
		sigdelset(&set, SIGLWP);
	if (!sigisempty(&set)) {
		register int sig;

		for (sig = 1; sig < NSIG; sig++) {
			if (sigismember(&set, sig) &&
			    (tracing(p, sig) ||
			    !sigismember(&p->p_ignore, sig))) {
				/*
				 * Don't promote a signal that will stop
				 * the process when lwp_nostop is set.
				 */
				if (!lwp->lwp_nostop ||
				    u.u_signal[sig-1] != SIG_DFL ||
				    !sigismember(&stopdefault, sig))
					return (1);
			}
		}
	}

	return (0);
}

static int
issig_forreal()
{
	register int sig = 0;
	register kthread_id_t t = curthread;
	register klwp_id_t lwp = ttolwp(t);
	register proc_t *p = ttoproc(t);
	int toproc = 0;

	ASSERT(t->t_state == TS_ONPROC);

	mutex_enter(&p->p_lock);

	for (;;) {
		if (p->p_flag & (EXITLWPS|SKILLED)) {
			mutex_exit(&p->p_lock);
			return (lwp->lwp_cursig = SIGKILL);
		}

		if (lwp->lwp_asleep && ISHOLD(p)) {
			mutex_exit(&p->p_lock);
			return (lwp->lwp_cursig);
		}

		/*
		 * If the request if PR_CHECKPOINT, ignore the rest of signals
		 * or requests. Honor other stop requests or signals later.
		 * Go back to top of loop here to check if an exit or hold
		 * event has occurred while stopped.
		 */
		if ((t->t_proc_flag & TP_CHKPT) && !lwp->lwp_nostop) {
			stop(t, PR_CHECKPOINT, 0);
			continue;
		}

		/*
		 * Honor HOLDLWP2 before dealing with signals or /proc.
		 * The process is undergoing fork1().
		 * Again, go back to top of loop to check if an exit or
		 * hold event has occurred while stopped.
		 */
		if ((p->p_flag & HOLDLWP2) && !lwp->lwp_nostop) {
			stop(t, PR_SUSPENDED, 0);
			continue;
		}

		/*
		 * Honor requested stop before dealing with the
		 * current signal; a debugger may change it.
		 * Do not want to go back to loop here since this is a special
		 * stop that means: make incremental progress before the next
		 * stop. The danger is that returning to top of loop would most
		 * likely drop the thread right back here to stop soon after it
		 * was continued, violating the incremental progress request.
		 */
		if ((t->t_proc_flag & TP_PRSTOP) && !lwp->lwp_nostop)
			stop(t, PR_REQUESTED, 0);

		/*
		 * If a debugger wants us to take a signal it will
		 * have left it in lwp->lwp_cursig.  If this thread is the aslwp
		 * then disable posting of a signal to it in this manner by
		 * clearing it here if noticed. If lwp_cursig has been cleared
		 * or if it's being ignored, we continue on looking for another
		 * signal.  Otherwise we return the specified signal, provided
		 * it's not a signal that causes a job control stop.
		 *
		 * When stopped on PR_JOBCONTROL, there is no current
		 * signal; we cancel lwp->lwp_cursig temporarily before
		 * calling isjobstop().  The current signal may be reset
		 * by a debugger while we are stopped in isjobstop().
		 */
		if ((curthread == p->p_aslwptp) && lwp->lwp_cursig != 0) {
			lwp->lwp_cursig = 0;
			if (lwp->lwp_curinfo != NULL) {
				if (lwp->lwp_curinfo->sq_func != NULL)
					(lwp->lwp_curinfo->sq_func)
					    (lwp->lwp_curinfo);
				else
					kmem_free((caddr_t)lwp->lwp_curinfo,
					    sizeof (*lwp->lwp_curinfo));
				lwp->lwp_curinfo = NULL;
			}
		}
		if ((sig = lwp->lwp_cursig) != 0) {
			ASSERT(curthread != p->p_aslwptp);
			lwp->lwp_cursig = 0;
			if (!sigismember(&p->p_ignore, sig) &&
			    !isjobstop(sig)) {
				if (p->p_flag & (EXITLWPS|SKILLED))
					sig = SIGKILL;
				lwp->lwp_cursig = (u_char)sig;
				mutex_exit(&p->p_lock);
				return (sig);
			}
			/*
			 * The signal is being ignored or it caused a
			 * job-control stop.  If another current signal
			 * has not been established, return the current
			 * siginfo, if any, to the memory manager.
			 */
			if (lwp->lwp_cursig == 0 && lwp->lwp_curinfo != NULL) {
			    if (lwp->lwp_curinfo->sq_func != NULL)
				(lwp->lwp_curinfo->sq_func)(lwp->lwp_curinfo);
			    else
				kmem_free((caddr_t)lwp->lwp_curinfo,
					    sizeof (*lwp->lwp_curinfo));
				lwp->lwp_curinfo = NULL;
			}
			/*
			 * Loop around again in case we were stopped
			 * on a job control signal and a /proc stop
			 * request was posted or another current signal
			 * was established while we were stopped.
			 */
			continue;
		}

		if (p->p_stopsig && !lwp->lwp_nostop) {
			/*
			 * Some lwp in the process has already stopped
			 * showing PR_JOBCONTROL.  This is a stop in
			 * sympathy with the other lwp, even if this
			 * lwp is blocking the stopping signal.
			 */
			stop(t, PR_JOBCONTROL, p->p_stopsig);
			continue;
		}
		/*
		 * If this is the aslwp, check again for exit case, and then
		 * return without going through the subsequent code to process
		 * pending signals.
		 */
		if (curthread == p->p_aslwptp) {
			if (p->p_flag & (EXITLWPS|SKILLED)) {
				lwp->lwp_cursig = SIGKILL;
				sig = SIGKILL;
			}
			mutex_exit(&p->p_lock);
			return (sig);
		}
		/*
		 * Loop on the pending signals until we find a
		 * non-held signal that is traced or not ignored.
		 * First check the signals pending for the lwp,
		 * then the signals pending for the process as a whole.
		 */
		for (;;) {
			k_sigset_t tsig;

			/*
			 * If the lwp is in an MT process (ASLWP flag is set),
			 * and it is asleep, it should not be woken up due to
			 * SIGLWP - see comment in issig_justlooking() where a
			 * similar check is carried out. SIGLWP is left intact,
			 * however, in t_sig, so it is eventually received when
			 * the lwp is no longer asleep.
			 */
			tsig = t->t_sig;
			if ((p->p_flag & ASLWP) && lwp->lwp_asleep)
				sigdelset(&tsig, SIGLWP);
			if ((sig = fsig(&tsig, t))) {
				toproc = 0;
				if (tracing(p, sig) ||
				    !sigismember(&p->p_ignore, sig))
					break;
				sigdelset(&t->t_sig, sig);
				sigdelq(p, t, sig);
			} else if (!(p->p_flag & ASLWP) &&
			    (sig = fsig(&p->p_sig, t))) {
				toproc = 1;
				if (tracing(p, sig) ||
				    !sigismember(&p->p_ignore, sig))
					break;
				sigdelset(&p->p_sig, sig);
				sigdelq(p, NULL, sig);
			} else {
				if (p->p_flag & (EXITLWPS|SKILLED)) {
					lwp->lwp_cursig = SIGKILL;
					sig = SIGKILL;
				}
				mutex_exit(&p->p_lock);
				return (sig);
			}
		}

		/*
		 * If we have been informed not to stop (i.e., we are being
		 * called from within a network operation), then don't promote
		 * the signal at this time, just return the signal number.
		 * We will call issig() again later when it is safe.
		 *
		 * fsig() does not return a jobcontrol stopping signal
		 * with a default action of stopping the process if
		 * lwp_nostop is set, so we won't be causing a bogus
		 * EINTR by this action.  (Such a signal is eaten by
		 * isjobstop() when we loop around to do final checks.)
		 */
		if (lwp->lwp_nostop) {
			mutex_exit(&p->p_lock);
			return (sig);
		}

		/*
		 * Promote the signal from pending to current.
		 *
		 * Note that sigdeq() will set lwp->lwp_curinfo to NULL
		 * if no siginfo_t exists for this signal.
		 */
		lwp->lwp_cursig = (u_char)sig;
		aston(t);		/* so post_syscall will see signal */
		ASSERT(lwp->lwp_curinfo == NULL);
		if (toproc) {
			sigdelset(&p->p_sig, sig);
			sigdeq(p, NULL, sig, &lwp->lwp_curinfo);
		} else {
			sigdelset(&t->t_sig, sig);
			sigdeq(p, t, sig, &lwp->lwp_curinfo);
		}

		if (tracing(p, sig))
			stop(t, PR_SIGNALLED, sig);

		/*
		 * Loop around to check for requested stop before
		 * performing the usual current-signal actions.
		 */
	}
}

/*
 * Return true if the process is currently stopped showing PR_JOBCONTROL.
 * This is true only if all of the process's lwp's are so stopped.
 * If this is asked by one of the lwps in the process, exclude that lwp.
 */
int
jobstopped(proc_t *p)
{
	register kthread_t *t;

	ASSERT(MUTEX_HELD(&p->p_lock));

	if ((t = p->p_tlist) == NULL)
		return (0);

	do {
		thread_lock(t);
		/* ignore current, zombie and suspended lwps in the test */
		if (!(t == curthread || t->t_state == TS_ZOMB ||
		    SUSPENDED(t)) &&
		    (t->t_state != TS_STOPPED ||
		    t->t_whystop != PR_JOBCONTROL)) {
			thread_unlock(t);
			return (0);
		}
		thread_unlock(t);
	} while ((t = t->t_forw) != p->p_tlist);

	return (1);
}

/*
 * Put the specified lwp into the stopped state and notify tracers.
 */
void
stop(kthread_t *t, int why, int what)
{
	register proc_t		*p = ttoproc(t);
	register klwp_t		*lwp = ttolwp(t);
	register kthread_t	*tx;
	register int		procstop;
	register int		flags = TS_ALLSTART;

	ASSERT(t == curthread);

	/*
	 * Can't stop a system process.
	 */
	if (p == NULL || lwp == NULL || (p->p_flag & SSYS) || p->p_as == &kas)
		return;

	ASSERT(MUTEX_HELD(&p->p_lock));

	if (why != PR_SUSPENDED && why != PR_CHECKPOINT) {
		/*
		 * Don't stop an lwp with SIGKILL pending.
		 * Don't stop if the process or lwp is exiting.
		 */
		if (lwp->lwp_cursig == SIGKILL ||
		    sigismember(&t->t_sig, SIGKILL) ||
		    sigismember(&p->p_sig, SIGKILL) ||
		    (t->t_proc_flag & TP_LWPEXIT) ||
		    (p->p_flag & (EXITLWPS|SKILLED))) {
			p->p_stopsig = 0;
			return;
		}
	}

	/*
	 * Make sure we don't deadlock on a recursive call to prstop().
	 * prstop() sets the lwp_nostop flag.
	 */
	if (lwp->lwp_nostop)
		return;

	/*
	 * Make sure the lwp is in an orderly state for inspection
	 * by a debugger through /proc or for dumping via core().
	 */
	t->t_proc_flag |= TP_STOPPING;	/* must set before dropping p_lock */
	mutex_exit(&p->p_lock);
	prstop(lwp, why, what);
	mutex_enter(&p->p_lock);
	ASSERT(t->t_state == TS_ONPROC);

	switch (why) {
	case PR_CHECKPOINT:
		/* t_proc_flag is protected by p_lock */
		t->t_proc_flag &= ~TP_CHKPT;
		flags &= ~TS_RESUME;
		break;

	case PR_JOBCONTROL:
		flags &= ~TS_XSTART;
		break;

	case PR_SUSPENDED:
		/*
		 * The situation may have changed since we dropped
		 * and reacquired p->p_lock.  Double-check now
		 * whether we should stop or not.
		 */
		if (!(ISHOLD(p) || (p->p_flag & HOLDLWP2)))
			return;
		flags &= ~TS_CSTART;
		break;

	default:	/* /proc stop */
		flags &= ~TS_PSTART;
		/*
		 * Do synchronous stop unless the async-stop flag is set.
		 */
		if (why != PR_REQUESTED && !(p->p_flag & SPASYNC)) {
			register int notify;

			for (tx = t->t_forw; tx != t; tx = tx->t_forw) {
				notify = 0;
				thread_lock(tx);
				if (ISTOPPED(tx) ||
				    (tx->t_proc_flag & TP_PRSTOP)) {
					thread_unlock(tx);
					continue;
				}
				tx->t_proc_flag |= TP_PRSTOP;
				aston(tx);
				if (tx->t_state == TS_SLEEP &&
				    (tx->t_flag & T_WAKEABLE)) {
					/*
					 * Don't actually wake it up if it's
					 * in one of the lwp_*() syscalls.
					 * Mark it virtually stopped and
					 * notify /proc waiters (below).
					 */
					if (tx->t_wchan0 == 0)
						setrun_locked(tx);
					else {
						tx->t_proc_flag |= TP_PRVSTOP;
						notify = 1;
					}
				}
				/*
				 * force the thread into the kernel
				 * if it is not already there.
				 */
				if (tx->t_state == TS_ONPROC &&
				    tx->t_cpu != CPU)
					poke_cpu(tx->t_cpu->cpu_id);
				thread_unlock(tx);
				if (notify && tx->t_trace)
					prnotify(tx->t_trace);
			}
			/*
			 * We do this just in case one of the threads we asked
			 * to stop is in holdlwps() (called from cfork()) or
			 * lwp_suspend().
			 */
			cv_broadcast(&p->p_holdlwps);
		}
		break;
	}

	if (why == PR_JOBCONTROL || (why == PR_SUSPENDED && p->p_stopsig)) {
		/*
		 * Determine if the whole process is jobstopped.
		 */
		if (jobstopped(p)) {
			if (p->p_stopsig == 0)
				p->p_stopsig = (u_char) what;
			mutex_exit(&p->p_lock);
			mutex_enter(&pidlock);
			p->p_wcode = CLD_STOPPED;
			p->p_wdata = p->p_stopsig;
			sigcld(p);
			/*
			 * Grab p->p_lock before releasing pidlock so the
			 * parent and the child don't have a race condition.
			 */
			mutex_enter(&p->p_lock);
			mutex_exit(&pidlock);
		} else if (why == PR_JOBCONTROL && p->p_stopsig == 0) {
			/*
			 * Set p->p_stopsig and wake up sleeping lwps
			 * so they will stop in sympathy with this lwp.
			 */
			p->p_stopsig = (u_char) what;
			pokelwps(p);
			/*
			 * We do this just in case one of the threads we asked
			 * to stop is in holdlwps() (called from cfork()) or
			 * lwp_suspend().
			 */
			cv_broadcast(&p->p_holdlwps);
		}
	}

	if (why != PR_JOBCONTROL && why != PR_CHECKPOINT) {
		/*
		 * Do process-level notification when all lwps are
		 * either stopped on events of interest to /proc
		 * or are stopped showing PR_SUSPENDED or are zombies.
		 */
		procstop = 1;
		for (tx = t->t_forw; procstop && tx != t; tx = tx->t_forw) {
			if (VSTOPPED(tx))
				continue;
			thread_lock(tx);
			switch (tx->t_state) {
			case TS_ZOMB:
				break;
			case TS_STOPPED:
				/* neither ISTOPPED nor SUSPENDED? */
				if ((tx->t_schedflag & (TS_CSTART | TS_PSTART))
				    == (TS_CSTART | TS_PSTART))
					procstop = 0;
				break;
			default:
				procstop = 0;
				break;
			}
			thread_unlock(tx);
		}
		if (procstop) {
			if (p->p_flag & STRC) {
				/* ptrace() compatibility */
				mutex_exit(&p->p_lock);
				mutex_enter(&pidlock);
				p->p_wcode = CLD_TRAPPED;
				p->p_wdata = (why == PR_SIGNALLED)?
				    what : SIGTRAP;
				cv_broadcast(&p->p_parent->p_cv);
				/*
				 * Grab p->p_lock before releasing pidlock so
				 * parent and child don't have a race condition.
				 */
				mutex_enter(&p->p_lock);
				mutex_exit(&pidlock);
			}
			if (p->p_trace)		/* /proc */
				prnotify(p->p_trace);
		}
		if (why != PR_SUSPENDED) {
			if (t->t_trace)		/* /proc */
				prnotify(t->t_trace);
			t->t_proc_flag &= ~(TP_PRSTOP|TP_PRVSTOP);
			prnostep(lwp);
		}
	}

	if (why == PR_SUSPENDED) {
		ASSERT(ISHOLD(p) || (p->p_flag & HOLDLWP2));
		if (--p->p_lwprcnt == 0 || (t->t_proc_flag & TP_HOLDLWP))
			cv_broadcast(&p->p_holdlwps);
	}

	/*
	 * Need to do this here (rather than after the thread is officially
	 * stopped) because we can't call mutex_enter from a stopped thread.
	 */
	if (why == PR_CHECKPOINT)
		del_one_utstop();

	thread_lock(t);
	t->t_schedflag |= flags;
	t->t_whystop = (short)why;
	t->t_whatstop = (short)what;
	CL_STOP(t, why, what);
	if (t->t_proc_flag & TP_MSACCT)
		(void) new_mstate(t, LMS_STOPPED);
	THREAD_STOP(t);			/* set stop state and drop lock */

	if (why != PR_SUSPENDED && why != PR_CHECKPOINT) {
		/*
		 * We may have gotten a SIGKILL when we
		 * released p->p_lock; make one last check.
		 * Also check for a /proc run-on-last-close.
		 */
		if (sigismember(&t->t_sig, SIGKILL) ||
		    sigismember(&p->p_sig, SIGKILL) ||
		    (t->t_proc_flag & TP_LWPEXIT) ||
		    (p->p_flag & (EXITLWPS|SKILLED))) {
			p->p_stopsig = 0;
			thread_lock(t);
			t->t_schedflag |= TS_XSTART | TS_PSTART;
			setrun_locked(t);
			thread_unlock_nopreempt(t);
		} else if (why != PR_JOBCONTROL &&
		    !(t->t_proc_flag & TP_STOPPING)) {
			/*
			 * This resulted from a /proc run-on-last-close.
			 */
			thread_lock(t);
			t->t_schedflag |= TS_PSTART;
			setrun_locked(t);
			thread_unlock_nopreempt(t);
		}
	}
	t->t_proc_flag &= ~TP_STOPPING;

	mutex_exit(&p->p_lock);

	swtch();
	mutex_enter(&p->p_lock);
	prbarrier(p);	/* barrier against /proc locking */
}

/* Interface for resetting user thread stop count. */
void
utstop_init()
{
	mutex_enter(&thread_stop_lock);
	num_utstop = 0;
	mutex_exit(&thread_stop_lock);
}

/* Interface for registering a user thread stop request. */
void
add_one_utstop()
{
	mutex_enter(&thread_stop_lock);
	num_utstop++;
	mutex_exit(&thread_stop_lock);
}

/* Interface for cancelling a user thread stop request */
void
del_one_utstop()
{
	mutex_enter(&thread_stop_lock);
	num_utstop--;
	if (num_utstop == 0)
		cv_broadcast(&utstop_cv);
	mutex_exit(&thread_stop_lock);
}

/* Interface to wait for all user threads to be stopped */
void
utstop_timedwait(long ticks)
{
	mutex_enter(&thread_stop_lock);
	if (num_utstop > 0)
		(void) cv_timedwait(&utstop_cv, &thread_stop_lock,
		    ticks + lbolt);
	mutex_exit(&thread_stop_lock);
}

/*
 * Perform the action specified by the current signal.
 * The usual sequence is:
 * 	if (issig())
 * 		psig();
 * The signal bit has already been cleared by issig(),
 * the current signal number has been stored in lwp_cursig,
 * and the current siginfo is now referenced by lwp_curinfo.
 */

void
psig()
{
	register kthread_t *t = curthread;
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	register int sig = lwp->lwp_cursig;
	void (*func)();
	int rc, code;

	mutex_enter(&p->p_lock);
	code = CLD_KILLED;

	if (p->p_flag & EXITLWPS) {
		lwp_exit();
#ifdef LOCKNEST	/* Unlocked in lwp_exit */
		mutex_exit(&p->p_lock);
#endif
		return;			/* not reached */
	}

	ASSERT(sig);
	ASSERT(!sigismember(&p->p_ignore, sig) || (curthread == p->p_aslwptp));

	if ((func = u.u_signal[sig-1]) == SIG_DFL)
		mutex_exit(&p->p_lock);
	else {
		k_siginfo_t *sip = NULL;

		/*
		 * save siginfo pointer here, in case the
		 * the signal's reset bit is on
		 *
		 * The presence of a current signal prevents paging
		 * from succeeding over a network.  We copy the current
		 * signal information to the side and cancel the current
		 * signal so that sendsig() will succeed.
		 */
		if (sigismember(&p->p_siginfo, sig)) {
			if (lwp->lwp_curinfo) {
				bcopy((caddr_t)&lwp->lwp_curinfo->sq_info,
				    (caddr_t)&lwp->lwp_siginfo,
				    sizeof (k_siginfo_t));
				sip = &lwp->lwp_siginfo;
			} else if (sig == SIGPROF &&
			    t->t_rprof != NULL &&
			    t->t_rprof->rp_anystate &&
			    lwp->lwp_siginfo.si_signo == SIGPROF)
				sip = &lwp->lwp_siginfo;
		}

		if (t->t_flag & T_TOMASK)
			t->t_flag &= ~T_TOMASK;
		else
			lwp->lwp_sigoldmask = t->t_hold;
		sigorset(&t->t_hold, &u.u_sigmask[sig-1]);
		if (!sigismember(&u.u_signodefer, sig))
			sigaddset(&t->t_hold, sig);
		if (sigismember(&u.u_sigresethand, sig))
			setsigact(sig, SIG_DFL, nullsmask, 0);
		lwp->lwp_cursig = 0;
		if (lwp->lwp_curinfo) {
			if (lwp->lwp_curinfo->sq_func != NULL)
				(lwp->lwp_curinfo->sq_func)(lwp->lwp_curinfo);
			else
				kmem_free((caddr_t)lwp->lwp_curinfo,
					    sizeof (*lwp->lwp_curinfo));
			lwp->lwp_curinfo = NULL;
		}
		mutex_exit(&p->p_lock);
		lwp->lwp_ru.nsignals++;
		rc = sendsig(sig, sip, func);
		if (rc)
			return;
		sig = lwp->lwp_cursig = SIGSEGV;
	}

	if (sigismember(&coredefault, sig)) {
		/* terminate all LWPs but don't discard them */
		exitlwps(p, 1);
		/* if we got a SIGKILL from anywhere, no core dump */
		if (p->p_flag & SKILLED)
			sig = SIGKILL;
		else {
#ifdef C2_AUDIT
			if (audit_active)		/* audit core dump */
				audit_core_start(sig);
#endif
			if (core("core", ttoproc(t), CRED(),
			    u.u_rlimit[RLIMIT_CORE].rlim_cur, sig) == 0)
				code = CLD_DUMPED;
#ifdef C2_AUDIT
			if (audit_active)		/* audit core dump */
				audit_core_finish(code);
#endif
		}
	}

	exit(code, sig);
}

/*
 * Find next unheld signal in ssp for thread t.
 */
int
fsig(k_sigset_t *ssp, kthread_id_t t)
{
	register int i;
	k_sigset_t temp;

	temp = *ssp;
	sigdiffset(&temp, &t->t_hold);
	if (ttoproc(t)->p_flag & SVFORK)
		sigdiffset(&temp, &holdvfork);
	if (sigismember(&temp, SIGKILL))
		return (SIGKILL);
	if (sigismember(&temp, SIGPROF))
		return (SIGPROF);
	/*
	 * Don't promote a signal that will stop
	 * the process when lwp_nostop is set.
	 */
	if (ttolwp(t)->lwp_nostop) {
		register struct user *up = PTOU(ttoproc(t));

		sigdelset(&temp, SIGSTOP);
		if (up->u_signal[SIGTSTP-1] == SIG_DFL)
			sigdelset(&temp, SIGTSTP);
		if (up->u_signal[SIGTTIN-1] == SIG_DFL)
			sigdelset(&temp, SIGTTIN);
		if (up->u_signal[SIGTTOU-1] == SIG_DFL)
			sigdelset(&temp, SIGTTOU);
	}
	for (i = 0; i < sizeof (k_sigset_t)/sizeof (u_long); i++) {
		if (temp.__sigbits[i])
			return ((i * BT_NBIPUL) + lowbit(temp.__sigbits[i]));
	}
	return (0);
}

void
setsigact(int sig, void (*disp)(), k_sigset_t mask, int flags)
{
	register proc_t *pp = ttoproc(curthread);
	kthread_t *t;

	ASSERT(MUTEX_HELD(&pp->p_lock));

	u.u_signal[sig - 1] = disp;

	if (disp != SIG_DFL && disp != SIG_IGN) {
		sigdelset(&pp->p_ignore, sig);
		sigdiffset(&mask, &cantmask);
		u.u_sigmask[sig - 1] = mask;
		if (!sigismember(&cantreset, sig)) {
			if (flags & SA_RESETHAND)
				sigaddset(&u.u_sigresethand, sig);
			else
				sigdelset(&u.u_sigresethand, sig);
		}
		if (flags & SA_NODEFER)
			sigaddset(&u.u_signodefer, sig);
		else
			sigdelset(&u.u_signodefer, sig);
		if (flags & SA_RESTART)
			sigaddset(&u.u_sigrestart, sig);
		else
			sigdelset(&u.u_sigrestart, sig);
		if (flags & SA_ONSTACK)
			sigaddset(&u.u_sigonstack, sig);
		else
			sigdelset(&u.u_sigonstack, sig);
		if (flags & SA_SIGINFO)
			sigaddset(&pp->p_siginfo, sig);
		else if (sigismember(&pp->p_siginfo, sig))
			sigdelset(&pp->p_siginfo, sig);
	} else if (disp == SIG_IGN ||
	    (disp == SIG_DFL && sigismember(&ignoredefault, sig))) {
		sigaddset(&pp->p_ignore, sig);
		if (pp->p_aslwptp != NULL) {
			/*
			 * There exists an aslwp. Delete from notification set.
			 * Note that the signal and sigqueue from the aslwp will
			 * be deleted via the thread list traversal below.
			 */
			sigdelset(&pp->p_notifsigs, sig);
		} else {
			sigdelset(&pp->p_sig, sig);
			sigdelq(pp, NULL, sig);
		}
		t = pp->p_tlist;
		do {
			sigdelset(&t->t_sig, sig);
			sigdelq(pp, t, sig);
		} while ((t = t->t_forw) != pp->p_tlist);
	} else
		sigdelset(&pp->p_ignore, sig);

	switch (sig) {
	case SIGCLD:
		if (flags & SA_NOCLDWAIT) {
			register proc_t *cp, *tp;

			pp->p_flag |= SNOWAIT;
			mutex_exit(&pp->p_lock);
			mutex_enter(&pidlock);
			/* LINTED lint confusion: used before set: tp */
			for (cp = pp->p_child; cp != NULL; cp = tp) {
				tp = cp->p_sibling;
				if (cp->p_stat == SZOMB)
					freeproc(cp);
			}
			mutex_exit(&pidlock);
			mutex_enter(&pp->p_lock);
		} else {
			pp->p_flag &= ~SNOWAIT;
		}
		if (flags & SA_NOCLDSTOP)
			pp->p_flag &= ~SJCTL;
		else
			pp->p_flag |= SJCTL;
		break;

	case SIGWAITING:
		if (flags & SA_WAITSIG) {
			if (!(pp->p_flag & SWAITSIG)) {
				pp->p_flag |= SWAITSIG;
				pp->p_lwpblocked += 1;
			}
		} else if (pp->p_flag & SWAITSIG) {
			pp->p_flag &= ~SWAITSIG;
			pp->p_lwpblocked -= 1;
		}
		break;
	}
}

void
sigcld(proc_t *cp)
{
	register proc_t *pp = cp->p_parent;
	k_siginfo_t info;

	ASSERT(MUTEX_HELD(&pidlock));

	switch (cp->p_wcode) {

		case CLD_EXITED:
		case CLD_DUMPED:
		case CLD_KILLED:
			ASSERT(cp->p_stat == SZOMB);
			/*
			 * The broadcast on p_srwchan_cv is a kludge to
			 * wakeup a possible thread in uadmin(A_SHUTDOWN).
			 */
			cv_broadcast(&cp->p_srwchan_cv);

			/*
			 * Add to newstate list of the parent
			 */
			add_ns(pp, cp);

			cv_broadcast(&pp->p_cv);
			if (!(pp->p_flag & SNOWAIT))
				break;
			freeproc(cp);
			return;

		case CLD_STOPPED:
		case CLD_CONTINUED:
			cv_broadcast(&pp->p_cv);
			if (pp->p_flag & SJCTL)
				break;
			return;

		default:
			return;
	}

	winfo(cp, &info, 0);
	mutex_enter(&pp->p_lock);
	/* XXX: Should be KM_SLEEP but we have to avoid deadlock */
	sigaddq(pp, NULL, &info, KM_NOSLEEP);
	mutex_exit(&pp->p_lock);
}

int
sigsendproc(proc_t *p, sigsend_t *pv)
{
	register struct cred *cr;
	register proc_t *myprocp = curproc;

	ASSERT(MUTEX_HELD(&pidlock));

	if (p->p_pid == 1 && pv->sig && sigismember(&cantmask, pv->sig))
		return (EPERM);

	cr = CRED();

	mutex_enter(&p->p_crlock);
	if (pv->checkperm == 0 || hasprocperm(p->p_cred, cr) ||
	    (pv->sig == SIGCONT &&
				p->p_sessp == ttoproc(curthread)->p_sessp)) {
		mutex_exit(&p->p_crlock);
		pv->perm++;
		if (pv->sig) {
			if (SI_CANQUEUE(pv->sicode)) {
				register sigqueue_t *sqp;
				mutex_enter(&myprocp->p_lock);
				sqp = (sigqueue_t *)sigqalloc(myprocp);
				mutex_exit(&myprocp->p_lock);
				if (sqp == NULL)
					return (EAGAIN);
				sqp->sq_info.si_signo = pv->sig;
				sqp->sq_info.si_code = pv->sicode;
				sqp->sq_info.si_pid =
						ttoproc(curthread)->p_pid;
				sqp->sq_info.si_uid = cr->cr_ruid;
				sqp->sq_info.si_value = pv->value;
				sqp->sq_func = sigqrel;
				sqp->sq_next = NULL;
				mutex_enter(&p->p_lock);
				sigaddqa(p, NULL, sqp);
				mutex_exit(&p->p_lock);
			} else {
				k_siginfo_t info;
				struct_zero((caddr_t)&info, sizeof (info));
				info.si_signo = pv->sig;
				info.si_code = pv->sicode;
				info.si_pid = ttoproc(curthread)->p_pid;
				info.si_uid = cr->cr_ruid;
				mutex_enter(&p->p_lock);
				/*
				 * XXX: Should be KM_SLEEP but
				 * we have to avoid deadlock.
				 */
				sigaddq(p, NULL, &info, KM_NOSLEEP);
				mutex_exit(&p->p_lock);
			}
		}
	} else
		mutex_exit(&p->p_crlock);

	return (0);
}

int
sigsendset(procset_t *psp, sigsend_t *pv)
{
	register int error;

	error = dotoprocs(psp, sigsendproc, (char *)pv);
	if (error == 0 && pv->perm == 0)
		return (EPERM);

	return (error);
}

/*
 * Dequeue a queued siginfo structure.
 * If a non-null thread pointer is passed then dequeue from
 * the thread queue, otherwise dequeue from the process queue.
 * Returns 1 if the dequeue resulted in a queued up occurrence of the
 * signal "sig" being sent to the process or thread. Otherwise it
 * returns zero.
 */
int
sigdeq(proc_t *p, kthread_t *t, int sig, sigqueue_t **qpp)
{
	register sigqueue_t **psqp, *sqp;
	register int rc = 0;

	ASSERT(MUTEX_HELD(&p->p_lock));

	*qpp = NULL;

	if (t != (kthread_t *)NULL)
		psqp = &t->t_sigqueue;
	else
		psqp = &p->p_sigqueue;

	for (;;) {
		if ((sqp = *psqp) == NULL)
			return (rc);
		if (sqp->sq_info.si_signo == sig)
			break;
		else
			psqp = &sqp->sq_next;
	}
	*qpp = sqp;
	*psqp = sqp->sq_next;
	for (sqp = *psqp; sqp; sqp = sqp->sq_next) {
		if (sqp->sq_info.si_signo == sig) {
			if (t != (kthread_t *)NULL) {
				sigaddset(&t->t_sig, sig);
				aston(t);
				rc = 1;
			} else {
				sigaddset(&p->p_sig, sig);
				set_proc_ast(p);
				rc = 1;
			}
			break;
		}
	}
	return (rc);
}

/*
 * Delete queued siginfo structures.
 * If a non-null thread pointer is passed then delete from
 * the thread queue, otherwise delete from the process queue.
 */
void
sigdelq(proc_t *p, kthread_t *t, int sig)
{
	register sigqueue_t **psqp, *sqp;

	/* ASSERT(MUTEX_HELD(&p->p_lock)); */

	if (t != (kthread_t *)NULL)
		psqp = &t->t_sigqueue;
	else
		psqp = &p->p_sigqueue;

	while (*psqp) {
		sqp = *psqp;
		if (sig == 0 || sqp->sq_info.si_signo == sig) {
			*psqp = sqp->sq_next;
			if (sqp->sq_func != NULL)
				(sqp->sq_func)(sqp);
			else
				kmem_free(sqp, sizeof (sigqueue_t));
		} else
			psqp = &sqp->sq_next;
	}
}

/*
 * Insert a siginfo structure into a queue.
 * If a non-null thread pointer is passed then add to the thread queue,
 * otherwise add to the process queue.
 * jobcontrol signals are sent to the process regardless.
 *
 * The function sigaddqins() is called with sigqueue already allocated.
 * It is called from sigaddqa() and sigaddq() below.
 *
 * The value of si_code implicitly indicates whether sigp is to be
 * explicitly queued, or to be queued to depth one.
 */
static void
sigaddqins(proc_t *p, kthread_t *t, sigqueue_t *sigqp)
{
	register sigqueue_t **psqp;
	register int sig = sigqp->sq_info.si_signo;

	ASSERT(sig >= 1 && sig < NSIG);
	if (t == NULL || sig == SIGCONT || sigismember(&stopdefault, sig)) {
		if (p->p_aslwptp != NULL)
			psqp = &p->p_aslwptp->t_sigqueue;
		else
			psqp = &p->p_sigqueue;
	} else
		psqp = &t->t_sigqueue;

	if (SI_CANQUEUE(sigqp->sq_info.si_code) &&
	    sigismember(&p->p_siginfo, sig)) {
		for (; *psqp != NULL; psqp = &(*psqp)->sq_next)
				;
	} else {
		for (; *psqp != NULL; psqp = &(*psqp)->sq_next)
			if ((*psqp)->sq_info.si_signo == sig) {
				if (sigqp->sq_func != NULL)
					(sigqp->sq_func)(sigqp);
				else
					kmem_free(sigqp, sizeof (sigqueue_t));
				return;
			}
	}
	*psqp = sigqp;
	sigqp->sq_next = NULL;
}

/*
 * The function sigaddqa() is called with sigqueue already allocated.
 * If signal is ignored, discard but guarantee KILL and generation semantics.
 * It is called from sigqueue() and other places.
 */
void
sigaddqa(proc_t *p, kthread_t *t, sigqueue_t *sigqp)
{
	register int sig = sigqp->sq_info.si_signo;

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(sig >= 1 && sig < NSIG);

	if (!tracing(p, sig)) {
		if (sigismember(&p->p_ignore, sig)) {
			if (sigqp->sq_func != NULL)
				(sigqp->sq_func)(sigqp);
			else
				kmem_free(sigqp, sizeof (sigqueue_t));
			sigtoproc(p, t, sig,
			    SI_FROMUSER(&sigqp->sq_info));
			return;
		}
	}

	sigaddqins(p, t, sigqp);
	sigtoproc(p, t, sig, SI_FROMUSER(&sigqp->sq_info));
}


/*
 * Allocate the sigqueue_t structure and call sigaddqins().
 */
void
sigaddq(proc_t *p, kthread_t *t, k_siginfo_t *infop, int km_flags)
{
	register sigqueue_t *sqp;
	register int sig = infop->si_signo;

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(sig >= 1 && sig < NSIG);

	if (!tracing(p, sig)) {
		if (sigismember(&p->p_ignore, sig)) {
			sigtoproc(p, t, sig, SI_FROMUSER(infop));
			return;
		}
		/*
		 * If the process isn't requesting siginfo and it isn't
		 * blocking the signal (it *could* change it's mind while
		 * the signal is pending) then don't bother creating one.
		 */
		if (!sigismember(&p->p_siginfo, sig) &&
		    !sigismember(&p->p_user.u_signodefer, sig)) {
			sigtoproc(p, t, sig, SI_FROMUSER(infop));
			return;
		}
	}

	sqp = (sigqueue_t *)kmem_alloc(sizeof (sigqueue_t),
					km_flags);
	if (sqp == NULL) {
		sigtoproc(p, t, sig, SI_FROMUSER(infop));
		return;
	}
	bcopy((caddr_t)infop, (caddr_t)&sqp->sq_info,
					sizeof (k_siginfo_t));
	sqp->sq_func = NULL;
	sqp->sq_next = NULL;
	sigaddqins(p, t, sqp);
	sigtoproc(p, t, sig, SI_FROMUSER(infop));
}

sigqueue_t	*
sigappend(k_sigset_t *toks, sigqueue_t *toqueue,
		k_sigset_t *fromks, sigqueue_t *fromqueue)
{
	register sigqueue_t	**endsq;

	/*
	 * Assume that the caller locked the process that the
	 * signals are being sent to.
	 */

	sigorset(toks, fromks);
	if (fromqueue != NULL) {
		if (toqueue == NULL)
			toqueue = fromqueue;
		else {
			for (endsq = &toqueue->sq_next;
			    *endsq != NULL;
			    endsq = &(*endsq)->sq_next)
				;
			*endsq = fromqueue;
		}
	}
	return (toqueue);
}

sigqueue_t	*
sigprepend(k_sigset_t *toks, sigqueue_t *toqueue,
		k_sigset_t *fromks, sigqueue_t *fromqueue)
{
	register sigqueue_t	*endqueue;

	/*
	 * Assume that the caller locked the process that the
	 * signals are being sent to.
	 */

	sigorset(toks, fromks);
	if (fromqueue != NULL) {
		for (endqueue = fromqueue; endqueue->sq_next;
		    endqueue = endqueue->sq_next)
			;
		endqueue->sq_next = toqueue;
		toqueue = fromqueue;
	}
	return (toqueue);
}

/*
 * Handle stop-on-fault processing for the debugger.  Returns 0
 * if the fault is cleared during the stop, nonzero if it isn't.
 */
int
stop_on_fault(u_int fault, k_siginfo_t *sip)
{
	register proc_t *p = ttoproc(curthread);
	register klwp_t	*lwp = ttolwp(curthread);

	ASSERT(prismember(&p->p_fltmask, fault));

	/*
	 * Record current fault and siginfo structure so debugger can
	 * find it.
	 */
	mutex_enter(&p->p_lock);
	lwp->lwp_curflt = (u_char)fault;
	lwp->lwp_siginfo = *sip;

	stop(curthread, PR_FAULTED, fault);

	fault = lwp->lwp_curflt;
	lwp->lwp_curflt = 0;
	mutex_exit(&p->p_lock);
	return (fault);
}

void
sigorset(k_sigset_t *s1, k_sigset_t *s2)
{
	s1->__sigbits[0] |= s2->__sigbits[0];
	s1->__sigbits[1] |= s2->__sigbits[1];
}

void
sigandset(k_sigset_t *s1, k_sigset_t *s2)
{
	s1->__sigbits[0] &= s2->__sigbits[0];
	s1->__sigbits[1] &= s2->__sigbits[1];
}

void
sigdiffset(k_sigset_t *s1, k_sigset_t *s2)
{
	s1->__sigbits[0] &= ~(s2->__sigbits[0]);
	s1->__sigbits[1] &= ~(s2->__sigbits[1]);
}

void
sigintr(k_sigset_t *smask, int intable)
{
	proc_t *p;
	int owned;
	k_sigset_t lmask;		/* local copy of cantmask */

	/*
	 * Mask out all signals except SIGHUP, SIGINT, SIGQUIT
	 *    and SIGTERM. (Preserving the existing masks).
	 *    This function supports the -intr nfs and ufs mount option.
	 */

	/*
	 * don't do kernel threads
	 */
	if (ttolwp(curthread) == NULL)
		return;

	/*
	 * get access to signal mask
	 */
	p = ttoproc(curthread);
	owned = mutex_owned(&p->p_lock);	/* this is filthy */
	if (!owned)
		mutex_enter(&p->p_lock);

	/*
	 * remember the current mask
	 */
	*smask = curthread->t_hold;

	/*
	 * mask out all signals
	 */
	sigfillset(&curthread->t_hold);

	/*
	 * Unmask the non-maskable signals (e.g., KILL), as long as
	 * they aren't already masked (which could happen at exit).
	 * The first sigdiffset sets lmask to (cantmask & ~curhold).  The
	 * second sets the current hold mask to (~0 & ~lmask), which reduces
	 * to (~cantmask | curhold).
	 */
	lmask = cantmask;
	sigdiffset(&lmask, smask);
	sigdiffset(&curthread->t_hold, &lmask);

	/*
	 * Re-enable HUP, QUIT, and TERM iff they were originally enabled
	 * Re-enable INT iff it's originally enabled and the NFS mount option
	 * nointr is not set.
	 */
	if (!sigismember(smask, SIGHUP))
		sigdelset(&curthread->t_hold, SIGHUP);
	if (!sigismember(smask, SIGINT) && intable)
		sigdelset(&curthread->t_hold, SIGINT);
	if (!sigismember(smask, SIGQUIT))
		sigdelset(&curthread->t_hold, SIGQUIT);
	if (!sigismember(smask, SIGTERM))
		sigdelset(&curthread->t_hold, SIGTERM);

	/*
	 * release access to signal mask
	 */
	if (!owned)
		mutex_exit(&p->p_lock);
}

void
sigunintr(k_sigset_t *smask)
{
	proc_t *p;
	int owned;
	/*
	 * Reset previous mask (See sigintr() above)
	 */
	if (ttolwp(curthread) != NULL) {
		p = ttoproc(curthread);
		owned = mutex_owned(&p->p_lock);	/* this is filthy */
		if (!owned)
			mutex_enter(&p->p_lock);
		curthread->t_hold = *smask;
		aston(curthread);	/* so unmasked signals will be seen */
		if (!owned)
			mutex_exit(&p->p_lock);
	}
}

void
sigreplace(k_sigset_t *newmask, k_sigset_t *oldmask)
{
	proc_t	*p;
	int owned;
	/*
	 * Save current signal mask in oldmask, then
	 * set it to newmask.
	 */
	if (ttolwp(curthread) != NULL) {
		p = ttoproc(curthread);
		owned = mutex_owned(&p->p_lock);	/* this is filthy */
		if (!owned)
			mutex_enter(&p->p_lock);
		if (oldmask != NULL)
			*oldmask = curthread->t_hold;
		curthread->t_hold = *newmask;
		aston(curthread);
		if (!owned)
			mutex_exit(&p->p_lock);
	}
}

/*
 * check if process should be sent a SIGWAITING signal.
 * this happens when the number of LWPs that are indefinitely
 * blocked equals the number of LWPs in the process.
 *
 * If we have to drop the specified mutex, return non-zero.
 * If we don't have to drop the specified mutex, return zero.
 */
int
sigwaiting_check(kmutex_t *lp)
{
	proc_t *p = ttoproc(curthread);
	int dropped = 0;
	int owned;

	ASSERT(lp == NULL || MUTEX_HELD(lp));

	owned = mutex_owned(&p->p_lock);	/* this is filthy */
	if (!owned) {
		/*
		 * The tryenter is necessary to avoid a possible
		 * lock ordering violation leading to deadlock.
		 * If the tryenter fails, we have to drop the specified
		 * mutex in order to do a blocking lock on p_lock.
		 *
		 * The test should really be for lp == NULL, but we
		 * only test for the one known lock ordering problem,
		 * that of poll() calling cv_wait_sig() with the lock
		 * curthread->t_pollstate.ps_lock held.  This is
		 * to guard against triggering existing brokenness
		 * by returning early from cv_wait_sig().  (We are
		 * paranoid because it is late on the SunOS5.4 release.)
		 */
		pollstate_t *ps = curthread->t_pollstate;

		if (ps == NULL || lp != &ps->ps_lock) {
			mutex_enter(&p->p_lock);
		} else if (mutex_tryenter(&p->p_lock) == 0) {
			dropped = 1;
			mutex_exit(lp);
			mutex_enter(&p->p_lock);
		}
	}
	ASSERT(p->p_lwpblocked >= -1);
	if (++p->p_lwpblocked == p->p_lwpcnt) {
		if (!sigismember(&p->p_ignore, SIGWAITING) &&
		    !sigisheld(p, SIGWAITING))
			sigtoproc(p, NULL, SIGWAITING, 0);
	}
	/*
	 * If we dropped the lock we will not really sleep in the caller.
	 * Reset p_lwpblocked back to its original value so the caller
	 * will not have to call sigwaiting_decr().
	 */
	if (dropped)
		p->p_lwpblocked--;
	if (!owned)
		mutex_exit(&p->p_lock);
	if (dropped)
		mutex_enter(lp);

	return (dropped);
}

/*
 * decrement p_lwpblocked for the current process.
 */
void
sigwaiting_decr(void)
{
	proc_t *p = curproc;

	if (!mutex_owned(&p->p_lock)) {
		mutex_enter(&p->p_lock);
		p->p_lwpblocked--;
		ASSERT(p->p_lwpblocked >= -1);
		mutex_exit(&p->p_lock);
	} else {
		p->p_lwpblocked--;
		ASSERT(p->p_lwpblocked >= -1);
	}
}

/*
 * allocate a sigqueue structure from the per process sigqueue pool.
 * The entire pool (with _SIGQUEUE_MAX entries) is pre-allocated at
 * the first sigqueue call.
 */
static sigqueue_t *
sigqalloc(proc_t *p)
{
	register int i;
	register sigqbucket_t *sq;
	register sigqhdr_t *sigqhdrp = p->p_sigqhdr;

	ASSERT(MUTEX_HELD(&p->p_lock));

	if (p->p_sigqhdr == NULL) { /* allocate sigqueue table */
		sigqhdrp = p->p_sigqhdr = (sigqhdr_t *)
			kmem_zalloc(_SIGQUEUE_MAX * sizeof (sigqbucket_t)
				+ sizeof (sigqhdr_t), KM_NOSLEEP);
		if (sigqhdrp == NULL)
			return (NULL);
		sigqhdrp->sqb_count = _SIGQUEUE_MAX;
		sigqhdrp->sqb_pexited = 0;
		sigqhdrp->sqb_link = sq = (sigqbucket_t *)
			((int)sigqhdrp + sizeof (sigqhdr_t));
		for (i = _SIGQUEUE_MAX-1; i > 0; i--) {
			sq->sqb_link = sq + 1;
			++sq;
		}
		sq->sqb_link = NULL;
		mutex_init(&sigqhdrp->sqb_lock, "sigqueue structures",
			MUTEX_DEFAULT, DEFAULT_WT);
	}
	if (sigqhdrp->sqb_count > 0) {
		mutex_enter(&sigqhdrp->sqb_lock);
		sigqhdrp->sqb_count--;
		sq = sigqhdrp->sqb_link;
		sigqhdrp->sqb_link = sq->sqb_link;
		sq->sqb_link = (sigqbucket_t *)sigqhdrp;
		mutex_exit(&sigqhdrp->sqb_lock);

		return (sigqueue_t *)(sq);
	} else
		return (NULL);
}

/*
 * Return a sigqueue structure back to the pre-allocated pool.
 */
static void
sigqrel(sigqueue_t *sq)
{
	sigqhdr_t *sqh;
	register sigqbucket_t *sqbp;

	sqbp = (sigqbucket_t *)sq;
	sqh = (sigqhdr_t *)sqbp->sqb_link;
	mutex_enter(&sqh->sqb_lock);
	if (sqh->sqb_pexited && sqh->sqb_count == _SIGQUEUE_MAX-1) {
		mutex_exit(&sqh->sqb_lock);
		mutex_destroy(&sqh->sqb_lock);
		kmem_free(sqh, _SIGQUEUE_MAX * sizeof (sigqbucket_t)
			+ sizeof (sigqhdr_t));
		return;
	}
	struct_zero((caddr_t)&sqbp->sqb_bucket.sq_info,
			sizeof (k_siginfo_t));
	sqh->sqb_count++;
	sqbp->sqb_link = sqh->sqb_link;
	sqh->sqb_link = sqbp;
	mutex_exit(&sqh->sqb_lock);
}

/*
 * Free up the pre-allocated sigqueue pool if possible.
 * Called only by the owning process during exiting.
 */
void
sigqfree(proc_t *p)
{
	ASSERT(MUTEX_HELD(&p->p_lock));

	if (p->p_sigqhdr) {
		mutex_enter(&p->p_sigqhdr->sqb_lock);
		if (p->p_sigqhdr->sqb_count == _SIGQUEUE_MAX) {
			mutex_exit(&p->p_sigqhdr->sqb_lock);
			mutex_destroy(&p->p_sigqhdr->sqb_lock);
			kmem_free(p->p_sigqhdr, sizeof (sigqhdr_t) +
				_SIGQUEUE_MAX * sizeof (sigqbucket_t));
		} else {
			p->p_sigqhdr->sqb_pexited = 1;
			mutex_exit(&p->p_sigqhdr->sqb_lock);
		}
		p->p_sigqhdr = NULL;
	}
}

/*
 * Generate a synchronous signal caused by a hardware
 * condition encountered by an lwp.  Called from trap().
 */
void
trapsig(k_siginfo_t *ip, int restartable)
{
	register proc_t *p = ttoproc(curthread);
	register int sig = ip->si_signo;
	sigqueue_t *sqp =
	    (sigqueue_t *)kmem_zalloc(sizeof (sigqueue_t), KM_SLEEP);

	ASSERT(sig > 0 && sig < NSIG);

	mutex_enter(&p->p_lock);
	/*
	 * Avoid a possible infinite loop if the lwp is holding the
	 * signal generated by a trap of a restartable instruction or
	 * if the signal so generated is being ignored by the process.
	 */
	if (restartable &&
	    (sigismember(&curthread->t_hold, sig) ||
	    p->p_user.u_signal[sig-1] == SIG_IGN)) {
		sigdelset(&curthread->t_hold, sig);
		p->p_user.u_signal[sig-1] = SIG_DFL;
		sigdelset(&p->p_ignore, sig);
	}
	bcopy((caddr_t)ip, (caddr_t)&sqp->sq_info, sizeof (k_siginfo_t));
	sigaddqa(p, curthread, sqp);
	mutex_exit(&p->p_lock);
}
