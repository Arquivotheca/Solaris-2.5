/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _THREAD_DB2_H
#define	_THREAD_DB2_H

#pragma ident	"@(#)thread_db2.h	1.2	94/12/31 SMI"

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
* r_concurrency is the amount of concurrency requested(i.e.,
* the number of processors requested for the process).
* r_concurrency is set either by the application or through the
* td_ta_setconcurrency() interface.
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

/*
*   Different synchronization variables define flags differently but
* the definition here with tyd_sync_flags_t and TS_SV_MAX_FLAGS,
* allocates enough bytes for all types of synchronization variables.
*/
#define	TD_SV_MAX_FLAGS 8

#define	TD_MUTEX_LOCKED 1
#define	TD_MUTEX_UNLOCKED 0
#define	TD_PENDING_SIGNAL 1
#define	TD_NO_PENDING_SIGNAL 0

typedef uint8_t td_sync_flags_t;

typedef enum td_sync_type_e {
	TD_SYNC_UNKNOWN,	/* Sync. variable of unknown type  */
	TD_SYNC_COND,		/* Condition variable  */
	TD_SYNC_MUTEX,		/* Mutex lock  */
	TD_SYNC_SEMA,		/* Semaphore  */
	TD_SYNC_RWLOCK		/* Reader/Writer lock  */
} td_sync_type_e;


typedef struct td_sync_stats {
	int		waiters;	/* average number of waiters */
	int		contention;	/* total number of waiters */
	int		acquires;	/* total number of acquires */
	int		waittime;	/* average amount of wait time */
} td_sync_stats_t;


/*
* Synchronization Information(si).
*/

typedef struct td_syncinfo {
	td_thragent_t *si_ta_p;	/* thread agent */
	paddr_t		si_sv_addr;	/* address of synch variable */
	td_sync_type_e	si_type;	/* type of synch variable */
	uint32_t	si_shared_type;
				/*
				 * USYNC_THREAD, USYNC_PROCESS,
				 * TRACE_TYPE
				 */
	td_sync_flags_t	si_flags[TD_SV_MAX_FLAGS];
				/*
				 *flags for given sync.  type \
				 */
	union _si_un_state {
		int		sema_count;	/* semaphore count */
		int		nreaders;
						/*
						 *number of readers, -1
						 * means writer
						 */
		int		mutex_locked;	/* boolean value */
	} si_state;
	int		si_size;	/* size in bytes of synch variable */
	uchar_t		si_has_waiters;	/* boolean value */
	uchar_t		si_is_wlock;
					/*
					 * boolean value for rw locked owned
					 * by writer
					 */
			td_thrhandle_t si_owner;	/* for locks only */
	paddr_t		si_data;	/* pointer to optional data */
	td_sync_stats_t	si_stats;	/* synch variable statistics */
} td_syncinfo_t;

