/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prioinherit.c	1.39	95/05/01 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/t_lock.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/disp.h>
#include <sys/cpuvar.h>
#include <sys/pirec.h>
#include <sys/sleepq.h>
#include <sys/tblock.h>
#include <sys/sobject.h>
#include <sys/prioinherit.h>

#ifdef	PISTATS

typedef struct pi_stats {
	u_int	pi_inversions;
	u_int	pi_expostfacto;
	u_int	pi_onrunq;
	u_int	pi_onsleepq;
	u_int	pi_onproc;
} pi_stats_t;

pi_stats_t	pi_stats =
	{ 0, 0, 0, 0, 0 };

#define	PISTATS_WILLTO() ++pi_stats.pi_inversions

#define	PISTATS_EXPOST()	\
	{ ++pi_stats.pi_inversions; ++pi_stats.pi_expostfacto; }

#define	PISTATS_SLEEP()		++pi_stats.pi_onsleepq
#define	PISTATS_RUNQ()		++pi_stats.pi_onrunq
#define	PISTATS_ONPROC()	++pi_stats.pi_onproc


#else

#define	PISTATS_WILLTO()
#define	PISTATS_EXPOST()
#define	PISTATS_SLEEP()
#define	PISTATS_RUNQ()
#define	PISTATS_ONPROC()

#endif	/* PISTATS */

#ifdef	PIREC_VERIFY
#define	PIREC_VRFY(p, b)	pirec_verify(p, b)
#else
#define	PIREC_VRFY(p, b)
#endif	/* PIREC_VERIFY */

static void	pi_changepri(kthread_t *, pri_t);

static void
deadlock_panic(kthread_t *t)
{
	cmn_err(CE_WARN, "Deadlock detected: cycle in blocking chain.\n");
	cmn_err(CE_PANIC, "Thread: 0x%x Blocked on %s: 0x%x Owner: 0x%x\n",
		(int)t, (int)t->t_sobj_ops->sobj_class, (int)t->t_wchan,
		(int)SOBJ_OWNER(t->t_sobj_ops, t->t_wchan));
}

/*
 * Operations to perform when a thread wishes to "will" its
 * dispatch priority to all the threads blocking it, directly
 * or indirectly.
 *
 * Assumptions: This is called for a thread that has already
 *		been blocked on a synchronization object.
 *
 * We save information in the thread structure about
 * the synchronization object we are blocking on, so
 * that the priority inheritance mechanisms can get
 * it to resolve indirect blocking scenarios.
 *
 * If the synchronization object is "priority inverted"
 * (i.e., the assigned priority of the thread holding
 * the synchronization object is less than the dispatch
 * priority of the requesting thread), we apply priority
 * inheritance to the thread holding the synchronization
 * object, and potentially do the same for every thread
 * in the blocking chain.
 *
 * Conditions:	Called with the thread lock held on the blocked
 *		thread.
 *
 * Results: The dispatch priority of blocked is propagated to all
 *	    threads in the blocking chain.
 *
 * Side Effects: The thread lock on the blocked thread is released
 *		 before return.
 */

void
pi_willto(kthread_t *waiter)
{
	register kthread_t	*owner;
	register kthread_t	*thread_willing;

	ASSERT(THREAD_LOCK_HELD(waiter));
	/*
	 * Get a pointer to the thread that is blocking
	 * the execution of "waiter".
	 */
	owner = SOBJ_OWNER(waiter->t_sobj_ops, waiter->t_wchan);

	/*
	 * Save a pointer to the thread initiating the priority
	 * inheritance--we can't assume this is simply curthread,
	 * since improved CV/MUTEXes would need to call pi_willto()
	 * on a thread we are waking up.
	 */
	thread_willing = waiter;

	/*
	 * Follow the blocking chain to its end.
	 */
	while ((owner != NULL) && (waiter != NULL)) {
		pirec_t		*p;
		turnstile_t	*ts;
		pri_t		disp_pri;

		/*
		 * Check for simple deadlocks due to cycles in
		 * the blocking chain.
		 */
		if (thread_willing == owner) {
			thread_unlock(waiter);
			deadlock_panic(thread_willing);
		}

		disp_pri = DISP_PRIO(waiter);

		/*
		 * If "owner" is not priority inverted
		 * with respect to "waiter", then we don't
		 * need to follow the blocking chain any
		 * farther--so let's bail out.
		 */
		if (disp_pri <= owner->t_pri) {
			thread_unlock_nopreempt(waiter);
			return;
		}
		thread_lock_high(owner);
		PISTATS_WILLTO();
		ts = waiter->t_ts;
		p = &ts->tsun.ts_prioinv;
		if (TSTILE_PRIO_INVERTED(ts)) {
			/*
			 * The owner of this synchronization object is
			 * already inheriting a priority via another
			 * thread.  Check to see whether we need to
			 * raise the dispatch priority associated with
			 * this synch. object.
			 */
			PIREC_RAISE(p, (u_int)disp_pri);
		} else {
			/*
			 * The synchronization object is priority
			 * inverted with respect to the thread
			 * "owner"; mark the pirec in the synch.
			 * object's turnstile to reflect this, and
			 * put the pirec/turnstile into the owning
			 * thread's pirec list.
			 */
			pirec_init(p, owner, disp_pri);
			if (owner->t_prioinv == NULL) {
				owner->t_prioinv = p->pi_forw =
					p->pi_back = p;
			} else pirec_insque(p, owner->t_prioinv);
			PIREC_VRFY(owner->t_prioinv, owner);
		}
		/*
		 * Check whether this new inheritance causes a
		 * change in the dispatch priority of the owning
		 * thread. If so, change its priority.
		 */
		if (disp_pri > DISP_PRIO(owner))
			pi_changepri(owner, disp_pri);
		thread_unlock_high(waiter);
		waiter = owner;
		if (waiter->t_sobj_ops != NULL) {
			owner = SOBJ_OWNER(waiter->t_sobj_ops, waiter->t_wchan);
			/*
			 * Check if this is a mutex without an owner.  If so,
			 * wake it up.  The thread that should be acquiring the
			 * mutex may be low priority and unable to run.  If we
			 * guessed wrong it will just go back to sleep.
			 */
			if (owner == NULL &&
			    SOBJ_TYPE(waiter->t_sobj_ops) == SOBJ_MUTEX)
				SOBJ_UNSLEEP(waiter->t_sobj_ops, waiter);
		} else {
			owner = NULL;
		}
	}
	thread_unlock_nopreempt(waiter);
}	/* end of pi_willto */


