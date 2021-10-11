/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)turnstile.c	1.37	94/11/21 SMI"

/*
 * Turnstiles are an abstract data type that encapsulates the
 * sleep queue and the priority inheritance information
 * associated with a synchronization object.
 *
 * Turnstiles are allocated when a thread goes to sleep waiting
 * for a synchronization object. When the last thread sleeping on
 * a synchronization object is awakened, the associated turnstile
 * is returned to the pool of available turnstiles.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/cpuvar.h>
#include <sys/pirec.h>
#include <sys/sleepq.h>
#include <sys/turnstile.h>
#include <sys/t_lock.h>
#include <sys/tblock.h>
#include <sys/prioinherit.h>
#include <sys/kmem.h>
#include <sys/spl.h>
#include <sys/cmn_err.h>

#ifdef	TSTILESTATS
static int	tstile_more_cnt = 0;

static void tstile_stats_fill(u_int);
static void tstile_stats_init(void);
static void tstile_stats_alloc(void);
static void tstile_stats_free(void);

#define	TSSTATS_FILL(cnt)	tstile_stats_fill(cnt)
#define	TSSTATS_INIT()		tstile_stats_init()
#define	TSSTATS_ALLOC()		tstile_stats_alloc()
#define	TSSTATS_FREE()		tstile_stats_free()
#define	TSSTATS_MORE()		++tstile_more_cnt

#else	/* !TSTILESTATS */

#define	TSSTATS_FILL(cnt)
#define	TSSTATS_INIT()
#define	TSSTATS_ALLOC()
#define	TSSTATS_FREE()
#define	TSSTATS_MORE()

#endif	/* TSTILESTATS */


#define	TSTILE_POOL_LOCK(pool)	\
		disp_lock_enter_high(&(pool)->tsp_lock)
#define	TSTILE_POOL_UNLOCK(pool)	\
		disp_lock_exit_high(&(pool)->tsp_lock)


#define	TSTILE_MOD_LOCK(s)	\
	s = lock_set_spl(&tstile_mod.tsm_lock, ipltospl(LOCK_LEVEL))

#define	TSTILE_MOD_UNLOCK(s)	\
	lock_clear_splx(&tstile_mod.tsm_lock, s)

#define	TS_POOLSZ		64

#define	TSTILE_HASH(ts)		(((u_int)(ts) >> 3) & (TS_POOLSZ - 1))


typedef	struct
{
	turnstile_t	*tsp_list;	/* list of turnstiles in this pool */
	disp_lock_t	tsp_lock;	/* lock for this pool */
} ts_poolent_t;

typedef struct
{
	u_int		tsm_poolsz;		/* # turnstiles in pool */
	u_int		tsm_rowcnt;		/* # of active rows in chunk */
	u_int		tsm_colsz;		/* # tstiles/column */
	u_int		tsm_active;		/* # of active turnstiles */
	kmutex_t	tsm_mutex;		/* chunk allocation mutex */
	ts_poolent_t	tsm_pools;		/* turnstile pool */
	lock_t		tsm_lock;		/* turnstile module lock */
	u_char		tsm_init;		/* turnstile init flag */
	turnstile_t	*tsm_chunk[TS_ROWSZ];	/* turnstile chunks */
} tstile_module_t;

tstile_module_t	tstile_mod;

/*
 * The following is the initial chunk of turnstiles.
 * Since we know we are going to start off with at least
 * this many, we go ahead and statically allocate them.
 */
static turnstile_t	ts_chunk0[TS_COLSZ - 1];

/*
 * Add 1 chunk of turnstiles to the pool of
 * available turnstiles.
 */
static void
tstile_chunk_install(turnstile_t *ts, int ts_cnt)
{
	ts_poolent_t	*pool;

	TSSTATS_FILL(ts_cnt);
	tstile_mod.tsm_poolsz += ts_cnt;
	pool = &tstile_mod.tsm_pools;
	TSTILE_POOL_LOCK(pool);
	while (ts_cnt-- > 0) {
		ts->tsun.ts_forw = pool->tsp_list;
		pool->tsp_list = ts++;
	}
	TSTILE_POOL_UNLOCK(pool);
}	/* end of tstile_chunk_install */

/*
 * Each turnstile is stamped with its uniquely
 * identifying turnstile-id. Thus, we can get
 * the turnstile-id of a turnstile in isolation.
 */
