/*	Copyright (c) 1993 SMI	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident   "@(#)sigaction.c 1.74     95/03/24     SMI"

#ifdef __STDC__
#pragma weak sigaction = _sigaction
#pragma weak sigprocmask = _sigprocmask
#endif

#include "libthread.h"
#include <ucontext.h>
#include <errno.h>
#include <stdio.h>

/*
 * Global variables
 */
lwp_mutex_t _sighandlerlock;
void	(*_tsiguhandler[NSIG])();
void	(*_siglwp_handler)() = SIG_DFL;
struct sigaction __alarm_sigaction = {0, SIG_DFL, 0};
struct sigaction __null_sigaction = {0, SIG_DFL, 0};
sigset_t _bpending;		/* signals pending re-assignment */
mutex_t	_bpendinglock;		/* shared mutex protecting _bpending */
sigset_t _ignoredefault;

#ifdef NO_SWAP_INSTRUCTION
struct sigaction __segv_sigaction = {0, SIG_DFL, 0};
#endif /* NO_SWAP_INSTRUCTION */


/*
 * Static variables
 */
static	sigset_t _sigumask[NSIG];
static	int _siguflags[NSIG];
static	int _sigalarm_flags;
/*
 * sigacthandler should always be static - otherwise, it might require run-time
 * symbol resolution by the run-time linker. If this were to happen for SIGLWP,
 * the linker would be entered without the T_INSIGLWP flag set, causing a
 * possible deadlock in the presence of thr_suspend() usage.
 */
static void sigacthandler(int sig, siginfo_t *sip, ucontext_t *uap);

/*
 * functions used within this file only but not declared STATIC
 * bcause dbx does not recognize them
 */
extern void setcontext_masksigs(ucontext_t *uc);
/*
 * There are 4 different ways signals can be delivered to a thread:
 * a) Process Asynchronous: sent to the process, and bounced by aslwp
 *		- sent by user (via kill() or sigqueue()): any signal
 *		- sent by kernel: SIGCLD, SIGPOLL and
 *				  recently added SIGPIPE, SIGSYS, SIGXFSZ
 *				  which were being sent to the LWP before.
 *				  Also, SIGALRM, due to alarm(2) is sent
 *				  to process. It is retained across exec().
 * b) Thread Asynchronous: sent to a thread via thr_kill(): any signal
 * c) Synchronous: delivered synchronously with faulting instruction e.g.
 *	SIGILL, SIGSEGV, SIGBUS, SIGTRAP, SIGFPE, SIGEMT
 * d) Pseudo-synchronous: delivered to an LWP:
 *		i.e. SIGALRM (due to setitimer(ITIMER_REAL, ...)), SIGPROF,
 *		     SIGVTALRM
 *	SIGALRM: due to setitimer(ITIMER_REAL, ...)
 *		 The kernel continues to send it to the thread that
 *		 caused the alarm, mainly because the callout thread uses
 *		 setitimer() and a SIGALRM due to it, if sent to the
 *		 process could end up on another thread. Also, re-doing the
 *		 callout thread to not use setitimer() but something like
 *		 nanosleep() would not match its current design of waiting in
 *		 lwp_sema_p() for both an lwp_sema_v() or a SIGALRM. nanosleep()
 *		 would be synchronous whereas it needs an asynchronous signal
 *		 to get it out of the lwp_sema_p().
 *		 This implies that only bound threads can use
 *		 setitimer(ITIMER_EAL, ...).
 *	SIGVTALRM: caused due to setitimer(ITIMER_VIRTUAL, ...) - works only
 *		   for bound threads since it is sent to the LWP.
 *	SIGPROF: works completely only for bound threads - the man page clearly
 *		 points out the per-LWP nature of this signal. For the MT
 *		 collector, it now works for unbound threads in a limited
 *		 way. See Comment at the start of sigacthandler().
 */

/*
 * SIGPROF, SIGCLD, SIGPOLL are received with SI_FROMKERNEL signal codes but
 * it is OK if they are masked when generated - unlike signals such as SIGSEGV.
 * NOTE: the kernel directs PROF to an LWP, whereas CLD and POLL are directed
 * to the process.
 */
sigset_t __ksigsok = {sigmask(SIGPROF)|sigmask(SIGCLD)|sigmask(SIGPOLL)};

/*
 * SIGPROF is sent to the LWP that caused this signal to occur.
 */
sigset_t __pseudosynch = {sigmask(SIGPROF)};

