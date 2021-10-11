/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_THREAD_H
#define	_THREAD_H

#pragma ident	"@(#)thread.h	1.40	95/08/15 SMI"

/*
 * thread.h:
 * definitions needed to use the thread interface except synchronization.
 * use <synch.h> for thread synchronization.
 */

#ifndef _ASM
#include <sys/signal.h>
#include <sys/time.h>
#include <synch.h>
#endif	/* _ASM */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASM
typedef unsigned int thread_t;
typedef unsigned int thread_key_t;
#endif /* _ASM */

#ifndef _ASM
#ifdef __STDC__

int	thr_create(void *, size_t, void *(*)(void *), void *, long, thread_t *);
int	thr_join(thread_t, thread_t *, void **);
int	thr_setconcurrency(int);
int	thr_getconcurrency(void);
void	thr_exit(void *);
thread_t	thr_self();

/*
 * the definition of thr_sigsetmask() is not strict ansi-c since sigset_t is
 * not in the strict ansi-c name space. Hence, include the prototype for
 * thr_sigsetmask() only if strict ansi-c conformance is not turned on.
 */
#if (__STDC__ - 0 == 0) || defined(__EXTENSIONS__)
int		thr_sigsetmask(int, const sigset_t *, sigset_t *);
#endif

/*
 * the definition of thr_stksegment() is not strict ansi-c since stack_t is
 * not in the strict ansi-c name space. Hence, include the prototype for
 * thr_stksegment() only if strict ansi-c conformance is not turned on.
 */
#if (__STDC__ - 0 == 0) || defined(__EXTENSIONS__)
int		thr_stksegment(stack_t *);
#endif

int		thr_main(void);
int		thr_kill(thread_t, int);
int		thr_suspend(thread_t);
int		thr_continue(thread_t);
void		thr_yield(void);
int		thr_setprio(thread_t, int);
int		thr_getprio(thread_t, int *);
int		thr_keycreate(thread_key_t *, void(*)(void *));
int		thr_setspecific(thread_key_t, void *);
int		thr_getspecific(thread_key_t, void **);
size_t		thr_min_stack(void);

#else /* __STDC */

int	thr_create();
int	thr_join();
int	thr_setconcurrency();
int	thr_getconcurrency();
void	thr_exit();
thread_t	thr_self();
int	thr_sigsetmask();
int	thr_stksegment();
int	thr_main();
int	thr_kill();
int	thr_suspend();
int	thr_continue();
void	thr_yield();
int	thr_setprio();
int	thr_getprio();
int	thr_keycreate();
int	thr_setspecific();
int	thr_getspecific();
size_t	thr_min_stack();

#endif /* __STDC */
#endif /* _ASM */

#define	THR_MIN_STACK	thr_min_stack()
/*
 * thread flags (one word bit mask)
 */
/*
 * POSIX.1c Note:
 * THR_BOUND is defined same as PTHREAD_SCOPE_SYSTEM in <pthread.h>
 * THR_DETACHED is defined same as PTHREAD_CREATE_DETACHED in <pthread.h>
 * Any changes in these definitions should be reflected in <pthread.h>
 */
#define	THR_BOUND		0x00000001	/* = PTHREAD_SCOPE_SYSTEM */
#define	THR_NEW_LWP		0x00000002
#define	THR_DETACHED		0x00000040	/* = PTHREAD_CREATE_DETACHED */
#define	THR_SUSPENDED		0x00000080
#define	THR_DAEMON		0x00000100

#ifdef __cplusplus
}
#endif

#endif	/* _THREAD_H */