static void
tstile_chunk_init(int r, int c, int sz)
{
	turnstile_t	*ts_chunk;

	ts_chunk = tstile_mod.tsm_chunk[r];
	while (c < sz) {
		turnstile_t	*ts;

		ts = &ts_chunk[c];
		ts->ts_id = TS_ROWCOL(r, c);
		++c;
	}
}	/* end of tstile_chunk_init */

/*
 * Initialize the turnstile pool:
 * o Allocate and initialize the first row of the
 *   turnstile chunk.
 * o Put this first row into the turnstile pool.
 */
static void
tstile_poolinit()
{
	int		ts_colsz0;

	tstile_mod.tsm_rowcnt = 1;
	tstile_mod.tsm_colsz = TS_COLSZ;
	disp_lock_init(&tstile_mod.tsm_pools.tsp_lock, "turnstile pool lock");
	mutex_init(&tstile_mod.tsm_mutex, "Turnstile pool mutex",
				MUTEX_DEFAULT, NULL);
	/*
	 * The very first turnstile ([0,0]) is never used:
	 * turnstile-id 0 is reserved for the NIL turnstile.
	 * Thus, we don't bother to allocate any space for it.
	 *
	 * Instead, we adjust the tsm_chunk[0] pointer to point
	 * to the non-existant turnstile immediately before
	 * ts_chunk0[0].
	 *
	 * Dereferencing and modifying tsm_chunk[0] could have
	 * unpleasant side effects.
	 */
	tstile_mod.tsm_chunk[0] = (turnstile_t *)ts_chunk0;
	--tstile_mod.tsm_chunk[0];
	ts_colsz0 = tstile_mod.tsm_colsz - 1;
	tstile_chunk_init(0, 1, tstile_mod.tsm_colsz);
	tstile_chunk_install(ts_chunk0, ts_colsz0);
}	/* end of tstile_poolinit */

/*
 * Initialize the turnstile_t management
 * mechanism.
 */
void
tstile_init()
{
	int	s;

	TSTILE_MOD_LOCK(s);
	if (!tstile_mod.tsm_init) {
		TSSTATS_INIT();
		tstile_poolinit();
		tstile_mod.tsm_init = 1;
	}
	TSTILE_MOD_UNLOCK(s);
}	/* end of tstile_init */


/*
 * Check to see whether we need to allocate
 * more turnstiles: the intention is that we
 * try to keep pace with the number of threads
 * in the system.
 */
int
tstile_more(int	nthread, int slp)
{
	kmutex_t	*mp;

	if (nthread >= (tstile_mod.tsm_poolsz - 1)) {
		mp = &tstile_mod.tsm_mutex;
		mutex_enter(mp);
		/*
		 * We need to check again, since we may have been
		 * preempted between the "if" and mutex_enter().
		 */
		if (nthread >= (tstile_mod.tsm_poolsz - 1)) {
			turnstile_t	*ts_row;
			int		row_no;
			int		s;

			TSSTATS_MORE();
			slp = slp ? KM_NOSLEEP : 0;
			ts_row = TS_NEW(tstile_mod.tsm_colsz, slp);
			if (ts_row == NULL) {
				mutex_exit(mp);
				return (0);
			}
			row_no = tstile_mod.tsm_rowcnt++;
			tstile_mod.tsm_chunk[row_no] = ts_row;
			tstile_chunk_init(row_no, 0, tstile_mod.tsm_colsz);
			s = splhigh();
			tstile_chunk_install(ts_row, tstile_mod.tsm_colsz);
			(void) splx(s);
		}
		mutex_exit(mp);
	}
	return (1);
}	/* end of tstile_more */

/*
 * Get a zeroed turnstile_t structure from the pool.
 *
 * Returns:
 * 	A pointer to the allocated turnstile is returned as
 *	a function result to permit subsequent associated
 *	operations on turnstiles to use the turnstile directly,
 *	or the caller can use the turnstile ID in ts->ts_id.
 */
turnstile_t *
tstile_alloc()
{
	ts_poolent_t	*pool;
	turnstile_t	*ts;
	tstile_module_t	*tsm;

	TSSTATS_ALLOC();
	tsm = &tstile_mod;
	/*
	 * Take a turnstile out of the pool of free
	 * turnstiles.
	 */
	pool = &tsm->tsm_pools;
	TSTILE_POOL_LOCK(pool);
	ASSERT(pool->tsp_list != NULL);
	ts = pool->tsp_list;
	pool->tsp_list = pool->tsp_list->tsun.ts_forw;
	++tsm->tsm_active;
	TSTILE_POOL_UNLOCK(pool);

	ASSERT(ts->ts_flags == TSTILE_FREE);
	ASSERT(ts->tsun.ts_prioinv.pi_benef == NULL);
	/*
	 * Initialize the turnstile/pirec.
	 */
	pirec_clear(&ts->tsun.ts_prioinv);
	ts->ts_flags = TSTILE_ACTIVE;
	return (ts);
}	/* end of tstile_alloc */

