/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ifndef _LIBTHREAD_H
#define	_LIBTHREAD_H

#pragma ident	"@(#)libthread.h	1.82	95/10/06 SMI"

/*
 * libthread.h:
 *	struct thread and struct lwp definitions.
 */
#include <signal.h>
#include <siginfo.h>
#include <sys/ucontext.h>
#include <sys/reg.h>
#include <sys/param.h>
#include <sys/asm_linkage.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <thread.h>
#include <sys/synch.h>
#include <synch32.h>
#include <sys/lwp.h>
#include <utrace.h>
#include <debug.h>
#include <machlibthread.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * thread stack layout.
 *
 *	-----------------	high address
 *	|		|
 *	| struct thread |
 *	|		|
 *	|		|
 *	|		|
 *	|	tls	|
 *	|		|
 *	-----------------
 *	|		| <- 	thread stack bottom
 *	|		|
 *	|		|
 *	|		|
 *	|		|
 *	-----------------	low address
 */

/*
 * default stack allocation parameters
 *
 * DEFAULTSTACK is defined as 1mb
 */
#define	DEFAULTSTACK	0x100000	/* 1 MB stacks */
#define	DAEMON_STACK	0x2000		/* 8K stacks for daemons */

#define	BPW 32		/* number of bits per word */
#define	DEFAULTSTACKINCR	8
#define	MAXSTACKS		16
#ifdef TLS
extern int _etls;
#define	MINSTACK		(SA(MINFRAME + (int)&_etls))
#else
#define	MINSTACK		(SA(sizeof (struct thread) + 512))
#endif

/*
 * default stack cache definition.
 */
typedef struct _stkcache {
	int size;
	char *next;
	int busy;
	cond_t cv;
} stkcache_t;

extern stkcache_t _defaultstkcache;
extern mutex_t _stkcachelock;

/*
 * thread priority range.
 */
#define	THREAD_MIN_PRIORITY	0		/* minimum scheduling pri */
#define	THREAD_MAX_PRIORITY	127		/* max scheduling priority */
#define	NPRI			(THREAD_MAX_PRIORITY - THREAD_MIN_PRIORITY+1)
#define	MAXRUNWORD		(NPRI/BPW)
#define	IDLE_THREAD_PRI		-1		/* idle thread's priority */

/*
 * Is thread temporarily bound to this LWP?
 */
#define	ISTEMPBOUND(t)	(((t)->t_flag & T_TEMPBOUND))

/*
 * used to implement a callout mechanism.
 */
typedef struct callo {
	thread_t tid;
	char flag;
	char running;
	cond_t waiting;
	struct timeval time;
	void (*func)();
	int arg;
	struct callo *forw;
	struct callo *backw;
} callo_t;

/* callout flags */
#define	CO_TIMER_OFF	0
#define	CO_TIMEDOUT	1
#define	CO_TIMER_ON	2

#define	ISTIMEDOUT(x)	((x)->flag == CO_TIMEDOUT)

typedef	char	thstate_t;

/*
 * thread internal structure
 *
 * READ THIS NOTE IF YOU'RE PLANNING ON ADDING ANYTHING TO THIS
 * STRUCTURE:
 *	libaio is dependent on the size of struct thread. if you
 *	are enlarging its size, always make sure that the "structure
 *	aio_worker" contains enough padding to hold a thread struct.
 *	you'll see that the first field of this structure is a character
 *	array of some number of pad bytes. you'll find the aio_worker
 *	structure in lib/libaio/common/libaio.h.
 */
