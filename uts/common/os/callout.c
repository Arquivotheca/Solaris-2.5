/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)callout.c	1.10	95/02/10 SMI"

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/callo.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/map.h>
#include <sys/swap.h>
#include <sys/vmsystm.h>
#include <sys/class.h>
#include <sys/callb.h>
#include <sys/debug.h>
#include <sys/vtrace.h>

/*
 * Callout table management.  These routines provide a generic callout table
 * implementation; currently, there are two such tables (normal and realtime).
 * A brief summary of the interfaces:
 *
 * callout type		To create		To cancel	Executed by
 * ------------		---------		---------	-----------
 * normal		timeout()		untimeout()	callout_thread
 * realtime		realtime_timeout()	untimeout()	softcall
 */

extern	clock_t	lbolt;

callout_state_t callout_state, rt_callout_state;
static void callout_thread(void);
static void realtime_timein(caddr_t);
static void callout_save(callout_state_t *, callout_state_t *);
static void callout_restore(callout_state_t *, callout_state_t *);
static void callout_cpr_callb(void *, int code);

/*
 * Initialize a callout table.
 */
static void
callout_setup(callout_state_t *cs, char *lock_name, int table_id)
{
	int i;

	for (i = 0; i < CALLOUT_BUCKETS; i++) {
		/*
		 * Each bucket is represented as a circular, singly linked
		 * list.  The list is never empty, since it always contains
		 * the bucket head; this fact simplifies bucket insertions
		 * and deletions.  Note that, for this to work, the linkage
		 * fields of the callout_t and callout_bucket_t structures
		 * must be at the same offsets.
		 */
		cs->cs_bucket[i].b_first = (callout_t *)&cs->cs_bucket[i];
		cs->cs_bucket[i].b_short_id = i | table_id;
		cs->cs_bucket[i].b_long_id = i | table_id | CALLOUT_LONGTERM;
	}
	mutex_init(&cs->cs_lock, lock_name, MUTEX_DEFAULT, NULL);
}

/*
 * Initialize all callout tables.  Called at boot time just before clkstart().
 */
void
callout_init(void)
{
	int i;

	/*
	 * Normal callouts
	 */
	callout_setup(&callout_state, "callout lock", CALLOUT_NORMAL);
	for (i = 0; i < CALLOUT_THREADS; i++) {
		if (thread_create(NULL, 0, callout_thread, NULL, 0,
		    &p0, TS_RUN, maxclsyspri) == NULL)
			panic("callout_thread: cannot create");
	}

	/*
	 * Realtime callouts
	 */
	callout_setup(&rt_callout_state, "RT callout lock", CALLOUT_REALTIME);
	(void) callb_add(callout_cpr_callb, 0, CB_CL_CPR_CALLOUT, "callout");
}

/*
 * Try to allocate a callout structure.  We try quite hard because
 * we can't sleep, and if we can't do the allocation, we're toast.
 */
static callout_t *
callout_alloc(callout_state_t *cs)
{
	callout_t *cp;
	int size = sizeof (callout_t);

	cp = kmem_perm_alloc(size, 0, KM_NOSLEEP);
	while (cp == NULL && size < 4096)
		cp = kmem_alloc(size++, KM_NOSLEEP);
	if (cp == NULL)
		cmn_err(CE_PANIC, "Timeout table overflow");
	bzero((void *)cp, sizeof (callout_t));
	cs->cs_ncallout++;
	return (cp);
}

/*
 * callout_insert() is called to arrange that func(arg) be called
 * after delta clock ticks.  Must be called with (&cs->cs_lock) held.
 */
