/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)newstubs.c 1.6     94/11/02 SMI"

/*
* _thr_stksegment() is a new function that should be added to this
* file if it is again compiled into libc.  lib/libc/${MACH}/Makefile
* currently has this file commented out.
*/

#ifndef PIC
#pragma weak _cond_init = _return_zero
#pragma weak _cond_destroy = _return_zero
#pragma weak _cond_wait = _return_zero
#pragma weak _cond_timedwait = _return_zero
#pragma weak _cond_signal = _return_zero
#pragma weak _cond_broadcast = _return_zero
#pragma weak _mutex_init = _return_zero
#pragma weak _mutex_destroy = _return_zero
#pragma weak _mutex_lock = _return_zero
#pragma weak _mutex_trylock = _return_zero
#pragma weak _mutex_unlock = _return_zero
#pragma weak _rwlock_init = _return_zero
#pragma weak _rwlock_destroy = _return_zero
#pragma weak _rw_rdlock = _return_zero
#pragma weak _rw_wrlock = _return_zero
#pragma weak _rw_unlock = _return_zero
#pragma weak _rw_tryrdlock = _return_zero
#pragma weak _rw_trywrlock = _return_zero
#pragma weak _sema_init = _return_zero
#pragma weak _sema_destroy = _return_zero
#pragma weak _sema_wait = _return_zero
#pragma weak _sema_post = _return_zero
#pragma weak _sema_trywait = _return_zero
#pragma weak _thr_create = _return_zero
#pragma weak _thr_join = _return_zero
#pragma weak _thr_setconcurrency = _return_zero
#pragma weak _thr_getconcurrency = _return_zero
#pragma weak _thr_exit = _return_zero
#pragma weak _thr_kill = _return_zero
#pragma weak _thr_suspend = _return_zero
#pragma weak _thr_continue = _return_zero
#pragma weak _thr_yield = _return_zero
#pragma weak _thr_setprio = _return_zero
#pragma weak _thr_getprio = _return_zero
#pragma weak _thr_min_stack = _return_zero
#pragma weak _thr_keycreate = _return_zero
#pragma weak _thr_setspecific = _return_zero
#pragma weak _thr_getspecific = _return_zero
#pragma weak _thr_sigsetmask = _return_zero
#pragma weak _thr_self = _return_one
#pragma weak _thr_main = _return_minusone
#pragma weak _sema_held = _return_one
#pragma weak _rw_read_held = _return_one
#pragma weak _rw_write_held = _return_one
#pragma weak _mutex_held = _return_one
#pragma weak _thr_errnop = _return_errnop	     
#pragma weak cond_init = _return_zero
#pragma weak cond_destroy = _return_zero
#pragma weak cond_wait = _return_zero
#pragma weak cond_timedwait = _return_zero
#pragma weak cond_signal = _return_zero
#pragma weak cond_broadcast = _return_zero
#pragma weak mutex_init = _return_zero
#pragma weak mutex_destroy = _return_zero
#pragma weak mutex_lock = _return_zero
#pragma weak mutex_trylock = _return_zero
#pragma weak mutex_unlock = _return_zero
#pragma weak rwlock_init = _return_zero
#pragma weak rwlock_destroy = _return_zero
#pragma weak rw_rdlock = _return_zero
#pragma weak rw_wrlock = _return_zero
#pragma weak rw_unlock = _return_zero
#pragma weak rw_tryrdlock = _return_zero
#pragma weak rw_trywrlock = _return_zero
#pragma weak sema_init = _return_zero
#pragma weak sema_destroy = _return_zero
#pragma weak sema_wait = _return_zero
#pragma weak sema_post = _return_zero
#pragma weak sema_trywait = _return_zero
#pragma weak thr_create = _return_zero
#pragma weak thr_join = _return_zero
#pragma weak thr_setconcurrency = _return_zero
#pragma weak thr_getconcurrency = _return_zero
#pragma weak thr_exit = _return_zero
#pragma weak thr_kill = _return_zero
#pragma weak thr_suspend = _return_zero
#pragma weak thr_continue = _return_zero
#pragma weak thr_yield = _return_zero
#pragma weak thr_setprio = _return_zero
#pragma weak thr_getprio = _return_zero
#pragma weak thr_min_stack = _return_zero
#pragma weak thr_keycreate = _return_zero
#pragma weak thr_setspecific = _return_zero
#pragma weak thr_getspecific = _return_zero
#pragma weak thr_sigsetmask = _return_zero
#pragma weak thr_self = _return_one
#pragma weak thr_main = _return_minusone
#define	STATIC
#define	return_errnop	_return_errnop
#define return_enosys	_return_enosys
#define	return_zero	_return_zero
#define return_minusone	_return_minusone
#define	return_one	_return_one
#else /* !PIC */
#pragma weak cond_init = _cond_init
#pragma weak cond_destroy = _cond_destroy
#pragma weak cond_wait = _cond_wait
#pragma weak cond_timedwait = _cond_timedwait
#pragma weak cond_signal = _cond_signal
#pragma weak cond_broadcast = _cond_broadcast
#pragma weak mutex_init = _mutex_init
#pragma weak mutex_destroy = _mutex_destroy
#pragma weak mutex_lock = _mutex_lock
#pragma weak mutex_trylock = _mutex_trylock
#pragma weak mutex_unlock = _mutex_unlock
#pragma weak rwlock_init = _rwlock_init
#pragma weak rwlock_destroy = _rwlock_destroy
#pragma weak rw_rdlock = _rw_rdlock
#pragma weak rw_wrlock = _rw_wrlock
#pragma weak rw_unlock = _rw_unlock
#pragma weak rw_tryrdlock = _rw_tryrdlock
#pragma weak rw_trywrlock = _rw_trywrlock
#pragma weak sema_init = _sema_init
#pragma weak sema_destroy = _sema_destroy
#pragma weak sema_wait = _sema_wait
#pragma weak sema_post = _sema_post
#pragma weak sema_trywait = _sema_trywait
#pragma weak thr_create = _thr_create
#pragma weak thr_join = _thr_join
#pragma weak thr_setconcurrency = _thr_setconcurrency
#pragma weak thr_getconcurrency = _thr_getconcurrency
#pragma weak thr_exit = _thr_exit
#pragma weak thr_kill = _thr_kill
#pragma weak thr_suspend = _thr_suspend
#pragma weak thr_continue = _thr_continue
#pragma weak thr_yield = _thr_yield
#pragma weak thr_setprio = _thr_setprio
#pragma weak thr_getprio = _thr_getprio
#pragma weak thr_min_stack = _thr_min_stack
#pragma weak thr_keycreate = _thr_keycreate
#pragma weak thr_setspecific = _thr_setspecific
#pragma weak thr_getspecific = _thr_getspecific
#pragma weak thr_sigsetmask = _thr_sigsetmask
#pragma weak thr_main = _thr_main
#pragma weak thr_self = _thr_self
#pragma weak _sema_held = __sema_held
#pragma weak _rw_read_held = __rw_read_held
#pragma weak _rw_write_held = __rw_write_held
#pragma weak _mutex_held = __mutex_held
#endif

