/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)shuttle.c	1.9	95/06/26 SMI"

/*
 * Routines to support shuttle synchronization objects
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/class.h>
#include <sys/debug.h>
#include <sys/sobject.h>
#include <sys/cpuvar.h>


/*
 * Place the thread in question on the run q.
 */
static void
shuttle_unsleep(kthread_t *t)
{
	ASSERT(THREAD_LOCK_HELD(t));

	/* Waiting on a shuttle */
	ASSERT(t->t_wchan0 == 1 && t->t_wchan == NULL);
	t->t_flag &= ~T_WAKEABLE;
	t->t_wchan0 = 0;
	t->t_sobj_ops = NULL;
	THREAD_TRANSITION(t);
	CL_SETRUN(t);
}

static kthread_t *
shuttle_owner()
{
	return (NULL);
}

static void
shuttle_changepri(kthread_t *t, pri_t p)
{
	t->t_epri = p;
}

static sobj_ops_t	shuttle_sops = {
	"Shuttle",
	SOBJ_SHUTTLE,
	QOBJ_DEF,
	shuttle_owner,
	shuttle_unsleep,
	shuttle_changepri
};

/*
 * Mark the current thread as sleeping on a shuttle object, and
 * resume the specified thread. The 't' thread must be marked as ONPROC.
 *
 * No locks other than 'l' should be held at this point.
 */
void
shuttle_resume(kthread_t *t, kmutex_t *l)
{
	klwp_t	*lwp = ttolwp(curthread);
	extern disp_lock_t	shuttle_lock;

	thread_lock(curthread);
	disp_lock_enter_high(&shuttle_lock);
	lwp->lwp_asleep = 1;			/* /proc */
	lwp->lwp_sysabort = 0;			/* /proc */
	lwp->lwp_ru.nvcsw++;
	curthread->t_flag |= T_WAKEABLE;
	curthread->t_sobj_ops = &shuttle_sops;
	/*
	 * setting cpu_dispthread before changing thread state
	 * so that kernel preemption will be deferred to after swtch_to()
	 */
	CPU->cpu_dispthread = t;
	/*
	 * Set the wchan0 field so that /proc won't just do a setrun
	 * on this thread when trying to stop a process. Instead,
	 * /proc will mark the thread as VSTOPPED similar to threads
	 * that are blocked on user level condition variables.
	 */
	curthread->t_wchan0 = 1;
	THREAD_SLEEP(curthread, &shuttle_lock);

	/* Update ustate records (there is no waitrq obviously) */
	if (curthread->t_proc_flag & TP_MSACCT)
		(void) new_mstate(curthread, LMS_SLEEP);
	if (t->t_proc_flag & TP_MSACCT)
		restore_mstate(t);
	t->t_flag &= ~T_WAKEABLE;
	t->t_wchan0 = 0;
	t->t_sobj_ops = NULL;
	disp_lock_exit_high(&shuttle_lock);
	mutex_exit(l);
	/*
	 * Make sure we didn't receive any important events while
	 * we weren't looking
	 */
	if (curthread->t_post_sys_ast || ISHOLD(curproc)) {
		if (ISSIG(curthread, JUSTLOOKING) || ISHOLD(curproc))
			setrun(curthread);
	}
	swtch_to(t);
	/*
	 * Caller must check for ISSIG/lwp_sysabort conditions
	 * and clear lwp->lwp_asleep/lwp->lwp_sysabort
	 */
}

/*
 * Mark the current threads as sleeping on a shuttle object, and
 * switch to a new thread.
 * No locks other than 'l' should be held at this point.
 */
void
shuttle_swtch(kmutex_t *l)
{
	klwp_t	*lwp = ttolwp(curthread);
	extern disp_lock_t	shuttle_lock;

	thread_lock(curthread);
	disp_lock_enter_high(&shuttle_lock);
	lwp->lwp_asleep = 1;			/* /proc */
	lwp->lwp_sysabort = 0;			/* /proc */
	lwp->lwp_ru.nvcsw++;
	curthread->t_flag |= T_WAKEABLE;
	curthread->t_sobj_ops = &shuttle_sops;
	curthread->t_wchan0 = 1;
	THREAD_SLEEP(curthread, &shuttle_lock);
	if (curthread->t_proc_flag & TP_MSACCT)
		(void) new_mstate(curthread, LMS_SLEEP);
	disp_lock_exit_high(&shuttle_lock);
	mutex_exit(l);
	if (curthread->t_post_sys_ast || ISHOLD(curproc)) {
		if (ISSIG(curthread, JUSTLOOKING) || ISHOLD(curproc))
			setrun(curthread);
	}
	swtch();
	/*
	 * Caller must check for ISSIG/lwp_sysabort conditions
	 * and clear lwp->lwp_asleep/lwp->lwp_sysabort
	 */
}
