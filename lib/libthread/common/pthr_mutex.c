/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	" @(#)pthr_mutex.c 1.7 94/09/14 "

#ifdef __STDC__

#pragma weak	pthread_mutexattr_init = _pthread_mutexattr_init
#pragma weak	pthread_mutexattr_destroy =  _pthread_mutexattr_destroy
#pragma weak	pthread_mutexattr_setpshared =  _pthread_mutexattr_setpshared
#pragma weak	pthread_mutexattr_getpshared =  _pthread_mutexattr_getpshared
#pragma weak	pthread_mutexattr_setprotocol =  _pthread_mutexattr_setprotocol
#pragma weak	pthread_mutexattr_getprotocol =  _pthread_mutexattr_getprotocol
#pragma weak	pthread_mutexattr_setprioceiling = \
					_pthread_mutexattr_setprioceiling
#pragma weak	pthread_mutexattr_getprioceiling = \
					_pthread_mutexattr_getprioceiling
#pragma weak	pthread_mutex_setprioceiling =  _pthread_mutex_setprioceiling
#pragma weak	pthread_mutex_getprioceiling =  _pthread_mutex_getprioceiling
#pragma weak	pthread_mutex_init = _pthread_mutex_init

#endif /* __STDC__ */

#include "libpthr.h"
#include "libthread.h"

/*
 * POSIX.1c
 * pthread_mutexattr_init: allocates the mutex attribute object and
 * initializes it with the default values.
 */
int
_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	mattr_t	*ap;

	if ((ap = (mattr_t *)_alloc_attr(sizeof (mattr_t))) != NULL) {
		ap->pshared = DEFAULT_TYPE;
		attr->pthread_mutexattrp = ap;
		return (0);
	} else
		return (ENOMEM);
}

/*
 * POSIX.1c
 * pthread_mutexattr_destroy: frees the mutex attribute object and
 * invalidates it with NULL value.
 */
int
_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	if (attr == NULL || attr->pthread_mutexattrp == NULL ||
			_free_attr(attr->pthread_mutexattrp) < 0)
		return (EINVAL);
	attr->pthread_mutexattrp = NULL;
	return (0);
}

/*
 * POSIX.1c
 * pthread_mutexattr_setpshared: sets the shared attr to PRIVATE or
 * SHARED.
 * This is equivalent to setting USYNC_PROCESS/USYNC_THREAD flag in
 * mutex_init().
 */
int
_pthread_mutexattr_setpshared(pthread_mutexattr_t *attr,
						int pshared)
{
	mattr_t	*ap;


	if (attr != NULL && (ap = attr->pthread_mutexattrp) != NULL &&
		(pshared == PTHREAD_PROCESS_PRIVATE ||
			pshared == PTHREAD_PROCESS_SHARED)) {
		ap->pshared = pshared;
		return (0);
	} else {
		return (EINVAL);
	}
}

/*
 * POSIX.1c
 * pthread_mutexattr_getpshared: gets the shared attr.
 */
int
_pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
						int *pshared)
{
	mattr_t	*ap;

	if (pshared != NULL && attr != NULL &&
			(ap = attr->pthread_mutexattrp) != NULL) {
		*pshared = ap->pshared;
		return (0);
	} else {
		return (EINVAL);
	}
}

/*
 * POSIX.1c
 * pthread_mutexattr_setprioceiling: sets the prioceiling attr.
 * Currently unsupported.
 */
int
_pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr,
						int prioceiling)
{
	return (ENOSYS);
}

/*
 * POSIX.1c
 * pthread_mutexattr_getprioceiling: gets the prioceiling attr.
 * Currently unsupported.
 */
int
_pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *attr,
						int *ceiling)
{
	return (ENOSYS);
}

/*
 * POSIX.1c
 * pthread_mutexattr_setprotocol: sets the protocol attribute.
 * Currently unsupported.
 */
int
_pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
						int protocol)
{
	return (ENOSYS);
}

/*
 * POSIX.1c
 * pthread_mutexattr_getprotocol: gets the protocol attribute.
 * Currently unsupported.
 */
int
_pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
						int *protocol)
{
	return (ENOSYS);
}

/*
 * POSIX.1c
 * pthread_mutex_init: Initializes the mutex object. It copies the
 * pshared attr into type argument and calls mutex_init().
 */
int
_pthread_mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *attr)
{
	int	type;

	if (attr != NULL) {
		if (attr->pthread_mutexattrp == NULL)
			return (EINVAL);

		type = ((mattr_t *)attr->pthread_mutexattrp)->pshared;
	} else {
		type = DEFAULT_TYPE;
	}

	return (_mutex_init((mutex_t *) mutex, type, NULL));
}

/*
 * POSIX.1c
 * pthread_mutex_setprioceiling: sets the prioceiling.
 * Currently unsupported.
 */
_pthread_mutex_setprioceiling(pthread_mutex_t *mutex,
				int prioceiling, int *oldceiling)
{
	return (ENOSYS);
}

/*
 * POSIX.1c
 * pthread_mutex_getprioceiling: gets the prioceiling.
 * Currently unsupported.
 */
int
_pthread_mutex_getprioceiling(const pthread_mutex_t *mutex,
					int *ceiling)
{
	return (ENOSYS);
}