static int
callout_insert(callout_state_t *cs, void (*func)(), caddr_t arg, long delta)
{
	callout_bucket_t *bucket;
	callout_t *cp;
	int id;
	long curtime, runtime;

	/* ASSERT(MUTEX_HELD(&cs->cs_lock)); */

	/*
	 * Take a callout structure from the free list, interleaving
	 * stores of initial values (this keeps the stores spaced out,
	 * which helps most hardware)
	 */
	if ((cp = cs->cs_freelist) == NULL)
		cp = callout_alloc(cs);
	cp->c_func = func;
	curtime = cs->cs_curtime;
	cp->c_arg = arg;
	cs->cs_freelist = cp->c_next;

	/*
	 * Make sure the callout runs at least 1 tick in the future
	 * The use of lbolt guarantees that lbolt has reached at least
	 *	the value of current_lbolt + delta when func is called,
	 *	as required by cv_timedwait*()
	 */
	runtime = lbolt + delta;
	if (runtime <= curtime)
		runtime = (delta <= 0) ? curtime + 1 : INT_MAX;
	cp->c_runtime = runtime;

	/*
	 * Assign an ID to this callout.  The high-order bits contain
	 * auxiliary information, e.g. which callout table the ID came
	 * from; the low-order bits are the bucket number; and the middle
	 * bits are a free-running counter.  The bucket head contains
	 * the most recent long-term and short-term callout IDs assigned
	 * to this bucket.  The manipulation of the CALLOUT_LONGTERM bit,
	 * which is right next to the highest-order bit of the counter, is
	 * to prevent overflow into the auxiliary bits when the ID wraps.
	 * See sys/callo.h for full details.
	 */
	bucket = &cs->cs_bucket[runtime & CALLOUT_BUCKET_MASK];
	if (runtime - curtime > CALLOUT_LONGTERM_TICKS)
		bucket->b_long_id = id = (bucket->b_long_id - CALLOUT_LONGTERM +
			CALLOUT_BUCKETS) | CALLOUT_LONGTERM;
	else
		bucket->b_short_id = id = (bucket->b_short_id +
			CALLOUT_BUCKETS) & ~CALLOUT_LONGTERM;
	cp->c_xid = UNSAFE_DRIVER_LOCK_HELD() ? id | CALLOUT_UNSAFE_DRIVER : id;

	/*
	 * We insert the new callout at the head of its bucket, so that
	 * rapid timeout()/untimeout() pairs are fast -- untimeout() will
	 * usually find the callout it's looking for on the first try.
	 */
	cp->c_next = bucket->b_first;
	bucket->b_first = cp;

	return (id);
}

/*
 * callout_delete(id) is called to remove an entry in the callout
 * table that was originally placed there by a call to callout_insert().
 * Must be called with (&cs->cs_lock) held.
 */
static int
callout_delete(callout_state_t *cs, int id)
{
	callout_bucket_t *bucket;
	callout_t *cp, *prev, *freelist;
	long time_left, curtime;
	int xid;

	/* ASSERT(MUTEX_HELD(&cs->cs_lock)); */

	freelist = cs->cs_freelist;
	curtime = cs->cs_curtime;
	/*
	 * Search the bucket for a callout with matching id.
	 */
	bucket = &cs->cs_bucket[id & CALLOUT_BUCKET_MASK];
	prev = (callout_t *)bucket;
	cp = bucket->b_first;
	while (((xid = cp->c_xid) & CALLOUT_ID_MASK) != id &&
	    cp != (callout_t *)bucket) {
		prev = cp;
		cp = cp->c_next;
	}
	if (cp == (callout_t *)bucket) {
		TRACE_0(TR_FAC_CALLOUT, TR_CALLOUT_DELETE_BOGUS_ID,
			"callout_delete_bogus_id");
		return (-1);
	}
	if (xid & CALLOUT_EXECUTING) {
		/*
		 * Bummer, the callout we want to delete is currently
		 * executing.  The DDI states that we must wait until the
		 * callout completes before returning.  So, we cv_wait on
		 * this callout's c_done, which will be cv_signaled when
		 * the callout completes.  We verify that the callout really
		 * is done (not just a spurious setrun()) by checking for a
		 * new id in the callout stucture (this will be either the
		 * id of a new callout, or zero if it's on the free list).
		 */
		TRACE_0(TR_FAC_CALLOUT, TR_CALLOUT_DELETE_EXECUTING,
			"callout_delete_executing");
		if (cp->c_executor == curthread) {
			/*
			 * What a revoltin' development.  There are two ways
			 * this can happen: (1) the callout routine contains a
			 * call to untimeout() itself (which is legal), or
			 * (2) the untimeout() is being attempted by an
			 * interrupt executing above THREAD_LEVEL (illegal).
			 * (Case (1) really should be illegal as well, since it
			 * creates a logical deadlock with the above-mentioned
			 * DDI requirement.)  Either way, waiting for the
			 * callout to complete would cause deadlock, so we
			 * return immediately.
			 */
			TRACE_0(TR_FAC_CALLOUT, TR_CALLOUT_DELETE_NESTED,
				"callout_delete_nested");
			return (-1);
		}
		if (xid & CALLOUT_UNSAFE_DRIVER) {
			ASSERT(MUTEX_HELD(&unsafe_driver));
			mutex_exit(&unsafe_driver);
			while (cp->c_xid == xid)
				cv_wait(&cp->c_done, &cs->cs_lock);
			mutex_exit(&cs->cs_lock);
			mutex_enter(&unsafe_driver);
			mutex_enter(&cs->cs_lock);
		} else {
			while (cp->c_xid == xid)
				cv_wait(&cp->c_done, &cs->cs_lock);
		}
		return (-1);
	}
	/*
	 * Delete entry from bucket and return to freelist, interleaved
	 * with computation of return value (to space out stores).
	 */
	prev->c_next = cp->c_next;
	time_left = cp->c_runtime - curtime;
	cp->c_next = freelist;
	if (time_left < 0)
		time_left = 0;
	cs->cs_freelist = cp;

	return (time_left);
}

