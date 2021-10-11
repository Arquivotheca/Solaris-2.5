/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)fork1.c	1.24	95/08/24	SMI"


#ifdef __STDC__
#pragma weak fork1 = _fork1
#pragma weak fork = _fork
#endif /* __STDC__ */

#include "libthread.h"

/*
 * The following two functions are used to make the rwlocks fork1 safe
 * see file common/rwlock.c for more information.
 */
extern void _rwlsub_lock(void);
extern void _rwlsub_unlock(void);

/*
 * The following variable is initialized to 1 if libptherad
 * is loaded, and that is the indication that user wants POSIX
 * behavior.
 */
int _libpthread_loaded = 0; /* set to 1 by libpthread init */

int
_fork1()
{
	int pid;

	_prefork_handler();
	_rw_wrlock(&tsd_common.lock);
	_rwlsub_lock();
	/*
	 * Grab all internal threads lib locks. When new internal locks
	 * are added, they should be added here. This is to guarantee
	 * two things - consistent libthread data structures in child,
	 * e.g. the TSD key table, and to prevent a deadlock in child due
	 * to an internal lock.
	 */
	_lmutex_lock(&_stkcachelock);
	while (_defaultstkcache.busy)
		_cond_wait(&_defaultstkcache.cv, &_stkcachelock);
	_lmutex_lock(&_calloutlock);
	_lmutex_lock(&_tsslock);
	_lwp_mutex_lock(&_reaplock);
	_lmutex_lock(&_tidlock);
	_lwp_mutex_lock(&_sighandlerlock);
	_sched_lock();
	_lprefork_handler();
	pid = __fork1();
	if (pid == 0) { /* child */
		/* first disable SIGWAITING */
		_sigwaitingset = 0;
#ifdef i386
		_cleanup_ldt();
#endif
		_resetlib();
		_lpostfork_child_handler();
		_sched_unlock();
		/*
		 * Now release all internal locks grabbed in parent above.
		 */
		_lwp_mutex_unlock(&_sighandlerlock);
		_lmutex_unlock(&_tidlock);
		_lwp_mutex_unlock(&_reaplock);
		_lmutex_unlock(&_tsslock);
		_lmutex_unlock(&_stkcachelock);
		_lmutex_unlock(&_calloutlock);
		/*
		 * Since _schedlock needs to be called by _reaper_create()
		 * which is called near the end of this routine, _resetlib()
		 * should not be called with _schedlock held.
		 * Since _resetlib is called only from the child of a fork1()
		 * which does not yet have threads or lwps, this is OK.
		 */
		ASSERT(!(MUTEX_HELD(&_schedlock)));
		_reaper_create();
		_dynamic_create();
		_rwlsub_unlock();
		_rw_unlock(&tsd_common.lock);
		_postfork_child_handler();
	} else {
		/*
		 * Unlock all locks in parent too.
		 */
		_lpostfork_parent_handler();
		_sched_unlock();
		_lwp_mutex_unlock(&_sighandlerlock);
		_lmutex_unlock(&_tidlock);
		_lwp_mutex_unlock(&_reaplock);
		_lmutex_unlock(&_tsslock);
		_lmutex_unlock(&_stkcachelock);
		_lmutex_unlock(&_calloutlock);
		_rwlsub_unlock();
		_rw_unlock(&tsd_common.lock);
		_postfork_parent_handler();
	}
	return (pid);
}


/*
 * We want to define fork() such a way that if user links with
 * -lthread, the original Solaris implemntation of fork (i.e .
 * forkall) should be called. If the user links with -lpthread
 * which is a filter library for posix calls, we want to make
 * fork() behave like Solaris fork1().
 */

_fork()
{
	if (_libpthread_loaded == 0)
		/* if linked with -lthread */
		return (__fork());
	else
		/* if linked with -lpthread */
		return (_fork1());
}

/*
 * This function is defined in libpthread's init section.
 * Since libpthread is "filter" library, all symbols map
 * in libthread. Hence, following function which sets
 * variable to 1 to indicate the presence of POSIX library
 */
__fork_init()
{
	_libpthread_loaded = 1;
}
