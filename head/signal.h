/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SIGNAL_H
#define	_SIGNAL_H

#pragma ident	"@(#)signal.h	1.27	95/08/30 SMI"	/* SVr4.0 1.5.3.4 */

#include <sys/feature_tests.h>

#include <sys/signal.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef int	sig_atomic_t;

#if defined(__STDC__)

extern const char	**_sys_siglistp;	/* signal descriptions */
extern const int	_sys_siglistn;		/* # of signal descriptions */

#if defined(__EXTENSIONS__) || \
	(!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))
#define	_sys_siglist	_sys_siglistp
#define	_sys_nsig	_sys_siglistn
#endif

#ifdef __cplusplus
extern "C" SIG_PF signal(int, SIG_PF);
#else
extern void (*signal(int, void (*)(int)))(int);
#endif
extern int raise(int);

#if defined(__EXTENSIONS__) || __STDC__ == 0 || \
		defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)
#include <sys/types.h>
extern int kill(pid_t, int);
extern int sigaction(int, const struct sigaction *, struct sigaction *);
#ifndef	sigaddset
extern int sigaddset(sigset_t *, int);
#endif
#ifndef	sigdelset
extern int sigdelset(sigset_t *, int);
#endif
#ifndef	sigemptyset
extern int sigemptyset(sigset_t *);
#endif
#ifndef	sigfillset
extern int sigfillset(sigset_t *);
#endif
#ifndef	sigismember
extern int sigismember(const sigset_t *, int);
#endif
extern int sigpending(sigset_t *);
extern int sigprocmask(int, const sigset_t *, sigset_t *);
extern int sigsuspend(const sigset_t *);
#endif

#if defined(__EXTENSIONS__) || (__STDC__ == 0 && \
		!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))
#include <sys/procset.h>
extern int gsignal(int);
extern void (*sigset(int, void (*)(int)))(int);
extern int sighold(int);
extern int sigrelse(int);
extern int sigignore(int);
extern int sigpause(int);
extern int (*ssignal(int, int (*)(int)))(int);
extern int sigaltstack(const stack_t *, stack_t *);
extern int sigsend(idtype_t, id_t, int);
extern int sigsendset(const procset_t *, int);
extern int sig2str(int, char *);
extern int str2sig(const char *, int *);
#define	SIG2STR_MAX	32
#endif

#if defined(__EXTENSIONS__) || (__STDC__ == 0 && \
		!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
		(_POSIX_C_SOURCE > 2)
#include <sys/siginfo.h>
#include <sys/time.h>
extern int sigwaitinfo(const sigset_t *, siginfo_t *);
extern int sigtimedwait(const sigset_t *, siginfo_t *, const struct timespec *);
extern int sigqueue(pid_t, int, const union sigval);
#endif

#else	/* __STDC__ */

extern char	**_sys_siglistp;	/* signal descriptions */
extern int	_sys_siglistn;		/* # of signal descriptions */
#define	_sys_siglist	_sys_siglistp
#define	_sys_nsig	_sys_siglistn

extern	void(*signal())();
extern int raise();

extern int kill();
extern int sigaction();
#ifndef	sigaddset
extern int sigaddset();
#endif
#ifndef	sigdelset
extern int sigdelset();
#endif
#ifndef	sigemptyset
extern int sigemptyset();
#endif
#ifndef	sigfillset
extern int sigfillset();
#endif
#ifndef	sigismember
extern int sigismember();
#endif
extern int sigpending();
extern int sigprocmask();
extern int sigsuspend();

#if	!defined(_POSIX_C_SOURCE)
extern int gsignal();
extern void (*sigset())();
extern int sighold();
extern int sigrelse();
extern int sigignore();
extern int sigpause();
extern int (*ssignal)();
extern int sigaltstack();
extern int sigsend();
extern int sigsendset();
extern int sig2str();
extern int str2sig();
#define	SIG2STR_MAX	32
#endif

#if	!defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2)
#include <sys/siginfo.h>
#include <sys/time.h>
extern int sigwaitinfo();
extern int sigtimedwait();
extern int sigqueue();
#endif

#endif	/* __STDC__ */

/*
 * sigwait() prototype is defined here.
 */

#if	defined(__EXTENSIONS__) || (__STDC__ == 0 && \
	!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#if	defined(__STDC__)

#if	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#ifdef __PRAGMA_REDEFINE_EXTNAME
extern int sigwait(const sigset_t *, int *);
#pragma redefine_extname sigwait __posix_sigwait
#else  /* __PRAGMA_REDEFINE_EXTNAME */

static int
sigwait(const sigset_t *__setp, int *__signo)
{
	extern int __posix_sigwait(const sigset_t *, int *);
	return (__posix_sigwait(__setp, __signo));
}
#endif /* __PRAGMA_REDEFINE_EXTNAME */

#else  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

extern int sigwait(sigset_t *);

#endif  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */


#else  /* __STDC__ */


#if	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_POSIX_PTHREAD_SEMANTICS)

#ifdef __PRAGMA_REDEFINE_EXTNAME
extern int sigwait();
#pragma redefine_extname sigwait __posix_sigwait
#else  /* __PRAGMA_REDEFINE_EXTNAME */

static int
sigwait(__setp, __signo)
	sigset_t *__setp;
	int *__signo;
{
	extern int __posix_sigwait();
	return (__posix_sigwait(__setp, __signo));
}
#endif /* __PRAGMA_REDEFINE_EXTNAME */

#else  /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

extern int sigwait();

#endif /* (_POSIX_C_SOURCE - 0 >= 199506L) || ... */

#endif /* __STDC__ */

#endif /* defined(__EXTENSIONS__) || (__STDC__ == 0 ... */

#ifdef	__cplusplus
}
#endif

#endif	/* _SIGNAL_H */
