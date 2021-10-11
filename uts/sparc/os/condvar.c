/*
 * Copyright (c) 1991, Sun Microsystems, Inc.
 *
 * This file contains the intrinsic condition variable routines.
 */

#pragma ident	"@(#)condvar.c	1.62	94/08/14 SMI"

#include	<sys/thread.h>
#include	<sys/proc.h>
#include	<sys/debug.h>
#include	<sys/debug/debug.h>
#include	<sys/cmn_err.h>
#include	<sys/systm.h>
#include	<sys/kmem.h>
#include	<sys/sobject.h>
#include	<sys/tblock.h>
#include	<sys/sleepq.h>
#include	<sys/kmem.h>
#include	<sys/cpuvar.h>
#include	<sys/condvar.h>
#include	<sys/condvar_impl.h>

extern char *panicstr;
extern void panic_hook();

kthread_t * cv_owner(caddr_t cv);
static void	cv_unsleep(kthread_t *t);
static void	cv_changepri(kthread_t *t, pri_t pri);

/*
 * The sobj_ops vector exports a set of functions needed
 * when a thread is asleep on a synchronization object of
 * this type.
 *
 * Every blocking s-object should define one of these
 * structures and set the t_sobj_ops field of blocking
 * threads to it's address.
 *
 * Current users are setrun() and pi_changepri().
 */
static sobj_ops_t	cv_sops = {
	"Condition Variable",
	SOBJ_CV,
	QOBJ_CV,
	cv_owner,
	cv_unsleep,
	cv_changepri
};


/*
 * The cv_block() function blocks a thread on a condition variable
 * by putting it in a hashed sleep queue associated with the
 * synchronization object.
 *
 * Threads are taken off the hashed sleep queues via calls to
 * cv_signal(), cv_broadcast(), or cv_unsleep().
 *
 */

/* ARGSUSED */
void
cv_init(kcondvar_t *cvp, char *name, kcv_type_t type, void *arg)
{
	condvar_impl_t	*cv = (condvar_impl_t *)cvp;

	switch (type) {

	case CV_DEFAULT:
	case CV_DRIVER:
		cv->cv_waiters = 0;
		break;
	default:
		cmn_err(CE_PANIC, "cv_init: bad type %d", type);
		break;
	}
}

/*
 * cv_destroy is not currently needed, but is part of the DDI.
 * This is in case cv_init ever needs to allocate something for a cv.
 */
/* ARGSUSED */
void
cv_destroy(kcondvar_t *cvp)
{
}

static void
cv_block(condvar_impl_t *cvp)
{
	register sleepq_head_t	*sqh;
	register klwp_t *lwp	= ttolwp(curthread);

	ASSERT(THREAD_LOCK_HELD(curthread));
	ASSERT(curthread != CPU->cpu_idle_thread);
	ASSERT(CPU->cpu_on_intr == 0);
	ASSERT(curthread->t_wchan0 == 0 && curthread->t_wchan == NULL);
	ASSERT(curthread->t_state == TS_ONPROC);

	CL_SLEEP(curthread, 0);			/* assign kernel priority */
	curthread->t_wchan = (caddr_t)cvp;
	curthread->t_sobj_ops = &cv_sops;
	if (lwp != NULL) {
		lwp->lwp_ru.nvcsw++;
		if (curthread->t_proc_flag & TP_MSACCT)
			(void) new_mstate(curthread, LMS_SLEEP);
	}

	sqh = sqhash((caddr_t)cvp);
	disp_lock_enter_high(&sqh->sq_lock);
	ASSERT(cvp->cv_waiters < 65535);
	++cvp->cv_waiters;
	THREAD_SLEEP(curthread, &sqh->sq_lock);
	sleepq_insert(&sqh->sq_queue, curthread);
	/*
	 * THREAD_SLEEP() moves curthread->t_lockp to point to the
	 * lock sqh->sq_lock. This lock is later released by the caller
	 * when it calls thread_unlock() on curthread.
	 */
}

