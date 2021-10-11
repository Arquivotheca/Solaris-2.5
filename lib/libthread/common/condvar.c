/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)condvar.c	1.58	95/09/07	SMI"

#ifdef __STDC__

#pragma weak cond_init = _cond_init
#pragma weak cond_destroy = _cond_destroy
#pragma weak cond_wait = _cond_wait_cancel
#pragma weak cond_timedwait = _cond_timedwait_cancel
#pragma weak cond_signal = _cond_signal
#pragma weak cond_broadcast = _cond_broadcast

#pragma weak pthread_cond_destroy = _cond_destroy
#pragma weak pthread_cond_wait = _pthread_cond_wait
#pragma weak pthread_cond_signal = _cond_signal
#pragma weak pthread_cond_broadcast = _cond_broadcast
#pragma weak pthread_cond_timedwait = _pthread_cond_timedwait
#pragma weak _pthread_cond_destroy = _cond_destroy
#pragma weak _pthread_cond_signal = _cond_signal
#pragma weak _pthread_cond_broadcast = _cond_broadcast

#endif /* __STDC__ */

#include "libthread.h"


#if defined(UTRACE) || defined(ITRACE)
#define	TRACE_NAME(x)	(((x)->type & TRACE_TYPE) ? (x)->name : "<noname>")
#include <string.h>
#endif


int
_cond_init(cond_t *cvp, int type, void *arg)
{
	if (type != USYNC_THREAD && type != USYNC_PROCESS)
		return (EINVAL);
	cvp->cond_type = type;
	cvp->cond_waiters  = 0;

#if defined(UTRACE) || defined(ITRACE)
	if (arg) {
		cvp->cond_type |= TRACE_TYPE;
	}
#endif
	return (0);
}

int
_cond_destroy(cond_t *cvp)
{
	return (0);
}

/*
 * "ts" is the timeout value and is an absolute time expressed as number
 * of seconds and nanoseconds since epoch (00:00, UCT, 1/1/1970).
 *
 * This function is a cancellation point. If libthread needs to use
 * _cond_timedwait internally and calling function is NOT a cancellation
 * point then you use another _cond_timedwait which does not
 * have _cancelon/_canceloff code in it.
 * Currently sleep in libthread is using cancel-version because
 * sleep is a cancellation point.
 */
int
_pthread_cond_timedwait(pthread_cond_t *cvp,
				pthread_mutex_t *mp, struct timespec *ts)
{
	int ret;

	ret = _cond_timedwait_cancel((cond_t *)cvp,
				(mutex_t *)mp, (timestruc_t *)ts);
	if (ret == ETIME)
		return (ETIMEDOUT);
	else if (ret == EINTR)
		return (0);
	else
		return (ret);
}

