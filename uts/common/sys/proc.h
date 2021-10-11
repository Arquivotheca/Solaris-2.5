/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_PROC_H
#define	_SYS_PROC_H

#pragma ident	"@(#)proc.h	1.85	95/08/22 SMI"	/* SVr4.0 11.65 */

#include <sys/time.h>

#include <sys/thread.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/timer.h>
#if defined(i386) || defined(__i386)
#include <sys/segment.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * One structure allocated per active process.  It contains all
 * data needed about the process while the process may be swapped
 * out.  Other per-process data (user.h) is also inside the proc structure.
 * Lightweight-process data (lwp.h) and the kernel stack may be swapped out.
 */
typedef struct	proc {
	/*
	 * Fields requiring no explicit locking
	 */
	struct	vnode *p_exec;		/* pointer to a.out vnode */
	struct	as *p_as;		/* process address space pointer */
	kmutex_t p_lock;		/* proc struct's mutex lock */
	kmutex_t p_crlock;		/* lock for p_cred */
	struct	cred	*p_cred;	/* process credentials */
	/*
	 * Fields protected by pidlock
	 */
	int	p_swapcnt;		/* number of swapped out lwps */
	char	p_stat;			/* status of process */
	char	p_wcode;		/* current wait code */
	int 	p_wdata;		/* current wait return value */
	pid_t	p_ppid;			/* process id of parent */
	struct	proc	*p_link;	/* forward link */
	struct	proc	*p_parent;	/* ptr to parent process */
	struct	proc	*p_child;	/* ptr to first child process */
	struct	proc	*p_sibling;	/* ptr to next sibling proc on chain */
	struct	proc	*p_psibling;	/* ptr to prev sibling proc on chain */
	struct	proc	*p_sibling_ns;	/* prt to siblings with new state */
	struct	proc	*p_child_ns;	/* prt to children with new state */
	struct	proc 	*p_next;	/* active chain link next */
	struct	proc 	*p_prev;	/* active chain link prev */
	struct	proc 	*p_nextofkin;	/* gets accounting info at exit */
	struct	proc 	*p_orphan;
	struct	proc 	*p_nextorph;
	struct	proc	*p_pglink;	/* process group hash chain link next */
	struct	proc	*p_ppglink;	/* process group hash chain link prev */
	struct	sess	*p_sessp;	/* session information */
	struct	pid 	*p_pidp;	/* process ID info */
	struct	pid 	*p_pgidp;	/* process group ID info */
	/*
	 * Fields protected by p_lock
	 */
	kcondvar_t p_cv;		/* proc struct's condition variable */
	kcondvar_t p_flag_cv;
	kcondvar_t p_lwpexit;		/* waiting for some lwp to exit */
	kcondvar_t p_holdlwps;		/* process is waiting for its lwps */
					/* to to be held.  */
	kcondvar_t p_flock;		/* asleep in closef */
	u_short	p_prwant;		/* /proc: process wanted for locking */
	u_int	p_flag;			/* protected while set. */
					/* flags defined below */
	clock_t	p_utime;		/* user time, this process */
	clock_t	p_stime;		/* system time, this process */
	clock_t	p_cutime;		/* sum of children's user time */
	clock_t	p_cstime;		/* sum of children's system time */
	caddr_t *p_segacct;		/* segment accounting info */
	caddr_t	p_brkbase;		/* base address of heap */
	u_int	p_brksize;		/* heap size in bytes */
	/*
	 * Per process signal stuff.
	 */
	k_sigset_t p_sig;		/* signals pending to this process */
	k_sigset_t p_ignore;		/* ignore when generated */
	k_sigset_t p_siginfo;		/* gets signal info with signal */
	struct sigqueue *p_sigqueue;	/* queued siginfo structures */
	struct sigqhdr *p_sigqhdr;	/* hdr to sigqueue structure pool */
	u_char	p_stopsig;		/* jobcontrol stop signal */
	/*
	 * Per process lwp and kernel thread stuff
	 */
	int	p_lwptotal;		/* total number of lwps created */
	int 	p_lwpcnt;		/* number of lwps in this process */
	int	p_lwprcnt;		/* number of not stopped lwps */
	int	p_lwpblocked;		/* number of blocked lwps. kept  */
					/*    consistent by sched_lock() */
	int	p_zombcnt;		/* number of zombie LWPs */
	kthread_t *p_tlist;		/* circular list of threads */
	kthread_t *p_zomblist;		/* circular list of zombie LWPs */
	/*
	 * XXX Not sure what locks are needed here.
	 */
	k_sigset_t p_sigmask;		/* mask of traced signals (/proc) */
	k_fltset_t p_fltmask;		/* mask of traced faults (/proc) */
	struct	vnode *p_trace;		/* pointer to primary /proc vnode */
	struct	vnode *p_plist;		/* list of /proc vnodes for process */

	struct	proc  *p_rlink;		/* linked list for server */
	kcondvar_t p_srwchan_cv;
	u_int	p_stksize;		/* process stack size in bytes */
	/*
	 * Microstate accounting, resource usage, and real-time profiling
	 */
	hrtime_t p_mstart;		/* hi-res process start time */
	hrtime_t p_mterm;		/* hi-res process termination time */
	hrtime_t p_mlreal;		/* elapsed time sum over defunct lwps */
	hrtime_t p_acct[NMSTATES];	/* microstate sum over defunct lwps */
	struct lrusage p_ru;		/* lrusage sum over defunct lwps */
	struct itimerval p_rprof_timer;	/* ITIMER_REALPROF interval timer */
	int	p_rprof_timerid;	/* interval timer's timeout id */
	u_int	p_defunct;		/* number of defunct lwps */
	/*
	 * profiling. A lock is used in the event of multiple lwp's
	 * using the same profiling base/size.
	 */
	kmutex_t p_pflock;		/* protects user pr_base in lwp */

	/*
	 * The user structure
	 */
	struct user p_user;		/* (see sys/user.h) */

	/*
	 * Doors.
	 */
	kthread_t		*p_server_threads;
	struct door_node	*p_unref_list;
	kcondvar_t		p_server_cv;
	char			p_unref_thread;	/* unref thread created */

	/*
	 * Kernel probes
	 */
	u_char			p_tnf_flags;

	/*
	 * C2 Security  (C2_AUDIT)
	 */
	caddr_t p_audit_data;		/* per process audit structure */
	kthread_t	*p_aslwptp;	/* thread ptr representing "aslwp" */
#if defined(i386) || defined(__i386)
	/*
	 * LDT support.
	 */
	kmutex_t p_ldtlock;		/* protects the following fields */
	struct seg_desc *p_ldt;		/* Pointer to private LDT */
	struct seg_desc p_ldt_desc;	/* segment descriptor for private LDT */
	int p_ldtlimit;			/* highest selector used */
#endif
	u_int p_swrss;			/* resident set size before last swap */
	struct aio	*p_aio;		/* pointer to async I/O struct */
	struct timerstr	*p_itimer;	/* posix timers */
	k_sigset_t	p_notifsigs;	/* signals in notification set */
	kcondvar_t	p_notifcv;	/* notif cv to synchronize with aslwp */
	int		p_alarmid;	/* alarm's timeout id */
} proc_t;

