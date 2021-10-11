/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)vold_thread.c	1.9	94/11/09 SMI"

#include	<errno.h>

#include	"multithread.h"




int
mutex_setup(mutex_t *mp)
{
	int	rval = 0;


#ifdef	SUN_LWPS
	(void) memset((char *)mp, 0, sizeof (mutex_t));
#endif

#ifdef	SUN_THREADS
	rval = mutex_init(mp, USYNC_THREAD, 0);
#endif

	return (rval);
}


int
mutex_enter(mutex_t *mp)
{
	int	rval = 0;

#ifdef SUN_LWPS
	int	err;

	lwp_mutex_enter(mp, &err);
	if (rval == -1) {
		errno = err;
	}
#endif

#ifdef SUN_THREADS
	rval = mutex_lock(mp);
#endif

	return (rval);
}


int
mutex_tryenter(mutex_t *mp)
{
	int	rval = 0;


#ifdef	SUN_LWPS
	rval = lwp_mutex_trylock(mp);
#endif

#ifdef	SUN_THREADS
	rval = mutex_trylock(mp);
#endif

	return (rval);
}


int
mutex_exit(mutex_t *mp)
{
	int	rval = 0;

#ifdef SUN_LWPS
	int	err;

	rval = lwp_mutex_exit(mp, &err);
	if (rval == -1) {
		errno = err;
	}
#endif

#ifdef SUN_THREADS
	rval = mutex_unlock(mp);
#endif

	return (rval);
}


int
thread_self()
{
	int	rval = 0;

#ifdef SUN_LWPS
	rval = lwp_self();
#endif

#ifdef SUN_THREADS
	rval = thr_self();
#endif

	return (rval);
}


int
thread_join(int tid)
{
	int	rval = 0;

#ifdef SUN_LWPS
	rval = lwp_wait((lwp_id_t)tid, 0);
#endif

#ifdef SUN_THREADS
	rval = thr_join((thread_t)tid, 0, 0);
#endif

	return (rval);
}


void
thread_exit()
{

#ifdef	SUN_THREADS
	thr_exit(NULL);
#endif

#ifdef	SUN_LWPS
	lwd_destroy(SELF);
#endif
}


#ifdef SUN_LWPS
#define	STKSIZE	8192
#endif

#ifdef	SUN_THREADS
#define	STKSIZE	(32 * 1024)	/* 32k -- 8k (default) is too small! */
#endif

int
thread_create(void *stk, size_t ssize, void *(*func)(void *), void *arg)
{
	int	id = 0;
	int	rval;


#ifdef SUN_LWPS
	/*
	 * XXX: This business of checking for stk == 0 is just to allow
	 * XXX: easier switching between LWPS and threads -- we never
	 * XXX: actually free this stack, so this is not a very good
	 * XXX: thing.
	 */
	if (stk == NULL) {
		if (ssize == 0) {
			stk = (void *)malloc(STKSIZE);
		} else {
			stk = (void *)malloc(ssize);
		}
		/* XXX: assume malloc succeeds ? */
	}

	id = (int)lwp_create(stk, ssize, (void(*)())func, (int)arg, 0);
#endif

#ifdef SUN_THREADS
	/*
	 * if caller's using default stk & size (i.e. 0) then set stack
	 *	to minimum (which seems empirically to be 16k)
	 */
	if ((stk == NULL) && (ssize == 0)) {
	    ssize = STKSIZE;				/* set stack size */
	}

	rval = thr_create(stk, ssize, func, arg, THR_BOUND,
		(thread_t *)&id);
	if (rval != 0) {
		id = -1;
	}
#endif

#ifdef NO_MT
	(void) (*func)(arg);
#endif
	return (id);
}


int
thread_kill(int tid, int sig)
{
	int	rval = 0;

#ifdef SUN_LWPS
	rval = lwp_kill(tid, sig);
#endif

#ifdef SUN_THREADS
	rval = thr_kill((thread_t)tid, sig);
#endif

	return (rval);
}


int
cv_setup(cond_t *cvp)
{
	int	rval = 0;

#ifdef	SUN_LWPS
	(void) memset(cvp, 0, sizeof (cond_t));
#endif

#ifdef	SUN_THREADS
	rval = cond_init(cvp, USYNC_THREAD, 0);
#endif

	return (rval);
}


int
cv_wait(cond_t *cvp, mutex_t *mp)
{
	int	rval = 0;

#ifdef SUN_LWPS
	int	err;

	rval = lwp_cv_wait(cvp, mp, 0, &err);
#endif

#ifdef SUN_THREADS
	rval = cond_wait(cvp, mp);
#endif

	return (rval);
}


int
cv_broadcast(cond_t *cvp)
{
	int	rval = 0;

#ifdef SUN_LWPS
	int	err;

	rval = lwp_cv_broadcast(cvp, &err);
#endif

#ifdef SUN_THREADS
	rval = cond_broadcast(cvp);
#endif

	return (rval);
}