#include "synonyms.h"
#include <sys/errno.h>
#ifdef PIC
#define	STATIC	static
#include "newstubs.h"
#endif /* PIC */

STATIC int *return_errnop();
STATIC int return_enosys();
STATIC int return_zero();
STATIC int return_minusone();
STATIC int return_one();
extern int errno;

#ifdef PIC
static call_vector cv_stubs = {
	return_zero,		/* _cond_init */
	return_zero,		/* _cond_destroy */
	return_zero,		/* _cond_wait */
	return_zero,		/* _cond_timedwait */
	return_zero,		/* _cond_signal */
	return_zero,		/* _cond_broadcast */
	return_zero,		/* _mutex_init */
	return_zero,		/* _mutex_destroy */
	return_zero,		/* _mutex_lock */
	return_zero,		/* _mutex_trylock */
	return_zero,		/* _mutex_unlock */
	return_zero,		/* _rwlock_init */
	return_zero,		/* _rwlock_destroy */
	return_zero,		/* _rw_rdlock */
	return_zero,		/* _rw_wrlock */
	return_zero,		/* _rw_unlock */
	return_zero,		/* _rw_tryrdlock */
	return_zero,		/* _rw_trywrlock */
	return_zero,		/* _sema_init */
	return_zero,		/* _sema_destroy */
	return_zero,		/* _sema_wait */
	return_zero,		/* _sema_post */
	return_zero,		/* _sema_trywait */
	return_zero,		/* _thr_create */
	return_zero,		/* _thr_join */
	return_zero,		/* _thr_setconcurrency */
	return_zero,		/* _thr_getconcurrency */
	return_zero,		/* _thr_exit */
	return_zero,		/* _thr_kill */
	return_zero,		/* _thr_suspend */
	return_zero,		/* _thr_continue */
	return_zero,		/* _thr_yield */
	return_zero,		/* _thr_setprio */
	return_zero,		/* _thr_getprio */
	return_zero,		/* _thr_min_stack */
	return_zero,		/* _thr_keycreate */
	return_zero,		/* _thr_setspecific */
	return_zero,		/* _thr_getspecific */
	return_zero,		/* _thr_sigsetmask */
	return_one,		/* _thr_self */
	return_minusone,	/* _thr_main */
	return_one,		/* _sema_held */
	return_one,		/* _rw_read_held */
	return_one,		/* _rw_write_held */
	return_one,		/* _mutex_held */
	return_errnop		/* _thr_errnop */
};

