/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *	Copyright (c) 1993,1994,1995 by Sun Microsystems, Inc.
 *	All Rights Reserved.
 */

#ifndef _SYS_SIGNAL_H
#define	_SYS_SIGNAL_H

#pragma ident	"@(#)signal.h	1.44	95/03/16 SMI"	/* SVr4.0 11.44 */

#include <sys/feature_tests.h>
#include <sys/unistd.h>		/* needed for _SC_SIGRT_MIN/MAX */

#ifdef	__cplusplus
extern "C" {
#endif

#define	SIGHUP	1	/* hangup */
#define	SIGINT	2	/* interrupt (rubout) */
#define	SIGQUIT	3	/* quit (ASCII FS) */
#define	SIGILL	4	/* illegal instruction (not reset when caught) */
#define	SIGTRAP	5	/* trace trap (not reset when caught) */
#define	SIGIOT	6	/* IOT instruction */
#define	SIGABRT 6	/* used by abort, replace SIGIOT in the future */
#define	SIGEMT	7	/* EMT instruction */
#define	SIGFPE	8	/* floating point exception */
#define	SIGKILL	9	/* kill (cannot be caught or ignored) */
#define	SIGBUS	10	/* bus error */
#define	SIGSEGV	11	/* segmentation violation */
#define	SIGSYS	12	/* bad argument to system call */
#define	SIGPIPE	13	/* write on a pipe with no one to read it */
#define	SIGALRM	14	/* alarm clock */
#define	SIGTERM	15	/* software termination signal from kill */
#define	SIGUSR1	16	/* user defined signal 1 */
#define	SIGUSR2	17	/* user defined signal 2 */
#define	SIGCLD	18	/* child status change */
#define	SIGCHLD	18	/* child status change alias (POSIX) */
#define	SIGPWR	19	/* power-fail restart */
#define	SIGWINCH 20	/* window size change */
#define	SIGURG	21	/* urgent socket condition */
#define	SIGPOLL 22	/* pollable event occured */
#define	SIGIO	SIGPOLL	/* socket I/O possible (SIGPOLL alias) */
#define	SIGSTOP 23	/* stop (cannot be caught or ignored) */
#define	SIGTSTP 24	/* user stop requested from tty */
#define	SIGCONT 25	/* stopped process has been continued */
#define	SIGTTIN 26	/* background tty read attempted */
#define	SIGTTOU 27	/* background tty write attempted */
#define	SIGVTALRM 28	/* virtual timer expired */
#define	SIGPROF 29	/* profiling timer expired */
#define	SIGXCPU 30	/* exceeded cpu limit */
#define	SIGXFSZ 31	/* exceeded file size limit */
#define	SIGWAITING 32	/* process's lwps are blocked */
#define	SIGLWP	33	/* special signal used by thread library */
#define	SIGFREEZE 34	/* special signal used by CPR */
#define	SIGTHAW 35	/* special signal used by CPR */
#define	SIGCANCEL 36	/* thread cancellation signal used by libthread */
/* insert new signals here, and move _SIGRTM* appropriately */
#define	_SIGRTMIN 37	/* first (highest-priority) realtime signal */
#define	_SIGRTMAX 44	/* last (lowest-priority) realtime signal */
extern long _sysconf(int);	/* System Private interface to sysconf() */
#define	SIGRTMIN _sysconf(_SC_SIGRT_MIN)	/* first realtime signal */
#define	SIGRTMAX _sysconf(_SC_SIGRT_MAX)	/* last realtime signal */

#if	defined(__cplusplus)

typedef	void SIG_FUNC_TYP(int);
typedef	SIG_FUNC_TYP *SIG_TYP;
#define	SIG_PF SIG_TYP

#define	SIG_DFL	(SIG_PF)0
#define	SIG_ERR (SIG_PF)-1
#define	SIG_IGN	(SIG_PF)1
#define	SIG_HOLD (SIG_PF)2

#elif	defined(lint)

#define	SIG_DFL	(void(*)())0
#define	SIG_ERR (void(*)())0
#define	SIG_IGN	(void (*)())0
#define	SIG_HOLD (void(*)())0

#else

#define	SIG_DFL	(void(*)())0
#define	SIG_ERR	(void(*)())-1
#define	SIG_IGN	(void (*)())1
#define	SIG_HOLD (void(*)())2

#endif

#define	SIG_BLOCK	1
#define	SIG_UNBLOCK	2
#define	SIG_SETMASK	3

#define	SIGNO_MASK	0xFF
#define	SIGDEFER	0x100
#define	SIGHOLD		0x200
#define	SIGRELSE	0x400
#define	SIGIGNORE	0x800
#define	SIGPAUSE	0x1000

#if defined(__EXTENSIONS__) || (__STDC__ - 0 == 0) || \
	defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)