#define	cv_block_sig(cvp)	\
	{ curthread->t_flag |= T_WAKEABLE; cv_block(cvp); }


/*
 * Block on the indicated condition variable and release the
 * associated kmutex while blocked.
 */
void
cv_wait(kcondvar_t *cvp, kmutex_t *mp)
{
	if (panicstr) {
		panic_hook();
		return;
	}
	ASSERT(curthread->t_schedflag & TS_DONT_SWAP);
	thread_lock(curthread);			/* lock the thread */
	cv_block((condvar_impl_t *)cvp);
	thread_unlock_nopreempt(curthread);	/* unlock the waiters field */
	mutex_exit(mp);
	swtch();
	mutex_enter(mp);
}


/*
 * Same as cv_wait except the thread will unblock at 'tim'
 * (an absolute time) if it hasn't already unblocked.
 *
 * Returns the amount of time left from the original 'tim' value
 * when it was unblocked.
 */
int
cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, long tim)
{
	int		id;
	int		timeleft;

	if (panicstr) {
		panic_hook();
		return (0);
	}
	if (tim <= lbolt)
		return (-1);
	id = realtime_timeout(setrun, (caddr_t)curthread, tim - lbolt);
	thread_lock(curthread);		/* lock the thread */
	cv_block((condvar_impl_t *)cvp);
	thread_unlock_nopreempt(curthread);
	mutex_exit(mp);
	if (tim <= lbolt)
		setrun(curthread);
	swtch();
	/*
	 * Get the time left. untimeout() returns -1 if the timeout has
	 * occured or the time remaining.  If the time remaining is zero,
	 * the timeout has occured between when we were awoken and
	 * we called untimeout.  We will treat this as if the timeout
	 * has occured and set timeleft to -1.
	 */
	timeleft = untimeout(id);
	if (timeleft <= 0)
		timeleft = -1;
	mutex_enter(mp);
	return (timeleft);
}

int
cv_wait_sig(kcondvar_t *cvp, kmutex_t *mp)
{
	register kthread_t *t = curthread;
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	int rval = 1;

	if (panicstr) {
		panic_hook();
		return (rval);
	}

	if (lwp == NULL) {
		cv_wait(cvp, mp);
		return (rval);
	}

	ASSERT(curthread->t_schedflag & TS_DONT_SWAP);
	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	if (sigwaiting_check(mp) == 0) {
		thread_lock(t);
		cv_block_sig((condvar_impl_t *)cvp);
		thread_unlock_nopreempt(t);
		mutex_exit(mp);
		if (ISSIG(t, JUSTLOOKING) || ISHOLD(p))
			setrun(t);
		/* ASSERT(no locks are held) */
		swtch();
		t->t_flag &= ~T_WAKEABLE;
		sigwaiting_decr();
		mutex_enter(mp);
	}
	if (ISSIG_PENDING(t, lwp, p)) {
		mutex_exit(mp);
		if (issig(FORREAL))
			rval = 0;
		mutex_enter(mp);
	}
	if (lwp->lwp_sysabort || ISHOLD(p))
		rval = 0;
	lwp->lwp_asleep = 0;
	lwp->lwp_sysabort = 0;
	return (rval);
}


/*
 * Returns:
 * 	Function result in order of presidence:
 *			 0 if if a signal was recieved
 *			-1 if timeout occured
 *			 1 if awakened via cv_signal() or cv_broadcast().
 *
 * cv_timedwait_sig() is now part of the DDI.
 */