static char *msg = "got signal while tempbound\n";
/*
 * If sigacthandler() is invoked in a libthread critical section (t_nosig > 0),
 * the restored lwpmask remains unchanged on return from sigacthandler().
 * This is so that in _swtch(), the value of the lwpmask does not change
 * when assigning the lwp mirror to the new thread being switched to.
 * Note that the lwpmask cannot be restored to _pmask, the virtual process
 * signal mask which could be temporarily out of sync.
 */
static void
sigacthandler(int sig, siginfo_t *sip, ucontext_t *uap)
{
	struct thread *t = curthread;
	char bufa [64];
	char *buf = bufa;
	void (*hdlr)();
	void (*handler)();
	int impossible2 = 0;
	sigset_t omask;
	sigset_t _lsigumask;
	extern void __sig_resethand();

	/*
	 * DO NOT PUT ANY CODE HERE, WHICH MIGHT REQUIRE RUN-TIME LINKER
	 * RESOLUTION, BEFORE SETTING THE T_INSIGLWP FLAG BELOW.
	 * Otherwise, you could see deadlocks due to thr_suspend(3t).
	 * First thing here should be to set the T_INSIGLWP flag, if necessary.
	 */
	if (sig == SIGLWP) {
		t->t_flag |= T_INSIGLWP;
	}
#ifdef NO_SWAP_INSTRUCTION
	if (sig == SIGSEGV)
		__advance_pc_if_munlock_segv(sig, sip, uap);
#endif
	if (sig == SIGPROF && (IDLETHREAD(t) || t->t_flag & T_INTERNAL))
		setcontext(uap);
	/*
	 * An unbound thread can now call into setitimer() for ITIMER_PROF
	 * or ITIMER_REALPROF with the following restrictions:
	 *	- it can be called only before any threads are created.
	 *	- a thread's mask will be silently ignored for this signal
	 *	  although the assertions below will fail if SIGPROF is masked.
	 * This is especially to support the MT collector tool.
	 * For bound threads, there are no such restrictions as above.
	 * Now, for unbound threads, SIGPROF signals could occur through the
	 * pool's lwps which have inherited the setitimer setting from the main
	 * lwp. Such a lwp could get a SIGPROF while running an idle thread or
	 * an internal thread. If that is the case, sigacthandler() should just
	 * return, ignoring the signal since one should not profile libthread
	 * internals.
	 * Another issue: Only bound threads can use the interfaces that result
	 * in generating signals with the SI_TIMER, SI_ASYNCIO, and SI_MESGQ
	 * signal codes which are sent to the LWP.
	 */
	ASSERT(ISBOUND(curthread) || sip == NULL || (sip->si_code != SI_TIMER
	    && sip->si_code != SI_ASYNCIO && sip->si_code != SI_MESGQ));
	/*
	 * Unless the signal is sent by the kernel, it is impossible for the
	 * signal to be masked in the current thread.
	 */
	ASSERT(sip == NULL || SI_FROMKERNEL(sip) ||
	    !sigismember(&t->t_hold, sig));
	/*
	 * Of the signals sent by the kernel, SIGCLD and SIGPOLL cannot be
	 * masked by the current thread since they are sent to the process
	 * and hence directed to this thread by the aslwp.
	 */
	ASSERT(sip == NULL || (!SI_FROMKERNEL(sip) || (sig != SIGCLD &&
	    sig != SIGPOLL) || !sigismember(&t->t_hold, sig)));

	ASSERT(_tsiguhandler[sig-1] != SIG_DFL &&
	    _tsiguhandler[sig-1] != SIG_IGN);
	/*
	 * sig cannot be masked by current thread except for synchronous
	 * signals (SI_FROMKERNEL), since all directed signals
	 * are received by this thread only when this thread has it unmasked,
	 * and undirected signals are received by "aslwp", the designated
	 * lwp which directs the signal to a thread which does not have it
	 * blocked. Of course, receiving SI_FROMKERNEL signal on a thread which
	 * has it masked is an error condition.
	 * Also, all sigs which are not SI_FROMKERNEL should appear in t_psig
	 * or in t_bsig since all such signals are directed, either through
	 * thr_kill() from user threads or via __thr_sigredirect() from the
	 * "aslwp" daemon lwp which fields undirected signals.
	 */
	ASSERT(sip == NULL || SI_FROMKERNEL(sip) || sig == SIGLWP ||
	    sig == SIGCLD || sip->si_code != SI_LWP ||
	    curthread->t_tid == _co_tid ||
	    (sigismember(&t->t_psig, sig) && (t->t_flag & T_SIGWAIT ||
	    (sigismember(&t->t_ssig, sig) && !sigismember(&t->t_hold, sig)))));


	ASSERT(sig != SIGCANCEL || !sigismember(&t->t_hold, sig));


	if ((t->t_nosig > 0 || sigismember(&t->t_hold, sig)) && sip != NULL &&
	    SI_FROMKERNEL(sip) && !sigismember(&__ksigsok, sip->si_signo)) {
		sprintf(buf, "signal fault in critical section\n");
		_write(2, buf, strlen(buf));
		sprintf(buf, "signal number: %d, signal code: %d, \
		    fault address: 0x%x, ",
		    sip->si_signo, sip->si_code, sip->si_addr);
		_write(2, buf, strlen(buf));
		sprintf(buf, "pc: 0x%x, sp: 0x%x\n",
		    uap->uc_mcontext.gregs[PC],
		    uap->uc_mcontext.gregs[SP]);
		_write(2, buf, strlen(buf));
		if (t->t_nosig > 0) {
			_panic("fault in libthread critical section");
		} else if (sigismember(&t->t_hold, sig)) {
			sprintf(buf, "ABORT: core dump\n");
			_write(2, buf, strlen(buf));
			__sigaction(sig, &__null_sigaction, NULL);
			if (sig == SIGLWP)
				t->t_flag &= ~T_INSIGLWP;
			setcontext(uap);
		}
	}
	if (t->t_nosig > 0) {
		/*
		 * Cannot take signal - in libthread critical section.
		 * Now, the same action needs to be taken for both directed and
		 * un-directed signals. Note: The un-directed signal got here
		 * through an _lwp_sigredirect() but its si_code remains
		 * untouched.
		 */
		ASSERT(sig == SIGLWP || (!sigismember(&t->t_psig, sig) ||
		    !sigismember(&t->t_ssig, sig)) || t->t_flag & T_SIGWAIT ||
		    !sigismember(&t->t_hold, sig));
		if (sig == SIGLWP) {
			/*
			 * preemption is disabled while in a critical
			 * section, however, thread's preempt flag should
			 * be set so that thread is preempted later. Toss away
			 * the SIGLWP - just return via setcontext().
			 */
			t->t_preempt = 1;
			t->t_flag &= ~T_INSIGLWP;
			setcontext(uap);
		}
		/*
		 * Store the signal for later delivery. Mask all sigs on
		 * LWP. Also make the thread temporarily bound to this LWP.
		 */
		t->t_sig = sig;
		t->t_olmask = uap->uc_sigmask;
		if (sip != NULL)
			memcpy(&t->t_si, sip, sizeof (siginfo_t));
		else
			t->t_si.si_signo = -1;
		if (t->t_schedlocked) {
			t->t_flag |= T_TEMPBOUND;
		} else {
			_sched_lock_nosig();
			t->t_flag |= T_TEMPBOUND;
			_sched_unlock_nosig();
		}
		if (sig == SIGLWP)
			t->t_flag &= ~T_INSIGLWP;
		setcontext_masksigs(uap);
	} else {
		ASSERT((!sigismember(&t->t_hold, sig)) ||
		    (sig == SIGPROF && (t->t_flag & T_INTERNAL)));
		/*
		 * If this is an internal thread which happened to be running
		 * on an lwp which is taking SIGPROF signals because of the
		 * MT collector being on, return. No need to take this signal
		 * because internal threads are not being profiled.
		 */
		if (sig == SIGPROF && (t->t_flag & T_INTERNAL))
			setcontext(uap);
		if (sig != SIGLWP && sip != NULL && sip->si_code == SI_LWP &&
		    (!sigismember(&t->t_psig, sig) ||
		    !sigismember(&t->t_ssig, sig))) {
			/*
			 * Since the syscall wrappers have been removed, and
			 * lwp_kill() for the thr_kill() is done under
			 * schedlock, this should now be impossible.
			 */
			    ASSERT(impossible2);
		}
		/*
		 * toss away siglwp for idle lwps.
		 * XXX: if _idle_thread_create() would have masked
		 * SIGLWP/SIGCANCEL in t_hold we could avoid following check.
		 */
		if ((sig == SIGLWP || sig == SIGCANCEL) && IDLETHREAD(t)) {
			if (sig == SIGLWP)
				t->t_flag &= ~T_INSIGLWP;
			setcontext(uap);
		}
		/*
		 * Since all signals are blocked here on the underlying LWP,
		 * we should not use the async-safe primitives 
		 * __sighandler_lock()/unlock() here. Just use the
		 * _lwp_mutex* primitives.
		 */
		_lwp_mutex_lock(&_sighandlerlock);
		_lsigumask = _sigumask[sig-1];
		handler = _tsiguhandler[sig-1];
		_lwp_mutex_unlock(&_sighandlerlock);

		_sched_lock_nosig();
		if (sip == NULL || sip->si_code != SI_LWP) {
			if (sigismember(&t->t_bsig, sig)) {
				sigdelset(&t->t_bsig, sig);
				t->t_bdirpend = !sigisempty(&t->t_bsig);
				if (t->t_bdirpend == 0)
					t->t_flag &= ~T_BSSIG;
			}
		} else {
			if (sigismember(&t->t_psig, sig)) {
			/*
			 * a signal is result of a thr_kill() when the received
			 * signal is in the thread's set of pending signals as
			 * represented by t_psig.
			 */
				ASSERT(sigismember(&t->t_ssig, sig));
				ASSERT(sig != SIGLWP);
				sigdelset(&t->t_ssig, sig);
				sigdelset(&t->t_psig, sig);
				t->t_pending = !sigisempty(&t->t_psig);
			}
		}
		ASSERT(handler != SIG_IGN && handler != SIG_DFL);
		if (t->t_state == TS_SLEEP) {
			if (sig == SIGLWP && PREEMPTED(t))
			    _panic("sigacthandler: preemption on sleeping t");
			_unsleep(t);
			t->t_state = TS_ONPROC;
			if (ISPARKED(t))
				_unpark(t);
			if (!ISBOUND(t))
				_onproc_enq(t);
		}
		_sched_unlock_nosig();
		/*
		 * No need to grab _sighandlerlock here. It is a race anyway.
		 */
		if (_siguflags[sig-1] & SA_RESETHAND)
			__sig_resethand(sig);
		if (sig == SIGLWP)
			t->t_flag |= T_DONTPREEMPT;
		if (sig == SIGPROF || t->t_tid == _co_tid) {
			t->t_flag |= (T_DONTPREEMPT|T_TEMPBOUND);
			thr_sigsetmask(SIG_BLOCK, &_lsigumask, &omask);
			__sigprocmask(SIG_SETMASK, &t->t_hold, NULL);
		} else {
			__sigprocmask(SIG_SETMASK, &_null_sigset, NULL);
			/*
			 * __thr_sigsetmask() is special and to be used here
			 * since it does not delete SIGLWP/SIGCANCEL from the
			 * mask like the normal thr_sigsetmask(). This is for
			 * _siglwp() - the libthread SIGLWP handler.
			 */
			if (sig == SIGLWP)
				__thr_sigsetmask(SIG_BLOCK, &_lsigumask,&omask);
			else
				thr_sigsetmask(SIG_BLOCK, &_lsigumask, &omask);
		}
		/*
		 * omask is passed to the handler to be used only by SIGALRM
		 * and/or SIGSEGV libthread handlers. See _callin() and
		 * __libthread_segvhandler().
		 */
		(*handler)(sig, sip, uap, &omask);
		if (sig == SIGPROF || t->t_tid == _co_tid) {
			t->t_flag &= ~(T_DONTPREEMPT|T_TEMPBOUND);
			thr_sigsetmask(SIG_SETMASK, &omask, NULL);
			/*
			 * Let the setcontext below restore the LWP mask= NULL
			 */
		} else {
			thr_sigsetmask(SIG_SETMASK, &omask, NULL);
		}
		if (sig == SIGLWP)
			t->t_flag &= ~T_DONTPREEMPT;
#ifdef sparc
		/*
		 * If this is a floating point exception and the queue
		 * is non-empty, pop the top entry from the queue.  This
		 * is to maintain expected behavior.
		 */
		if ((sig == SIGFPE) && uap->uc_mcontext.fpregs.fpu_qcnt) {
			fpregset_t *fp = &uap->uc_mcontext.fpregs;

			if (--fp->fpu_qcnt > 0) {
				unsigned char i;
				struct fq *fqp;

				fqp = fp->fpu_q;
				for (i = 0; i < fp->fpu_qcnt; i++)
					fqp[i] = fqp[i+1];
			}
		}
#endif
		ASSERT(pmaskok(&uap->uc_sigmask, &_pmask));
		if (sig == SIGLWP)
			t->t_flag &= ~T_INSIGLWP;
		setcontext(uap);
	}
}