int
_cond_timedwait_cancel(cond_t *cvp, mutex_t *mp, timestruc_t *ts)
{
	int cv_timedout = 0;
	timestruc_t ts2;
	struct timeval tv;
	struct timeval curtime;
	int ret = 0;
	register uthread_t *t = curthread;

	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_TIMEDWAIT_START,
	    "cond_timedwait start:name %s, addr 0x%x, type 0x%x, \
	    mutex_name_addr %s, 0x%x", TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), mp);
	/*
	 * cond_eot is initialized at program start-up to be
	 * (pgm_start_time_in_secs_from_epoch + COND_REL_EOT)
	 * which is a huge absolute time, sufficiently far into the future.
	 */
	if (ts->tv_sec < 0 || ts->tv_sec > _cond_eot ||
	    ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000)
		return (EINVAL);
	if (cvp->cond_type & USYNC_PROCESS) {
		_sigoff();
		*(&ts2) = *ts;
		_cancelon();
		if ((ret = _lwp_cond_timedwait(cvp, mp, &ts2)) == ETIME)
			cv_timedout = 1;
		_canceloff();
		_sigon();
	} else {
		/*
		 * First, compute relative time from absolute time.
		 */
		if (_gettimeofday(&curtime, NULL) == -1) {
			perror("cond_timedwait: gettimeofday");
		}
		tv.tv_sec  = ts->tv_sec;
		tv.tv_usec = ts->tv_nsec/1000;
		if (tv.tv_usec >= curtime.tv_usec) {
			if (tv.tv_sec >= curtime.tv_sec) {
				tv.tv_sec  -=  curtime.tv_sec;
				tv.tv_usec -=  curtime.tv_usec;
			} else
				return (ETIME);
		} else {
			if (tv.tv_sec > curtime.tv_sec) {
				tv.tv_sec  -=  (curtime.tv_sec + 1);
				tv.tv_usec -=  (curtime.tv_usec - 1000000);
			} else
				return (ETIME);
		}
		_setcallout(&curthread->t_cv_callo, NULL, &tv, _setrun,
		    (int)curthread);
		_sched_lock();
		if (ISTIMEDOUT(&curthread->t_cv_callo)) {
			_sched_unlock();
			cv_timedout = 1;
		} else {
			t->t_flag &= ~T_INTR;
			t->t_flag |= T_WAITCV;
			_t_block((caddr_t)cvp);
			cvp->cond_waiters = 1;
			_sched_unlock_nosig();
			_cancelon();
			_mutex_unlock(mp);
			_swtch(0);
			cv_timedout = _rmcallout(&curthread->t_cv_callo);
			_mutex_lock(mp);
			_canceloff();
			_sigon();
			/* do not need to grab schedlock here */
			if (t->t_flag & T_INTR) {
				t->t_flag &= ~T_INTR;
				TRACE_5(UTR_FAC_THREAD_SYNC,
				    UTR_COND_TIMEDWAIT_END,
				    "cond_timedwait end:name %s, addr 0x%x, \
				    type 0x%x, mutex_name %s how %d",
				    TRACE_NAME(cvp), (u_long)cvp,
				    (u_long)cvp->cond_type, TRACE_NAME(mp), 2);
				return (EINTR);
			}
		}
	}
	if (cv_timedout) {
		TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_TIMEDWAIT_END,
		    "cond_timedwait end:name %s, addr 0x%x, type 0x%x, \
		    mutex_name %s how %d ",
		    TRACE_NAME(cvp), (u_long)cvp,
		    (u_long)cvp->cond_type, TRACE_NAME(mp), 1);
		return (ETIME);
	}
	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_TIMEDWAIT_END,
	    "cond_timedwait end:name %s, addr 0x%x, type 0x%x, \
	    mutex_name %s how %d",
	TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), 0);
	return (ret);
}

/*
 * Non-cancel version of _cond_timedwait to be used internally
 * if calling function is NOT a cancellation point.
 */
int
_cond_timedwait(cond_t *cvp, mutex_t *mp, timestruc_t *ts)
{
	int cv_timedout = 0;
	timestruc_t ts2;
	struct timeval tv;
	struct timeval curtime;
	int ret = 0;
	register uthread_t *t = curthread;

	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_TIMEDWAIT_START,
	    "cond_timedwait start:name %s, addr 0x%x, type 0x%x, \
	    mutex_name_addr %s, 0x%x", TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), mp);
	/*
	 * cond_eot is initialized at program start-up to be
	 * (pgm_start_time_in_secs_from_epoch + COND_REL_EOT)
	 * which is a huge absolute time, sufficiently far into the future.
	 */
	if (ts->tv_sec < 0 || ts->tv_sec > _cond_eot ||
	    ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000)
		return (EINVAL);
	if (cvp->cond_type & USYNC_PROCESS) {
		_sigoff();
		*(&ts2) = *ts;
		if ((ret = _lwp_cond_timedwait(cvp, mp, &ts2)) == ETIME)
			cv_timedout = 1;
		_sigon();
	} else {
		/*
		 * First, compute relative time from absolute time.
		 */
		if (_gettimeofday(&curtime, NULL) == -1) {
			perror("cond_timedwait: gettimeofday");
		}
		tv.tv_sec  = ts->tv_sec;
		tv.tv_usec = ts->tv_nsec/1000;
		if (tv.tv_usec >= curtime.tv_usec) {
			if (tv.tv_sec >= curtime.tv_sec) {
				tv.tv_sec  -=  curtime.tv_sec;
				tv.tv_usec -=  curtime.tv_usec;
			} else
				return (ETIME);
		} else {
			if (tv.tv_sec > curtime.tv_sec) {
				tv.tv_sec  -=  (curtime.tv_sec + 1);
				tv.tv_usec -=  (curtime.tv_usec - 1000000);
			} else
				return (ETIME);
		}
		_setcallout(&curthread->t_cv_callo, NULL, &tv, _setrun,
		    (int)curthread);
		_sched_lock();
		if (ISTIMEDOUT(&curthread->t_cv_callo)) {
			_sched_unlock();
			cv_timedout = 1;
		} else {
			t->t_flag &= ~T_INTR;
			t->t_flag |= T_WAITCV;
			_t_block((caddr_t)cvp);
			cvp->cond_waiters = 1;
			_sched_unlock_nosig();
			_mutex_unlock(mp);
			_swtch(0);
			cv_timedout = _rmcallout(&curthread->t_cv_callo);
			_mutex_lock(mp);
			_sigon();
			/* do not need to grab schedlock here */
			if (t->t_flag & T_INTR) {
				t->t_flag &= ~T_INTR;
				TRACE_5(UTR_FAC_THREAD_SYNC,
				    UTR_COND_TIMEDWAIT_END,
				    "cond_timedwait end:name %s, addr 0x%x, \
				    type 0x%x, mutex_name %s how %d",
				    TRACE_NAME(cvp), (u_long)cvp,
				    (u_long)cvp->cond_type, TRACE_NAME(mp), 2);
				return (EINTR);
			}
		}
	}
	if (cv_timedout) {
		TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_TIMEDWAIT_END,
		    "cond_timedwait end:name %s, addr 0x%x, type 0x%x, \
		    mutex_name %s how %d ",
		    TRACE_NAME(cvp), (u_long)cvp,
		    (u_long)cvp->cond_type, TRACE_NAME(mp), 1);
		return (ETIME);
	}
	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_TIMEDWAIT_END,
	    "cond_timedwait end:name %s, addr 0x%x, type 0x%x, \
	    mutex_name %s how %d",
	TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), 0);
	return (ret);
}
/*
 * Cancel version of _cond_wait for exported API use.
 * We dont want internal functions to be cancellation points
 * just because cond_wait is, and they need to use _cond_wait().
 * This should be called internally only if calling function is
 * a cancellation point.
 */
