/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)sem.c	1.3	94/12/16	SMI"

#ifdef __STDC__
#pragma weak	sem_open = _sem_open
#pragma weak	sem_close = _sem_close
#pragma weak	sem_unlink = _sem_unlink
#pragma weak	sem_init = _sem_init
#pragma weak	sem_destroy = _sem_destroy
#pragma weak	sem_wait = _sem_wait
#pragma weak	sem_trywait = _sem_trywait
#pragma weak	sem_post = _sem_post
#pragma weak	sem_getvalue = _sem_getvalue
#pragma weak	_sema_init
#pragma weak	_sema_destroy
#pragma weak	_sema_post
#pragma weak	_sema_trywait
#pragma weak	_sema_wait_cancel
#endif	/* __STDC__ */


#include <sys/types.h>
#include <semaphore.h>
#include <synch.h>
#include <errno.h>


sem_t *
_sem_open(const char *name, int oflag, /* mode_t mode, int value */ ...)
{
	errno = ENOSYS;
	return ((sem_t *)-1);
}


int
_sem_close(sem_t *sem)
{
	errno = ENOSYS;
	return (-1);
}


int
_sem_unlink(const char *name)
{
	errno = ENOSYS;
	return (-1);
}

int
_sem_init(sem_t *sem, int pshared, uint_t value)
{
	int	err;
	char	type = USYNC_THREAD;

	if (_thr_main() == -1) {
		/* libthread is not linked */
		errno = ENOSYS;
		return (-1);
	}

	if (pshared != 0)
		type = USYNC_PROCESS;

	/* call to libthread sema_init */
	err = _sema_init((sema_t *)sem, value, type, 0);
	if (err != 0) {
		errno = err;
		return (-1);
	}
	return (0);
}


int
_sem_destroy(sem_t *sem)
{
	int	err;

	if (_thr_main() == -1) {
		/* libthread is not linked */
		errno = ENOSYS;
		return (-1);
	}

	/* call to libthread sema_destroy */
	err = _sema_destroy((sema_t *)sem);
	if (err != 0) {
		errno = err;
		return (-1);
	}
	return (0);
}


int
_sem_post(sem_t *sem)
{
	int	err;

	if (_thr_main() == -1) {
		/* libthread is not linked */
		errno = ENOSYS;
		return (-1);
	}

	/* call to libthread sema_post */
	err = _sema_post((sema_t *)sem);
	if (err != 0) {
		errno = err;
		return (-1);
	}
	return (0);
}


int
_sem_wait(sem_t *sem)
{
	int	err;

	if (_thr_main() == -1) {
		/* libthread is not linked */
		errno = ENOSYS;
		return (-1);
	}

	/* call to libthread sema_wait */
	err = _sema_wait_cancel((sema_t *)sem);
	if (err != 0) {
		errno = err;
		return (-1);
	}
	return (0);
}


int
_sem_trywait(sem_t *sem)
{
	int	err;

	if (_thr_main() == -1) {
		/* libthread is not linked */
		errno = ENOSYS;
		return (-1);
	}

	/* call to libthread sema_trywait */
	err = _sema_trywait((sema_t *)sem);
	if (err != 0) {
		errno = err;
		return (-1);
	}
	return (0);
}


int
_sem_getvalue(sem_t *sem, int *sval)
{
	*sval =  sem->sem_count;
	return (0);
}
