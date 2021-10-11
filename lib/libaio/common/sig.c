#include <libaio.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/reg.h>
#include <wait.h>
#include <errno.h>

lwp_mutex_t __sigio_pendinglock;	/* protects __sigio_pending */
int __sigio_pending = 0;		/* count of pending SIGIO signals */
int _sigio_enabled;			/* set if SIGIO has a signal handler */
static struct sigaction sigioact;
static struct sigaction  sigcanact;
void aiosigiohndlr(int, siginfo_t *, ucontext_t *);
void aiosigcancelhndlr(int, siginfo_t *, ucontext_t *);
int _aiosigaction(int, const struct sigaction *, struct sigaction *);

#ifdef __STDC__
#pragma	weak sigaction = _aiosigaction
#endif

_aio_create_worker(aiop, flag)
	struct aio_req *aiop;
	int flag;
{
	_aio_worker_cnt++;
	return (_aio_lwp_exec(_aio_do_request, aiop, &_worker_set, flag));
}

void
_aio_cancel_on(aiowp)
	struct aio_worker *aiowp;
{
	aiowp->work_cancel_flg = 1;
}

void
_aio_cancel_off(aiowp)
	struct aio_worker *aiowp;
{
	aiowp->work_cancel_flg = 0;
}

/*
 * resend a SIGIO signal that was sent while the
 * __aio_mutex was locked.
 */
void
__aiosendsig(void)
{
	sigset_t omask;

	_lwp_mutex_lock(&__sigio_pendinglock);
	__sigio_pending = 0;
	_lwp_mutex_unlock(&__sigio_pendinglock);

	kill(__pid, SIGIO);
}

/*
 * this is the low-level handler for SIGIO. the application
 * handler will not be called if the signal is being blocked.
 */
static void
aiosigiohndlr(int sig, siginfo_t *sip, ucontext_t *uap)
{
	/*
	 * SIGIO signal is being blocked if either _sigio_masked
	 * or sigio_maskedcnt is set or if both these variables
	 * are clear and the _aio_mutex is locked. the last
	 * condition can only happen when _aio_mutex is being
	 * unlocked. this is a very small window where the mask
	 * is clear and the lock is about to be unlocked, however,
	 * it`s still set and so the signal should be defered.
	 */
	if ((__sigio_masked | __sigio_maskedcnt) ||
	    (MUTEX_HELD(&__aio_mutex))) {
		/*
		 * aio_lock() is supposed to be non re-entrant with
		 * respect to SIGIO signals. if a SIGIO signal
		 * interrupts a region of code locked by _aio_mutex
		 * the SIGIO signal should be deferred until this
		 * mutex is unlocked. a flag is set, sigio_pending,
		 * to indicate that a SIGIO signal is pending and
		 * should be resent to the process via a kill().
		 */
		_lwp_mutex_lock(&__sigio_pendinglock);
		__sigio_pending = 1;
		_lwp_mutex_unlock(&__sigio_pendinglock);
	} else {
		/*
		 * call the real handler.
		 */
		sigprocmask(SIG_SETMASK, &sigioact.sa_mask, NULL);
		(sigioact.sa_handler)(sig, sip, uap);
	}
}

void
aiosigcancelhndlr(int sig, siginfo_t *sip, ucontext_t *uap)
{
	struct aio_worker *aiowp;
	struct sigaction act;

	if (sip->si_code == SI_LWP) {
		aiowp = (struct aio_worker *)_lwp_getprivate();
		ASSERT(aiowp != NULL);
		if (aiowp->work_cancel_flg)
			siglongjmp(aiowp->work_jmp_buf, 1);
	} else if (sigcanact.sa_handler == SIG_DFL) {
		act.sa_handler = SIG_DFL;
		_sigaction(SIGAIOCANCEL, &act, NULL);
		kill(getpid(), sig);
	} else if (sigcanact.sa_handler != SIG_IGN) {
		sigprocmask(SIG_SETMASK, &sigcanact.sa_mask, NULL);
		(sigcanact.sa_handler)(sig, sip, uap);
	}
}

_aiosigaction(int sig, const struct sigaction *nact, struct sigaction *oact)
{
	struct sigaction tact;
	struct sigaction *tactp;
	struct sigaction oldact;
	void (*ohandler)();

	/*
	 * Only interpose on SIGIO when it is given a dispostion other than
	 * SIG_IGN, or SIG_DFL. Because SIGAIOCANCEL is SIGPROF, this
	 * signal always should be interposed on, so that SIGPROF can
	 * also be used by the application for profiling.
	 */
	if (nact &&
	    ((sig == SIGIO) && (nact->sa_handler != SIG_DFL &&
	    nact->sa_handler != SIG_IGN)) ||
	    (sig == SIGAIOCANCEL)) {
		tactp = &tact;
		*tactp = *nact;
		if (sig == SIGIO) {
			_sigio_enabled = 1;
			*(&oldact) = *(&sigioact);
			*(&sigioact) = *tactp;
			tactp->sa_handler = aiosigiohndlr;
			sigaddset(&tactp->sa_mask, SIGIO);
			if (_sigaction(sig, tactp, oact) == -1) {
				*(&sigioact) = *(&oldact);
				return (-1);
			}
		} else {
			*(&oldact) = *(&sigcanact);
			*(&sigcanact) = *tactp;
			tactp->sa_handler = aiosigcancelhndlr;
			sigaddset(&tactp->sa_mask, SIGAIOCANCEL);
			if (_sigaction(sig, tactp, oact) == -1) {
				*(&sigcanact) = *(&oldact);
				return (-1);
			}
		}
		if (oact)
			*oact = *(&oldact);
		return (0);
	}

	if (oact && (sig == SIGIO || sig == SIGAIOCANCEL)) {
		if (sig == SIGIO) 
			*oact = *(&sigioact);
		else
			*oact = *(&sigcanact);
	} else
		return (_sigaction(sig, nact, oact));
}