typedef struct thread {
	struct thread	*t_link;	/* run/sleep queue */
	char		*t_stk;		/* stack base */
	unsigned int	t_stksize;	/* size of stack */
	char		*t_tls;		/* pointer to thread local storage */
	resumestate_t	t_resumestate;	/* any extra state needed by resume */
	long		t_startpc;	/* start func called by thr_create() */
	thread_t	t_tid;		/* thread id */
	lwpid_t		t_lwpid;	/* lwp id */
	int		t_usropts;	/* usr options, (THR_BOUND, ...) */
	int		t_flag;		/* flags, (T_ALLOCSTK, T_PARK) */
	_cleanup_t	*t_clnup_hdr;	/* head to cleanup handlers list */
	int		t_pri;		/* scheduling priority */
	/* Keep following 8 fields together. See _clean_thread() */
	thstate_t	t_state;	/* thread state */
	char		t_nosig;	/* block signal handlers */
	char		t_stop;		/* stop thread when set */
	char		t_preempt;	/* preempt thread when set */
	char		t_schedlocked;	/* flag set, thread holding schedlock */
	char		t_bdirpend;	/* pending directed bounced signals */
	char		t_pending;	/* set when t_psig is not empty */
	char		t_sig;		/* signal rcvd in critical section */
	/* cancel stuff - keep follwing 4 fields togather */
	char		t_can_pending;
	char		t_can_state;
	char		t_can_type;
	char		t_cancelable;
	/* cancel stuff word finish */
	sigset_t	t_hold;		/* per thread signal mask */
	sigset_t	t_psig;		/* pending signals */
	sigset_t	t_ssig;		/* signals sent, still pending */
	sigset_t	t_bsig;		/* sigs bounced to this thread's lwp */
	char		*t_wchan;	/* sleep wchan */
	void		*t_exitstat;	/* exit status - non-detached threads */
	mutex_t		*t_handoff;	/* mutex hand off for cv_signal() */
	lwp_sema_t	t_park;		/* used to park threads */
	sigset_t	t_olmask;	/* lwp mask when deferred sig taken */
	siginfo_t	t_si;		/* siginfo for deferred signal */
	struct thread	*t_idle;	/* pointer to an idle thread */
	struct callo	t_itimer_callo;	/* alarm callout (per thread) */
	struct callo	t_cv_callo;	/* cv_timedwait callout (per thread) */
	struct itimerval t_realitimer;	/* real time interval timer */
	struct thread	*t_next;	/* circular queue of all threads */
	struct thread	*t_prev;
	/* only multiplexing threads use the following fields */
	mutex_t		t_lock;		/* locked when loaded into a LWP */
	struct thread	*t_iforw;	/* circular queue of idling threads */
	struct thread	*t_ibackw;
	struct thread	*t_forw;	/* circular queue of ONPROC threads */
	struct thread	*t_backw;
	int		t_errno;	/* thread specific errno */
	int		t_rtldbind;	/* dynamic linking flags */
	/* PROBE_SUPPORT begin */
	void		*t_tpdp;	/* thread probe data pointer */
	/* PROBE_SUPPORT end */
} uthread_t;

/*
 * thread states
 */
#define	TS_SLEEP	1
#define	TS_RUN		2
#define	TS_DISP		3
#define	TS_ONPROC	4
#define	TS_STOPPED	5
#define	TS_ZOMB		6

/*
 * t_flag values
 */
#define	T_ALLOCSTK	0x1	/* thread library allocated thread's stack */
#define	T_LWPDIRSIGS	0x2	/* thread has called setitimer(2) VIRT, PROF */
#define	T_PARKED	0x4	/* thread is parked on its LWP */
#define	T_PREEMPT	0x8	/* thread has a pending preemption */
#define	T_DONTPREEMPT	0x10	/* suspend pre-emption until cleared */
#define	T_INTR		0x20	/* sleep interrupted by an unmasked signal */
#define	T_IDLETHREAD	0x40	/* thread is an idle thread */
#define	T_INSIGLWP	0x80	/* thread is in siglwp handler */
#define	T_IDLE		0x100	/* thread is idle */
#define	T_OFFPROC	0x200	/* thread is dispatchable */
#define	T_ZOMBIE	0x400	/* thread is on zombie queue */
#define	T_SIGWAIT	0x800	/* thread is in a sigwait(2) */
#define	T_TEMPBOUND	0x1000	/* thread is temporarily bound to the lwp */
#define	T_INTERNAL	0x2000	/* an internal libthread daemon thread */
#define	T_2BZOMBIE	0x4000	/* thread is on the way to zombie queue */
#define	T_BSSIG		0x8000	/* thread's lwp has pending bounced signals */
#define	T_WAITCV	0x10000	/* thread is/was asleep for a condition var */
#define	T_EXUNWIND	0x20000	/* thread is exiting due to cancellation */

/*  t_can_state values */
#define	TC_DISABLE		-1	/* disable cancellation */
#define	TC_ENABLE		00	/* enable cancellation */

/*  t_can_type values */
#define	TC_ASYNCHRONOUS		-1	/* async cancelable */
#define	TC_DEFERRED		00	/* deferred cancelable */

