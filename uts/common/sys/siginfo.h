/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/
/*
 *	Copyright (c) 1993, by Sun Microsystems, Inc.
 */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_SIGINFO_H
#define	_SYS_SIGINFO_H

#pragma ident	"@(#)siginfo.h	1.36	95/08/24 SMI"	/* SVr4.0 1.20 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2) || \
	defined(__EXTENSIONS__)
union sigval {
	int	sival_int;	/* integer value */
	void	*sival_ptr;	/* pointer value */
};

struct sigevent {
	int		sigev_notify;	/* notification mode */
	union {
		int	_sigev_signo;	/* signal number */
		void	(*_sigev_notify_function)(union sigval);
	} _sigev_un;
	union sigval	sigev_value;	/* signal value */
	int		_sigev_pad1;
	void		*_sigev_notify_attributes;
	int		_sigev_pad2;
};
#define	sigev_signo	_sigev_un._sigev_signo

/* values of sigev_notify */
#define	SIGEV_NONE	1		/* no notification */
#define	SIGEV_SIGNAL	2		/* queued signal notification */
#define	SIGEV_THREAD	3		/* call back from another thread */

/*
 * negative signal codes are reserved for future use for user generated
 * signals
 */

#define	SI_FROMUSER(sip)	((sip)->si_code <= 0)
#define	SI_FROMKERNEL(sip)	((sip)->si_code > 0)

#define	SI_NOINFO	32767	/* no signal information */
#define	SI_USER		0	/* user generated signal via kill() */
#define	SI_LWP		(-1)	/* user generated signal via lwp_kill() */
#define	SI_QUEUE	(-2)	/* user generated signal via sigqueue() */
#define	SI_TIMER	(-3)	/* from timer expiration */
#define	SI_ASYNCIO	(-4)	/* from asynchronous I/O completion */
#define	SI_MESGQ	(-5)	/* from message arrival */
#endif /* !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2)... */

#if !defined(_POSIX_C_SOURCE) || defined(__EXTENSIONS__)

/*
 * Get the machine dependent signal codes (SIGILL, SIGFPE, SIGSEGV, and
 * SIGBUS) from <sys/machsig.h>
 */

#include <sys/machsig.h>

/*
 * SIGTRAP signal codes
 */

#define	TRAP_BRKPT	1	/* breakpoint trap */
#define	TRAP_TRACE	2	/* trace trap */
#define	NSIGTRAP	2

/*
 * SIGCLD signal codes
 */

#define	CLD_EXITED	1	/* child has exited */
#define	CLD_KILLED	2	/* child was killed */
#define	CLD_DUMPED	3	/* child has coredumped */
#define	CLD_TRAPPED	4	/* traced child has stopped */
#define	CLD_STOPPED	5	/* child has stopped on signal */
#define	CLD_CONTINUED	6	/* stopped child has continued */
#define	NSIGCLD		6

/*
 * SIGPOLL signal codes
 */

#define	POLL_IN		1	/* input available */
#define	POLL_OUT	2	/* output possible */
#define	POLL_MSG	3	/* message available */
#define	POLL_ERR	4	/* I/O error */
#define	POLL_PRI	5	/* high priority input available */
#define	POLL_HUP	6	/* device disconnected */
#define	NSIGPOLL	6

/*
 * SIGPROF signal codes
 */

#define	PROF_SIG	1	/* have to set code non-zero */
#define	NSIGPROF	1

#endif /* !defined(_POSIX_C_SOURCE) || defined(__EXTENSIONS__) */

#if !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2) || \
	defined(__EXTENSIONS__)


#define	SI_MAXSZ	128
#define	SI_PAD		((SI_MAXSZ / sizeof (int)) - 3)

/*
 * We need <sys/time.h> for the declaration of timestruc_t.
 */
#include <sys/time.h>

typedef struct
#if !defined(_POSIX_C_SOURCE)
	/* can't pollute the POSIX compilation namespace */
	siginfo