/*
 * See if there is any callout processing to be done on this tick.  If so,
 * schedule it accordingly.  Note: the loop "while (runtime <= curtime)" is
 * almost always exactly one iteration, since runtime simply tracks curtime.
 * This routine should be called by the clock thread on each tick, for each
 * callout table.
 */
void
callout_schedule(callout_state_t *cs)
{
	callout_bucket_t *bucket;
	callout_t *cp;
	int xid;
	long curtime, runtime;

	mutex_enter(&cs->cs_lock);
	cs->cs_curtime = curtime = lbolt;
	while ((runtime = cs->cs_runtime) <= curtime) {
		bucket = &cs->cs_bucket[runtime & CALLOUT_BUCKET_MASK];
		for (cp = bucket->b_first; cp != (callout_t *)bucket;
		    cp = cp->c_next) {
			xid = cp->c_xid;
			if (cp->c_runtime == runtime &&
			    !(xid & CALLOUT_EXECUTING)) {
				if (xid & CALLOUT_REALTIME)
					softcall(realtime_timein, NULL);
				else
					cv_signal(&cs->cs_threadpool);
				cs->cs_busyticks++;
				mutex_exit(&cs->cs_lock);
				return;
			}
		}
		cs->cs_runtime++;
	}
	mutex_exit(&cs->cs_lock);
}

/*
 * Do the actual work of executing callouts.  This routine is called either
 * by the callout_thread (normal case), or by softcall (realtime case).
 * Must be called with the callout state lock (&cs->cs_lock) held.
 */