/*
 * Check for valid signal number as per SVID.
 */
#define	CHECK_SIG(s, code) \
	if ((s) <= 0 || (s) >= NSIG || (s) == SIGKILL || (s) == SIGSTOP) { \
		errno = EINVAL; \
		return (code); \
	}

/*
 * Equivalent to stopdefault set in the kernel implementation (sig.c).
 */
#define	STOPDEFAULT(s) \
	((s) == SIGSTOP || (s) == SIGTSTP || (s) == SIGTTOU || (s) == SIGTTIN)


/*
 * SVr3.x signal compatibility routines. They are now
 * implemented as library routines instead of system
 * calls.
 */

void(*
signal(int sig, void(*func)(int)))(int)
{
	struct sigaction nact;
	struct sigaction oact;

	CHECK_SIG(sig, SIG_ERR);

	nact.sa_handler = func;
	nact.sa_flags = SA_RESETHAND|SA_NODEFER;
	(void) _sigemptyset(&nact.sa_mask);

	/*
	 * Pay special attention if sig is SIGCHLD and
	 * the disposition is SIG_IGN, per sysV signal man page.
	 */
	if (sig == SIGCHLD) {
		nact.sa_flags |= SA_NOCLDSTOP;
		if (func == SIG_IGN)
			nact.sa_flags |= SA_NOCLDWAIT;
	}

	if (STOPDEFAULT(sig))
		nact.sa_flags |= SA_RESTART;

	if (_aiosigaction(sig, &nact, &oact) < 0)
		return (SIG_ERR);

	/*
	 * Old signal semantics require that the existance of ZOMBIE children
	 * when a signal handler for SIGCHLD is set to generate said signal.
	 * (Filthy and disgusting)
	 */
	if (sig == SIGCHLD && func != SIG_IGN &&
	    func != SIG_DFL && func != SIG_HOLD) {
		siginfo_t info;
		if (!waitid(P_ALL, (id_t)0, &info, WEXITED|WNOHANG|WNOWAIT) &&
		    info.si_pid != 0)
			(void) kill(getpid(), SIGCHLD);
	}

	return (oact.sa_handler);
}

int
sigignore(int sig)
{
	struct sigaction act;
	sigset_t set;

	CHECK_SIG(sig, -1);

	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	(void) _sigemptyset(&act.sa_mask);

	/*
	 * Pay special attention if sig is SIGCHLD and
	 * the disposition is SIG_IGN, per sysV signal man page.
	 */
	if (sig == SIGCHLD) {
		act.sa_flags |= SA_NOCLDSTOP;
		act.sa_flags |= SA_NOCLDWAIT;
	}

	if (STOPDEFAULT(sig))
		act.sa_flags |= SA_RESTART;

	if (_aiosigaction(sig, &act, (struct sigaction *)0) < 0)
		return (-1);

	(void) _sigemptyset(&set);
	if (_sigaddset(&set, sig) < 0)
		return (-1);
	return (_sigprocmask(SIG_UNBLOCK, &set, (sigset_t *)0));
}

void(*
sigset(int sig, void(*func)(int)))(int)
{
	struct sigaction nact;
	struct sigaction oact;
	sigset_t nset;
	sigset_t oset;
	int code;

	CHECK_SIG(sig, SIG_ERR);

	(void) _sigemptyset(&nset);
	if (_sigaddset(&nset, sig) < 0)
		return (SIG_ERR);

	if (func == SIG_HOLD) {
		if (_sigprocmask(SIG_BLOCK, &nset, &oset) < 0)
			return (SIG_ERR);
		if (_aiosigaction(sig, (struct sigaction *)0, &oact) < 0)
			return (SIG_ERR);
	} else {
		nact.sa_handler = func;
		nact.sa_flags = 0;
		(void) _sigemptyset(&nact.sa_mask);
		/*
		 * Pay special attention if sig is SIGCHLD and
		 * the disposition is SIG_IGN, per sysV signal man page.
		 */
		if (sig == SIGCHLD) {
			nact.sa_flags |= SA_NOCLDSTOP;
			if (func == SIG_IGN)
				nact.sa_flags |= SA_NOCLDWAIT;
		}

		if (STOPDEFAULT(sig))
			nact.sa_flags |= SA_RESTART;

		if (_aiosigaction(sig, &nact, &oact) < 0)
			return (SIG_ERR);

		if (_sigprocmask(SIG_UNBLOCK, &nset, &oset) < 0)
			return (SIG_ERR);
	}

	/*
	 * Old signal semantics require that the existance of ZOMBIE children
	 * when a signal handler for SIGCHLD is set to generate said signal.
	 * (Filthy and disgusting)
	 */
	if (sig == SIGCHLD && func != SIG_IGN &&
	    func != SIG_DFL && func != SIG_HOLD) {
		siginfo_t info;
		if (!waitid(P_ALL, (id_t)0, &info, WEXITED|WNOHANG|WNOWAIT) &&
		    info.si_pid != 0)
			(void) kill(getpid(), SIGCHLD);
	}

	if ((code = _sigismember(&oset, sig)) < 0)
		return (SIG_ERR);
	else if (code == 1)
		return (SIG_HOLD);

	return (oact.sa_handler);
}
