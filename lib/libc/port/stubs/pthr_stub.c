/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)pthr_stub.c 1.1	95/08/24 SMI"

#pragma weak  pthread_mutexattr_init = _pthread_stub
#pragma weak  _pthread_mutexattr_init = _pthread_stub
#pragma weak  pthread_mutexattr_destroy = _pthread_stub
#pragma weak  _pthread_mutexattr_destroy = _pthread_stub
#pragma weak  pthread_mutexattr_setpshared = _pthread_stub
#pragma weak  _pthread_mutexattr_setpshared = _pthread_stub
#pragma weak  pthread_mutexattr_getpshared = _pthread_stub
#pragma weak  _pthread_mutexattr_getpshared = _pthread_stub
#pragma weak  pthread_mutexattr_setprotocol = _pthread_stub
#pragma weak  _pthread_mutexattr_setprotocol = _pthread_stub
#pragma weak  pthread_mutexattr_getprotocol = _pthread_stub
#pragma weak  _pthread_mutexattr_getprotocol = _pthread_stub
#pragma weak  pthread_mutexattr_setprioceiling = _pthread_stub
#pragma weak  _pthread_mutexattr_setprioceiling = _pthread_stub
#pragma weak  pthread_mutexattr_getprioceiling = _pthread_stub
#pragma weak  _pthread_mutexattr_getprioceiling = _pthread_stub
#pragma weak  pthread_mutex_init = _pthread_stub
#pragma weak  _pthread_mutex_init = _pthread_stub
#pragma weak  pthread_mutex_destroy = _pthread_stub
#pragma weak  _pthread_mutex_destroy = _pthread_stub
#pragma weak  pthread_mutex_lock = _pthread_stub
#pragma weak  _pthread_mutex_lock = _pthread_stub
#pragma weak  pthread_mutex_unlock = _pthread_stub
#pragma weak  _pthread_mutex_unlock = _pthread_stub
#pragma weak  pthread_mutex_trylock = _pthread_stub
#pragma weak  _pthread_mutex_trylock = _pthread_stub
#pragma weak  pthread_mutex_setprioceiling = _pthread_stub
#pragma weak  _pthread_mutex_setprioceiling = _pthread_stub
#pragma weak  pthread_mutex_getprioceiling = _pthread_stub
#pragma weak  _pthread_mutex_getprioceiling = _pthread_stub
#pragma weak  pthread_condattr_init = _pthread_stub
#pragma weak  _pthread_condattr_init = _pthread_stub
#pragma weak  pthread_condattr_destroy = _pthread_stub
#pragma weak  _pthread_condattr_destroy = _pthread_stub
#pragma weak  pthread_condattr_setpshared = _pthread_stub
#pragma weak  _pthread_condattr_setpshared = _pthread_stub
#pragma weak  pthread_condattr_getpshared = _pthread_stub
#pragma weak  _pthread_condattr_getpshared = _pthread_stub
#pragma weak  pthread_cond_init = _pthread_stub
#pragma weak  _pthread_cond_init = _pthread_stub
#pragma weak  pthread_cond_destroy = _pthread_stub
#pragma weak  _pthread_cond_destroy = _pthread_stub
#pragma weak  pthread_cond_broadcast = _pthread_stub
#pragma weak  _pthread_cond_broadcast = _pthread_stub
#pragma weak  pthread_cond_signal = _pthread_stub
#pragma weak  _pthread_cond_signal = _pthread_stub
#pragma weak  pthread_cond_wait = _pthread_stub
#pragma weak  _pthread_cond_wait = _pthread_stub
#pragma weak  pthread_cond_timedwait = _pthread_stub
#pragma weak  _pthread_cond_timedwait = _pthread_stub

int
_pthread_stub()
{
	return (0);
}