void
__sig_resethand(int sig)
{
	struct sigaction act;

	ASSERT(!sigismember(&_cantreset, sig));
	/*
	 * Since all signals are blocked here on the underlying LWP, we should
	 * not use the async-safe primitives __sighandler_lock()/unlock()
	 * here. Just use the _lwp_mutex* primitives.
	 */
	_lwp_mutex_lock(&_sighandlerlock);
	if (_setsighandler(sig, &__null_sigaction, NULL) == -1) {
		_lwp_mutex_unlock(&_sighandlerlock);
		_panic("failed to reset hdlr\n");
	}
	_lwp_mutex_unlock(&_sighandlerlock);
}

void
__sendsig(int sig)
{
	uthread_t *t = curthread;
	ucontext_t uc;
	siginfo_t si;
	sigset_t omask;
	sigset_t ocntxtmask;
	void (*handler)();
	sigset_t _lsigumask;
	int flags;

	ASSERT(sig >= 1 && sig < NSIG);
	ASSERT(t->t_flag & T_TEMPBOUND);
	ASSERT(sig != SIGLWP);
	ASSERT(t->t_state != TS_SLEEP);
	ASSERT(t->t_sig == 0);
	ocntxtmask = t->t_olmask;
	memcpy(&si, &t->t_si, sizeof (siginfo_t));
	_preempt_off();
	_sched_lock_nosig();
	if (si.si_signo != -1) {
		if (si.si_code == NULL || si.si_code != SI_LWP) {
			if (sigismember(&t->t_bsig, sig)) {
				sigdelset(&t->t_bsig, sig);
				t->t_bdirpend = !sigisempty(&t->t_bsig);
				if (t->t_bdirpend == 0)
					t->t_flag &= ~T_BSSIG;
			}
		} else {
			if (sigismember(&t->t_psig, sig)) {
			/*
			* a signal is result of a thr_kill() when the received
			* signal is in the thread's set of pending signals as
			* represented by t_psig.
			*/
				ASSERT(sigismember(&t->t_ssig, sig));
				ASSERT(sig != SIGLWP);
				sigdelset(&t->t_ssig, sig);
				sigdelset(&t->t_psig, sig);
				t->t_pending = !sigisempty(&t->t_psig);
			}
		}
	}
	t->t_flag &= ~T_TEMPBOUND;
	_sched_unlock_nosig();
	/*
	 * Since all signals are blocked here on the underlying LWP, we should
	 * not use the async-safe primitives __sighandler_lock()/unlock()
	 * here. Just use the _lwp_mutex* primitives.
	 */
	_lwp_mutex_lock(&_sighandlerlock);
	_lsigumask = _sigumask[sig-1];
	handler = _tsiguhandler[sig-1];
	flags = _siguflags[sig-1];
	_lwp_mutex_unlock(&_sighandlerlock);
	if (flags & SA_RESETHAND)
		__sig_resethand(sig);
	if (sig == SIGPROF) {
		t->t_flag |= (T_TEMPBOUND|T_DONTPREEMPT);
		__thr_sigsetmask(SIG_BLOCK, &_lsigumask, &omask);
		__sigprocmask(SIG_SETMASK, &t->t_hold, NULL);
	} else {
		__sigprocmask(SIG_SETMASK, &ocntxtmask, NULL);
		thr_sigsetmask(SIG_BLOCK, &_lsigumask, &omask);
	}
	getcontext(&uc);
	(*handler)(sig, &si, &uc, &omask);
	if (sig == SIGPROF) {
		t->t_flag &= ~(T_TEMPBOUND|T_DONTPREEMPT);
		__thr_sigsetmask(SIG_SETMASK, &omask, NULL);
		__sigprocmask(SIG_SETMASK, &ocntxtmask, NULL);
	} else {
		thr_sigsetmask(SIG_SETMASK, &omask, NULL);
	}
	_preempt_on();
}