#endif
			{

	int	si_signo;			/* signal from signal.h	*/
	int 	si_code;			/* code from above	*/
	int	si_errno;			/* error from errno.h	*/

	union {

		int	_pad[SI_PAD];		/* for future growth	*/

		struct {			/* kill(), SIGCLD, siqqueue() */
			pid_t	_pid;		/* process ID		*/
			union {
				struct {
					uid_t	_uid;
					union sigval	_value;
				} _kill;
				struct {
					clock_t _utime;
					int	_status;
					clock_t _stime;
				} _cld;
			} _pdata;
		} _proc;

		struct {	/* SIGSEGV, SIGBUS, SIGILL and SIGFPE	*/
			caddr_t	_addr;		/* faulting address	*/
			int	_trapno;	/* illegal trap number	*/
		} _fault;

		struct {			/* SIGPOLL, SIGXFSZ	*/
		/* fd not currently available for SIGPOLL */
			int	_fd;		/* file descriptor	*/
			long	_band;
		} _file;

		struct {			/* SIGPROF */
			caddr_t	_faddr;		/* last fault address	*/
			timestruc_t _tstamp;	/* real time stamp	*/
			short	_syscall;	/* current syscall	*/
			char	_nsysarg;	/* number of arguments	*/
			char	_fault;		/* last fault type	*/
			long	_sysarg[8];	/* syscall arguments	*/
			long	_mstate[17];	/* exactly fills struct	*/
		} _prof;

	} _data;

} siginfo_t;

/*
 * XXX -- internal version is identical to siginfo_t but without the padding.
 * This must be maintained in sync with it.
 */

#if !defined(_POSIX_C_SOURCE)

typedef struct k_siginfo {

	int	si_signo;			/* signal from signal.h	*/
	int 	si_code;			/* code from above	*/
	int	si_errno;			/* error from errno.h	*/

	union {
		struct {			/* kill(), SIGCLD, siqqueue() */
			pid_t	_pid;		/* process ID		*/
			union {
				struct {
					uid_t	_uid;
					union sigval	_value;
				} _kill;
				struct {
					clock_t _utime;
					int	_status;
					clock_t _stime;
				} _cld;
			} _pdata;
		} _proc;

		struct {	/* SIGSEGV, SIGBUS, SIGILL and SIGFPE	*/
			caddr_t	_addr;		/* faulting address	*/
			int	_trapno;	/* illegal trap number	*/
		} _fault;

		struct {			/* SIGPOLL, SIGXFSZ	*/
		/* fd not currently available for SIGPOLL */
			int	_fd;		/* file descriptor	*/
			long	_band;
		} _file;

		struct {			/* SIGPROF */
			caddr_t	_faddr;		/* last fault address	*/
			timestruc_t _tstamp;	/* real time stamp	*/
			short	_syscall;	/* current syscall	*/
			char	_nsysarg;	/* number of arguments	*/
			char	_fault;		/* last fault type	*/
			/* these are omitted to keep k_siginfo_t small	*/
			/* long	_sysarg[8]; */
			/* long	_mstate[17]; */
		} _prof;

	} _data;

} k_siginfo_t;

typedef struct sigqueue {
	struct sigqueue	*sq_next;
	k_siginfo_t	sq_info;
	void		(*sq_func)(struct sigqueue *); /* destructor function */
	void		*sq_backptr;	/* pointer to the data structure */
					/* associated by sq_func()	*/
} sigqueue_t;

/*  indication whether to queue the signal or not */
#define	SI_CANQUEUE(c)	((c) <= SI_QUEUE)

#endif /* !defined(_POSIX_C_SOURCE) */

#define	si_pid		_data._proc._pid
#define	si_status	_data._proc._pdata._cld._status
#define	si_stime	_data._proc._pdata._cld._stime
#define	si_utime	_data._proc._pdata._cld._utime
#define	si_uid		_data._proc._pdata._kill._uid
#define	si_value	_data._proc._pdata._kill._value
#define	si_addr		_data._fault._addr
#define	si_trapno	_data._fault._trapno
#define	si_fd		_data._file._fd
#define	si_band		_data._file._band

#define	si_tstamp	_data._prof._tstamp
#define	si_syscall	_data._prof._syscall
#define	si_nsysarg	_data._prof._nsysarg
#define	si_sysarg	_data._prof._sysarg
#define	si_fault	_data._prof._fault
#define	si_faddr	_data._prof._faddr
#define	si_mstate	_data._prof._mstate

#endif /* !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE > 2) ... */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SIGINFO_H */
