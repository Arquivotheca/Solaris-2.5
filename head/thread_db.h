/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _THREAD_TD_H
#define	_THREAD_TD_H

#pragma ident	"@(#)thread_db.h	1.6	94/12/31 SMI"

/*
 *
 *  Description:
 *	Types, global variables, and function definitions for user
 * of libthread_db.
 *
 */


#include <sys/lwp.h>
#include <sys/procfs.h>
#include <thread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define		TD_THR_ANY_USER_FLAGS	0xffffffff
#define		TD_THR_LOWEST_PRIORITY 0
#define		TD_SIGNO_MASK 0
#define		TD_EVENTSIZE 2

	struct ps_prochandle;
	struct td_thragent;

	typedef struct td_thragent td_thragent_t;

/*
 * Averages are calculated as follows.  Quantity q_i is observed at
 * time t_i.  The time of the next observation is t_ip1.  The
 * average for q, qbar, is calculated as
 *
 * 	qbar = (sum qbar_i)/(sum deltat_i) for all i
 * where
 *	qbar_i = q_i*(deltat_i)
 *	deltat_i = t_ip1 - t_i
 * and it is assumed that
 *	q_0 = 0
 *
 * nthreads is the current number of active threads.
 *
 * r_concurrency is the amount of concurrency requested (i.e.,
 * the number of processors requested for the process).
 *
 * nrunnable is the average number of threads that are ready to
 * run.
 *	q_i is number of runnable threads at time t_i.
 *	t_i is a time at which a thread scheduling event occurs.
 *
 * a_concurrency is the average concurrency observed.
 *	q_i is the number of threads running at time t_i.
 *	t_i is a time at which a thread scheduling event occurs.
 *
 * nlwps is the average number of LWP's that have been participating
 * in running the process. This is different than the current
 * number of LWP'S participating in the process. The latter number
 * is assumed to be available to the debugger through other means
 * (e.g., /proc). nlwps is different than a_concurrency in
 * that idling LPW's do not contribute to a_concurrency but do
 * contribute to nlwps.
 *	q_i is the number of LWP's participating in the process at
 *		time t_i.
 *	t_i is a time at which a thread scheduling event occurs.
 *
 * nidle is the average number of idling LWP's.
 *	q_i is the number of idling LWP's at time t_i.
 *	t_i is a time at which an LWP enters or exits the idle state.
 */

	typedef int	td_key_iter_f(thread_key_t, void (*destructor) (),
		void *);

	enum td_thr_state_e {
		TD_THR_ANY_STATE,
		TD_THR_UNKNOWN,
		TD_THR_STOPPED,
		TD_THR_RUN,
		TD_THR_ACTIVE,
		TD_THR_ZOMBIE,
		TD_THR_SLEEP,
		TD_THR_STOPPED_ASLEEP
	};

	typedef enum td_thr_state_e td_thr_state_e;

/*
 * TD_THR_ANY_TYPE is type used in thread iterator so that
 * all threads are returned.
 *
 * TD_THR_USER is type for user created threads.
 *
 * TD_THR_SYSTEM is type for libthread created threads.
 */

	enum td_thr_type_e {
		TD_THR_ANY_TYPE,
		TD_THR_USER,
		TD_THR_SYSTEM
	};

	typedef enum td_thr_type_e td_thr_type_e;

	struct td_thr_siginfo {
		siginfo_t	ti_sip;	/* siginfo_t of deferred signal */
		ucontext_t	ti_uc;	/* ucontext_t of deferred signal */
	};

	typedef struct td_thr_siginfo td_thr_siginfo_t;

/*
 *   Event information.
 */

	struct td_thr_events {
		u_long	event_bits[TD_EVENTSIZE];
	};

	typedef struct td_thr_events td_thr_events_t;


