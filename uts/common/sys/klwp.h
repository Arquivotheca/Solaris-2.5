/*
 *	Copyright (c) 1991, Sun Microsystems, Inc.
 */

#ifndef	_SYS_KLWP_H
#define	_SYS_KLWP_H

#pragma ident	"@(#)klwp.h	1.20	95/02/16 SMI"

#include <sys/types.h>
#include <sys/condvar.h>
#include <sys/thread.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/pcb.h>
#include <sys/time.h>
#include <sys/msacct.h>
#include <sys/ucontext.h>
#include <sys/lwp.h>

#if (defined(_KERNEL) || defined(_KMEMUSER)) && defined(_MACHDEP)
#include <sys/machparam.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The light-weight process object and the methods by which it
 * is accessed.
 */

#define	MAXSYSARGS	8	/* Maximum # of arguments passed to a syscall */

/* lwp_eosys values */
#define	NORMALRETURN	0	/* normal return; adjusts PC, registers */
#define	JUSTRETURN	1	/* just return, leave registers alone */
#define	RESTARTSYS	2	/* back up PC, restart system call */

/*
 * Resource usage, per-lwp plus per-process (sum over defunct lwps).
 */
struct lrusage {
	u_long	minflt;		/* minor page faults */
	u_long	majflt;		/* major page faults */
	u_long	nswap;		/* swaps */
	u_long	inblock;	/* input blocks */
	u_long	oublock;	/* output blocks */
	u_long	msgsnd;		/* messages sent */
	u_long	msgrcv;		/* messages received */
	u_long	nsignals;	/* signals received */
	u_long	nvcsw;		/* voluntary context switches */
	u_long	nivcsw;		/* involuntary context switches */
	u_long	sysc;		/* system calls */
	u_long	ioch;		/* chars read and written */
};

typedef struct _klwp	*klwp_id_t;

typedef struct _klwp {
	/*
	 * user-mode context
	 */
	struct pcb	lwp_pcb;		/* user regs save pcb */
	struct ucontext *lwp_oldcontext;	/* previous user context */

	/*
	 * system-call interface
	 */
	int	*lwp_ap;	/* pointer to arglist */
	int	lwp_errno;	/* error for current syscall (private) */
	/*
	 * support for I/O
	 */
	char	lwp_error;	/* return error code */
	char	lwp_eosys;	/* special action on end of syscall */
	char	lwp_argsaved;	/* are all args in lwp_arg */
	int	lwp_arg[MAXSYSARGS];	/* args to current syscall */
	void	*lwp_regs;	/* pointer to saved regs on stack */
	void	*lwp_fpu;	/* pointer to fpu regs */
	label_t	lwp_qsav;	/* longjmp label for quits and interrupts */

	/*
	 * signal handling and debugger interface
	 */
	u_char	lwp_cursig;		/* current signal */
	u_char	lwp_curflt;		/* current fault */
	u_char	lwp_sysabort;		/* if set, abort syscall */
	u_char	lwp_asleep;		/* lwp asleep in syscall */
	stack_t lwp_sigaltstack;	/* alternate signal stack */
	struct sigqueue *lwp_curinfo;	/* siginfo for current signal */
	k_siginfo_t	lwp_siginfo;	/* siginfo for stop-on-fault */
	k_sigset_t	lwp_sigoldmask;	/* for sigsuspend */

	/*
	 * profiling. p_prlock in the proc is used in the event of multiple
	 * lwp's using the same profiling base/size.
	 */
	struct prof {			/* profile arguments */
		short	*pr_base;	/* buffer base */
		unsigned pr_size;	/* buffer size */
		unsigned pr_off;	/* pc offset */
		unsigned pr_scale;	/* pc scaling */
	} lwp_prof;
	clock_t	lwp_scall_start;	/* stime at start of syscall */

	/*
	 * Microstate accounting.  Timestamps are made at the start and the
	 * end of each microstate (see <sys/msacct.h> for state definitions)
	 * and the corresponding accounting info is updated.  The current
	 * microstate is kept in the thread struct, since there are cases
	 * when one thread must update another thread's state (a no-no
	 * for an lwp since it may be swapped/paged out).  The rest of the
	 * microstate stuff is kept here to avoid wasting space on things
	 * like kernel threads that don't have an associated lwp.
	 */
	struct mstate {
		int ms_prev;			/* previous running mstate */
		hrtime_t ms_start;		/* lwp creation time */
		hrtime_t ms_term;		/* lwp termination time */
		hrtime_t ms_state_start;	/* start time of this mstate */
		hrtime_t ms_acct[NMSTATES];	/* per mstate accounting */
	} lwp_mstate;

	/*
	 * Per-lwp resource usage.
	 */
	struct lrusage lwp_ru;

	/*
	 * Things to keep for real-time (SIGPROF) profiling.
	 */
	int	lwp_lastfault;
	caddr_t	lwp_lastfaddr;

	/*
	 * timers. Protected by lwp->procp->p_lock
	 */
	struct itimerval lwp_timer[3];

	/*
	 * used to stop/alert lwps
	 */
	char	lwp_oweupc;	/* owe a profiling tick on next AST */
	char	lwp_state;	/* Running in User/Kernel mode (no lock req) */
	u_short	lwp_nostop;	/* Don't stop this lwp (no lock required) */
	kcondvar_t lwp_cv;

	/*
	 * Per-lwp time.
	 */
	clock_t	lwp_utime;	/* time spent at user level */
	clock_t lwp_stime;	/* time spent at system level */

	/*
	 * linkage
	 */
	struct _kthread	*lwp_thread;
	struct proc	*lwp_procp;
} klwp_t;

/* lwp states */
#define	LWP_USER	0x01		/* Running in user mode */
#define	LWP_SYS		0x02		/* Running in kernel mode */

#define	LWPNULL (klwp_t *)0

#if	defined(_KERNEL)
extern	int	lwp_default_stksize;
extern	int	lwp_reapcnt;

struct	_kthread_t;
extern	struct _kthread *lwp_deathrow;

#endif	/* defined(_KERNEL) */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KLWP_H */