static call_vector *cv = &cv_stubs;
static int changed_once = 0;

#endif /* PIC */

STATIC int
return_enosys()
{
	return (ENOSYS);
}

STATIC int
return_zero()
{
	return (0);
}

STATIC int
return_minusone()
{
	return (-1);
}

STATIC int
return_one()
{
	return (1);
}

STATIC int *
return_errnop()
{
	return (&errno);
}

#ifdef PIC

int *
_thr_errnop(void)
{
	return ((cv->_thr_errnop)());
}

DEFSTUB0(_thr_main)
DEFSTUB0(_thr_self)
DEFSTUB0(_thr_min_stack)
DEFSTUB0(_thr_yield)
DEFSTUB0(_thr_getconcurrency)

DEFSTUB1(_cond_destroy, cond_t *)
DEFSTUB1(_cond_signal, cond_t *)
DEFSTUB1(_cond_broadcast, cond_t *)
DEFSTUB1(_mutex_destroy, mutex_t *)
DEFSTUB1(_mutex_lock, mutex_t *)
DEFSTUB1(_mutex_trylock, mutex_t *)
DEFSTUB1(_mutex_unlock, mutex_t *)
DEFSTUB1(_rwlock_destroy, rwlock_t *)
DEFSTUB1(_rw_rdlock, rwlock_t *)
DEFSTUB1(_rw_wrlock, rwlock_t *)
DEFSTUB1(_rw_unlock, rwlock_t *)
DEFSTUB1(_rw_tryrdlock, rwlock_t *)
DEFSTUB1(_rw_trywrlock, rwlock_t *)
DEFSTUB1(_sema_destroy, sema_t *)
DEFSTUB1(_sema_wait, sema_t *)
DEFSTUB1(_sema_post, sema_t *)
DEFSTUB1(_sema_trywait, sema_t *)
DEFSTUB1(__sema_held, sema_t *)
DEFSTUB1(__rw_read_held, rwlock_t *)
DEFSTUB1(__rw_write_held, rwlock_t *)
DEFSTUB1(__mutex_held, mutex_t *)
DEFSTUB1(_thr_setconcurrency, int)
DEFSTUB1(_thr_exit, void *)
DEFSTUB1(_thr_suspend, thread_t)
DEFSTUB1(_thr_continue, thread_t)

DEFSTUB2(_cond_wait, cond_t *, mutex_t *)
DEFSTUB2(_thr_setspecific, thread_key_t, void *)
DEFSTUB2(_thr_getspecific, thread_key_t, void **)
DEFSTUB2(_thr_keycreate, thread_key_t *, PFrV)
DEFSTUB2(_thr_kill, thread_t, int)
DEFSTUB2(_thr_setprio, thread_t, int)
DEFSTUB2(_thr_getprio, thread_t, int *)

DEFSTUB3(_cond_init, cond_t *, int, void *)
DEFSTUB3(_cond_timedwait, cond_t *, mutex_t *, timestruc_t *)
DEFSTUB3(_mutex_init, mutex_t *, int, void *)
DEFSTUB3(_rwlock_init, rwlock_t *, int, void *)
DEFSTUB3(_thr_sigsetmask, int, sigset_t *, sigset_t *)
DEFSTUB3(_thr_join, thread_t, thread_t *, void **)

DEFSTUB4(_sema_init, sema_t *, unsigned int, int, void *)

int
_thr_create(void *x1, size_t x2, void *(x3)(void *), void *x4,
		long x5, thread_t *x6)
{
	return ((cv->_thr_create)(x1, x2, x3, x4, x5, x6));
}

void
_libc_setup_threads(call_vector *newcv)
{
	if (changed_once == 0) {
		cv = newcv;
		changed_once = 1;
	}
}
#endif /* PIC */