void
__sighandler_lock()
{
	_sigoff();
	_lwp_mutex_lock(&_sighandlerlock);
}

void
__sighandler_unlock()
{
	_lwp_mutex_unlock(&_sighandlerlock);
	_sigon();
}

_sigaction(int sig, const struct sigaction *nact, struct sigaction *oact)
{
	if (sig <= 0 || sig > NSIG || sig == SIGWAITING ||
				sig == SIGCANCEL || sig == SIGLWP) {
		errno = EINVAL;
		return (-1);
	}
	__sighandler_lock();
	if (_setsighandler(sig, nact, oact) == -1) {
		__sighandler_unlock();
		return (-1);
	}
	__sighandler_unlock();
	return (0);
}

int
_setsighandler(int sig, const struct sigaction *nact,
				struct sigaction *oact)
{
	struct sigaction tact;
	register struct sigaction *tactp;
	struct sigaction osigaction;

	if (sig == SIGALRM) {
		osigaction = __alarm_sigaction;
#ifdef NO_SWAP_INSTRUCTION
        } else if (sig == SIGSEGV) {
		osigaction = __segv_sigaction;
#endif
	} else {
		/*
		 * XXX: Replace below with an array of "struct sigaction"
		 * "struct sigaction __tsigaction[NSIGS];"
		 * so the following would be replaced with:
		 * "osigaction = __tsigaction[sig-1];"
		 * Not being done currently, since that would mean a lot
		 * of gratuitous change. Cleanup later (for 2.6).
		 */
		osigaction.sa_handler = _tsiguhandler[sig-1];
		osigaction.sa_mask    =	_sigumask[sig-1];
		osigaction.sa_flags = _siguflags[sig-1];
	}
	if (tactp = (struct sigaction *)nact) {
		tact = *nact;
		tactp = &tact;
		if (sigismember(&_cantreset, sig))
			tactp->sa_flags &= ~SA_RESETHAND;
		if (sig == SIGALRM && tactp->sa_handler != _callin) {
			__alarm_sigaction = *tactp;
			if (!(tactp->sa_flags & SA_NODEFER))
				sigaddset(&__alarm_sigaction.sa_mask, SIGALRM);
			sigdiffset(&__alarm_sigaction.sa_mask, &_cantmask);
			masktotalsigs(&tactp->sa_mask);
			tactp->sa_handler = _callin;
			/*
			 * Pass only the SA_ONSTACK and SA_RESTART flags to the
			 * kernel.  Any others are emulated:
			 * SA_NODEFER was handled above.
			 * SA_RESETHAND is emulated in sigacthandler() in the
			 *	call to __sig_resethand(). This flag is not
			 *	passed to the kernel call to __sigaction() below
			 * The other flags (SA_NOCLD*) are meaningless for
			 * SIGALRM, so ignore them.
			 */
                        tactp->sa_flags =
                           __alarm_sigaction.sa_flags & (SA_ONSTACK|SA_RESTART);
		}
#ifdef NO_SWAP_INSTRUCTION
                if (sig == SIGSEGV && tactp->sa_handler !=
                    __libthread_segvhdlr) {
			__segv_sigaction = *tactp;
                        if (!(tactp->sa_flags & SA_NODEFER))
                                sigaddset(&__segv_sigaction.sa_mask, SIGSEGV);
                        sigdiffset(&__segv_sigaction.sa_mask, &_cantmask);
			masktotalsigs(&tactp->sa_mask);
                        tactp->sa_handler = __libthread_segvhdlr;
			/*
			 * Pass only the SA_ONSTACK and SA_RESTART flags to the
			 * kernel.  Any others are emulated:
			 * SA_NODEFER was handled above.
			 * SA_RESETHAND is emulated in sigacthandler() in the
			 *	call to __sig_resethand(). This flag is not
			 *	passed to the kernel call to __sigaction() below
			 * The other flags (SA_NOCLD*) are meaningless for
			 * SIGSEGV, so ignore them.
			 */
                        tactp->sa_flags =
                            __segv_sigaction.sa_flags & (SA_ONSTACK|SA_RESTART);
                }
#endif /* NO_SWAP_INSTRUCTION */

		_tsiguhandler[sig-1] = tactp->sa_handler;
		_sigumask[sig-1] = tactp->sa_mask;
		_siguflags[sig-1] = tactp->sa_flags;

		if (tactp->sa_handler != SIG_IGN &&
		    tactp->sa_handler != SIG_DFL) {
			/*
			 * All signals are disabled when the kernel
			 * dispatches a signal to the common signal
			 * handler. This enables the thread library
			 * to disable signals without having to do
			 * a system call. The signal is delivered to
			 * the LWP and set pending on the thread.
			 * Signals remain disabled until the thread
			 * restores its t_nosig flag to zero.
			 */
			if (!(tactp->sa_flags & SA_NODEFER))
				sigaddset(&_sigumask[sig-1], sig);
			sigdiffset(&_sigumask[sig-1], &_cantmask);
			/*
			 * set SA_SIGINFO for all signals which have a handler
			 * - necessary for correct re-dispatch of signal in
			 * sigacthandler to implement fast _thr_sigsetmask()
			 */
			tactp->sa_flags |= SA_SIGINFO;
			/*
			 * Turn off SA_NODEFER and SA_RESETHAND in the flags
			 * passed to the kernel. SA_NODEFER is emulated above
			 * and SA_RESETHAND is emulated in sigacthandler()
			 * in the call to __sig_resethand().
			 */
			tactp->sa_flags &= ~(SA_NODEFER|SA_RESETHAND);
			masktotalsigs(&tactp->sa_mask);
			tactp->sa_handler = sigacthandler;
		}
	}
	if (__sigaction(sig, tactp, oact) == -1) {
		if (sig == SIGALRM) {
			__alarm_sigaction = osigaction;
#ifdef NO_SWAP_INSTRUCTION
                } else if (sig == SIGSEGV) {
			__segv_sigaction = osigaction;
#endif
		} else {
			_tsiguhandler[sig-1] = osigaction.sa_handler;
			_sigumask[sig-1] = osigaction.sa_mask;
			_siguflags[sig-1] = osigaction.sa_flags;
		}
		return (-1);
	}
	/*
	 * Since the threads library installs sigacthandler for all signals
	 * with a user defined handler, the system can never return anything
	 * other than SIG_DFL, SIG_IGN or sigacthandler.
	 */
	ASSERT(oact == NULL || oact->sa_handler == sigacthandler ||
	    oact->sa_handler == SIG_DFL || oact->sa_handler == SIG_IGN);
	if (oact && oact->sa_handler != SIG_IGN) {
		oact->sa_handler = osigaction.sa_handler;
		oact->sa_mask = osigaction.sa_mask;
		oact->sa_flags = osigaction.sa_flags;
	}
	return (0);
}

