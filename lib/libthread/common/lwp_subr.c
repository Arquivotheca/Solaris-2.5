/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)lwp_subr.c	1.21	95/03/15	SMI"

#include "libthread.h"

/*
 * the thread library uses this routine when it needs another LWP.
 */
int
_new_lwp(struct thread *t, void (*func)())
{
	uthread_t *aging = NULL;
	long flags;
	int ret;

	if (t == NULL) {
		/*
		 * increase the pool of LWPs on which threads execute.
		 */
		if ((aging = _idle_thread_create()) == NULL)
			return (EAGAIN);
		_sched_lock();
		_nlwps++;
		_sched_unlock();
		ret = _lwp_exec(aging, (int)_thr_exit,(caddr_t)aging->t_sp,
		    _age, (LWP_SUSPENDED | LWP_DETACHED), &(aging->t_lwpid));
		if (ret) {
			/* if failed */
			_thread_free(aging);
			_sched_lock();
			_nlwps--;
			_sched_unlock();
			return (ret);
		}
		ASSERT(aging->t_lwpid != 0);
		_lwp_continue(aging->t_lwpid);
		return (0);
	}
	flags = 0;
	ASSERT(t->t_usropts & THR_BOUND);
	flags |= LWP_DETACHED;
	if (t->t_usropts & THR_SUSPENDED)
		flags |= LWP_SUSPENDED;
	else
		t->t_state = TS_ONPROC;
	ret = _lwp_exec(t, (int)_thr_exit, (caddr_t)t->t_sp, func,
					flags, &(t->t_lwpid));
	ASSERT(ret || t->t_lwpid != 0);
	return (ret);
}