/*
 * Thread Information (ti).
 */

	struct td_thrinfo {
		td_thragent_t	*ti_ta_p;	/* thread agent */
		unsigned	ti_user_flags;
						/*
						 * flags as passed by user to
						 * thr_create()
						 */
		thread_t	ti_tid;
						/*
						 * value as returned by
						 * thr_create()
						 */
		char		*ti_tls;
						/*
						 * thread local storage
						 * pointer
						 */
		paddr_t		ti_startfunc;
						/*
						 * value as passed to
						 * thr_create()
						 */
		paddr_t		ti_stkbase;	/* base of thread's stack */
		int		ti_stksize;	/* size of thread's stack */
		paddr_t		ti_ro_area;
						/*
						 * physical address of read
						 * only area
						 */
		int		ti_ro_size;
						/*
						 * size in bytes of read only
						 * area
						 */
		td_thr_state_e	ti_state;	/* thread states */
		uchar_t		ti_db_suspended;
						/*
						 * thread suspended by debugger
						 */
		td_thr_type_e	ti_type;	/* type of thread */
		int		ti_pc;	/* PC when state == TD_THR_SLEEP */
		int		ti_sp;	/* SP when state == TD_THR_SLEEP */
		short		ti_flags; /* special flags used by libthread */
		int		ti_pri;	/* thread priority */
		lwpid_t		ti_lid;	/* LWP running this thread */
		sigset_t	ti_sigmask;	/* thread signal mask */
		uchar_t		ti_traceme;	/* enable / disable tracing */
		uchar_t		ti_preemptflag;	/* was thread preempted */
		uchar_t		ti_pirecflag;
						/*
						 * priority inheritance
						 * happened
						 */
		sigset_t	ti_pending;	/* set of pending signals */
		td_thr_events_t ti_events;	/* bitmap of enabled events */
	};

	typedef struct td_thrinfo td_thrinfo_t;


/*
 * ti_ta_p is a pointer to a thread agent.
 *
 * ti_user_flags is the long flags parameter to thr_create()
 *	(see thr_create(3T))
 *
 * ti_tid is the thread_t *new_thread parameter from thr_create()
 *	(see thr_create(3T))
 *
 * ti_startfunc is the void* (*start_routine)(void *) parameter
 *	(see thr_create(3T))
 *
 * ti_stkbase is the base of the stack for the thread
 *
 * ti_stksize is the size of the stack in bytes for the thread
 *
 * ti_ro_area is the address of a read only area within a thread.
 *
 * ti_ro_size is the size in bytes of the read only area.
 */


/*
 * ti_state is the state of the thread as enumerated by
 * td_thr_state_e.
 *
 * ti_flags is a set of special flags used by libthread.
 *
 * ti_db_suspended is a flag indicated that the thread has been
 * suspended by the debugger.
 *
 * ti_type is the type of the thread as enumerated by td_thr_type_e.
 *
 * ti_pc and ti_sp are program counter and the stack pointer,
 * respectively, for a thread in a sleep state (TD_THR_SLEEP in
 * td_thr_state_e). There values cannot be
 * provided reliably for other states.
 *
 * ti_pri is the priority of the thread (see thr_setprio(3T) and/or
 * thr_getprio(3T))
 *
 * ti_lid is the identifier (see _lwp_create(2)) for the LWP
 * associated with this thread. If the thread is in the
 * active state (TD_THR_ACTIVE in td_thr_state_e), the
 * LWP was executing the thread. If not in
 * the active state, the value is zero.
 *
 * ti_sigmask is the signal mask for the thread (see sys/signal.h).
 *
 * ti_traceme is a flag to enable or disable all events for this
 * thread. A non-zero value enables events.
 *
 * ti_preemptflag is a flag indicating that this thread was preempted
 * the last time it was active. A non-zero value indicates that a
 * preemption occurred.
 *
 * ti_pirectflag is a flag indicating that priority inheritance has
 * occurred.  A nonzero value indicates the occurrence of priority
 * inheritance.  This flag is cleared once the thread has returned to
 * its normal priority.
 *
 * ti_pending holds the set of pending signals.
 *
 * ti_events is a bit map of events that are enabled
 * (see td_thr_events_t) for this thread. If the bit
 * corresponding to an event is set, the event
 * is enabled. Events occurring for a thread without
 * the corresponding bit set are ignored.
 *
 */



/*
 *   Thread handle.
 * The thread handle is a unique identifier for a thread.
 * It's definition is public because it is part of the
 * synch. variable.
 */

	struct td_thrhandle {
		td_thragent_t	*th_ta_p;
		paddr_t		th_unique;
	};

	typedef struct td_thrhandle td_thrhandle_t;

