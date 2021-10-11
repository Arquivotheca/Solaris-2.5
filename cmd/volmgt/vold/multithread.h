/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */


#ifndef _MULTITHREAD_H
#define	_MULTITHREAD_H

#pragma ident	"@(#)multithread.h	1.5	94/08/26 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


#include	<sys/types.h>

#ifdef SUN_THREADS
#include	<thread.h>
#include	<synch.h>
#endif

#ifdef SUN_LWPS
#include	"lwp/lwp.h"
#endif

#ifdef NO_MT
typedef unsigned int mutex_t;
#endif

#ifndef NO_MT
#define	MT
#endif

int	mutex_setup(mutex_t *mp);
int	mutex_enter(mutex_t *mp);
int	mutex_tryenter(mutex_t *mp);
int	mutex_exit(mutex_t *mp);

int	thread_create(void *stk, size_t ssize,
		void *(*func)(void *), void *arg);
int	thread_join(int tid);
void	thread_exit(void);
int	thread_self(void);
int	thread_kill(int tid, int sig);

int	cv_setup(cond_t *);
int	cv_wait(cond_t *, mutex_t *);
int	cv_broadcast(cond_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _MULTITHREAD_H */
