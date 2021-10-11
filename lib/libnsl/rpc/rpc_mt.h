
/*
 *	Contains the mt libraries include definitions
 */

#pragma ident	"@(#)rpc_mt.h	1.8	94/10/19 SMI"

#ifdef _REENTRANT

#define	mutex_lock(m)			_mutex_lock(m)
#define	mutex_trylock(m)		_mutex_trylock(m)
#define	mutex_unlock(m)			_mutex_unlock(m)
#define	mutex_init(m, n, p)		_mutex_init(m, n, p)
#define	cond_signal(m)			_cond_signal(m)
#define	cond_wait(c, m)			_cond_wait(c, m)
#define	cond_init(m, n, p)		_cond_init(m, n, p)
#define	cond_broadcast(c)		_cond_broadcast(c)
#define	rwlock_init(m, n, p)		_rwlock_init(m, n, p)
#define	rw_wrlock(x)			_rw_wrlock(x)
#define	rw_rdlock(x)			_rw_rdlock(x)
#define	rw_unlock(x)			_rw_unlock(x)
#define	thr_self()			_thr_self()
#define	thr_exit(x)			_thr_exit(x)
#define	thr_keycreate(m, n)		_thr_keycreate(m, n)
#define	thr_setspecific(k, p)		_thr_setspecific(k, p)
#define	thr_getspecific(k, p)		_thr_getspecific(k, p)
#define	thr_sigsetmask(f, n, o)		_thr_sigsetmask(f, n, o)
#define	thr_create(s, t, r, a, f, n)	_thr_create(s, t, r, a, f, n)

#else

#define	rwlock_init(m)
#define	rw_wrlock(x)
#define	rw_rdlock(x)
#define	rw_unlock(x)
#define	_rwlock_init(m, n, p)
#define	_rw_wrlock(x)
#define	_rw_rdlock(x)
#define	_rw_unlock(x)
#define	mutex_lock(m)
#define	mutex_unlock(m)
#define	mutex_init(m, n, p)
#define	_mutex_lock(m)
#define	_mutex_unlock(m)
#define	_mutex_init(m, n, p)
#define	cond_signal(m)
#define	cond_wait(m)
#define	cond_init(m, n, p)
#define	_cond_signal(m)
#define	_cond_wait(m)
#define	_cond_init(m, n, p)
#define	cond_broadcast(c)
#define	_cond_broadcast(c)
#define	thr_self()
#define	_thr_self()
#define	thr_exit(x)
#define	_thr_exit(x)
#define	thr_keycreate(m, n)
#define	_thr_keycreate(m, n)
#define	thr_setspecific(k, p)
#define	_thr_setspecific(k, p)
#define	thr_getspecific(k, p)
#define	_thr_getspecific(k, p)
#define	thr_setsigmask(f, n, o)
#define	_thr_setsigmask(f, n, o)
#define	thr_create(s, t, r, a, f, n)
#define	_thr_create(s, t, r, a, f, n)

#endif _REENTRANT

#define	MT_ASSERT_HELD(x)


#include <thread.h>
#include <synch.h>

extern int _thr_main();