#if defined(__EXTENSIONS__) || ((__STDC__ - 0 == 0) && \
	!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
		(_POSIX_C_SOURCE > 2)
/*
 * We need <sys/siginfo.h> for the declaration of siginfo_t.
 */
#include <sys/siginfo.h>

#endif

typedef struct {		/* signal set type */
	unsigned long	__sigbits[4];
} sigset_t;

typedef	struct {
	unsigned long	__sigbits[2];
} k_sigset_t;

/*
 * The signal handler routine can have either one or three arguments.
 * Existing C code has used either form so not specifing the arguments
 * neatly finesses the problem.  C++ doesn't accept this.  To C++
 * "(*sa_handler)()" indicates a routine with no arguments (ANSI C would
 * specify this as "(*sa_handler)(void)").  One or the other form must be
 * used for C++ and the only logical choice is "(*sa_handler)(int)" to allow
 * the SIG_* defines to work.  "(*sa_sigaction)(int, siginfo_t *, void *)"
 * can be used for the three argument form.
 */
struct sigaction {
	int sa_flags;
	union {
#ifdef	__cplusplus
		void (*_handler)(int);
#else
		void (*_handler)();
#endif
#if defined(__EXTENSIONS__) || ((__STDC__ - 0 == 0) && \
	!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	(_POSIX_C_SOURCE > 2)
		void (*_sigaction)(int, siginfo_t *, void *);
#endif
	}	_funcptr;
	sigset_t sa_mask;
	int sa_resv[2];
};
#define	sa_handler	_funcptr._handler
#define	sa_sigaction	_funcptr._sigaction

/* this is only valid for SIGCLD */
#define	SA_NOCLDSTOP	0x00020000	/* don't send job control SIGCLD's */
#endif

#if defined(__EXTENSIONS__) || \
	((__STDC__ - 0 == 0) && !defined(_POSIX_C_SOURCE))

			/* non-comformant ANSI compilation	*/

/* definitions for the sa_flags field */
#define	SA_ONSTACK	0x00000001
#define	SA_RESETHAND	0x00000002
#define	SA_RESTART	0x00000004
#endif

#if defined(__EXTENSIONS__) || ((__STDC__ - 0 == 0) && \
	!defined(_POSIX_C_SOURCE)) || (_POSIX_C_SOURCE > 2)
#define	SA_SIGINFO	0x00000008
#endif

#if defined(__EXTENSIONS__) || ((__STDC__ - 0 == 0) && \
	!defined(_POSIX_C_SOURCE))
#define	SA_NODEFER	0x00000010

/* this is only valid for SIGCLD */
#define	SA_NOCLDWAIT	0x00010000	/* don't save zombie children	 */

/* this is only valid for SIGWAITING */
#define	SA_WAITSIG	0x00010000	/* send SIGWAITING if all lwps block */

/*
 * use of these symbols by applications is injurious
 *	to binary compatibility, use _sys_nsig instead
 */
