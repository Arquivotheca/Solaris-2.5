#pragma ident	"@(#)semaphore.c	1.44	95/02/12 SMI"
/*
 * This file contains the semaphore operations.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/semaphore.h>
#include <sys/sema_impl.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/tblock.h>
#include <sys/disp.h>
#include <sys/cpuvar.h>
#include <sys/sleepq.h>

static void sema_unsleep(kthread_t *t);
static void sema_changepri(kthread_t *t, pri_t pri);
static kthread_t *sema_owner(ksema_t *);

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
static sobj_ops_t	sema_sops = {
	"Kernel Semaphore",	/* Name of class of s-objects */
	SOBJ_SEMA,		/* Numerical type */
	QOBJ_SEMA,		/* Q-number to use (if applicable) */
	sema_owner,		/* Return owner (if applicable) */
	sema_unsleep,		/* awaken and enable it to run */
	sema_changepri		/* change a blocked thread's priority */
};

/*
 * SEMA_BLOCK(sema_impl_t *s)
 * SEMA_BLOCK_SIG(sema_impl_t *s)
 */
#define	SEMA_BLOCK(s)							\
	{								\
		kthread_t	*tp;					\
		kthread_t	**tpp;					\
		pri_t		cpri;					\
		sleepq_head_t	*sqh;					\
		klwp_t	*lwp = ttolwp(curthread);			\
		ASSERT(THREAD_LOCK_HELD(curthread));			\
		ASSERT(curthread != CPU->cpu_idle_thread);		\
		ASSERT(CPU->cpu_on_intr == 0);				\
		ASSERT(curthread->t_wchan0 == 0);			\
		ASSERT(curthread->t_wchan == NULL);			\
		ASSERT(curthread->t_state == TS_ONPROC);		\
		CL_SLEEP(curthread, 0);					\
		sqh = sqhash((caddr_t)s);				\
		THREAD_SLEEP(curthread, &sqh->sq_lock);			\
		curthread->t_wchan = (caddr_t)s;			\
		curthread->t_sobj_ops = &sema_sops;			\
		if (lwp != NULL) {					\
			lwp->lwp_ru.nvcsw++;				\
			if (curthread->t_proc_flag & TP_MSACCT)		\
				(void) new_mstate(curthread, LMS_SLEEP); \
		}							\
		cpri = DISP_PRIO(curthread);				\
		tpp = &s->s_slpq;					\
		while ((tp = *tpp) != NULL) {				\
			if (cpri > DISP_PRIO(tp))			\
				break;					\
			tpp = &tp->t_link;				\
		}							\
		*tpp = curthread;					\
		curthread->t_link = tp;					\
		ASSERT(s->s_slpq != NULL);				\
	}								\

#define	SEMA_BLOCK_SIG(s)			\
	curthread->t_flag |= T_WAKEABLE;	\
	SEMA_BLOCK(s)

/* ARGSUSED */
void
sema_init(ksema_t *sp, unsigned count, char *name, ksema_type_t type, void *arg)
{
	sema_impl_t	*s;

	s = (sema_impl_t *)sp;
	switch (type) {

	case SEMA_DEFAULT:
	case SEMA_DRIVER:
		s->s_count = count;
		s->s_slpq = NULL;
		break;

	default:
		cmn_err(CE_PANIC, "sema_init: bad type %d, sema = 0x%x\n",
				type, (int)s);
		break;
	}
}

void
sema_destroy(ksema_t *sp)
{
	ASSERT(((sema_impl_t *)sp)->s_slpq == NULL);
	/* that's it now that ksema_t doesn't contain a disp lock */
}

/*
 * Put a thread on the sleep queue for this
 * semaphore.
 */
static void
sema_queue(ksema_t *sp, kthread_t *t)
{
	register kthread_t	**tpp;
	register kthread_t	*tp;
	register pri_t		cpri;
	register sema_impl_t	*s;

	ASSERT(THREAD_LOCK_HELD(t));
	s = (sema_impl_t *)sp;
	tpp = &s->s_slpq;
	cpri = DISP_PRIO(t);
	while ((tp = *tpp) != NULL) {
		if (cpri > DISP_PRIO(tp))
			break;
		tpp = &tp->t_link;
	}
	*tpp = t;
	t->t_link = tp;
}

/*
 * Remove a thread from the sleep queue for this
 * semaphore.
 */
static void
sema_dequeue(ksema_t *sp, kthread_t *t)
{
	register kthread_t	**tpp;
	register kthread_t	*tp;
	register sema_impl_t	*s;

	ASSERT(THREAD_LOCK_HELD(t));
	s = (sema_impl_t *)sp;
	tpp = &s->s_slpq;
	while ((tp = *tpp) != NULL) {
		if (tp == t) {
			*tpp = t->t_link;
			t->t_link = NULL;
			return;
		}
		tpp = &tp->t_link;
	}
}

/* ARGSUSED */
static kthread_t *
sema_owner(ksema_t *sp)
{
	return ((kthread_t *)NULL);
}

/*
 * Wakeup a thread sleeping on a semaphore, and put it
 * on the dispatch queue.
 * Called via SOBJ_UNSLEEP().
 */
static void
sema_unsleep(kthread_t *t)
{
	register kthread_t	**tpp;
	register kthread_t	*tp;
	register sema_impl_t	*s;

	ASSERT(THREAD_LOCK_HELD(t));
	s = (sema_impl_t *)t->t_wchan;
	tpp = &s->s_slpq;
	while ((tp = *tpp) != NULL) {
		if (tp == t) {
			*tpp = t->t_link;
			t->t_link = NULL;
			t->t_sobj_ops = NULL;
			t->t_wchan = NULL;
			t->t_wchan0 = NULL;
			/*
			 * Change thread to transition state and
			 * drop the semaphore sleep queue lock.
			 */
			THREAD_TRANSITION(t);
			CL_SETRUN(t);
			return;
		}
		tpp = &tp->t_link;
	}
}