/*  t_cancelable values */
#define	TC_CANCELABLE		-1	/* thread is in cancellation point */

/*  t_can_pending values */
#define	TC_PENDING		-1	/* cancellation pending on thread */

/*
 * Checks if thread was created with the DETACHED flag set.
 */
#define	DETACHED(t)	((t)->t_usropts & THR_DETACHED)

/*
 * Is thread permanently bound to a LWP?
 */
#define	ISBOUND(t)	((t)->t_usropts & THR_BOUND)

/*
 * Is thread parked on its LWP?
 */
#define	ISPARKED(t)	((t)->t_flag & T_PARKED)

/*
 * Does this thread have a preemption pending ?
 */
#define	PREEMPTED(t)	((t)->t_flag & T_PREEMPT)

/*
 * Is this thread stopped ?
 */
#define	STOPPED(t)	((t)->t_stop)

/*
 * Was thread created to be an idle thread?
 */
#define	IDLETHREAD(t)	((t)->t_flag & T_IDLETHREAD)

/*
 * Is thread on idle queue?
 */
#define	ON_IDLE_Q(t)	((t)->t_iforw)

/*
 * Is thread at some cancellation point?
 */
#define	CANCELABLE(t)		((t)->t_cancelable == TC_CANCELABLE)

/*
 * Is thread cancellation pending?
 */
#define	CANCELPENDING(t)	((t)->t_can_pending == TC_PENDING)

/*
 * Is thread cancellation enabled/disable?
 */
#define	CANCELENABLE(t)		((t)->t_can_state == TC_ENABLE)
#define	CANCELDISABLE(t)	((t)->t_can_state == TC_DISABLE)

/*
 * Is thread deferred/async cancelable?
 */
#define	CANCELDEFERED(t)	((t)->t_can_type == TC_DEFERRED)
#define	CANCELASYNC(t)		((t)->t_can_type ==  TC_ASYNCHRONOUS)

/*
 * Is thread on/off the _onprocq ?
 */
#define	ONPROCQ(t) ((t)->t_forw != NULL && (t)->t_backw != NULL)
#define	OFFPROCQ(t) ((t)->t_forw == NULL && (t)->t_backw == (t)->t_forw)

/* Assumes p >= 0 */
#define	HASH_PRIORITY(p, index) index = p - THREAD_MIN_PRIORITY; \

#define	_lock_bucket(ix) \
if (ix != -1) _lmutex_lock(&(_allthreads[ix].lock)); else

#define	_unlock_bucket(ix) \
if (ix != -1) _lmutex_unlock(&(_allthreads[ix].lock)); else

/* convert a thread_t to a struct thread pointer */
#define	THREAD(x)	(((x) == 0) ? curthread : (uthread_t *)_idtot(x))
/*
 * A relative time increment used to set the absolute time "cond_eot" to be
 * sufficiently far into the future. cond_eot is used to validate the timeout
 * argument to cond_timedwait().
 */
#define	COND_REL_EOT 50000000

/* convert a thread to its lwpid */
#define	LWPID(t)	(t)->t_lwpid


#define	ALLTHR_TBLSIZ 512
#define	HASH_TID(tid) ((tid) == 0 ? -1 : (tid) % ALLTHR_TBLSIZ)

typedef struct thrtab {
	uthread_t *first;
	mutex_t   lock;
} thrtab_t;