/*
 * Ex post facto priority inheritance. When a thread
 * acquires a synchronization object on which there
 * are other threads already blocked, priority inversion
 * can occur. We provide a mechanism for letting the
 * acquiring thread inherit from the blocked threads,
 * when necessary.
 *
 * Calling pi_inheritfrom() makes thread "owner" inherit a
 * dispatch (effective) priority equal to the maximum
 * dispatch priority of all the threads waiting in the
 * sleep queues of the turnstile referenced by "ts".
 *
 * Note that it is possible that "owner" may already have
 * an inherited priority.
 *
 * It is assumed that "owner" isn't already inheriting
 * from this s-object/turnstile.
 *
 * Thus, no checks need to be made to see whether this lock
 * is already in owner's pirec_t list.
 */
static void
pi_inheritfrom(kthread_t *owner, turnstile_t *ts)
{
	pri_t		disp_pri;

	ASSERT(THREAD_LOCK_HELD(owner));

	disp_pri = tstile_maxpri(ts);
	if (disp_pri > owner->t_pri) {
		pirec_t		*p;

		PISTATS_EXPOST();
		p = &ts->tsun.ts_prioinv;
		/*
		 * The synchronization object is priority
		 * inverted with respect to the thread
		 * "owner"; mark the pirec in the synch.
		 * object's turnstile to reflect this, and
		 * put the pirec/turnstile into the owning
		 * thread's pirec list.
		 */
		pirec_init(p, owner, disp_pri);
		if (owner->t_prioinv == NULL) {
			owner->t_prioinv = p->pi_forw =
				p->pi_back = p;
		} else pirec_insque(p, owner->t_prioinv);
		PIREC_VRFY(owner->t_prioinv, owner);
		/*
		 * Check whether this new inheritance causes a
		 * change in the dispatch priority of the owner
		 * thread. If so, change its priority.
		 */
		if (disp_pri > DISP_PRIO(owner))
			pi_changepri(owner, disp_pri);
	}
}	/* end of pi_inheritfrom */

/*
 * Operations to perform when we wish to cancel
 * any priority inheritance that a thread may
 * have obtained via a particular synchronization
 * object.
 *
 * This primitive is generally called when a thread
 * is releasing a synchronization object.
 *
 * The operations are:
 *
 * o Clear the pirec_t structure in the turnstile_t,
 *   so that the synchronization object being released
 *   is no longer marked as "priority inverted."
 *
 * o Remove the pirec_t structure from the releasing
 *   thread's ("owner"'s) list of pirecs for held priority
 *   inverted synchronization objects. If this is the last
 *   pirec_t in the releasing thread's list, then cancel
 *   priority inheritance for the releasing thread.
 */
