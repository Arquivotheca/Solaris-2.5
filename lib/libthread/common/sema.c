/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)sema.c	1.35	95/08/08	SMI"



#ifdef __STDC__
#pragma weak sema_init = _sema_init
#pragma weak sema_destroy = _sema_destroy
#pragma weak sema_wait = _sema_wait_cancel
#pragma weak sema_trywait = _sema_trywait
#pragma weak sema_post = _sema_post
#endif /* __STDC__ */

#include "libthread.h"

/*
 * Check to see if anyone is waiting for this semaphore.
 */
_sema_held(sema_t *sp)
{
	return (sp->count <= 0);
}

int
_sema_init(sema_t *sp, unsigned int count, int type, void *arg)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t	*semacv	= (cond_t *)&sp->pad2[0];

	if (type != USYNC_THREAD && type != USYNC_PROCESS)
		return (EINVAL);

	_mutex_init(semalock, type, NULL);
	_cond_init(semacv, type, NULL);

	sp->count = count;
	sp->type = type;
	return (0);
}

int
_sema_destroy(sema_t *sp)
{
	return (0);
}
/*
 * sema_wait():
 * This function is a cancellation point. If libthread needs to use
 * _sema_wait in future and calling function is NOT a cancellation
 * point then you should use _sema_wait which does  not
 * have _cancelon/_canceloff code in it.
 */

int
_sema_wait_cancel(sema_t *sp)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t *semacv = (cond_t *)&sp->pad2[0];
	int err;

	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_WAIT_START,
	    "sema_wait start:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	if (sp->type == USYNC_PROCESS)
		return (_lwp_sema_wait((lwp_sema_t *)sp));
	else {
		err = 0;
		_lmutex_lock(semalock);
		while (sp->count == 0) {
			_sched_lock();
			curthread->t_flag &= ~T_INTR;
			curthread->t_flag |= T_WAITCV;
			_t_block((caddr_t)semacv);
			semacv->cond_waiters = 1;
			_sched_unlock_nosig();
			_lmutex_unlock(semalock);
			_cancelon();
			_swtch(0);
			_canceloff();
			_sigon();
			if (curthread->t_flag & T_INTR) {
				curthread->t_flag &= ~T_INTR;
				return (EINTR);
			}
			_lmutex_lock(semalock);
		}
		sp->count--;
		_lmutex_unlock(semalock);
	}
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_WAIT_END,
	    "sema_wait end:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	return (err);
}

int
_sema_wait(sema_t *sp)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t *semacv = (cond_t *)&sp->pad2[0];
	int err;

	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_WAIT_START,
	    "sema_wait start:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	if (sp->type == USYNC_PROCESS)
		return (_lwp_sema_wait((lwp_sema_t *)sp));
	else {
		err = 0;
		_lmutex_lock(semalock);
		while (sp->count == 0) {
			_sched_lock();
			curthread->t_flag &= ~T_INTR;
			curthread->t_flag |= T_WAITCV;
			_t_block((caddr_t)semacv);
			semacv->cond_waiters = 1;
			_sched_unlock_nosig();
			_lmutex_unlock(semalock);
			_swtch(0);
			_sigon();
			if (curthread->t_flag & T_INTR) {
				curthread->t_flag &= ~T_INTR;
				return (EINTR);
			}
			_lmutex_lock(semalock);
		}
		sp->count--;
		_lmutex_unlock(semalock);
	}
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_WAIT_END,
	    "sema_wait end:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	return (err);
}

int
_sema_trywait(sema_t *sp)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t *semacv = (cond_t *)&sp->pad2[0];
	int retval = 0;

	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_TRYWAIT_START,
	    "sema_trywait start:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	_lmutex_lock(semalock);
	if (sp->count > 0)
		sp->count--;
	else
		retval = EBUSY;
	_lmutex_unlock(semalock);
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_TRYWAIT_END,
	    "sema_trywait end:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	return (retval);
}

int
_sema_post(sema_t *sp)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t *semacv = (cond_t *)&sp->pad2[0];
	u_char waiters;
	thread_t *t;

	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_POST_START,
	    "sema_post start:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	if (sp->type == USYNC_PROCESS)
		return (_lwp_sema_post((lwp_sema_t *)sp));
	else {
		_lmutex_lock(semalock);
		sp->count++;
		if (semacv->cond_waiters) {
			_sched_lock();
			_t_release((caddr_t)semacv, &waiters, T_WAITCV);
			semacv->cond_waiters = waiters;
			_mutex_sema_unlock(semalock);
			_sched_unlock();
		} else
			_lmutex_unlock(semalock);
	}
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_SEMA_POST_END,
	    "sema_post end:name %s, addr 0x%x, count %d",
	    TRACE_SEM_NAME(sp), sp, (u_long)sp->count);
	return (0);
}

#if defined(ITRACE) || defined(UTRACE)
/*
 * Note that the following works if sp is of THREAD_SYNC_SHARED type. If
 * not, and e.g. trace_sema_post() is called by trace() at an internal libthread
 * tracepoint with _schedlock held, then there will be a deadlock if the
 * semlock is not available and the thread has to block.
 */
int
trace_sema_init(sema_t *sp, unsigned int count, char type, void *arg)
{
	trace_mutex_init(&sp->semlock, type, 0);
	trace_cond_init(&sp->semcv, type, 0);
	sp->count = count;
	sp->wakecnt = 0;
}

trace_sema_post(sema_t *sp)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t *semacv = (cond_t *)&sp->pad2[0];

	if (sp->type == USYNC_PROCESS)
		return (_lwp_sema_post(sp));
	else {
		trace_mutex_lock(semalock);
		if (semacv->cond_waiters) {
			_sched_lock();
			_t_release((caddr_t)semacv, &waiters, T_WAITCV);
			semacv->cond_waiters = waiters;
			sp->count++;
			_mutex_sema_unlock(semalock);
			_sched_unlock();
		} else {
			sp->count++;
			trace_mutex_unlock(semalock);
		}
	}
	return (0);
}

int
trace_sema_wait(sema_t *sp)
{
	mutex_t *semalock = (mutex_t *)&sp->pad1[0];
	cond_t *semacv = (cond_t *)&sp->pad2[0];

	if (sp->type == USYNC_PROCESS)
		return (_lwp_sema_wait(sp));
	else {
		trace_mutex_lock(semalock);
		while (sp->count == 0) {
			if (err = _cond_wait(semacv, semalock)) {
				trace_mutex_unlock(semalock);
				return (err);
			}
		}
		sp->count++;
		trace_mutex_unlock(semalock);
	}
}

#endif