extern thrtab_t _allthreads[]; 	/* doubly linked list of all threads */
extern thread_t _lasttid;	/* monotonically increasing global tid count */
extern mutex_t _tidlock;	/* protects access to _lasttid */
extern int _totalthreads;	/* total number of threads created */
extern int _userthreads;	/* number of user created threads */
extern int _u2bzombies;		/* u threads on their way 2b zombies */
extern int _d2bzombies;		/* daemon threads on their way 2b zombies */
extern long _idlethread;	/* address of IDLETHREAD */
extern uthread_t *_nidle;	/* list of idling threads */
extern int _nidlecnt;		/* number of threads idling */
extern int _onprocq_size;	/* size of onproc Q */
extern int _nagewakecnt;	/* number of awakened aging threads */
extern int _naging;		/* number of aging threads running */
extern int _minlwps;		/* min number of idle lwps */
extern int _nlwps;		/* number of lwps in this pool. */
extern int _ndie;		/* number of lwps to delete from this pool. */
extern int _nrunnable;		/* number of threads on the run queue */
extern int _nthreads;		/* number of unbound threads */
extern int _sigwaitingset;	/* 1 if sigwaiting enabled; 0 if disabled */
extern struct thread *_lowestpri_th;	/* the lowest priority running thread */
extern lwp_mutex_t _schedlock;	/* protects runqs and sleepqs */
extern uthread_t *_onprocq;	/* circular queue of ONPROC threads */
extern uthread_t *_zombies;	/* circular queue of zombie threads */
extern cond_t _zombied;		/* waiting for zombie threads */
extern int _zombiecnt;		/* nunber of zombied threads */
extern lwp_cond_t _suspended;	/* waiting for thread to suspend */
extern int _reapcnt;		/* number of threads to be reaped */
extern lwp_mutex_t _reaplock;	/* reaper thread's lock */
extern lwp_cond_t _untilreaped;	/* wait until _reapcnt < high mark */
extern int _lpagesize;		/* libthread pagesize initialized in _t0init */
extern sigset_t _allmasked;	/* all maskable signals except SIGLWP masked */
extern sigset_t _totalmasked;	/* all maskable signals masked */
extern int _maxpriq;		/* index of highest priority dispq */
extern long _dqactmap[];	/* bit map of priority queues */

extern int _timerset;		/* interval timer is set if timerset == 1 */
extern thread_t _co_tid;	/* thread that does the callout processing */
extern int _co_set;		/* create only one thread to run callouts */
extern int _calloutcnt;		/* number of pending callouts */
extern callo_t *_calloutp;	/* pointer to the callout queue */
extern mutex_t _calloutlock;	/* protects queue of callouts */
extern thread_t __dynamic_tid;	/* bound thread that handles SIGWAITING */
extern lwp_cond_t _aging;	/* condition on which threads age */

extern uthread_t *_sched_owner;
extern int _sched_ownerpc;
extern struct thread _thread;
extern struct	thread *_t0;	/* the initial thread */

extern int _libpthread_loaded;	/* indicates whether libpthread is loaded */
extern struct sigaction __alarm_sigaction; /* global sigaction for SIGALRM */
extern int _first_thr_create;

#ifdef TLS
#ifndef NOTHREAD
#pragma unshared (_thread);
#endif
#define	curthread (&_thread)
#else
extern uthread_t *_curthread();
#define	curthread (_curthread())
#endif


/*
 * global mutexes, read-write and condition locks.
 */
extern mutex_t _schedlock;	/* protects runq and sleepq */
extern lwp_mutex_t _sighandlerlock;	/* protects signal handlers */
extern rwlock_t _lrw_lock;

/*
 * sleep-wakeup hashing:  Each entry in slpq[] points
 * to the front and back of a linked list of sleeping processes.
 * Processes going to sleep go to the back of the appropriate
 * sleep queue and wakeprocs wakes them from front to back (so the
 * first process to go to sleep on a given channel will be the first
 * to run after a wakeup on that channel).
 * NSLEEPQ must be a power of 2.  Sqhash(x) is used to index into
 * slpq[] based on the sleep channel.
 */

#define	NSLEEPQ		64
#define	slpqhash(X)	(&_slpq[((int)X >> 4) & (NSLEEPQ - 1)])

struct slpq {
	struct thread	*sq_first;
	struct thread	*sq_last;
};

extern struct slpq _slpq[];

/*
 * Reserve one bucket for all threads with priorities > THREAD_MAX_PRIORITY
 * or less than THREAD_MIN_PRIORITY.
 * NOTE : Currently, thread_priority() returns an error if there is an
 * attempt to set a thread's priority out of this range, so the extra
 * bucket is not really used.
 */
#define	DISPQ_SIZE	(NPRI + 1)

typedef struct dispq {
	struct thread *dq_first;
	struct thread *dq_last;
} dispq_t;

extern dispq_t _dispq[];

typedef	void	(*PFrV) (void *);	/* pointer to function returning void */

/*
* Information common to all threads' TSD.
*/
struct tsd_common {
	unsigned int	nkeys;		/* number of used keys */
	unsigned int	max_keys;	/* number of allocated keys */
	PFrV		*destructors;	/* array of per-key destructor funcs */
	rwlock_t	lock;		/* lock for the above */
};

