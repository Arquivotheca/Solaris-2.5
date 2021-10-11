#ident	"@(#)sigpending.c 1.2 95/08/22"

#include "../../common/libthread.h"
#include <signal.h>
#include <errno.h>

int
sigpending(sigset_t *set)
{
	sigset_t ppsigs;
	uthread_t *t = curthread;

	/*
	 * Get directed pending signals 
	 */
	_sched_lock();
	sigandset(set, &t->t_hold, &t->t_psig);
	_sched_unlock();
	/*
	 * Get non-directed (to process) pending signals
	 */
	_lmutex_lock(&_pmasklock);
	sigandset(&ppsigs, &t->t_hold, &_pmask);
	_lmutex_unlock(&_pmasklock);
	sigorset(set, &ppsigs);
	return (0);
}
