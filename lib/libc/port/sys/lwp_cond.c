/* @(#)lwp_cond.c 1.8 94/10/25 */

#ifdef __STDC__
	#pragma weak _lwp_cond_wait = __lwp_cond_wait
	#pragma weak _lwp_cond_timedwait = __lwp_cond_timedwait
#endif /* __STDC__ */

#include "synonyms.h"
#include <sys/time.h>
#include <errno.h>
#include <synch.h>
#include <sys/lwp.h>

int
_lwp_cond_wait(cv, mp)
	cond_t *cv;
	mutex_t *mp;
{
	int error;

	error = ___lwp_cond_wait(cv, mp, NULL);
	_lwp_mutex_lock(mp);
	return (error);
}

int
_lwp_cond_timedwait(cv, mp, absts)
	cond_t *cv;
	mutex_t *mp;
	timestruc_t *absts;
{
	int error;
	struct timeval now1;
	timestruc_t now2, tslocal = *absts;

	gettimeofday(&now1, NULL);
	now2.tv_sec  = now1.tv_sec;
	now2.tv_nsec = (now1.tv_usec)*1000;

	if (tslocal.tv_nsec >= now2.tv_nsec) {
		if (tslocal.tv_sec >= now2.tv_sec) {
			tslocal.tv_sec -= now2.tv_sec;
			tslocal.tv_nsec -= now2.tv_nsec;
		} else
			return (ETIME);
	} else {
		if (tslocal.tv_sec > now2.tv_sec) {
			tslocal.tv_sec  -= (now2.tv_sec + 1);
			tslocal.tv_nsec -= (now2.tv_nsec - 1000000000);
		} else
			return (ETIME);
	}
	error = ___lwp_cond_wait(cv, mp, &tslocal);
	_lwp_mutex_lock(mp);
	return (error);
}