/*
 *   Declaration for thread iterator call back function.
 */

	typedef int	td_thr_iter_f(const td_thrhandle_t *, void *);

/*
 *   32 bit dependency
 */
#define	eventmask(n)	((unsigned int)1 << (((n) - 1) & (32 - 1)))
/*
 *   8 bits/byte dependency
 */
#define	eventword(n)	(((unsigned int)((n) - 1))>>5)

#define	eventemptyset(td_thr_events_t)					\
	{								\
		int _i_; 						\
		_i_ = TD_EVENTSIZE;					\
		while (_i_) (td_thr_events_t)->event_bits[--_i_] = (u_long) 0; \
	}

#define	eventfillset(td_thr_events_t)					\
	{								\
		int _i_;						\
		_i_ = TD_EVENTSIZE;					\
		while (_i_) (td_thr_events_t)->event_bits[--_i_] =	\
			(u_long) 0xffffffff;				\
	}

#define	eventaddset(td_thr_events_t, n)			\
	(((td_thr_events_t)->event_bits[eventword(n)]) |= eventmask(n))
#define	eventdelset(td_thr_events_t, n)			\
	(((td_thr_events_t)->event_bits[eventword(n)]) &= ~eventmask(n))
#define	eventismember(td_thr_events_t, n)		\
	(eventmask(n) & ((td_thr_events_t)->event_bits[eventword(n)]))
#define	eventisempty(td_thr_events_t)			\
	(!((td_thr_events_t)->event_bits[0]) &&		\
	!((td_thr_events_t)->event_bits[1]))

	typedef enum td_err_e {
		TD_OK,		/* generic "call succeeded" */
		TD_ERR,		/* generic error. */
		TD_NOTHR, /* no thread can be found to satisfy query */
		TD_NOSV,
				/*
				 * no synch. variable can be found to
				 * satisfy query
				 */
		TD_NOLWP,	/* no lwp can be found to satisfy query */
		TD_BADPH,	/* invalid process handle */
		TD_BADTH,	/* invalid thread handle */
		TD_BADSH,	/* invalid synchronization handle */
		TD_BADTA,	/* invalid thread agent */
		TD_BADKEY,	/* invalid key */
		TD_NOMSG,
				/*
				* td_thr_event_getmsg() called when there
				* was no message
				*/
		TD_NOFPREGS,
				/*
				 * FPU register set not available for
				 * given thread
				 */
		TD_NOLIBTHREAD,	/* application not linked with libthread */
		TD_NOEVENT,	/* requested event is not supported */
		TD_NOCAPAB,	/* capability not available */
		TD_DBERR,	/* Debugger service failed */
		TD_NOAPLIC,	/* Operation not applicable to */
		TD_NOTSD,
				/*
				 * No thread specific data for this
				 * thread
				 */
		TD_MALLOC,	/* Malloc failed */
		TD_PARTIALREG,
				/*
				 * Only part of register set was
				 * writen/read
				 */
		TD_NOXREGS
				/*
				 * X register set not available for
				 * given thread
				 */
	}	td_err_e;


