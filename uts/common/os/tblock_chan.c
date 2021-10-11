/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)tblock_chan.c	1.38	94/08/05 SMI"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/tblock.h>
#include <sys/sobject.h>
#include <sys/cmn_err.h>

static void	changepri(kthread_t *t, pri_t pri);
static void	unsleep(kthread_id_t t);
static kthread_t * no_owner(caddr_t sobj);

static sobj_ops_t	sleep_sops = {
	"sleep channel",
	SOBJ_NONE,
	QOBJ_DEF,
	no_owner,
	unsleep,
	changepri
};

void
t_block_chan(caddr_t chan)
{
	register sleepq_head_t	*sqh;
	register klwp_t *lwp	= ttolwp(curthread);

	ASSERT(THREAD_LOCK_HELD(curthread));
	ASSERT(curthread != CPU->cpu_idle_thread);
	ASSERT(CPU->cpu_on_intr == 0);
	ASSERT(curthread->t_wchan0 == 0 && curthread->t_wchan == NULL);
	ASSERT(curthread->t_state == TS_ONPROC);

	CL_SLEEP(curthread, 0);			/* assign kernel priority */
	curthread->t_wchan = chan;
	if (lwp != NULL) {
		lwp->lwp_ru.nvcsw++;
		if (curthread->t_proc_flag & TP_MSACCT)
			(void) new_mstate(curthread, LMS_SLEEP);
	}

	sqh = sqhash(chan);
	curthread->t_sobj_ops = &sleep_sops;
	disp_lock_enter_high(&sqh->sq_lock);
	THREAD_SLEEP(curthread, &sqh->sq_lock);
	sleepq_insert(&sqh->sq_queue, curthread);
	/*
	 * THREAD_SLEEP() moves curthread->t_lockp to point to the
	 * lock sqh->sq_lock. This lock is later released by the caller
	 * when it calls thread_unlock() on curthread.
	 */
}


void
t_block_sig_chan(caddr_t chan)
{
	curthread->t_flag |= T_WAKEABLE;
	t_block_chan(chan);
}


void
t_release_chan(caddr_t chan)
{
	sleepq_head_t	*sqh;

	sqh = sqhash(chan);
	disp_lock_enter(&sqh->sq_lock);
	ASSERT(CPU->cpu_on_intr == 0);
	sleepq_wakeone_chan(&sqh->sq_queue, chan);
	disp_lock_exit(&sqh->sq_lock);
}

void
t_release_all_chan(caddr_t chan)
{
	sleepq_head_t	*sqh;

	sqh = sqhash(chan);
	disp_lock_enter(&sqh->sq_lock);
	ASSERT(CPU->cpu_on_intr == 0);
	sleepq_wakeall_chan(&sqh->sq_queue, chan);
	disp_lock_exit(&sqh->sq_lock);
}

int
t_waitqempty_chan(caddr_t chan)
{
	register kthread_id_t	tp;
	register sleepq_head_t	*sqh;
	register sleepq_t	*sqp;

	sqh = sqhash(chan);
	sqp = &sqh->sq_queue;
	disp_lock_enter(&sqh->sq_lock);

	for (tp = sqp->sq_first; tp != 0; tp = tp->t_link) {
		if (tp->t_wchan == chan && tp->t_wchan0 == 0) {
			disp_lock_exit(&sqh->sq_lock);
			return (0);
		}
	}
	disp_lock_exit(&sqh->sq_lock);
	return (1);
}

/*
 * pi_willto() needs this when examining a blocking
 * chain where a thread is sleeping on a sleep channel.
 */
/* ARGSUSED */
static kthread_t *
no_owner(caddr_t sobj)
{
	return ((kthread_t *)NULL);
}	/* end of no_owner */

/*
 * Remove a thread from its sleep queue, if it isn't asleep
 * on a synchronization object.
 * Called via SOBJ_UNSLEEP() if the thread's t_sobj_ops
 * field is sleep_sops.
 */
static void
unsleep(kthread_id_t t)
{
	ASSERT(THREAD_LOCK_HELD(t));

	if (t->t_wchan != NULL) {
		register sleepq_head_t	*sqh;

		/*
		 * Because we hold the thread lock, we've already
		 * acquired the lock on the sleep queue (there're the same).
		 */
		sqh = sqhash(t->t_wchan);
		if (sleepq_unsleep(&sqh->sq_queue, t) == NULL)
			cmn_err(CE_PANIC, "unsleep: thread %x not on sleepq %x",
				(int)t, (int)sqh);
		disp_lock_exit_high(&sqh->sq_lock);
		CL_SETRUN(t);
	} else {
		cmn_err(CE_PANIC, "unsleep: thread %x not on sleepq", (int)t);
	}
}

/*
 * Change the priority of a thread sleeping in
 * the sleep queue (not on a s-object).
 * Called via SOBJ_CHANGEPRI() from pi_changepri()
 * when the thread's t_sobj_ops field == sleep_sops.
 */
static void
changepri(kthread_t *t, pri_t pri)
{
	ASSERT(THREAD_LOCK_HELD(t));
	if (t->t_wchan != NULL) {
		sleepq_head_t   *sqh;

		sqh = sqhash(t->t_wchan);
		(void) sleepq_dequeue(&sqh->sq_queue, t);
		t->t_epri = pri;
		sleepq_insert(&sqh->sq_queue, t);
	} else {
		cmn_err(CE_PANIC, "changepri: 0x%x not on a sleep queue", t);
	}
}