extern struct tsd_common tsd_common;


extern sigset_t _allunmasked;
extern sigset_t _cantmask;
extern sigset_t _lcantmask;
extern sigset_t _cantreset;
extern sigset_t _pmask;		/* virtual process signal mask */
extern mutex_t  _pmasklock;	/* lock for v. process signal mask */
extern sigset_t _bpending;	/* signals pending reassignment */
extern mutex_t	_bpendinglock;	/* mutex protecting _bpending */
extern cond_t	_sigwait_cv;
extern int _cond_eot;
extern mutex_t _tsslock;

extern sigset_t _ignoredefault;

/*
 * Signal test and manipulation macros.
 */
#define	sigdiffset(s1, s2)\
	(s1)->__sigbits[0] &= ~((s2)->__sigbits[0]); \
	(s1)->__sigbits[1] &= ~((s2)->__sigbits[1]); \
	(s1)->__sigbits[2] &= ~((s2)->__sigbits[2]); \
	(s1)->__sigbits[3] &= ~((s2)->__sigbits[3])

/* Mask all signal except - SIGSTOP/SIGKILL/SILWP/SIGCANCEL */
#define	maskallsigs(s) sigfillset((s)); \
	sigdiffset((s), &_cantmask)

/* Mask all signal except - SIGSTOP/SIGKILL */
#define	masktotalsigs(s) sigfillset((s)); \
	sigdiffset((s), &_lcantmask)

#define	sigcmpset(x, y)	(((x)->__sigbits[0] ^ (y)->__sigbits[0]) || \
			((x)->__sigbits[1] ^ (y)->__sigbits[1]) || \
			((x)->__sigbits[2] ^ (y)->__sigbits[2]) || \
			((x)->__sigbits[3] ^ (y)->__sigbits[3]))

/*
 * Are signals in "s" being manipulated in the mask (o = oldmask; n = newmask)?
 * Return true if yes, otherwise false.
 */
#define	changesigs(o, n, s)\
	((((o)->__sigbits[0] ^ (n)->__sigbits[0]) & (s)->__sigbits[0]) || \
	(((o)->__sigbits[1] ^ (n)->__sigbits[1]) & (s)->__sigbits[1]) || \
	(((o)->__sigbits[2] ^ (n)->__sigbits[2]) & (s)->__sigbits[2]) || \
	(((o)->__sigbits[3] ^ (n)->__sigbits[3]) & (s)->__sigbits[3]))

#define	sigorset(s1, s2)	(s1)->__sigbits[0] |= (s2)->__sigbits[0]; \
				(s1)->__sigbits[1] |= (s2)->__sigbits[1]; \
				(s1)->__sigbits[2] |= (s2)->__sigbits[2]; \
				(s1)->__sigbits[3] |= (s2)->__sigbits[3];

#define	sigisempty(s)\
	((s)->__sigbits[0] == 0 && (s)->__sigbits[1] == 0 &&\
	(s)->__sigbits[2] == 0 && (s)->__sigbits[3] == 0)


/*
 * following macro copied from sys/signal.h since inside #ifdef _KERNEL there.
 */
#define	sigmask(n)	((unsigned int)1 << (((n) - 1) & (32 - 1)))

/*
 * masksmaller(sigset_t *m1, sigset_t *m2)
 * return true if m1 is smaller (less restrictive) than m2
 */
#define	masksmaller(m1, m2) \
	((~((m1)->__sigbits[0]) & (m2)->__sigbits[0]) ||\
	    (~((m1)->__sigbits[1]) & (m2)->__sigbits[1]) ||\
	    (~((m1)->__sigbits[2]) & (m2)->__sigbits[2]) ||\
	    (~((m1)->__sigbits[3]) & (m2)->__sigbits[3]))

#define	sigand(x, y) (\
	((x)->__sigbits[0] & (y)->__sigbits[0]) ||\
	    ((x)->__sigbits[1] & (y)->__sigbits[1]) ||\
	    ((x)->__sigbits[2] & (y)->__sigbits[2]) ||\
	    ((x)->__sigbits[3] & (y)->__sigbits[3]))

