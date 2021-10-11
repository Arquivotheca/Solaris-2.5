/*
 *	Copyright (c) 1992 by Sun Microsystems, Inc
 */

#pragma ident	"@(#)sockmt.h	1.9	94/06/03 SMI"

#include <synch.h>
#include <thread.h>

#ifdef _REENTRANT
#define	MUTEX_LOCK_SIGMASK(lock, oldmask) \
{	sigset_t newmask; \
	(void) _sigfillset(&newmask); \
	_thr_sigsetmask(SIG_SETMASK, &newmask, &(oldmask)); \
	_mutex_lock(lock); \
}

#define	MUTEX_UNLOCK_SIGMASK(lock, oldmask) \
{	sigset_t tmpmask; \
	_mutex_unlock(lock); \
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

#ifdef lint
#define	_thr_sigsetmask(how, set, oset) thr_sigsetmask(how, set, oset)
#define	_mutex_lock(m)			mutex_lock(m)
#define	_mutex_unlock(m)		mutex_unlock(m)
#define	_ioctl(fd, req, arg)		ioctl(fd, req, arg)
#define	_mknod(path, mode, dev)		mknod(path, mode, dev)
#define	_stat(path, buf)		stat(path, buf)
#define	_fcntl(fd, cmd, arg)		fcntl(fd, cmd, arg)
#define	_listen(fd, count)		listen(fd, count)
#endif /* lint */