#define	NSIG	45	/* valid signals range from 1 to NSIG-1 */
#define	MAXSIG	44	/* size of u_signal[], NSIG-1 <= MAXSIG */
			/* Note: when changing MAXSIG, be sure to update the */
			/* sizes of u_sigmask and u_signal in */
			/* uts/adb/common/u.adb. */

#define	S_SIGNAL	1
#define	S_SIGSET	2
#define	S_SIGACTION	3
#define	S_NONE		4

#define	MINSIGSTKSZ	2048
#define	SIGSTKSZ	8192

#define	SS_ONSTACK	0x00000001
#define	SS_DISABLE	0x00000002

struct sigaltstack {
	char	*ss_sp;
	int	ss_size;
	int	ss_flags;
};

typedef struct sigaltstack stack_t;

#endif

#ifdef _KERNEL
#include <sys/t_lock.h>

extern k_sigset_t

	nullsmask,		/* a null signal mask */
	fillset,		/* valid signals, guaranteed contiguous */
	holdvfork,		/* held while doing vfork */
	cantmask,		/* cannot be caught or ignored */
	cantreset,		/* cannot be reset after catching */
	ignoredefault,		/* ignored by default */
	stopdefault,		/* stop by default */
	coredefault;		/* dumps core by default */

#define	sigmask(n)		((unsigned int)1 << (((n) - 1) & (32 - 1)))
#define	sigword(n)		(((unsigned int)((n) - 1))>>5)

#define	sigemptyset(s)		(*(s) = nullsmask)
#define	sigfillset(s)		(*(s) = fillset)
#define	sigaddset(s, n)		((s)->__sigbits[sigword(n)] |= sigmask(n))
#define	sigdelset(s, n)		((s)->__sigbits[sigword(n)] &= ~sigmask(n))
#define	sigismember(s, n)	(sigmask(n) & (s)->__sigbits[sigword(n)])
#define	sigisempty(s)		(!(((s)->__sigbits[0]) | ((s)->__sigbits[1])))
#define	sigutok(us, ks)		((ks)->__sigbits[0] = (us)->__sigbits[0], \
				    (ks)->__sigbits[1] = (us)->__sigbits[1])
#define	sigktou(ks, us)		((us)->__sigbits[0] = (ks)->__sigbits[0], \
				    (us)->__sigbits[1] = (ks)->__sigbits[1], \
				    (us)->__sigbits[2] = 0, \
				    (us)->__sigbits[3] = 0)
typedef struct {
	int	sig;				/* signal no.		*/
	int	perm;				/* flag for EPERM	*/
	int	checkperm;			/* check perm or not	*/
	int	sicode;				/* has siginfo.si_code	*/
	union sigval value;			/* user specified value	*/
} sigsend_t;

typedef struct sigqbucket {
	sigqueue_t		sqb_bucket;	/* sigqueue structure	*/
	struct sigqbucket	*sqb_link;	/* ptr to next struc	*/
} sigqbucket_t;

typedef struct sigqhdr {			/* sigqueue pool header	*/
	struct sigqbucket	*sqb_link;	/* next free sigq struc	*/
	int			sqb_count;	/* sigq free count	*/
	int			sqb_pexited;	/* process has exited	*/
	kmutex_t		sqb_lock;	/* lock for sigq pool	*/
} sigqhdr_t;

#define	_SIGQUEUE_MAX	32

#if defined(__STDC__)
extern	void	setsigact(int, void (*)(), k_sigset_t, int);
extern	void	sigorset(k_sigset_t *, k_sigset_t *);
extern	void	sigandset(k_sigset_t *, k_sigset_t *);
extern	void	sigdiffset(k_sigset_t *, k_sigset_t *);
extern	void	sigreplace(k_sigset_t *, k_sigset_t *);
#else
extern	void	setsigact();
extern	void	sigorset();
extern	void	sigandset();
extern	void	sigdiffset();
extern	void	sigreplace();
#endif	/* __STDC__ */

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SIGNAL_H */