#define	PROC_T				/* headers relying on proc_t are OK */

/* active process chain */

extern proc_t *practive;

/* Well known processes */

extern proc_t *proc_sched;		/* memory scheduler */
extern proc_t *proc_init;		/* init */
extern proc_t *proc_pageout;		/* pageout daemon */
extern proc_t *proc_fsflush;		/* filesystem sync-er */


/*
 * Stuff to keep track of the number of processes each uid has.
 *
 * A structure is allocated when a new uid shows up
 * There is a hash to find each uid's structure.
 */
struct	upcount	{
	struct	upcount	*up_next;
	uid_t		up_uid;
	u_long		up_count;
};

/* process ID info */

struct pid {
	unsigned int pid_prinactive :1;
	unsigned int pid_pgorphaned :1;
	unsigned int pid_padding :6;	/* used to be pid_ref, now an int */
	unsigned int pid_prslot :24;
	pid_t pid_id;
	struct proc *pid_pglink;
	struct pid *pid_link;
	u_int pid_ref;
};

extern proc_t p0;	/* process 0 */
extern struct pid pid0;

#define	p_pgrp p_pgidp->pid_id
#define	p_pid  p_pidp->pid_id
#define	p_slot p_pidp->pid_prslot
#define	p_detached p_pgidp->pid_pgorphaned