#define	sigandset(a, x, y) \
	(a)->__sigbits[0] = (x)->__sigbits[0] & (y)->__sigbits[0];\
	    (a)->__sigbits[1] = (x)->__sigbits[1] & (y)->__sigbits[1]; \
	    (a)->__sigbits[2] = (x)->__sigbits[2] & (y)->__sigbits[2]; \
	    (a)->__sigbits[3] = (x)->__sigbits[3] & (y)->__sigbits[3]

/*
 * sparc v7 has no native swap instruction. It is emulated on the ss1s, ipcs,
 * etc. So, use the SIGSEGV interpositioning solution to solve the
 * mutex_unlock() problem of reading the waiter bit after the lock is freed.
 * Add other architectures (say, arch_foo) here which do not have atomic swap
 * instructions. This would result in the conditional define changing to:
 * "#if defined (__sparc) || defined(sparc) || defined(arch_foo)"
 */

#if defined(__sparc) || defined(sparc)
extern __wrd; /* label in _mutex_unlock_asm() needed for SEGV handler */
#define NO_SWAP_INSTRUCTION
extern int __advance_pc_required(ucontext_t *, siginfo_t *);
extern int __munlock_segv(int, struct thread *, ucontext_t *);
void  __libthread_segvhdlr(int sig, siginfo_t *sip, ucontext_t *uap,
    sigset_t *omask);
extern struct sigaction __segv_sigaction; /* global sigaction for SEGV */
#endif

extern sigset_t __lwpdirsigs;
extern sigset_t _null_sigset;
sigset_t _tpmask;

#define	dbg_sigaddset(m, s)
#define	dbg_delset(m, s)
#define	pmaskok(tm, pm) (1)

extern void (*_tsiguhandler[])();

/*
 * ANSI PROTOTYPES of internal global functions
 */


uthread_t *_idtot(thread_t tid);
void	_dynamiclwps();
void	_mutex_sema_unlock(mutex_t *mp);
void	_lmutex_unlock(mutex_t *mp);
int	_lmutex_trylock(mutex_t *mp);
void	_lmutex_lock(mutex_t *mp);
void	_lprefork_handler();
void	_lpostfork_child_handler();
void	_lpostfork_parent_handler();
int	_lpthread_atfork();
int	_lrw_rdlock(rwlock_t *rwlp);
int	_lrw_wrlock(rwlock_t *rwlp);
int	_lrw_unlock(rwlock_t *rwlp);
int	_thrp_kill_unlocked(uthread_t *t, int ix,
					int sig, lwpid_t *lwpidp);
int	_thr_main(void);
int	_setcallout(callo_t *cop, thread_t tid, const struct timeval *tv,
					void (*func)(), int arg);
int	_rmcallout(callo_t *cop);
void	_callin(int sig, siginfo_t *sip, ucontext_t *uap, sigset_t *omask);
void	_t_block(caddr_t chan);
int	_t_release(caddr_t chan, u_char *waiters, int);
void	_t_release_all(caddr_t chan);
void	_unsleep(struct thread *t);
void	_setrun(struct thread *t);
void	_dopreempt();
void	_preempt(uthread_t *t, pri_t pri);
struct	thread *_choose_thread(pri_t pri);
int 	_lwp_exec(struct thread *t, long npc, caddr_t sp, void (*fcn)(),
					int flags, lwpid_t *retlwpid);
int 	_new_lwp(struct thread *t, void (*func)());
int	_alloc_stack(int size, caddr_t *sp);
void	_free_stack(caddr_t addr, int size);
int	_swtch(int dontsave);
void	_qswtch();
void	_age();
void	_onproc_deq(uthread_t *t);
void	_onproc_enq(uthread_t *t);
void	_unpark(uthread_t *t);
void	_setrq(uthread_t *t);
int	_dispdeq(uthread_t *t);
uthread_t *_idle_thread_create();
void	_thread_destroy(uthread_t *t, int ix);
void	_thread_free(uthread_t *t);
int	_alloc_thread(caddr_t stk, int stksize, struct thread **tp);
int 	_thread_call(uthread_t *t, void (*fcn)(), void *arg);
void 	_thread_ret(struct thread *t, void (*fcn)());
void 	_thread_start();
void	_reapq_add(uthread_t *t);
void	_reaper_create();
void	_reap_wait(cond_t *cvp);
void	_reap_wait_cancel(cond_t *cvp);
void	_reap_lock();
void	_reap_unlock();
void	_sigon();
void	_sigoff();
void	_t0init();
void	_deliversigs(const sigset_t *sigs);
int	_sigredirect(int sig);
void	_siglwp(int sig, siginfo_t *sip, ucontext_t *uap);
void	_sigcancel(int sig, siginfo_t *sip, ucontext_t *uap);
void	_sigwaiting_enabled();
int	_hibit(register unsigned long  i);
int	_fsig(sigset_t *s);
void	_sigmaskset(sigset_t *s1, sigset_t *s2, sigset_t *s3);
int	_blocking(sigset_t *sent, sigset_t *old, const sigset_t *new,
					sigset_t *resend);