static void
callout_execute(callout_state_t *cs)
{
	callout_bucket_t *bucket;
	callout_t *cp, *prev, *next, *cur;
	int xid;
	long runtime;

	ASSERT(MUTEX_HELD(&cs->cs_lock));

	TRACE_1(TR_FAC_CALLOUT, TR_CALLOUT_EXECUTE_START,
		"callout_execute_start:%K", cs);
	while ((runtime = cs->cs_runtime) <= cs->cs_curtime) {
		bucket = &cs->cs_bucket[runtime & CALLOUT_BUCKET_MASK];
		cp = bucket->b_first;
		while (cp != (callout_t *)bucket) {
			xid = cp->c_xid;
			ASSERT(cp->c_runtime >= runtime ||
			    (xid & CALLOUT_EXECUTING));	/* haven't missed any */
			if (cp->c_runtime != runtime ||
			    (xid & CALLOUT_EXECUTING)) {
				cp = cp->c_next;
				continue;
			}
			cp->c_executor = curthread;
			xid |= CALLOUT_EXECUTING;
			cp->c_xid = xid;
			mutex_exit(&cs->cs_lock);
			TRACE_3(TR_FAC_CALLOUT, TR_CALLOUT_START,
				"callout_start:id %x %K(%x)",
				cp->c_xid, cp->c_func, cp->c_arg);
			if (xid & CALLOUT_UNSAFE_DRIVER) {
				mutex_enter(&unsafe_driver);
				(*cp->c_func)(cp->c_arg);
				mutex_exit(&unsafe_driver);
			} else {
				(*cp->c_func)(cp->c_arg);
			}
			TRACE_0(TR_FAC_CALLOUT, TR_CALLOUT_END, "callout_end");
			mutex_enter(&cs->cs_lock);
			/*
			 * Delete callout from bucket, return to free list,
			 * and tell everyone who cares that we're done.
			 * Since we've dropped the callout lock, we have to
			 * rediscover the prev pointer.  This will usually take
			 * just one iteration, since we'll usually be executing
			 * the head of the bucket.
			 */
			prev = (callout_t *)bucket;
			while ((cur = prev->c_next) != cp)
				prev = cur;
			prev->c_next = next = cp->c_next;
			cp->c_next = cs->cs_freelist;
			cp->c_xid = 0;	/* Indicates on free list */
			cs->cs_freelist = cp;
			cv_broadcast(&cp->c_done);
			cp = next;
		}
		/*
		 * At this point, we have completed all callouts which were
		 * scheduled to run at "runtime".  If the global run time
		 * (cs->cs_runtime) still matches our local copy (runtime),
		 * then we advance the global run time; otherwise, another
		 * callout thread must have already done so.
		 */
		if (cs->cs_runtime == runtime)
			cs->cs_runtime = runtime + 1;
	}
	TRACE_0(TR_FAC_CALLOUT, TR_CALLOUT_EXECUTE_END, "callout_execute_end");
}

/*
 * Standard timeout interface
 */
int
timeout(void (*func)(), caddr_t arg, long delta)
{
	int id;

	TRACE_4(TR_FAC_CALLOUT, TR_TIMEOUT_START,
		"timeout_start:%K(%x) in %d ticks called from %K",
		func, arg, delta, caller());
	mutex_enter(&callout_state.cs_lock);
	id = callout_insert(&callout_state, func, arg, delta);
	mutex_exit(&callout_state.cs_lock);
	TRACE_1(TR_FAC_CALLOUT, TR_TIMEOUT_END, "timeout_end:id %x", id);
	return (id);
}

/*
 * Realtime timeout interface.  NOTE: this is not an exported inteface
 * and should only be used to call functions which are time critical.
 */
int
realtime_timeout(void (*func)(), caddr_t arg, long delta)
{
	int id;

	TRACE_4(TR_FAC_CALLOUT, TR_RT_TIMEOUT_START,
		"RT_timeout_start:%K(%x) in %d ticks called from %K",
		func, arg, delta, caller());
	mutex_enter(&rt_callout_state.cs_lock);
	id = callout_insert(&rt_callout_state, func, arg, delta);
	mutex_exit(&rt_callout_state.cs_lock);
	TRACE_1(TR_FAC_CALLOUT, TR_RT_TIMEOUT_END, "RT_timeout_end:id %x", id);
	return (id);
}

/*
 * All callout tables use the same untimeout() interface.  This is possible
 * because the table ID is encoded in the callout ID.
 */
int
untimeout(int id)
{
	callout_state_t *cs;
	int time_left;

	TRACE_1(TR_FAC_CALLOUT, TR_UNTIMEOUT_START,
		"untimeout_start:id %x", id);
	cs = (id & CALLOUT_REALTIME) ? &rt_callout_state : &callout_state;
	mutex_enter(&cs->cs_lock);
	time_left = callout_delete(cs, id);
	mutex_exit(&cs->cs_lock);
	TRACE_1(TR_FAC_CALLOUT, TR_UNTIMEOUT_END,
		"untimeout_end:%d ticks left", time_left);
	return (time_left);
}