int
_pthread_cond_wait(pthread_cond_t *cvp, pthread_mutex_t *mp)
{
	int ret;

	ret = _cond_wait_cancel((cond_t *)cvp, (mutex_t *)mp);
	if (ret == EINTR)
		return (0);
	else
		return (ret);
}

int
_cond_wait_cancel(cond_t *cvp, mutex_t	*mp)
{
	int retcode;
	register uthread_t *t = curthread;

	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_WAIT_START,
	    "cond_wait start:name %s, addr 0x%x, type 0x%x, \
	    mutex_name_addr %s, 0x%x", TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), mp);

	/* leave the sigoff effective */
	if (cvp->cond_type & USYNC_PROCESS) {
		_sigoff();
		_cancelon();
		retcode = __lwp_cond_wait(cvp, mp);
		_canceloff();
		_sigon();
		return (retcode);
	} else {
		_sched_lock();
		t->t_flag &= ~T_INTR;
		/*
		 * The T_WAITCV flag is turned on here to indicate that the
		 * thread about to sleep in _t_block() is sleeping for a CV.
		 * It is not turned off after thread wakes-up, in a bracketing
		 * manner, since the flag is read by _t_release() only for a
		 * thread found on the sleep queue. Whenever a thread calls
		 * _t_block(), T_WAITCV is either turned on (for cvs, as here)
		 * or off (for mutexes - see mutex.c)
		 */
		t->t_flag |= T_WAITCV;
		_t_block((caddr_t)cvp);
		cvp->cond_waiters = 1;
		_sched_unlock_nosig();
		_cancelon();
		_mutex_unlock(mp);
		_swtch(0);
		_mutex_lock(mp);
		_canceloff();
		_sigon();
		ASSERT(t->t_link == NULL);
		ASSERT(t->t_wchan == NULL);
		ASSERT(t->t_state == TS_ONPROC);
	}
	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_WAIT_END,
	    "cond_wait end:name %s, addr 0x%x, type 0x%x, \
	    mutex_name_addr %s, 0x%x", TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), mp);

	/* do not need to grab schedlock here */
	if (curthread->t_flag & T_INTR) {
		curthread->t_flag &= ~T_INTR;
		return (EINTR);
	}
	return (0);
}

/*
 * Non-cancel version of _cond_wait for internal use.
 * We dont want internal functions to be cancellation points
 * just because cond_wait is, and they need to use cond_wait().
 */