#define	PID_HOLD(pidp)	ASSERT(MUTEX_HELD(&pidlock)); \
			++(pidp)->pid_ref;
#define	PID_RELE(pidp)	ASSERT(MUTEX_HELD(&pidlock)); \
			(pidp)->pid_ref > 1 ? \
				--(pidp)->pid_ref : pid_rele(pidp);

/* stat codes */

#define	SSLEEP	1		/* awaiting an event */
#define	SRUN	2		/* running */
#define	SZOMB	3		/* process terminated but not waited for */
#define	SSTOP	4		/* process stopped by debugger */
#define	SIDL	5		/* intermediate state in process creation */
#define	SONPROC	6		/* process is being run on a processor */

/* flag codes */

/* XXX - several of these should be per-LWP. */

#define	SSYS	0x00000001	/* system (resident) process */
#define	STRC	0x00000002	/* ptrace() compatibility mode set by /proc */
#define	SNWAKE	0x00000004	/* process cannot be awakened by a signal */
#define	SLOAD	0x00000008	/* in core */
#define	SLOCK	0x00000010	/* process cannot be swapped */
#define	SPREXEC	0x00000020	/* process is in exec() (a flag for /proc) */
#define	SPROCTR	0x00000040	/* signal, fault or syscall tracing via /proc */
#define	SPRFORK	0x00000080	/* child inherits /proc tracing flags */
#define	SKILLED	0x00000100	/* SIGKILL has been posted to the process */
#define	SULOAD	0x00000200	/* u-block in core */
#define	SRUNLCL	0x00000400	/* set process running on last /proc close */
#define	SBPTADJ	0x00000800	/* adjust pc on breakpoint trap (/proc) */
#define	SKILLCL	0x00001000	/* kill process on last /proc close */
#define	SOWEUPC	0x00002000	/* owe process an addupc() call at next ast */
#define	SEXECED	0x00004000	/* this process has execed */
#define	SPASYNC	0x00008000	/* asynchronous stopping via /proc */
#define	SJCTL	0x00010000	/* SIGCLD sent when children stop/continue */
#define	SNOWAIT	0x00020000	/* children never become zombies */
#define	SVFORK	0x00040000	/* process resulted from vfork */
#define	SVFDONE	0x00080000	/* vfork child releasing parent as */
#define	EXITLWPS 0x00100000	/* have lwps exit within the process */
#define	HOLDLWPS 0x00200000	/* hold lwps where they're cloneable */
#define	SWAITSIG 0x00400000	/* SIGWAITING sent when all lwps block */
#define	HOLDLWP2 0x00800000	/* hold lwps in place (not cloning) */
#define	COREDUMP 0x01000000	/* process is dumping core */
#define	SMSACCT	0x02000000	/* process is keeping micro-state accounting */
#define	ASLWP 	0x04000000 	/* process uses an "aslwp" for async signals */
				/* also overloaded to mean it is a MT process */
#define	SPRLOCK	0x08000000	/* process locked by /proc */
#define	NOCD	0x10000000	/* new creds from VSxID, do not coredump */

/* Macro to convert proc pointer to a user block pointer */
#define	PTOU(p)		(&(p)->p_user)