/*
 * TD_OK indicates that the call completed successfully.
 *
 * TD_ERR is the generic error for use when no other code applies.
 *
 * TD_NOTHR is returned if the thread does not correspond
 * to any thread known to tdb. The thread may
 * no longer exist or the debugger is using a corrupted thread handle.
 *
 * TD_NOSV is returned if the synchronization variable does not
 * correspond to any synchronization variable known to tdb. The
 * synchronization variable may no longer exist or the debugger is
 * using a corrupted synchronization handle.
 *
 * TD_NOLWP is returned if there is no LWP that is part of the process
 * that corresponds to the given lwpid. This can occur if the
 * lwpid is corrupted or if the lwp has been returned to the OS.
 *
 * TD_BADPH indicates that the process handle is invalid.
 *
 * TD_BADTH indicates that the thread handle is invalid.
 *
 * How do we distinguish between TD_NOTHR and TD_BADTH? Similarly for SO's.
 *
 * TD_BADSH indicates that the synchronization handle is invalid.
 *
 * TD_BADKEY indicates that the key for the thread specific
 * data area is invalid.
 *
 * TD_NOMSG is returned by td_thr_event_getmsg() if there is
 * no message.
 *
 * TD_NOFPREGS is returned if the floating point registers are
 * not available for this lwp.
 *
 * TD_NOLIBTHREAD is returned if application is not multithreaded
 * (i.e., not linked with libthread).
 *
 * TD_NOEVENT is returned if an address was requested for an
 * event which is not supported.
 *
 * TD_NOCAPAB indicates that the requested capability is not
 * supported. The interface exists but there is no functionality
 * behind the interface.
 *
 * TD_DBERR indicates that an error occurred during a request
 * for service from the debugger.
 *
 * TD_NOAPPLIC is returned if the requested function does not apply
 * to the given variable. For example, td_sync_ownerpc() can be
 * applied to mutexes and rw locks but not condition variables
 * nor semaphores.
 *
 * TD_NOTSD is returned if there is no thread specific data for this
 * thread.  A key may exist without an entry for every thread.
 *
 * TD_MALLOC is returned if a malloc failed.
 *
 * TD_PARTIALREG is returned when an operation on a register set
 * was performed on only part of the register set.  For example,
 * at some points in the execution of a process not all the general
 * purpose registers may be available.
 *
 */



	extern void td_log(const int on_off);
	extern td_err_e td_ta_new(const struct ps_prochandle * ph_p,
		td_thragent_t **ta_pp);
	extern td_err_e td_ta_delete(td_thragent_t *ta_p);
	extern td_err_e td_init();
	extern td_err_e td_ta_get_ph(const td_thragent_t *ta_p,
		struct ps_prochandle ** ph_pp);
	extern td_err_e td_ta_get_nthreads(const td_thragent_t *ta_p,
		int *nthread_p);
	extern td_err_e td_ta_tsd_iter(const td_thragent_t *ta_p,
		td_key_iter_f * cb, void *cbdata_p);
	extern td_err_e td_ta_thr_iter(const td_thragent_t *ta_p,
		td_thr_iter_f * cb, void *cbdata_p, td_thr_state_e state,
		int ti_pri, sigset_t * ti_sigmask_p, unsigned ti_user_flags);
	extern td_err_e td_thr_validate(const td_thrhandle_t *th_p);
	extern td_err_e td_thr_tsd(const td_thrhandle_t *th_p,
		const thread_key_t key, void **data_pp);
	extern td_err_e td_thr_get_info(const td_thrhandle_t *th_p,
		td_thrinfo_t *ti_p);
	extern td_err_e td_thr_getfpregs(const td_thrhandle_t *th_p,
		prfpregset_t * fpregset);
	extern td_err_e td_thr_getxregsize(const td_thrhandle_t *th_p,
		int *xregsize);
	extern td_err_e td_thr_getxregs(const td_thrhandle_t *th_p,
		const caddr_t xregset);
	extern td_err_e td_thr_sigsetmask(const td_thrhandle_t *th_p,
		const sigset_t ti_sigmask);
	extern td_err_e td_thr_setprio(const td_thrhandle_t *th_p,
		const int ti_pri);
	extern td_err_e td_thr_setsigpending(const td_thrhandle_t *th_p,
		const uchar_t ti_pending_flag, const sigset_t ti_pending);
	extern td_err_e td_thr_setfpregs(const td_thrhandle_t *th_p,
		const prfpregset_t * fpregset);
	extern td_err_e td_thr_setxregs(const td_thrhandle_t *th_p,
		const caddr_t xregset);
	extern td_err_e td_ta_map_id2thr(const td_thragent_t *ta_p,
		thread_t tid, td_thrhandle_t *th_p);
	extern td_err_e td_ta_map_lwp2thr(const td_thragent_t *ta_p,
		lwpid_t lwpid, td_thrhandle_t *th_p);
	extern td_err_e td_thr_getgregs(const td_thrhandle_t *th_p,
		prgregset_t regset);
	extern td_err_e td_thr_setgregs(const td_thrhandle_t *th_p,
		const prgregset_t regset);
#ifdef __cplusplus
}
#endif

#endif /* _THREAD_TD_H */