/*
 * Given a turnstile-id, return a pointer to
 * the turnstile itself. It is assumed that the
 * waiters field of the synchronization object
 * is protected by the synch. object's disp_lock.
 */
turnstile_t *
tstile_pointer(turnstile_id_t tsid)
{
	u_int		row;
	u_int		col;

	row = (u_int)tsid;
	if (row != 0) {
		turnstile_t	*ts;

		col = TS_COL(row);
		row = TS_ROW(row);
		ASSERT(TS_VALID(row, col));
		ts = &tstile_mod.tsm_chunk[row][col];
		return (ts);
	} else return (NULL);
}	/* end of tstile_pointer */


/*
 * Given a turnstile-id, return a pointer to
 * the turnstile itself. It is assumed that the
 * waiters field of the synchronization object
 * is protected by the synch. object's disp_lock.
 */
turnstile_t *
tstile_pointer_verify(turnstile_id_t tsid)
{
	u_int		row;
	u_int		col;

	row = (u_int)tsid;
	if (row != 0) {
		turnstile_t	*ts;

		col = TS_COL(row);
		row = TS_ROW(row);
		if (TS_VALID(row, col))
			ts = &tstile_mod.tsm_chunk[row][col];
		else
			ts = NULL;
		return (ts);
	} else return (NULL);
}	/* end of tstile_pointer */

/*
 * If this turnstile is no longer needed, return it
 * to the pool of free turnstiles.
 */
void
tstile_free(turnstile_t *ts, turnstile_id_t *so_waiters)
{
	register sleepq_t	*spq;

	spq = ts->ts_sleepq;
	if ((spq[0].sq_first == NULL) && (spq[1].sq_first == NULL)) {
		ts_poolent_t	*pool;
		tstile_module_t	*tsm;

		/*
		 * Detach this turnstile from the s-object by
		 * clearing the s-object's waiters field.
		 */
		*so_waiters = 0;
		/*
		 * The following is necessary to fix a race condition
		 * between mutex_enter() and mutex_exit(). A thread
		 * executing mutex_exit() may be preempted in the window
		 * where the owner/lock field is cleared and where
		 * t_release() is called to wakeup a waiting thread. If
		 * this happens, another thread may execute mutex_enter()
		 * and become the "owner" of the not-quite-released
		 * mutex. If the mutex was formerly priority inverted,
		 * its pirec will still be in the t_prioinv list
		 * of the thread that was preempted while executing
		 * mutex_exit().
		 */
		if (ts->tsun.ts_prioinv.pi_benef != NULL)
			pi_waive(ts);
		ts->ts_flags = TSTILE_FREE;
		ts->ts_sobj_priv_data = NULL;
		TSSTATS_FREE();
		tsm = &tstile_mod;
		pool = &tsm->tsm_pools;
		TSTILE_POOL_LOCK(pool);
		ts->tsun.ts_forw = pool->tsp_list;
		pool->tsp_list = ts;
		--tsm->tsm_active;
		TSTILE_POOL_UNLOCK(pool);
	}
}	/* end of tstile_free */

/*
 * Insert a thread into the indicated sleep queue
 * of a turnstile.
 */
void
tstile_insert(turnstile_t *ts, qobj_t qnum, kthread_t *c)
{
	ASSERT(ts != NULL);
	TSTILE_INSERT(ts, qnum, c);
}	/* end of tstile_insert */

/*
 * Waje the highest priority thread sleeping in
 * queue # "qnum" of turnstile "ts".
 */
void
tstile_wakeone(turnstile_t *ts, qobj_t qnum)
{
	ASSERT(ts != NULL);
	TSTILE_WAKEONE(ts, qnum);
}	/* end of tstile_wakeone */

/*
 * Wake all the threads sleeping in queue # "qnum" of
 * turnstile "ts".
 */
void
tstile_wakeall(turnstile_t *ts, qobj_t qnum)
{
	ASSERT(ts != NULL);
	TSTILE_WAKEALL(ts, qnum);
}	/* end of tstile_wakeall */