/*
* si_ta_p is the thread agent for the process in which this
* synchronization variable resides.
*
* si_sv_addr is the address of the synchronization variable.
*
* si_type is the synchronization variable type as enumerated by
* td_sync_type_e.
*
* si_flags is a set of flags for a synchronization variable. The
* flags depend on the type of the synchronization variable.
*
* si_state is the state of the synchronization variable and
* depends on the type of the synchronization variable. For a
* semaphore, reader/writer lock, and lock, the state is the
* semaphore count, the number of readers and the state of the lock,
* respectively. A non-zero value for the state of a lock
* indicates that the lock is being held by a thread.
*
* si_size is the size in bytes of the synchronization variable.
*
* si_has_waiters is a flag that indicates that the synchronization
* variable has threads waiting to acquire it. This field is relevant
* for locks, read/writer locks, and semaphores.
*
* si_owners is the thread handle for the thread owning the
* synchronization variable if it is a lock or read/writer lock.
*
* si_data is a pointer to optional data associated with
* the synchronization variable.  This will be defined
* in the future.
*
* si_stats is a set of statistics kept for synchronization variable.
*
* Synchronization Variable statistics
*
* Averages here are calculated in the same manner as the thread
* agent averages, qbar.
*
* waiters is for semaphores, mutex locks and read/writer locks.
* It is the average number of threads waiting to acquire the
* synchronization variable. For a semaphore, waiters is the
* the average where
*	q_i is the number of threads waiting on a semaphore each time
*		a semapost(3T) is done
*	t_i is the time at which a sema_post operation is done.
* For a mutex lock, waiters is the average where
*	q_i is the number of threads waiting on a lock each time a
*		mutex_unlock(3T) is done
*	t_i is the time a mutex_unlock(3T) operation is done on the lock
* For a reader/writer lock, waiters is the average where
*	q_i is the number of threads waiting on a lock each time a
*		rw_unlock(3T) is done
*	t_i is the time a rw_unlock(3T) is done
*
* contention is available for semaphores, mutex locks and
* read/writer locks. contention is the maximum number of
* threads waiting for a semaphore, mutex
* lock or reader/writer lock as observed at a sema_post(3T),
* mutext_unlock(3T), or rw_unlock(3T), respectively.
*
* acquires is available for mutex locks and read/writer locks.
* acquires is the total number of mutex_unlock(3T) or
* rw_unlock(3T) operations on a mutex lock or read/writer lock,
* respectively.
*
* waittime is the simple average of the times
* that a thread waits for a semaphore, mutex lock,
* reader/writer lock or condition variable.
* waittime is measured in real time(i.e., no effort is made
* to subtract out time due to the process not being
* scheduled to run nor time due to the thread not being scheduled).
* For a semaphore, waittime is the average of the times
* between the time a thread executes a sema_wait(3T) that
* blocks and the time that the thread unblocks
* due to the execution of a sema_post(3T). A thread that
* executes a sema_wait(3T) and does not block does not
* contribute to waittime. For a mutex lock or a reader/writer
* lock, waittime is the average of the times
* between the time a thread executes a mutex_lock(3T)
* or rw_lock(3T), respectively, that blocks and the time
* the thread acquires the lock. As with semaphores, threads
* that do not block on a lock do not contribute to waittime.
* For a condition variable, waittime is the average of the times
* between the time a thread executes a cond_wait(3T) and the
* time the thread unblocks due to a cond_signal(3T). For a
* condition variable, waittime is explicitly the time a
* thread spends waiting for the signal on a
* condition variable and is not the time
* waited for a user specified condition often
* used in conjunction with a condition variable.
*/

struct td_synchandle {
	td_thragent_t	*sh_ta_p;
	paddr_t		sh_unique;
};

typedef struct td_synchandle td_synchandle_t;

typedef int
td_sync_iter_f(const td_synchandle_t *, void *);

/*
* TD_ALL_EVENTS - This value is used to clear/set all events.
*
* TD_DEATH - Thread has executed a thr_exit() or has returned
* from its start function. Thread information is still available.
*
* TD_READY - Thread is now runnable. Thread ready to be scheduled
* to an LWP but is not currently executing. A thread that is
* moving from a debugger suspended state(ti_db_suspended bit set)
* does not trigger this event.
*
* TD_SLEEP - Thread has blocked on a synchronization variable and is
* not runnable. The thread now appears on a list associated
* with that synchronization variable.
*
* TD_SWITCHTO - Thread is about to start executing on an LWP. The
* thread's state is now TD_THR_ACTIVE, but the thread is no yet
* executing user code. TD_SWITCHTO does not apply to transition of
* an LWP from a parked to an unparked state.
*
* TD_SWITCHFROM - Thread is no longer executing on an LWP. The
* thread's state is not TD_THR_ACTIVE. TD_SWITCHFROM does
* not apply to transition of an LWP from an unparked to
* a parked state.
*
* TD_LOCK_TRY - Thread is attempting to acquire a lock but is
* failing to do so.
*
* TD_CATCHSIG - A signal has been delivered to an LWP in the
* process and the signal will be forwarded to the signal handler
* of a thread that does not have the signal masked. This event
* occurs before the signal handler for the thread is called.
*
* TD_CREATE - The thread has been created. All thread data is valid
* but thread has not yet been run.
*/

#define	TD_ALL_EVENTS	 0
		/* use to clear/set all events  */
#define	TD_READY	(TD_ALL_EVENTS+1)
		/* a thread becomes runnable  - state is TD_THR_RUN */

#define	TD_SLEEP	(TD_READY+1)
		/* a thread becomes blocked - state is TD_THR_SLEEP */

#define	TD_SWITCHTO	(TD_SLEEP+1)
		/* a thread is becoming active - state is TD_THR_ACTIVE */

#define	TD_SWITCHFROM    (TD_SWITCHTO+1)
		/* a thread is no longer active - state is not TD_THR_ACTIVE */

#define	TD_LOCK_TRY	(TD_SWITCHFROM+1)
		/* a thread is contending for an unavailable lock */
#define	TD_CATCHSIG	(TD_LOCK_TRY+1)
		/* a signal was posted */
#define	TD_IDLE		(TD_CATCHSIG+1)
		/* a processor went into idle state */
#define	TD_CREATE	(TD_IDLE+1)
		/* a thread has been created */