int
cv_timedwait_sig(kcondvar_t *cvp, kmutex_t *mp, long tim)
{
	register proc_t *p = ttoproc(curthread);
	register klwp_t *lwp = ttolwp(curthread);
	int		id;
	int		ret_value = 1;

	if (panicstr) {
		panic_hook();
		return (ret_value);
	}

	/*
	 * If there is no lwp, then we don't need to wait for a signal.
	 */
	if (lwp == NULL) {
		return (cv_timedwait(cvp, mp, tim));
	}

	/*
	 * If tim is less than or equal to lbolt, then the timeout
	 * has already occured.  So just check to see if there is a signal
	 * pending.  If so return 0 indicating that there is a signal pending.
	 * Else return -1 indicating that the timeout occured. No need to
	 * wait on anything.
	 */
	if (tim <= lbolt) {
		lwp->lwp_asleep = 1;
		lwp->lwp_sysabort = 0;
		ret_value = -1;
		if (ISSIG_PENDING(curthread, lwp, p)) {
			mutex_exit(mp);
			if (issig(FORREAL))
				ret_value = 0;
			mutex_enter(mp);
		}
		if (lwp->lwp_sysabort || ISHOLD(p))
			ret_value = 0;
		lwp->lwp_asleep = 0;
		lwp->lwp_sysabort = 0;
		return (ret_value);
	}

	/*
	 * Set the timeout and wait.
	 */
	id = realtime_timeout(setrun, (caddr_t)curthread, tim - lbolt);
	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	if (sigwaiting_check(mp) == 0) {
		thread_lock(curthread);
		cv_block_sig((condvar_impl_t *)cvp);
		thread_unlock_nopreempt(curthread);
		mutex_exit(mp);
		if (ISSIG(curthread, JUSTLOOKING) ||
		    ISHOLD(p) || (tim <= lbolt))
			setrun(curthread);
		/* ASSERT(no locks are held) */
		swtch();
		curthread->t_flag &= ~T_WAKEABLE;
		sigwaiting_decr();
		mutex_enter(mp);
	}

	/*
	 * Untimeout the thread.  untimeout() returns -1 if the timeout has
	 * occured or the time remaining.  If the time remaining is zero,
	 * the timeout has occured between when we were awoken and
	 * we called untimeout.  We will treat this as if the timeout
	 * has occured and set ret_value to -1.
	 */
	ret_value = untimeout(id);
	if (ret_value <= 0)
		ret_value = -1;

	/*
	 * Check to see if a signal is pending.  If so, regardless of whether
	 * or not we were awoken due to the signal, the signal is now pending
	 * and a return of 0 has the highest priority.
	 */
	if (ISSIG_PENDING(curthread, lwp, p)) {
		mutex_exit(mp);
		if (issig(FORREAL))
			ret_value = 0;
		mutex_enter(mp);
	}
	if (lwp->lwp_sysabort || ISHOLD(p))
		ret_value = 0;
	lwp->lwp_asleep = 0;
	lwp->lwp_sysabort = 0;
	return (ret_value);
}

/*
 * Same as cv_wait_sig but the thread can be swapped out while waiting.
 * This should only be used when we know we aren't holding any locks.
 */
int
cv_wait_sig_swap(kcondvar_t *cvp, kmutex_t *mp)
{
	register kthread_t *t = curthread;
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	int rval = 1;

	if (panicstr) {
		panic_hook();
		return (rval);
	}

	if (lwp == NULL) {
		cv_wait(cvp, mp);
		return (rval);
	}

	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	if (sigwaiting_check(mp) == 0) {
		thread_lock(t);
		t->t_kpri_req = 0;	/* don't need kernel priority */
		cv_block_sig((condvar_impl_t *)cvp);
		/* I can be swapped now */
		curthread->t_schedflag &= ~TS_DONT_SWAP;
		thread_unlock_nopreempt(t);
		mutex_exit(mp);
		if (ISSIG(t, JUSTLOOKING) || ISHOLD(p))
			setrun(t);
		/* ASSERT(no locks are held) */
		swtch();
		t->t_flag &= ~T_WAKEABLE;
		/* TS_DONT_SWAP set by disp() */
		ASSERT(curthread->t_schedflag & TS_DONT_SWAP);
		sigwaiting_decr();
		mutex_enter(mp);
	}
	if (ISSIG_PENDING(t, lwp, p)) {
		mutex_exit(mp);
		if (issig(FORREAL))
			rval = 0;
		mutex_enter(mp);
	}
	if (lwp->lwp_sysabort || ISHOLD(p))
		rval = 0;
	lwp->lwp_asleep = 0;
	lwp->lwp_sysabort = 0;
	return (rval);
}