void	_resume_ret(uthread_t *oldthread);
void	_destroy_tsd(void);
void	_resetlib(void);
int	_assfail(char *, char *, int);
int	_setsighandler(int sig, const struct sigaction *nact,
						struct sigaction *oact);
void	__sighandler_lock();
void	__sighandler_unlock();
void	_initsigs();
void	_dynamic_create();
void	_cancel(uthread_t *t);
void	_canceloff(void);
void	_cancelon(void);
void	_prefork_handler();
void	_postfork_parent_handler();
void	_postfork_child_handler();
void	_tcancel_all(void *arg);
void	_thrp_exit();
void	_thr_exit_common(void *sts, int ex);
extern int (*__sigtimedwait_trap)(const sigset_t *, siginfo_t *,
    const struct timespec *);
extern int (*__sigsuspend_trap)(const sigset_t *);

/*
 * prototype of all the exported functions of internal
 * version of libthread calls. We need this since there
 * is no synonyms_mt.h any more.
 */

int	_cond_init(cond_t *cvp, int type, void *arg);
int	_cond_destroy(cond_t *cvp);
int	_cond_timedwait(cond_t *cvp, mutex_t *mp, timestruc_t *ts);
int	_cond_wait(cond_t *cvp, mutex_t *mp);
int	_cond_signal(cond_t *cvp);
int	_cond_broadcast(cond_t *cvp);

int	_mutex_init(mutex_t *mp, int type, void *arg);
int	_mutex_destroy(mutex_t *mp);
int	_mutex_lock(mutex_t *mp);
int	_mutex_unlock(mutex_t *mp);
int	_mutex_trylock(mutex_t *mp);
void	_mutex_op_lock(mutex_t *mp);
void	_mutex_op_unlock(mutex_t *mp);

int	_rwlock_init(rwlock_t *rwlp, int type, void *arg);
int	_rwlock_destroy(rwlock_t *rwlp);
int	_rw_rdlock(rwlock_t *rwlp);
int	_rw_wrlock(rwlock_t *rwlp);
int	_rw_unlock(rwlock_t *rwlp);
int	_rw_tryrdlock(rwlock_t *rwlp);
int	_rw_trywrlock(rwlock_t *rwlp);

int	_sema_init(sema_t *sp, unsigned int count, int type, void *arg);
int	_sema_destroy(sema_t *sp);
int	_sema_wait(sema_t *sp);
int	_sema_trywait(sema_t *sp);
int	_sema_post(sema_t *sp);

int	_thr_create(void *stk, size_t stksize, void *(*func)(void *),
		void *arg, long flags, thread_t *new_thread);
int	_thrp_create(void *stk, size_t stksize, void *(*func)(void *),
		void *arg, long flags, thread_t *new_thread, int prio);
int	_thr_join(thread_t tid, thread_t *departed, void **status);
int	_thr_setconcurrency(int n);
int	_thr_getconcurrency();
void	_thr_exit(void *status);
thread_t _thr_self();
int	_thr_sigsetmask(int how, const sigset_t *set, sigset_t *oset);
int	_thr_kill(thread_t tid, int sig);
int	_thr_suspend(thread_t tid);
int	_thr_continue(thread_t tid);
void	_thr_yield();
int	_thr_setprio(thread_t tid, int newpri);
int	_thr_getprio(thread_t tid, int *pri);
int	_thr_keycreate(thread_key_t *pkey, void (*destructor)(void *));
int	_thr_key_delete(thread_key_t key);
int	_thr_getspecific(thread_key_t key, void **valuep);
int	_thr_setspecific(unsigned int key, void *value);
size_t	_thr_min_stack();

#ifdef	__cplusplus
}
#endif

#endif /* _LIBTHREAD_H */