int
_cond_wait(cond_t *cvp, mutex_t *mp)
{
	int retcode;
	register uthread_t *t = curthread;

	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_WAIT_START,
	    "cond_wait_i start:name %s, addr 0x%x, type 0x%x, \
	    mutex_name_addr %s, 0x%x", TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), mp);

	/* leave the sigoff effective */
	if (cvp->cond_type & USYNC_PROCESS) {
		_sigoff();
		retcode = __lwp_cond_wait(cvp, mp);
		_sigon();
		return (retcode);
	} else {
		_sched_lock();
		t->t_flag &= ~T_INTR;
		t->t_flag |= T_WAITCV;
		_t_block((caddr_t)cvp);
		cvp->cond_waiters = 1;
		_sched_unlock_nosig();
		_mutex_unlock(mp);
		_swtch(0);
		_mutex_lock(mp);
		_sigon();
		ASSERT(t->t_link == NULL);
		ASSERT(t->t_wchan == NULL);
		ASSERT(t->t_state == TS_ONPROC);
	}
	TRACE_5(UTR_FAC_THREAD_SYNC, UTR_COND_WAIT_END,
	    "cond_wait_i end:name %s, addr 0x%x, type 0x%x, \
	    mutex_name_addr %s, 0x%x", TRACE_NAME(cvp), (u_long)cvp,
	    (u_long)cvp->cond_type, TRACE_NAME(mp), mp);

	/* do not need to grab schedlock here */
	if (curthread->t_flag & T_INTR) {
		curthread->t_flag &= ~T_INTR;
		return (EINTR);
	}
	return (0);
}

int
_cond_signal(cond_t *cvp)
{
	u_char waiters;

	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_COND_SIGNAL_START,
	    "cond_signal start:name %s, addr 0x%x, type 0x%x",
	    TRACE_NAME(cvp), (u_long)cvp, (u_long)cvp->cond_type);
	if (cvp->cond_waiters) {
		if (cvp->cond_type & USYNC_PROCESS) {
			if (_lwp_cond_signal(cvp) != 0) {
				_panic("cond_signal: _lwp_cond_signal failed");
			}
		} else {
			_sched_lock();
			_t_release((caddr_t)cvp, &waiters, T_WAITCV);
			cvp->cond_waiters = waiters;
			_sched_unlock();
		}
	}
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_COND_SIGNAL_END,
	    "cond_signal end:name %s, addr 0x%x, type 0x%x",
	    TRACE_NAME(cvp), (u_long)cvp, (u_long)cvp->cond_type);
	return (0);
}

int
_cond_broadcast(cond_t *cvp)
{
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_COND_BCST_START,
	    "cond_broadcast start:name %s, addr %x, type 0x%x",
	    TRACE_NAME(cvp), (u_long)cvp, (u_long)cvp->cond_type);
	if (cvp->cond_waiters) {
		if (cvp->cond_type & USYNC_PROCESS) {
			_lwp_cond_broadcast(cvp);
		} else {
			_sched_lock();
			_t_release_all((caddr_t)cvp);
			cvp->cond_waiters = 0;
			_sched_unlock();
		}
	}
	TRACE_3(UTR_FAC_THREAD_SYNC, UTR_COND_BCST_END,
	    "cond_broadcast end:name %s, addr %x, type 0x%x",
	    TRACE_NAME(cvp), (u_long)cvp, (u_long)cvp->cond_type);
	return (0);
}

#if defined(UTRACE) || defined(ITRACE)

int
trace_cond_init(cond_t *cvp, char type, void *arg)
{
	cvp->cond_type = type;
	cvp->cond_waiters  = 0;
	return (0);
}

int
trace_cond_wait(cond_t *cvp, mutex_t *mp)
{
	int retcode;

	if (cvp->type & USYNC_PROCESS) {
		___lwp_cond_wait(cvp, mp, 0);
	} else {
		_sched_lock();
		t->t_flag |= T_WAITCV;
		_t_block((caddr_t)cvp);
		cvp->cond_waiters = 1;
		_sched_unlock_nosig();
		_mutex_unlock(mp);
		_swtch(0);
		_sigon();
		ASSERT(curthread->t_link == NULL);
		ASSERT(curthread->t_wchan == NULL);
		ASSERT(curthread->t_state == TS_ONPROC);
	}
	trace_mutex_lock(mp);
	return (0);
}

int
trace_cond_signal(cond_t *cvp)
{
	u_char waiters;

	if (cvp->cond_waiters) {
		if (cvp->cond_type & USYNC_PROCESS) {
			if (_lwp_cond_signal(cvp) != 0) {
				_panic("_cond_signal: lwp_cond_signal failed");
			}
		} else {
			_sched_lock();
			_t_release((caddr_t)cvp, &waiters, T_WAITCV);
			cvp->cond_waiters = waiters;
			_sched_unlock();
		}
	}
	return (0);
}

int
trace_cond_broadcast(cond_t *cvp)
{
	if (cvp->cond_waiters) {
		if (cvp->cond_type & USYNC_PROCESS) {
			_lwp_cond_broadcast(cvp);
		} else {
			_sched_lock();
			_t_release_all((caddr_t)cvp);
			cvp->cond_waiters = 0;
			_sched_unlock();
		}
	}
	return (0);
}
#endif