#ifdef NO_SWAP_INSTRUCTION
void
__libthread_segvhdlr(int sig, siginfo_t *sip, ucontext_t *uap, sigset_t *tmask)
{
        struct sigaction act;
 
	/*
	 * At this point, sig cannot be a SEGV due to mutex_unlock() - such a
	 * reason for SEGV would have been captured in the call to 
	 * __advance_pc_if_munlock_segv() at the start of sigacthandler().
	 */
	ASSERT(!(sig == SIGSEGV && SI_FROMKERNEL(sip)
	    && uap->uc_mcontext.gregs[PC] == (greg_t)&__wrd));
	/*
	 * Since all signals are blocked on the thread, it is OK
	 * to not use the async-safe routines __sighandler_lock()/unlock() here.
	 * In fact, it is more efficient to use the _lwp_* primitives here
	 * to avoid the unnecessary cost of calling _sigon() here.
	 */
	_lwp_mutex_lock(&_sighandlerlock);
	act = __segv_sigaction;
	_lwp_mutex_unlock(&_sighandlerlock);

	if (act.sa_handler != SIG_DFL && act.sa_handler != SIG_IGN) {
		if (act.sa_handler == __libthread_segvhdlr)
			_panic("__libthread_segvhdlr: recursive");
		/*
		 * Emulate the SA_RESETHAND flag, if set, here.
		 */
    	    	if (act.sa_flags & SA_RESETHAND)
			__sig_resethand(sig);
		/*
		 * The thread mask has been changed by sigacthandler(). The old
		 * mask is passed in "tmask". So, before calling the user
		 * installed segv handler, establish the correct mask.
		 */
		sigorset(&act.sa_mask, tmask);
		thr_sigsetmask(SIG_SETMASK, &act.sa_mask, NULL);
		(*act.sa_handler)(sig, sip, uap);
		/*
		 * old thread mask will be restored in sigacthandler()
		 * which called __libthread_segvhdlr().
		 */
		return;
	} else if (act.sa_handler == SIG_DFL) {
		/* send SIGSEGV to myself and blow myself away */
		__sigaction(SIGSEGV, &act, NULL);
		if (_lwp_kill(curthread->t_lwpid, SIGSEGV) < 0) {
			_panic("__libthread_segvhdlr: _lwp_kill");
		}
	}
}
 