/*
 * operations to perform when changing the priority
 * of a thread asleep on a semaphore.
 * Called via SOBJ_CHANGEPRI().
 */
static void
sema_changepri(kthread_t *t, pri_t pri)
{
	ksema_t *sp;

	if ((sp = (ksema_t *)t->t_wchan) != NULL) {
		sema_dequeue(sp, t);
		t->t_epri = pri;
		sema_queue(sp, t);
	} else {
		cmn_err(CE_PANIC,
			"sema_changepri: 0x%x not on sleep queue", (int)t);
	}
}


/*
 * the semaphore is granted when the semaphore's
 * count is greater than zero and blocks when equal
 * to zero.
 */
void
sema_p(ksema_t *sp)
{
	sema_impl_t	*s;
	sleepq_head_t	*sqh;

	s = (sema_impl_t *)sp;
	sqh = sqhash((caddr_t)s);
	disp_lock_enter(&sqh->sq_lock);
	if (s->s_count-- > 0) {
		disp_lock_exit(&sqh->sq_lock);
	} else {
		if (panicstr) {
			disp_lock_exit(&sqh->sq_lock);
			panic_hook();
			return;
		}
		thread_lock_high(curthread);
		SEMA_BLOCK(s);
		thread_unlock_nopreempt(curthread);
		if (UNSAFE_DRIVER_LOCK_HELD()) {
			mutex_exit(&unsafe_driver);
					/* XXX was mutex_exit_locked */
			swtch();
			mutex_enter(&unsafe_driver);
		} else {
			swtch();
		}
	}
}

/*
 * similiar to sema_p except that it blocks at an interruptible
 * priority. if a signal is present then return 1 otherwise 0.
 */
int
sema_p_sig(ksema_t *sp)
{
	register kthread_t	*t = curthread;
	register klwp_t		*lwp = ttolwp(t);
	register sema_impl_t	*s;
	register sleepq_head_t	*sqh;

	if (lwp == NULL) {
		sema_p(sp);
		return (0);
	}

	s = (sema_impl_t *)sp;
	sqh = sqhash((caddr_t)s);
	disp_lock_enter(&sqh->sq_lock);
	s->s_count--;
	while (s->s_count < 0) {
		register proc_t *p = ttoproc(t);
		thread_lock_high(t);
		SEMA_BLOCK_SIG(s);
		lwp->lwp_asleep = 1;
		lwp->lwp_sysabort = 0;
		thread_unlock_nopreempt(t);
		if (ISSIG(t, JUSTLOOKING) || ISHOLD(p))
			setrun(t);
		swtch();
		t->t_flag &= ~T_WAKEABLE;
		if (ISSIG(t, FORREAL) || lwp->lwp_sysabort || ISHOLD(p)) {
			lwp->lwp_asleep = 0;
			lwp->lwp_sysabort = 0;
			disp_lock_enter(&sqh->sq_lock);
			s->s_count++;
			disp_lock_exit(&sqh->sq_lock);
			return (1);
		}
		lwp->lwp_asleep = 0;
		disp_lock_enter(&sqh->sq_lock);
	}
	disp_lock_exit(&sqh->sq_lock);
	return (0);
}

/*
 * the semaphore's count is incremented by one. a blocked thread
 * is awakened and re-tries to acquire the semaphore.
 */
void
sema_v(ksema_t *sp)
{
	sema_impl_t *s;
	register kthread_t *tp;
	sleepq_head_t	*sqh;

	s = (sema_impl_t *)sp;
	sqh = sqhash((caddr_t)s);
	disp_lock_enter(&sqh->sq_lock);
	if (s->s_count++ < 0) {
		kthread_t *sq;

		if (panicstr) {
			disp_lock_exit(&sqh->sq_lock);
			return;
		}
		sq = s->s_slpq;
		if (sq == NULL) {
			cmn_err(CE_PANIC, "sema_v");
		}
		tp = sq;
		ASSERT(THREAD_LOCK_HELD(tp));
		sq = sq->t_link;
		tp->t_link = NULL;
		tp->t_sobj_ops = NULL;
		tp->t_wchan = NULL;
		ASSERT(tp->t_state == TS_SLEEP);
		CL_WAKEUP(tp);
		s->s_slpq = sq;
		disp_lock_exit_high(&sqh->sq_lock);
		thread_unlock(tp);
	} else {
		disp_lock_exit(&sqh->sq_lock);
	}
}

/*
 * try to acquire the semaphore. if the semaphore is greater than
 * zero, then the semaphore is granted and returns 1. otherwise
 * return 0.
 */
int
sema_tryp(ksema_t *sp)
{
	sema_impl_t	*s;
	sleepq_head_t	*sqh;

	int	gotit = 0;

	s = (sema_impl_t *)sp;
	sqh = sqhash((caddr_t)s);
	disp_lock_enter(&sqh->sq_lock);
	if (s->s_count > 0) {
		s->s_count--;
		gotit = 1;
	}
	disp_lock_exit(&sqh->sq_lock);
	return (gotit);
}

int
sema_held(ksema_t *sp)
{
	sema_impl_t	*s;


	s = (sema_impl_t *)sp;
	if (panicstr)
		return (1);
	else
		return (s->s_count <= 0);
}