/*
 * Remove thread "t" if it is sleeping in a queue
 * of turnstile "ts".
 */
int
tstile_deq(turnstile_t *ts, qobj_t qnum, kthread_t *t)
{
	ASSERT(ts != NULL);
	return (TSTILE_DEQ(ts, qnum, t) != NULL);
}	/* end of tstile_deq */

/*
 * Remove a thread "t" from its sleep queue in
 * this turnstile.
 */
kthread_t *
tstile_unsleep(turnstile_t *ts, qobj_t qnum, kthread_t *t)
{
	ASSERT(ts != NULL);
	return (TSTILE_UNSLEEP(ts, qnum, t));
}	/* end of tstile_unsleep */

/*
 * Compute the "priority" of an active turnstile/sleep queue.
 * We define this value to be the priority of the waiting
 * thread with the highest dispatch (effective) priority.
 */
pri_t
tstile_maxpri(turnstile_t *ts)
{
	if (ts != NULL) {
		register kthread_t	*t0;
		register kthread_t	*t1;
		register sleepq_t	*spq;
		register u_int		t0pri;
		register u_int		t1pri;

		spq = ts->ts_sleepq;
		t0 = spq[0].sq_first;
		t1 = spq[1].sq_first;
		if (t0 != NULL) {
			t0pri = (u_int)DISP_PRIO(t0);
			if (t1 != NULL) {
				t1pri = (u_int)DISP_PRIO(t1);
				if (t1pri > t0pri)
					t0pri = t1pri;
			}
			return (t0pri);
		} else if (t1 != NULL)
			return (DISP_PRIO(t1));
	}
	/*
	 * If we get here, either the turnstile pointer is NULL,
	 * or both sleep queues are empty.
	 */
	return (0);
}	/* end of tstile_maxpri */

/*
 * Determine whether the synchronization object that contains
 * this turnstile is already priority inverted.
 */
int
tstile_prio_inverted(turnstile_t *ts)
{
	return (TSTILE_PRIO_INVERTED(ts));
}	/* end of tstile_prio_inverted */

/*
 * Determine whether queue # "qnum" of turnstile "ts" is empty.
 * (I.e., no threads are waiting in this queue).
 */
int
tstile_empty(turnstile_t *ts, qobj_t qnum)
{
	return (ts == NULL || TSTILE_EMPTY(ts, qnum));
}	/* end of tstile_empty */

/*
 * The turnstile referenced by "ts" is uniquely associated
 * with a synchronization object on which some threads are
 * blocked and which is currently owned by a thread. The thread
 * that is the owner is the beneficiary of any priority
 * inheritance applied through this synchronization object.
 *
 * Return a pointer to this owning thread.
 */
kthread_t *
tstile_inheritor(turnstile_t *ts)
{
	if (ts != NULL)
		return (ts->tsun.ts_prioinv.pi_benef);
	else return (NULL);
}	/* end of tstile_inheritor */

#ifdef	TSTILESTATS

/*
 * The stuff below is only for collecting usage
 * statistics, and it is not usually compiled in.
 */

typedef struct
{
	u_int	available;
	u_int	allocated;
	u_int	released;
	u_int	hi_water;
} tstile_stats_t;

tstile_stats_t	tstile_stats;

/*
 * Clear the structures for maintaining
 * statistics about turnstile usage.
 */
static void
tstile_stats_init()
{
	tstile_stats_t	*st;

	st = &tstile_stats;
	st->available = st->allocated =
		st->released = st->hi_water = 0;
}	/* end of tstile_stats_init */

/*
 * Set the statistics to indicate that the turnstile
 * pool has been filled.
 */
static void
tstile_stats_fill(u_int count)
{
	tstile_stats.available += count;
}	/* end of tstile_stats_fill */

/*
 * Update stats to reflect that a turnstile has been
 * allocated.
 */
static void
tstile_stats_alloc()
{
	tstile_stats_t	*st;
	u_int		num_held;

	st = &tstile_stats;
	++st->allocated;
	--st->available;
	num_held = tstile_mod.tsm_poolsz - st->available;
	if (num_held > st->hi_water)
		st->hi_water = num_held;
}	/* end of tstile_stats_alloc */

/*
 * Update the stats to reflect that a turnstile
 * has been deallocated.
 */
static void
tstile_stats_free()
{
	tstile_stats_t	*st;

	st = &tstile_stats;
	++st->released;
	++st->available;
}	/* end of tstile_stats_free */

#endif	/* TSTILESTATS */