#define	tracing(p, sig)	(sigismember(&(p)->p_sigmask, sig))

/* Macro to reduce unnecessary calls to issig() */

#define	ISSIG(t, why)	ISSIG_FAST(t, ttolwp(t), ttoproc(t), why)

/*
 * Fast version of ISSIG.
 *	1. uses register pointers to lwp and proc instead of reloading them.
 *	2. uses bit-wise OR of tests, since the usual case is that none of them
 *	   are true, this saves orcc's and branches.
 *	3. load the signal flags instead of using sigisempty() macro which does
 *	   a branch to convert to boolean.
 */
#define	ISSIG_FAST(t, lwp, p, why)		\
	(ISSIG_PENDING(t, lwp, p) && issig(why))

#define	ISSIG_PENDING(t, lwp, p)		\
	((lwp)->lwp_cursig |			\
	    (p)->p_sig.__sigbits[0] |		\
	    (p)->p_sig.__sigbits[1] |		\
	    (t)->t_sig.__sigbits[0] |		\
	    (t)->t_sig.__sigbits[1] |		\
	    (p)->p_stopsig |			\
	    ((t)->t_proc_flag & (TP_PRSTOP|TP_CHKPT)) |	\
	    ((p)->p_flag & (EXITLWPS|SKILLED|HOLDLWP2)))

#define	ISSTOP(sig)	 (u.u_signal[sig-1] == SIG_DFL && \
				sigismember(&stopdefault, sig))

#define	ISHOLD(p)	(((p)->p_flag & HOLDLWPS) || \
				(curthread->t_proc_flag & TP_HOLDLWP))

/* Reasons for calling issig() */

#define	FORREAL		0	/* Usual side-effects */
#define	JUSTLOOKING	1	/* Don't stop the process */

#ifdef _KERNEL

/* process management functions */

extern caddr_t findvaddr(proc_t *);
extern paddr_t vtop(caddr_t, proc_t *);
extern void pexit(void);
extern int newproc(void (*)(), id_t, int);
extern void vfwait(pid_t);
extern void freeproc(proc_t *);
extern void setrun(kthread_t *);
extern void setrun_locked(kthread_t *);
extern void exit(int, int);
extern void relvm(proc_t *);
extern void add_ns(proc_t *, proc_t *);
extern void delete_ns(proc_t *, proc_t *);
extern void upcount_inc(uid_t);
extern void upcount_dec(uid_t);
extern int upcount_get(uid_t);
#if defined(i386) || defined(__i386)
extern int ldt_dup(proc_t *, proc_t *);
#endif

extern void sigcld(proc_t *);
extern int fsig(k_sigset_t *, kthread_t *);
extern void psig(void);
extern void stop(kthread_t *, int, int);
extern int stop_on_fault(u_int, k_siginfo_t *);
extern int issig(int);
extern int jobstopped(proc_t *);
extern void psignal(proc_t *, int);
extern void tsignal(kthread_t *, int);
extern void sigtoproc(proc_t *, kthread_t *, int, int);
extern void trapsig(k_siginfo_t *, int);
extern int eat_signal(kthread_t *t, int sig);

extern void pid_setmin(void);
extern pid_t pid_assign(proc_t *);
extern int pid_rele(struct pid *);
extern void pid_exit(proc_t *);
extern void proc_entry_free(struct pid *);
extern proc_t *prfind(pid_t);
extern proc_t *pgfind(pid_t);
extern void pid_init(void);
extern proc_t *pid_entry(int);
extern int pid_slot(proc_t *);
extern void signal(pid_t, int);
extern void prsignal(struct pid *, int);

extern void pgsignal(struct pid *, int);
extern void pgjoin(proc_t *, struct pid *);
extern void pgcreate(proc_t *);
extern void pgexit(proc_t *);
extern void pgdetach(proc_t *);
extern int pgmembers(pid_t);