void
cv_signal(kcondvar_t *cvp)
{
	register condvar_impl_t *cp;

	cp = (condvar_impl_t *)cvp;
	if (cp->cv_waiters > 0) {
		sleepq_head_t	*sqh;

		sqh = sqhash((caddr_t)cp);
		disp_lock_enter(&sqh->sq_lock);
		/*
		 * We could have gotten preempted after we previewed
		 * cv_waiters above. Check the state again.
		 */
		ASSERT(CPU->cpu_on_intr == 0);
		if (cp->cv_waiters > 0) {
			--cp->cv_waiters;
			sleepq_wakeone_chan(&sqh->sq_queue, (caddr_t)cp);
		}
		disp_lock_exit(&sqh->sq_lock);
	}
}

void
cv_broadcast(kcondvar_t *cvp)
{
	register condvar_impl_t *cp;

	cp = (condvar_impl_t *)cvp;
	if (cp->cv_waiters > 0) {
		sleepq_head_t	*sqh;

		sqh = sqhash((caddr_t)cp);
		disp_lock_enter(&sqh->sq_lock);
		/*
		 * We could have gotten preempted after we previewed
		 * cv_waiters above. Check the state again.
		 */
		ASSERT(CPU->cpu_on_intr == 0);
		if (cp->cv_waiters > 0) {
			cp->cv_waiters = 0;
			sleepq_wakeall_chan(&sqh->sq_queue, (caddr_t)cp);
		}
		disp_lock_exit(&sqh->sq_lock);
	}
}

/*
 * Allocation routine allows drivers to be binary compatible even
 * if the size of kcondvar_t changes.  This will be part of the Sun DDI.
 */
kcondvar_t *
cv_alloc(char *name, kcv_type_t type, void *arg)
{
	register kcondvar_t *cv;

	cv = (kcondvar_t *)kmem_alloc(sizeof (kcondvar_t), KM_SLEEP);
	cv_init(cv, name, type, arg);
	return (cv);
}

void
cv_free(kcondvar_t *cv)
{
	kmem_free((caddr_t)cv, sizeof (kcondvar_t));
}


/*
 * Threads don't "own" condition variables.
 */
/* ARGSUSED */
kthread_t *
cv_owner(caddr_t cv)
{
	return ((kthread_t *)NULL);
}

/*
 * Wakeup a thread asleep on a condition variable and
 * get it ready to put on the dispatch queue.
 * Called via SOBJ_UNSLEEP() from unsleep().
 */
static void
cv_unsleep(kthread_t *t)
{
	sleepq_head_t	*sqh;
	condvar_impl_t	*cvp;

	ASSERT(THREAD_LOCK_HELD(t));

	if ((cvp = (condvar_impl_t *)t->t_wchan) != NULL) {
		/*
		 * Because we've got the thread lock held,
		 * we've already acquired the lock on the
		 * condition variable's sleep queue.
		 */
		sqh = sqhash((caddr_t)cvp);
		if (sleepq_unsleep(&sqh->sq_queue, t) != NULL) {
			--cvp->cv_waiters;
			disp_lock_exit_high(&sqh->sq_lock);
			CL_SETRUN(t);
			return;
		}
	}
	cmn_err(CE_PANIC, "cv_unsleep: thread %x not on sleepq %x",
			(int)t, (int)sqh);
}

/*
 * Change the priority of a thread asleep on a
 * condition variable.
 */
static void
cv_changepri(kthread_t *t, pri_t pri)
{
	ASSERT(THREAD_LOCK_HELD(t));
	if (t->t_wchan) {
		sleepq_head_t   *sqh;

		sqh = sqhash(t->t_wchan);
		(void) sleepq_dequeue(&sqh->sq_queue, t);
		t->t_epri = pri;
		sleepq_insert(&sqh->sq_queue, t);
	} else {
		cmn_err(CE_PANIC,
			"cv_changepri: 0x%x not on sleep queue", (int)t);
	}

}