#endif /* NO_SWAP_INSTRUCTION */

static	struct sigaction _def_sigaction = {SA_SIGINFO, sigacthandler, 0}; 
			/* sa_mask is initialized in _initsigs() */
void
_initsigs()
{
	register int i;
	register int siglim = NSIG;

	/*
	 * SIGLWP disposition initialized separately below.
	 * SIGCANCEL disposition initialized separately below.
	 * SIGWAITING disposition does not matter, since it is always blocked by
	 * all threads except the "aslwp" bound daemon thread.
	 *
	 * Make note of the following scenario:
	 * SIGFOO's disposition is ignore, all threads block SIGFOO, SIGFOO is
	 * sent/generated, signal handler installed for SIGFOO, SIGFOO unmasked.
	 * Correct behavior: handler invoked. Bug: SIGFOO ignored when generated
	 * One option is to use the SA_RESTART flag to sigaction(2). The other
	 * is to let the "aslwp" lwp do the right thing. This is now fixed
	 * with the fix for 1136714.
	 */

	/*
	 * The following will not be necessary if we want to rely on SIG_DFL to
	 * be zero, since _tsiguhandler[] for all sigs is 0 as initially
	 * allocated.
	 */
	for (i = 1; i < siglim; i++)
		_tsiguhandler[i-1] = SIG_DFL;

	/*
	 * used to ignore signals at user-level.
	 */
	sigemptyset(&_ignoredefault);
	sigaddset(&_ignoredefault, SIGCONT);
	sigaddset(&_ignoredefault, SIGCLD);
	sigaddset(&_ignoredefault, SIGPWR);
	sigaddset(&_ignoredefault, SIGWINCH);
	sigaddset(&_ignoredefault, SIGURG);
	sigaddset(&_ignoredefault, SIGWAITING);
	sigaddset(&_ignoredefault, SIGLWP);
	sigaddset(&_ignoredefault, SIGCANCEL);

	masktotalsigs(&_def_sigaction.sa_mask);

	/* disposition for SIGLWP */
	_tsiguhandler[SIGLWP-1] = _siglwp;
	masktotalsigs(&_sigumask[SIGLWP-1]);
	__sigaction(SIGLWP, &_def_sigaction, NULL);

	/* disposition for SIGCANCEL */
	_tsiguhandler[SIGCANCEL-1] = _sigcancel;
	masktotalsigs(&_sigumask[SIGCANCEL-1]);
	__sigaction(SIGCANCEL, &_def_sigaction, NULL);

	sigemptyset(&_pmask);
#ifdef NO_SWAP_INSTRUCTION
        /*
         * Install libthread's segv handler to handle a possible fault in
         * _mutex_unlock_asm() on architectures which do not have an atomic
         * swap instruction. e.g. the x86 version of _mutex_unlock_asm() uses
         * the native "xchgl" instruction, and so it cannot incur seg faults on
         * reading the wait bit. Hence it is not necessary to set-up a segv
         * handler for x86. On sparc, some sun4c architectures such as ss1s,
         * have to emulate the swap instruction which is too expensive. Hence,
         * for all of sparc, the solution is to allow the waiter-read in
         * _mutex_unlock_asm(), and handle the possible, resulting segv
         * appropriately by using the segv handler __libthread_segvhdlr().
         */
	_tsiguhandler[SIGSEGV-1] = __libthread_segvhdlr;
	masktotalsigs(&_sigumask[SIGSEGV-1]);
	__sigaction(SIGSEGV, &_def_sigaction, NULL);
#endif
}

void
setcontext_masksigs(ucontext_t *uc)
{
	uc->uc_sigmask = _totalmasked;
#ifdef	i386
	uc->uc_mcontext.gregs[GS] = _getgs();
#endif
	if (__setcontext(uc) < 0)
		_panic("setcontext: failed\n");
}

int
setcontext(ucontext_t *uc)
{
	uc->uc_sigmask = _null_sigset;
#ifdef	i386
	uc->uc_mcontext.gregs[GS] = _getgs();
#endif
	if (__setcontext(uc) < 0)
		_panic("setcontext: failed\n");
}

/*
 * Calls to sigprocmask(2) in modules linked with libthread get the following.
 */
int
_sigprocmask(int how, sigset_t *set, sigset_t *oset)
{
	int lerrno;

	if (lerrno = _thr_sigsetmask(how, set, oset)) {
		errno = lerrno;
		return (-1);
	} else
		return (0);
}
