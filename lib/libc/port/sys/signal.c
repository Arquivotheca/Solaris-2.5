/*
 *	Copyright (c) Sun Microsystems Inc. 1991
 */

#ident	"@(#)signal.c	1.5	93/08/18 SMI"

#ifdef __STDC__
	#pragma weak signal = _signal
	#pragma weak sighold = _sighold
	#pragma weak sigrelse = _sigrelse
	#pragma weak sigignore = _sigignore
	#pragma weak sigpause = _sigpause
	#pragma weak sigset = _sigset
#endif

#include "synonyms.h"

#include <errno.h>
#include <signal.h>
#include <wait.h>

/*
 * Check for valid signal number as per SVID.
 */
#define	CHECK_SIG(s,code) \
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
	(void) sigemptyset(&nact.sa_mask);

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

	if (sigaction(sig, &nact, &oact) < 0)
		return (SIG_ERR);

	/*
	 * Old signal semantics require that the existance of ZOMBIE children
	 * when a signal handler for SIGCHLD is set to generate said signal.
	 * (Filthy and disgusting)
	 */
	if (sig == SIGCHLD && func != SIG_IGN
	    && func != SIG_DFL && func != SIG_HOLD) {
		siginfo_t info;
		if (!waitid(P_ALL, (id_t)0, &info, WEXITED|WNOHANG|WNOWAIT) &&
		    info.si_pid != 0)
			(void) kill(getpid(), SIGCHLD);
	}

	return (oact.sa_handler);
}

int
sighold(int sig)
{
	sigset_t set;

	CHECK_SIG(sig, -1);

	/*
	 * errno set on failure by either sigaddset or sigprocmask.
	 */
	(void) sigemptyset(&set);
	if (sigaddset(&set, sig) < 0)
		return (-1);
	return (sigprocmask(SIG_BLOCK, &set, (sigset_t *)0));
}

int
sigrelse(int sig)
{
	sigset_t set;

	CHECK_SIG(sig, -1);

	/*
	 * errno set on failure by either sigaddset or sigprocmask.
	 */
	(void) sigemptyset(&set);
	if (sigaddset(&set, sig) < 0)
		return (-1);
	return (sigprocmask(SIG_UNBLOCK, &set, (sigset_t *)0));
}

int
sigignore(int sig)
{
	struct sigaction act;
	sigset_t set;

	CHECK_SIG(sig, -1);

	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	(void) sigemptyset(&act.sa_mask);

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

	if (sigaction(sig, &act, (struct sigaction *)0) < 0)
		return (-1);

	(void) sigemptyset(&set);
	if (sigaddset(&set, sig) < 0)
		return (-1);
	return (sigprocmask(SIG_UNBLOCK, &set, (sigset_t *)0));
}

int
sigpause(int sig)
{
	sigset_t set;
	int rval;

	CHECK_SIG(sig, -1);

	/*
	 * sigpause() is defined to unblock the signal
	 * and not block it again on return.
	 * sigsuspend() restores the original signal set,
	 * so we have to unblock sig overtly.
	 */
	(void) sigprocmask(0, (sigset_t *)0, &set);
	if (sigdelset(&set, sig) < 0)
		return (-1);
	rval = sigsuspend(&set);
	(void) _sigrelse(sig);
	return (rval);
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

	(void) sigemptyset(&nset);
	if (sigaddset(&nset, sig) < 0)
		return (SIG_ERR);

	if (func == SIG_HOLD) {
		if (sigprocmask(SIG_BLOCK, &nset, &oset) < 0)
			return (SIG_ERR);
		if (sigaction(sig, (struct sigaction *)0, &oact) < 0)
			return (SIG_ERR);
	} else {
		nact.sa_handler = func;
		nact.sa_flags = 0;
		(void) sigemptyset(&nact.sa_mask);
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

		if (sigaction(sig, &nact, &oact) < 0)
			return (SIG_ERR);

		if (sigprocmask(SIG_UNBLOCK, &nset, &oset) < 0)
			return (SIG_ERR);
	}

	/*
	 * Old signal semantics require that the existance of ZOMBIE children
	 * when a signal handler for SIGCHLD is set to generate said signal.
	 * (Filthy and disgusting)
	 */
	if (sig == SIGCHLD && func != SIG_IGN
	    && func != SIG_DFL && func != SIG_HOLD) {
		siginfo_t info;
		if (!waitid(P_ALL, (id_t)0, &info, WEXITED|WNOHANG|WNOWAIT) &&
		    info.si_pid != 0)
			(void) kill(getpid(), SIGCHLD);
	}

	if ((code = sigismember(&oset, sig)) < 0)
		return (SIG_ERR);
	else if (code == 1)
		return (SIG_HOLD);

	return (oact.sa_handler);
}