void
pi_waive(turnstile_t *ts)
{
	pirec_t		*p;
	kthread_t	*benef;

	if (ts == 0)
		return;
	/*
	 * Clear the inheritance information attached to
	 * the synchronization object and the owning thread.
	 */
	p = &ts->tsun.ts_prioinv;
	benef = p->pi_benef;


	/*
	 * o Remove a pirec_t from thread "benef"'s list of
	 *   priority inverted locks.
	 *
	 * o If this is the last pirec_t in the thread's
	 *   list, cancel priority inheritance for this
	 *   thread.
	 *
	 * o Otherwise, traverse thread "benef"'s pirec_t list,
	 *   recalculating the thread's dispatch (effective)
	 *   priority.
	 */

	if (benef != NULL) {
		thread_lock_high(benef);
		if (p->pi_forw == p) {
			/*
			 * Last pirec: Cancel priority inheritance.
			 */
			pi_changepri(benef, 0);
			benef->t_prioinv = NULL;
		} else {
			pri_t		disp_pri;

			if (benef->t_prioinv == p)
				benef->t_prioinv = p->pi_forw;
			pirec_remque(p);
			/*
			 * Reevaluate the inherited priority.
			 */
			disp_pri = pirec_calcpri(benef->t_prioinv,
							benef->t_pri);
			if (disp_pri != DISP_PRIO(benef))
				pi_changepri(benef, disp_pri);
		}
		pirec_clear(p);
		if (DISP_PRIO(benef) < DISP_MAXRUNPRI(benef))
			cpu_surrender(benef);
		thread_unlock_high(benef);
	}
}	/* end of pi_waive */

/*
 * We want to ensure that the thread "inheritor", which owns
 * a synchronization object, is the beneficiary of any inheritance
 * that may accrue from the synchronization object.
 *
 * If some other thread is the current beneficiary,
 * change the beneficiary to "inheritor".
 * Conditions:	The thread "inheritor" and the waiters
 *		field of the synchronization object must
 *		be locked.
 */
void
pi_amend(kthread_t *inheritor, turnstile_t *ts)
{
	kthread_t	*benef;
	pirec_t		*p;

	ASSERT(THREAD_LOCK_HELD(inheritor));

	if (ts == NULL)
		return;

	/*
	 * Clear the inheritance information attached to
	 * the synchronization object and the owning thread.
	 */
	p = &ts->tsun.ts_prioinv;
	benef = p->pi_benef;


	if (benef == inheritor)
		return;

	if (benef != NULL) {
		register int	lock_benef;

		/*
		 * Check to see whether we need to lock the beneficiary.
		 * If the inheritor thread is an interrupt thread, it
		 * might be pinning the beneficiary. If that is the
		 * case, both threads t_lockp will be pointing to the
		 * same CPU disp_lock.
		 */

		lock_benef = (benef->t_lockp != inheritor->t_lockp);
		if (lock_benef)
			thread_lock_high(benef);
		if (p->pi_forw == p) {
			/*
			 * Last pirec: Cancel priority inheritance.
			 */
			pi_changepri(benef, 0);
			benef->t_prioinv = NULL;
		} else {
			pri_t	dpri;

			if (benef->t_prioinv == p)
				benef->t_prioinv = p->pi_forw;
			pirec_remque(p);
			/*
			 * Reevaluate the inherited priority.
			 */
			dpri = pirec_calcpri(benef->t_prioinv, benef->t_pri);
			if (dpri != DISP_PRIO(benef))
				pi_changepri(benef, dpri);
		}
		pirec_clear(p);
		if (DISP_PRIO(benef) < DISP_MAXRUNPRI(benef))
			cpu_surrender(benef);
		if (lock_benef)
			thread_unlock_high(benef);
	}
	pi_inheritfrom(inheritor, ts);
}	/* end of pi_amend */

/*
 * Change the dispatch priority of a thread in the system.
 * Used by priority inheritance when raising or lowering a
 * beneficiary thread's priority.
 *
 * Since threads are queued according to their priority, we
 * we must check the thread's state to determine whether it
 * is on a queue somewhere. If it is, we've got to:
 *
 *	o Dequeue the thread.
 *	o Change its priority.
 *	o Enqueue the thread.
 *
 * Assumptions: The thread whose priority we wish to change
 * must be locked before we call pi_changepri(). The pi_changepri()
 * function doesn't drop the thread lock--that must be done by
 * its caller.
 */
static void
pi_changepri(kthread_t *t, pri_t disp_pri)
{
	register u_int	state;

	ASSERT(THREAD_LOCK_HELD(t));

	state = t->t_state;

	/*
	 * If it's not on a queue, change the priority with
	 * impunity.
	 */
	if ((state & (TS_SLEEP | TS_RUN)) == 0) {
		PISTATS_ONPROC();
		t->t_epri = disp_pri;
		return;
	}

	/*
	 * It's either on a sleep queue or a run queue.
	 */
	if (state == TS_SLEEP) {

		/*
		 * Take the thread out of its sleep queue.
		 * Change the inherited priority.
		 * Re-enqueue the thread.
		 * Each synchronization object exports a function
		 * to do this in an appropriate manner.
		 */
		PISTATS_SLEEP();
		SOBJ_CHANGEPRI(t->t_sobj_ops, t, disp_pri);
	} else {
		/*
		 * The thread is on a run queue.
		 * Note: setbackdq() may not put the thread
		 * back on the same run queue where it originally
		 * resided.
		 */
		PISTATS_RUNQ();
		(void) dispdeq(t);
		t->t_epri = disp_pri;
		setbackdq(t);
	}
}	/* end of pi_changepri */
