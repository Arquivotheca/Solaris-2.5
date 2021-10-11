/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)lwp.c	1.31	95/08/24	SMI"


#ifdef DEBUG

#ifdef __STDC__
#pragma weak _lwp_cond_wait = __lwp_cond_wait
#pragma weak _lwp_cond_timedwait = __lwp_cond_timedwait
#endif /* __STDC__ */

#include "libthread.h"


int
__lwp_cond_wait(cond_t *cv, mutex_t *mp)
{
	int error;

	if (mp == &_schedlock)
		ASSERT(_sched_owner == curthread);
	error = ___lwp_cond_wait(cv, mp, NULL);
	__lwp_mutex_lock(mp);
	if (mp == &_schedlock) {
		_sched_owner = curthread;
		_sched_ownerpc = _getcaller();
	}
	return (error);
}

int
__lwp_cond_timedwait(cond_t *cv, mutex_t *mp, timestruc_t *ts)
{
	int error;
	struct timeval now1;
	timestruc_t now2;

	if (mp == &_schedlock)
		ASSERT((_sched_owner == curthread));
	error = _gettimeofday(&now1, NULL);
	if (error == -1)
		return (error);
	/* convert gettimeofday() value to a timestruc_t */
	now2.tv_sec  = now1.tv_sec;
	now2.tv_nsec = (now1.tv_usec)*1000;
	if (ts->tv_nsec >= now2.tv_nsec) {
		if (ts->tv_sec >= now2.tv_sec) {
			ts->tv_sec -= now2.tv_sec;
			ts->tv_nsec -= now2.tv_nsec;
		} else
			return (ETIME);
	} else {
		if (ts->tv_sec > now2.tv_sec) {
			ts->tv_sec  -= (now2.tv_sec + 1);
			ts->tv_nsec -= (now2.tv_nsec - 1000000000);
		} else
			return (ETIME);
	}
	error = ___lwp_cond_wait(cv, mp, ts);
	__lwp_mutex_lock(mp);
	if (mp == &_schedlock) {
		_sched_owner = curthread;
		_sched_ownerpc = _getcaller();
	}
	return (error);
}

#endif /* DEBUG */
