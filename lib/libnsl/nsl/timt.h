/*
 * Copyright (C) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_TIMT_H
#define	_TIMT_H

#pragma ident	"@(#)timt.h	1.8	94/05/26 SMI"

/*
 * Threading and mutual exclusion declarations
 */

#include <thread.h>
#include <synch.h>

#ifdef _REENTRANT

#define	mutex_lock(m)			_mutex_lock(m)
#define	mutex_trylock(m)		_mutex_trylock(m)
#define	mutex_unlock(m)			_mutex_unlock(m)
#define	mutex_init(m, n, p)		_mutex_init(m, n, p)
#define	thr_self()			_thr_self()
#define	thr_exit(x)			_thr_exit(x)

#define	MUTEX_LOCK_SIGMASK(lock, oldmask) \
{	sigset_t newmask; \
	_sigfillset(&newmask); \
	_thr_sigsetmask(SIG_SETMASK, &newmask, &(oldmask)); \
	mutex_lock(lock); \
}

#define	MUTEX_UNLOCK_SIGMASK(lock, oldmask) \
{	sigset_t tmpmask; \
	mutex_unlock(lock); \
	_thr_sigsetmask(SIG_SETMASK, &(oldmask), &tmpmask); \
}
#define	MUTEX_LOCK_PROCMASK(lock, oldmask) \
{	sigset_t newmask; \
	(void) _sigfillset(&newmask); \
	(void) _sigprocmask(SIG_SETMASK, &newmask, &(oldmask)); \
	_mutex_lock(lock); \
}

#define	MUTEX_UNLOCK_PROCMASK(lock, oldmask) \
{	sigset_t tmpmask; \
	_mutex_unlock(lock); \
	(void) _sigprocmask(SIG_SETMASK, &(oldmask), &tmpmask); \
}

#else /* _REENTRANT */

#define	mutex_lock(m)
#define	mutex_trylock(m)
#define	mutex_unlock(m)
#define	mutex_init(m, n, p)
#define	thr_self()
#define	thr_exit()

#define	MUTEX_LOCK_SIGMASK(lock, oldmask)
#define	MUTEX_UNLOCK_SIGMASK(lock, oldmask)
#define	MUTEX_LOCK_PROCMASK(lock, oldmask) \
{	sigset_t newmask; \
	(void) _sigfillset(&newmask); \
	(void) _sigprocmask(SIG_SETMASK, &newmask, &(oldmask)); \
}

#define	MUTEX_UNLOCK_PROCMASK(lock, oldmask) \
{	sigset_t tmpmask; \
	(void) _sigprocmask(SIG_SETMASK, &(oldmask), &tmpmask); \
}

#endif /* _REENTRANT */

#endif	/* _TIMT_H */
