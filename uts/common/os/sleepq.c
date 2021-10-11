/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sleepq.c	1.36	94/07/29 SMI"

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
#include <sys/tblock.h>

/*
 * Operations on sleepq_t structures.
 */

/*
 * Insert thread "c" into sleep queue "spq" in
 * dispatch priority order.
 */
void
sleepq_insert(sleepq_t *spq, kthread_t *c)
{
	kthread_t	*tp;
	kthread_t	**tpp;
	pri_t		cpri;

	ASSERT(THREAD_LOCK_HELD(c));	/* holding the lock on the turnstile */
	cpri = DISP_PRIO(c);
	tpp = &spq->sq_first;
	while ((tp = *tpp) != NULL) {
		if (cpri > DISP_PRIO(tp))
			break;
		tpp = &tp->t_link;
	}
	*tpp = c;
	c->t_link = tp;
}	/* end of sleepq_insert */

/*
 * What to do to initialize a sleeping thread when
 * we are going to wake it up.
 */
#define	THREAD_CLEAN(tp)	\
	(tp)->t_sobj_ops = NULL;	\
	(tp)->t_ts = NULL;	\
	(tp)->t_wchan = NULL;	\
	(tp)->t_wchan0 = NULL

/*
 * Wake the first (highest dispatch priority) thread
 * in sleep queue "spq"
 */
void
sleepq_wakeone(sleepq_t *spq)
{
	register kthread_t *tp;
	register kthread_t **tpp;

	tpp = &spq->sq_first;
	if ((tp = *tpp) != NULL) {
		ASSERT(THREAD_LOCK_HELD(tp));	/* thread locked via sleepq */
		*tpp = tp->t_link;
		tp->t_link = NULL;
		THREAD_CLEAN(tp);
		ASSERT(tp->t_state == TS_SLEEP);
		CL_WAKEUP(tp);
		thread_unlock_high(tp);		/* drop the run queue lock */
	}
}	/* end of sleepq_wakeone */

/*
 * Wake all threads in sleep queue "spq".
 * Returns the # of threads awakened.
 */
void
sleepq_wakeall(sleepq_t *spq)
{
	register kthread_t 	*tp;
	register kthread_t	**tpp;

	tpp = &spq->sq_first;
	while ((tp = *tpp) != NULL) {
		ASSERT(THREAD_LOCK_HELD(tp));	/* thread locked via sleepq */
		*tpp = tp->t_link;
		tp->t_link = NULL;
		THREAD_CLEAN(tp);
		ASSERT(tp->t_state == TS_SLEEP);
		CL_WAKEUP(tp);
		thread_unlock_high(tp);		/* drop the run queue lock */
	}
	spq->sq_first = NULL;
}	/* end of sleepq_wakeall */

/*
 * Yank a particular thread out of sleep queue "spq" and
 * wake it up.
 */
kthread_t *
sleepq_unsleep(sleepq_t *spq, kthread_t *t)
{
	register kthread_t	*nt;
	register kthread_t	**ptl;

	ASSERT(THREAD_LOCK_HELD(t));	/* thread locked via sleepq */

	ptl = &spq->sq_first;
	while ((nt = *ptl) != NULL) {
		if (nt == t) {
			*ptl = t->t_link;
			t->t_link = NULL;
			THREAD_CLEAN(t);
			ASSERT(t->t_state == TS_SLEEP);
			/*
			 * Change thread to transition state without
			 * dropping the sleep queue lock.
			 */
			THREAD_TRANSITION_NOLOCK(t);
			return (t);
		}
		ptl = &nt->t_link;
	}
	return (NULL);
}	/* end of sleepq_unsleep */

/*
 * Yank a particular thread out of sleep queue
 * "spq" but don't wake it up.
 */
kthread_t *
sleepq_dequeue(sleepq_t *spq, kthread_t *t)
{
	register kthread_t	*nt;
	register kthread_t	**ptl;

	ASSERT(THREAD_LOCK_HELD(t));	/* thread locked via sleepq */

	ptl = &spq->sq_first;
	while ((nt = *ptl) != NULL) {
		if (nt == t) {
			*ptl = t->t_link;
			t->t_link = NULL;
			return (t);
		}
		ptl = &nt->t_link;
	}
	return (NULL);
}	/* end of sleepq_dequeue */

/*
 * The stuff below is for the support of
 * the old sleep queue mechanism.
 */

static sleepq_head_t	sleepq_head[NSLEEPQ];

sleepq_head_t *
sqhash(caddr_t chan)
{
	return (&sleepq_head[sqhashindex(chan)]);
}	/* end of sqhash */

void
sleepq_wakeone_chan(sleepq_t *spq, caddr_t chan)
{
	register kthread_t 	*tp;
	register kthread_t	**tpp;

	tpp = &spq->sq_first;
	while ((tp = *tpp) != NULL) {
		if (tp->t_wchan == chan && tp->t_wchan0 == 0) {
			*tpp = tp->t_link;
			tp->t_wchan = NULL;
			tp->t_link = NULL;
			tp->t_sobj_ops = NULL;
			ASSERT(tp->t_state == TS_SLEEP);
			CL_WAKEUP(tp);
			thread_unlock_high(tp);		/* drop runq lock */
			return;
		}
		tpp = &tp->t_link;
	}
}	/* end of sleepq_wakeone_chan */

void
sleepq_wakeall_chan(sleepq_t *spq, caddr_t chan)
{
	register kthread_t 	*tp;
	register kthread_t	**tpp;

	tpp = &spq->sq_first;
	while ((tp = *tpp) != NULL) {
		if (tp->t_wchan == chan && tp->t_wchan0 == 0) {
			*tpp = tp->t_link;
			tp->t_wchan = NULL;
			tp->t_link = NULL;
			tp->t_sobj_ops = NULL;
			ASSERT(tp->t_state == TS_SLEEP);
			CL_WAKEUP(tp);
			thread_unlock_high(tp);		/* drop runq lock */
			continue;
		}
		tpp = &tp->t_link;
	}
}	/* end of sleepq_wakeall_chan */
