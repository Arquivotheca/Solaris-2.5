/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)tblock_sobj.c	1.28	94/09/29 SMI"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/cpuvar.h>
#include <sys/sleepq.h>
#include <sys/tblock.h>
#include <sys/disp.h>
#include <sys/sobject.h>

/*
 * Block the current thread on a synchronization object.
 * o The "ts" parameter is a pointer to the associated turnstile
 *   where the current thread will be queued.
 * o The address of the s-object is "so_addr".
 * o The "so_ops" parameter is a reference to the operations
 *   and information exported by the s-object for operations
 *   like unsleep and priority inheritance.
 * o The "so_lock" parameter is a pointer to the spin-lock that
 *   protects queuing on this s-object.
 */
void
t_block(turnstile_t *ts, caddr_t so_addr, sobj_ops_t *so_ops,
	disp_lock_t *so_lock)
{
	qobj_t	qnum;
	klwp_t	*lwp = ttolwp(curthread);

	ASSERT(THREAD_LOCK_HELD(curthread));
	ASSERT(curthread != CPU->cpu_idle_thread);
	ASSERT(CPU->cpu_on_intr == 0);
	ASSERT(curthread->t_wchan0 == 0 && curthread->t_wchan == NULL);
	ASSERT(curthread->t_state == TS_ONPROC);

	CL_SLEEP(curthread, 0);		/* assign kernel priority */
	qnum = SOBJ_QNUM(so_ops);
	THREAD_SLEEP(curthread, so_lock);
	curthread->t_wchan = so_addr;
	curthread->t_sobj_ops = so_ops;
	curthread->t_ts = ts;
	if (lwp != NULL) {
		lwp->lwp_ru.nvcsw++;
		if (curthread->t_proc_flag & TP_MSACCT)
			(void) new_mstate(curthread, LMS_SLEEP);
	}
	TSTILE_INSERT(ts, qnum, curthread);
}	/* end of t_block */

/*
 * Wakeable version of t_block().
 */
void
t_block_sig(turnstile_t *ts, caddr_t so_addr, sobj_ops_t *so_ops,
	    disp_lock_t *so_lock)
{
	curthread->t_flag |= T_WAKEABLE;
	t_block(ts, so_addr, so_ops, so_lock);
}	/* end of t_block_sig */

/*
 * Wake up the first thread (i.e., the thread with the
 * highest dispatch priority) in the indicated sleep queue.
 */
void
t_release(turnstile_t *ts, turnstile_id_t *so_waiters, qobj_t qnum)
{
	ASSERT(CPU->cpu_on_intr == 0);

	if (ts != NULL) {
		TSTILE_WAKEONE(ts, qnum);
		tstile_free(ts, so_waiters);
	}
}	/* end of t_release */

/*
 * Wake up all threads in the indicated sleep queue.
 */
void
t_release_all(turnstile_t *ts, turnstile_id_t *so_waiters, qobj_t qnum)
{
	ASSERT(CPU->cpu_on_intr == 0);

	if (ts != NULL) {
		TSTILE_WAKEALL(ts, qnum);
		tstile_free(ts, so_waiters);
	}
}	/* end of t_release_all */
