/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)plock.c	1.3	92/09/05 SMI"

/*
 * plock - lock "segments" in physical memory.
 *
 * Supports SVID-compatible plock, taking into account dynamically linked
 * objects (such as shared libraries).
 */
#ifdef __STDC__
	#pragma weak plock = _plock
#endif

#include "synonyms.h"
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/lock.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

/*
 * Module-scope variables.
 */
static	int page_size = 0;		/* cached pagesize */
static	int lock_state = 0;		/* lock state */
static	int state_pid = -1;		/* pid to which state belongs */
#ifdef _REENTRANT
static mutex_t plock_lock = DEFAULTMUTEX;
#endif _REENTRANT

/*
 * plock 
 */
int
plock(op)
	int op;				/* desired operation */
{
	int 	e = 0;			/* return value */
	int	pid;			/* current pid */
	caddr_t	a1, a2;			/* loop variables */

	/*
	 * Initialize static caches.
	 */
	_mutex_lock(&plock_lock);
	if (page_size == 0)
		page_size = sysconf(_SC_PAGESIZE);

	/*
	 * Validate state of lock's.  If parent has forked, then
	 * the lock state needs to be reset (children do not inherit
	 * memory locks, and thus do not inherit their state).
	 */
	if ((pid = getpid()) != state_pid) {
		lock_state = 0;
		state_pid = pid;
	}

	/*
	 * Dispatch on operation.  Note: plock and its relatives depend
	 * upon "op" being bit encoded.
	 */
	switch (op) {

	/*
	 * UNLOCK: remove all memory locks.  Requires that some be set!
	 */
	case UNLOCK:
		if (lock_state == 0) {
			errno = EINVAL;
			_mutex_unlock(&plock_lock);
			return (-1);
		}
		if (e = munlockall()){
			_mutex_unlock(&plock_lock);
			return (-1);
		}
		else {
			lock_state = 0;
			_mutex_unlock(&plock_lock);
			return (0);
		}
		/*NOTREACHED*/

	/*
	 * TXTLOCK: locks text segments.  
	 */
	case TXTLOCK:

		/*
		 * If a text or process lock is already set, then fail.
		 */
		if ((lock_state & TXTLOCK) || (lock_state & PROCLOCK)) {
			errno = EINVAL;
			_mutex_unlock(&plock_lock);
			return (-1);
		}

		/*
		 * Try to apply the lock(s).  If a failure occurs,
		 * memcntl backs them out automatically.
		 */
		e = memcntl(0, 0, MC_LOCKAS, (caddr_t) MCL_CURRENT, 
		    PROC_TEXT|PRIVATE, (int) NULL);
		 if (!e)
			lock_state |= TXTLOCK;
		_mutex_unlock(&plock_lock);
		return (e);
		/*NOTREACHED*/

	/*
	 * DATLOCK: locks data segment(s), including the stack and all
	 * future growth in the address space.
	 */
	case DATLOCK:

		/*
		 * If a data or process lock is already set, then fail.
		 */
		if ((lock_state & DATLOCK) || (lock_state & PROCLOCK)) {
			errno = EINVAL;
			_mutex_unlock(&plock_lock);
			return (-1);
		}

		/*
		 * Try to lock the data and stack segments.  On failure
		 * memcntl undoes the locks internally.
		 */
		e = memcntl(0, 0, MC_LOCKAS, (caddr_t) MCL_CURRENT, 
		    PROC_DATA|PRIVATE, (int) NULL);
		if (e) {
			_mutex_unlock(&plock_lock);
			return (-1);
		}

		/* try to set a lock for all future mappings. */
		e = mlockall(MCL_FUTURE);

		/*
		 * If failures have occurred, back out the locks
		 * and return failure.
		 */
		if (e) {
			e = errno;
			(void) memcntl(0, 0, MC_UNLOCKAS, (caddr_t)MCL_CURRENT,
				PROC_DATA|PRIVATE, (int)NULL);
			errno = e;
			_mutex_unlock(&plock_lock);
			return (-1);
		}

		/*
		 * Data, stack, and growth have been locked.  Set state
		 * and return success.
		 */
		lock_state |= DATLOCK;
		_mutex_unlock(&plock_lock);
		return (0);
		/*NOTREACHED*/

	/*
	 * PROCLOCK: lock everything, and all future things as well.
	 * There should be nothing locked when this is called.
	 */
	case PROCLOCK:
		if (lock_state) {
			errno = EINVAL;
			_mutex_unlock(&plock_lock);
			return (-1);
		}
		if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
			lock_state |= PROCLOCK;
			_mutex_unlock(&plock_lock);
			return (0);
		} else {
			_mutex_unlock(&plock_lock);
			return (-1);
		}
		/*NOTREACHED*/

	/*
	 * Invalid operation.
	 */
	default:
		errno = EINVAL;
		_mutex_unlock(&plock_lock);
		return (-1);
		/*NOTREACHED*/
	}
}