#define	TD_DEATH	(TD_CREATE+1)
		/* specified thread has died */

/*
* TD_PREEMPT - Thread is being preempted. This event occurs just
* prior to the preempted thread doing a yield.
*
* TD_PRI_INHERIT - Priority inheritance is occurring.
*
* TD_REAP - Storage for thread is being freed.
*
* TD_CONCURRENCY - The number of LWP's is changing.
*
* TD_TIMEOUT - Thread that executed a wait on a condition
* variable with a timeout is returning due to the timeout.
*/

/*
*  The events values in the range 16-31 are reserved for implementation
* specific events.
*/




#define	TD_PREEMPT	16	/* watch for thread preemption */

#define	TD_PRI_INHERIT	17	/* watch for priority inheritance */

#define	TD_REAP		18	/* watch the thread reaper */

#define	TD_CONCURRENCY	19	/* watch the LWP pool grow */

#define	TD_TIMEOUT	20
				/*
				 * thread returned from condition variable
				 * timed-wait
				 */


typedef struct td_event_msg {
	td_thr_events_t event;
	td_thrhandle_t *th_p;
	union {
		td_synchandle_t *sh;
		int		data;
	} msg;
} td_event_msg_t;



/*
*   Value of message for each event type
*
* TD_ALL_EVENTS - Not applicable.
*
* TD_DEATH - NULL
*
* TD_READY - NULL
*
* TD_SLEEP - synchronization handle(synchandle *)
* on which thread is blocked.
*
* TD_SWITCHTO - NULL
*
* TD_SWITCHFROM - New state(td_thr_state_e) of thread.
*
* TD_LOCK_TRY - synchronization handle(synchandle *) for which
*  thread is contending.
*
* TD_CATCHSIG - Signal(int) delivered.
*
* TD_PREEMPT - NULL
*
* TD_PRI_INHERIT -(To-be-determined)
*
* TD_REAP - NULL
*
* TD_CONCURRENCY - Number of LWP's(int)
*
* TD_TIMEOUT - synchronization handle(synchandle *) that timed out.
*
*/

/*
*   Ways that the event notification can take place:
*/
typedef enum {
	NOTIFY_BPT,
				/*
				 * bpt to be inserted at u.bptaddr by
				 * debugger
				 */
	NOTIFY_AUTOBPT,		/* bpt inserted at u.bptaddr by application */
	NOTIFY_SYSCALL		/* syscall u.syscallno will be invoked */
} td_notify_e;

/*
*   Information on ways that the event notification can take place:
*/
typedef struct td_notify {
	td_notify_e	type;
	union {
		paddr_t		bptaddr;
		int		syscallno;
	} u;
} td_notify_t;

typedef struct td_ta_stats {
	int		nthreads;	/* total number of threads in use */
	int		r_concurrency;	/* requested concurrency level */
	float		nrunnable;	/* average number of runnable threads */
	float		a_concurrency;	/* achieved concurrency level */
	float		nlwps;	/* average number of LWP's in use */
	int		nidle;	/* number of idling LWP's */
} td_ta_stats_t;

/*
*   Mimiced from sys/signal.h.
*/

/*
*   32 bit dependency
*/
#define	eventmask(n)	((unsigned int)1 << (((n) - 1) & (32 - 1)))
/*
*   8 bits/byte dependency
*/
#define	eventword(n)	(((unsigned int)((n) - 1))>>5)

#define	eventemptyset(td_thr_events_t)			\
	{						\
		int _i_; _i_ = TD_EVENTSIZE;		\
		while (_i_) (td_thr_events_t)->event_bits[--_i_] \
				= (u_long) 0; 			\
	}

#define	eventfillset(td_thr_events_t)				\
	{							\
		int _i_; _i_ = TD_EVENTSIZE;			\
		while (_i_) (td_thr_events_t)->event_bits[--_i_] =   \
			(u_long) 0xffffffff;			\
	}

#define	eventaddset(td_thr_events_t, n)		\
	(((td_thr_events_t)->event_bits[eventword(n)]) |= eventmask(n))
#define	eventdelset(td_thr_events_t, n)		\
	(((td_thr_events_t)->event_bits[eventword(n)]) &= ~eventmask(n))
#define	eventismember(td_thr_events_t, n)		\
	(eventmask(n) & ((td_thr_events_t)->event_bits[eventword(n)]))
#define	eventisempty(td_thr_events_t)			\
		(!((td_thr_events_t)->event_bits[0]) &&	\
		!((td_thr_events_t)->event_bits[1]))

#endif /* _THEAD_DB2_H */