extern	void	init_mstate(kthread_t *, int);
extern	int	new_mstate(kthread_t *, int);
extern	void	restore_mstate(kthread_t *);
extern	void	term_mstate(kthread_t *);
extern	void	estimate_msacct(kthread_t *, hrtime_t);
extern	void	disable_msacct(proc_t *);

extern	u_short	cpu_decay(u_short, clock_t);
extern	u_short	cpu_grow(u_short, clock_t);

extern void	set_proc_pre_sys(proc_t *p);
extern void	set_proc_post_sys(proc_t *p);
extern void	set_proc_sys(proc_t *p);
extern void	set_proc_ast(proc_t *p);
extern void	set_all_proc_sys(void);

/* thread function prototypes */

extern	kthread_t	*thread_create(
	caddr_t		stk,
	int		stksize,
	void		(*proc)(),
	caddr_t		arg,
	int		len,
	proc_t 		*pp,
	int		state,
	int		pri);
extern	void	thread_exit(void);
extern	void	thread_destroy(kthread_t *);
extern	void	thread_free(kthread_t *);
extern	int	reaper(void);
extern	void	savectx(kthread_t *);
extern	void	restorectx(kthread_t *);
extern	void	forkctx(kthread_t *, kthread_t *);
extern	void	freectx(kthread_t *);
extern	kthread_t *thread_unpin(void);
extern	void	release_interrupt(void);
extern	int	thread_create_intr(struct cpu *);
extern	void	thread_init(void);
extern	int	thread_load(kthread_t *, void (*)(), caddr_t, int);

extern	void	tsd_create(u_int *, void (*)(void *));
extern	void	tsd_destroy(u_int *);
extern	void	*tsd_getcreate(u_int *, void (*)(void *), void *(*)(void));
extern	void	*tsd_get(u_int);
extern	int	tsd_set(u_int, void *);
extern	void	tsd_exit(void);

/* lwp function prototypes */

extern	klwp_t 		*lwp_create(
	void		(*proc)(),
	caddr_t		arg,
	int		len,
	proc_t		*p,
	int		state,
	int		pri,
	k_sigset_t	smask,
	int		cid);
extern	void	lwp_exit(void);
extern	int	lwp_suspend(kthread_t *);
extern	void	lwp_continue(kthread_t *);
extern	void	holdlwp(void);
extern	void	stoplwp(void);
extern	int	holdlwps(proc_t *, int);
extern	void	pokelwps(proc_t *);
extern	void	continuelwps(proc_t *);
extern	void	exitlwps(proc_t *, int);
extern	klwp_t	*forklwp(klwp_t *, proc_t *);
extern	int	sigisheld(proc_t *, int);
extern	int	sigwaiting_check(kmutex_t *);
extern	void	sigwaiting_decr(void);
extern	void	lwp_load(klwp_t *, ucontext_t *);
extern	void	lwp_setrval(klwp_t *, int, int);
extern	void	lwp_forkregs(klwp_t *, klwp_t *);
extern	void	lwp_freeregs(klwp_t *);
extern	caddr_t	lwp_stk_init(klwp_t *, caddr_t);

/*
 * Signal queue function prototypes. Must be here due to header ordering
 * dependencies.
 */
extern void sigqfree(proc_t *);
extern int sigdeq(proc_t *, kthread_t *, int, sigqueue_t **);
extern void sigdelq(proc_t *, kthread_t *, int);
extern void sigaddq(proc_t *, kthread_t *, k_siginfo_t *, int);
extern void sigaddqa(proc_t *, kthread_t *, sigqueue_t *);
extern void sigdupq(proc_t *, proc_t *);
extern sigqueue_t *sigappend(k_sigset_t *, sigqueue_t *,
	k_sigset_t *, sigqueue_t *);
extern sigqueue_t *sigprepend(k_sigset_t *, sigqueue_t *,
	k_sigset_t *, sigqueue_t *);
extern void winfo(proc_t *, k_siginfo_t *, int);
extern int sendsig(int, k_siginfo_t *, void (*)());

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PROC_H */