/*
 * When callout_schedule() detects that there are normal (as opposed to
 * realtime) callouts to be processed, it signals a callout thread to
 * do the work.  We support multiple callout threads so the system doesn't
 * constipate when the occasional pig runs from the callout table.  This is
 * (currently) a rare event, so for now, we just have two callout threads --
 * more generic thread pool management would add complexity, but not value.
 */
static void
callout_thread(void)
{
	callout_state_t *cs = &callout_state;

	mutex_enter(&cs->cs_lock);
	for (;;) {
		cv_wait(&cs->cs_threadpool, &cs->cs_lock);
		callout_execute(cs);
	}
}

/*
 * Realtime timeout processing.  Called via softint.
 */
/* ARGSUSED */
static void
realtime_timein(caddr_t arg)
{
	mutex_enter(&rt_callout_state.cs_lock);
	callout_execute(&rt_callout_state);
	mutex_exit(&rt_callout_state.cs_lock);
}

/*
 * Callback handler used by CPR.
 */
static callout_state_t *old_callout_state, *old_rt_callout_state;

/*ARGSUSED*/
static void
callout_cpr_callb(void *arg, int code)
{
	switch (code) {
	case CB_CODE_CPR_CHKPT:
		old_callout_state = kmem_zalloc(sizeof (callout_state_t),
			KM_SLEEP);
		old_rt_callout_state = kmem_zalloc(sizeof (callout_state_t),
			KM_SLEEP);
		callout_save(&callout_state, old_callout_state);
		callout_save(&rt_callout_state, old_rt_callout_state);
		break;

	case CB_CODE_CPR_RESUME:
		callout_restore(&callout_state, old_callout_state);
		callout_restore(&rt_callout_state, old_rt_callout_state);
		kmem_free(old_callout_state, sizeof (callout_state_t));
		kmem_free(old_rt_callout_state, sizeof (callout_state_t));
		break;
	}
}

/*
 * Save the context of the current callout table.
 */
static void
callout_save(callout_state_t *cs, callout_state_t *ocs)
{
	int i;

	mutex_enter(&cs->cs_lock);

	/* save the callout table before checkpointing */
	bcopy((caddr_t)cs, (caddr_t)ocs, sizeof (callout_state_t));

	/* clean up the bucket pointers and start a new callout sequence */
	cs->cs_busyticks = 0;
	for (i = 0; i < CALLOUT_BUCKETS; i++) {
		cs->cs_bucket[i].b_first = (callout_t *)&cs->cs_bucket[i];
	}

	/* shouldn't be waiting on the old callout that's not there */
	cv_signal(&cs->cs_threadpool);

	mutex_exit(&cs->cs_lock);
}

/*
 * Restore the old context of the callout table; we are replying on the
 * the callout_schedule() to forward the cs_curtime; it also has the effect
 * of timeout'ing all of the leftovers from the checkpointing time.
 */
static void
callout_restore(callout_state_t *cs, callout_state_t *ocs)
{
	int i;
	callout_t *cp;
	callout_bucket_t *bucket, *obucket;

	mutex_enter(&cs->cs_lock);

	for (i = 0; i < CALLOUT_BUCKETS; i++) {

		bucket = &cs->cs_bucket[i];
		obucket = &ocs->cs_bucket[i];
		cp = bucket->b_first;

		/* insert the current callouts into the old ones */
		while (cp != (callout_t *)bucket) {
			callout_t *next;

			next = cp->c_next;
			cp->c_next = obucket->b_first;
			obucket->b_first = cp;
			cp = next;
		}
		bcopy((caddr_t)obucket, (caddr_t)bucket,
			sizeof (callout_bucket_t));
	}
	cs->cs_busyticks += ocs->cs_busyticks;
	cs->cs_runtime = ocs->cs_runtime;
	mutex_exit(&cs->cs_lock);
}
