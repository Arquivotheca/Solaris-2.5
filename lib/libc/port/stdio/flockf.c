/*	Copyright (c) 1992 Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)flockf.c	1.18	95/08/28 SMI"	/* SVr4.0 1.2   */

#ifdef __STDC__
#pragma weak flockfile = _flockfile
#pragma weak ftrylockfile = _ftrylockfile
#pragma weak funlockfile = _funlockfile
#endif /* __STDC__ */

#include "synonyms.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

/*
 * we move the threads check out of the
 * The _rmutex_lock/_rmutex_unlock routines are only called (within libc !)
 * by _flockget, _flockfile, and _flockrel, _funlockfile, respectively.
 * _flockget and _flockrel are only called by the FLOCKFILE/FUNLOCKFILE
 * macros in mtlib.h. We place the "if (_thr_main() == -1)" check for
 * threads there, and remove it from:
 *	_rmutex_lock(rm)
 *	_rmutex_unlock(rm)
 *	_flockget(FILE *iop)
 *	_ftrylockfile(FILE *iop)
 * No change is made to _funlockfile(iop) as it makes no additional thread
 * check.
 *
 * No such change is made to
 *	_rmutex_trylock(rl)		since it is called by _findiop
 *	_flockfile(iop)			since we don't know who uses it
 */

static void
_rmutex_lock(rm)
rmutex_t *rm;
{
	register thread_t self = _thr_self();
	register mutex_t *lk = &rm->_mutex;

	_mutex_lock(lk);
	if (rm->_owner != 0 && rm->_owner != self) {
		rm->_wait_cnt++;
		do {
			_cond_wait(&rm->_cond, lk);
		} while (rm->_owner != 0 && rm->_owner != self);
		rm->_wait_cnt--;
	}
	/* lock is now available to this thread */
	rm->_owner = self;
	rm->_lock_cnt++;
	_mutex_unlock(lk);
}


int
_rmutex_trylock(rm)
rmutex_t *rm;
{
	register thread_t self = _thr_self();
	register mutex_t *lk = &rm->_mutex;

	/*
	 * Treat like a stub if not linked with libthread as
	 * indicated by _thr_main() returning -1.
	 */
	if (_thr_main() == -1)
		return (0);

	_mutex_lock(lk);
	if (rm->_owner != 0 && rm->_owner != self) {
		_mutex_unlock(lk);
		return (-1);
	}
	/* lock is now available to this thread */
	rm->_owner = self;
	rm->_lock_cnt++;
	_mutex_unlock(lk);
	return (0);
}


/*
 * recursive mutex unlock function
 */

static void
_rmutex_unlock(rm)
rmutex_t *rm;
{
	register thread_t self = _thr_self();
	register mutex_t *lk = &rm->_mutex;

	_mutex_lock(lk);
	if (rm->_owner == self) {
		rm->_lock_cnt--;
		if (rm->_lock_cnt == 0) {
			rm->_owner = 0;
			if (rm->_wait_cnt)
				_cond_signal(&rm->_cond);
		}
	} else {
		char msg[] =
		    "libc internal error: _rmutex_unlock: rmutex not held.\n";
		(void) write(2, msg, sizeof (msg));
		abort();
	}
	_mutex_unlock(lk);
}


/*
 * compute the lock's position, acquire it and return its pointer
 */

rmutex_t *
_flockget(FILE *iop)
{
	register rmutex_t *rl = 0;

	rl = IOB_LCK(iop);
	/*
	 * IOB_LCK may return a NULL pointer which means that
	 * the iop does not require locking, and does not have
	 * a lock associated with it.
	 */
	if (rl != NULL)
		_rmutex_lock(rl);
	return (rl);
}


/*
 * POSIX.1c version of ftrylockfile().
 * It returns 0 if it gets the lock else returns -1 to indicate the error.
 */

int
_ftrylockfile(FILE *iop)
{
	return (_rmutex_trylock(IOB_LCK(iop)));
}


void
_flockrel(rmutex_t *rl)
{
	/*
	 * may be called with a NULL pointer which means that the
	 * associated iop had no lock. see comments in flockget()
	 * on how lock could be NULL.
	 */
	if (rl != NULL)
		_rmutex_unlock(rl);
}


void
_flockfile(iop)
FILE *iop;
{
	_rmutex_lock(IOB_LCK(iop));
}


void
_funlockfile(iop)
FILE *iop;
{
	_rmutex_unlock(IOB_LCK(iop));
}
