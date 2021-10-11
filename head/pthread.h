/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _PTHREAD_H
#define	_PTHREAD_H

#pragma ident	"@(#)pthread.h	1.13	95/08/24 SMI"

#ifndef	_ASM
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sched.h>
#endif	/* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

#ifndef	_UINT8_T
typedef	unsigned char	uint8_t;
#define	_UINT8_T
#endif
#ifndef	_UINT32_T
typedef	unsigned long	uint32_t;
#define	_UINT32_T
#endif
#ifndef	_UINT64_T
typedef	u_longlong_t	uint64_t;
#define	_UINT64_T
#endif
/*
 * POSIX definitions are same as defined in thread.h and synch.h.
 * They have been defined separately to avoid namespace pollution.
 * Any changes made to here should be reflected in corresponding
 * files as described in comments.
 */
typedef	unsigned int	pthread_t;	/* = thread_t in thread.h */
typedef	unsigned int	pthread_key_t;	/* = thread_key_t in thread.h */

typedef	struct	_pthread_mutex {		/* = mutex_t in synch.h */
	struct {
		uint8_t		pthread_mutex_flag[4];
		uint32_t 	pthread_mutex_type;
	} pthread_mutex_flags;
	union {
		struct {
			uint8_t	pthread_mutex_pad[8];
		} pthread_mutex_lock64;
		uint64_t pthread_mutex_owner64;
	} pthread_mutex_lock;
	uint64_t pthread_mutex_data;
} pthread_mutex_t;

typedef	struct	_pthread_cond {		/* = cond_t in synch.h */
	struct {
		uint8_t		pthread_cond_flag[4];
		uint32_t 	pthread_cond_type;
	} pthread_cond_flags;
	uint64_t pthread_cond_data;
} pthread_cond_t;

/*
 * attributes for threads, dynamically allocated by library
 */
typedef struct _pthread_attr {
	void	*pthread_attrp;
} pthread_attr_t;


/*
 * attributes for mutex, dynamically allocated by library
 */
typedef struct _pthread_mutexattr {
	void	*pthread_mutexattrp;
} pthread_mutexattr_t;


/*
 * attributes for cond, dynamically allocated by library
 */
typedef struct _pthread_condattr {
	void	*pthread_condattrp;
} pthread_condattr_t;

/*
 * pthread_once
 */
typedef	struct	_once {
	uint64_t	pthread_once_pad[4];
} pthread_once_t;

#endif	/* _ASM */

/*
 * Thread related attribute values defined as in thread.h.
 * These are defined as bit pattern in thread.h.
 * Any change here should be reflected in thread.h.
 */
/* detach */
#define	PTHREAD_CREATE_DETACHED		0x40	/* = THR_DETACHED */
#define	PTHREAD_CREATE_JOINABLE		0
/* scope */
#define	PTHREAD_SCOPE_SYSTEM		0x01	/* = THR_BOUND */
#define	PTHREAD_SCOPE_PROCESS		0

/*
 * Other attributes which are not defined in thread.h
 */
/* inherit */
#define	PTHREAD_INHERIT_SCHED		1
#define	PTHREAD_EXPLICIT_SCHED		0

/*
 * Value of process-shared attribute
 * These are defined as values defined in sys/synch.h
 * Any change here should be reflected in sys/synch.h.
 */
#define	PTHREAD_PROCESS_SHARED		1	/* = USYNC_PROCESS */
#define	PTHREAD_PROCESS_PRIVATE		0	/* = USYNC_THREAD */
#define	DEFAULT_TYPE			PTHREAD_PROCESS_PRIVATE

/*
 * macros - default initializers defined as in synch.h
 * Any change here should be reflected in synch.h.
 */
#define	PTHREAD_MUTEX_INITIALIZER	{0, 0, 0}	/* = DEFAULTMUTEX */

#define	PTHREAD_COND_INITIALIZER	{0, 0}		/* = DEFAULTCV */

/* cancellation type and state */
#define	PTHREAD_CANCEL_ENABLE		0x00
#define	PTHREAD_CANCEL_DISABLE		0x01
#define	PTHREAD_CANCEL_DEFERRED		0x00
#define	PTHREAD_CANCEL_ASYNCHRONOUS	0x02
#define	PTHREAD_CANCELED		-19

/* pthread_once related values */
#define	PTHREAD_ONCE_NOTDONE	0
#define	PTHREAD_ONCE_DONE	1
#define	PTHREAD_ONCE_INIT	{0, 0, 0, PTHREAD_ONCE_NOTDONE}

#ifndef	_ASM

/*
 * cancellation cleanup structure
 */
typedef struct _cleanup {
	uint32_t	pthread_cleanup_pad[4];
} _cleanup_t;

#ifdef	__STDC__

void	__pthread_cleanup_push(void (*routine)(void *), void *args,
					caddr_t fp, _cleanup_t *info);
void	__pthread_cleanup_pop(int ex, _cleanup_t *info);
caddr_t	_getfp(void);

#else	/* __STDC__ */

void	__pthread_cleanup_push();
void	__pthread_cleanup_pop();
caddr_t	_getfp();

#endif	/* __STDC__ */

#define	pthread_cleanup_push(routine, args) { \
	_cleanup_t _cleanup_info; \
	__pthread_cleanup_push((void (*)(void *))routine, (void *)args, \
				(caddr_t)_getfp(), &_cleanup_info);

#define	pthread_cleanup_pop(ex) \
	__pthread_cleanup_pop(ex, &_cleanup_info); \
}

#ifdef	__STDC__

/*
 * function prototypes - thread related calls
 */

int	pthread_attr_init(pthread_attr_t *attr);
int	pthread_attr_destroy(pthread_attr_t *attr);
int	pthread_attr_setstacksize(pthread_attr_t *attr,
					size_t stacksize);
int	pthread_attr_getstacksize(const pthread_attr_t *attr,
					size_t *stacksize);
int	pthread_attr_setstackaddr(pthread_attr_t *attr,
					void *stackaddr);
int	pthread_attr_getstackaddr(const pthread_attr_t *attr,
					void **stackaddr);
int	pthread_attr_setdetachstate(pthread_attr_t *attr,
					int detachstate);
int	pthread_attr_getdetachstate(const pthread_attr_t *attr,
					int *detachstate);
int	pthread_attr_setscope(pthread_attr_t *attr,
					int contentionscope);
int	pthread_attr_getscope(const pthread_attr_t *attr,
					int *scope);
int	pthread_attr_setinheritsched(pthread_attr_t *attr,
					int inherit);
int	pthread_attr_getinheritsched(const pthread_attr_t *attr,
					int *inheritsched);
int	pthread_attr_setschedpolicy(pthread_attr_t *attr,
					int policy);
int	pthread_attr_getschedpolicy(const pthread_attr_t *attr,
					int *policy);
int	pthread_attr_setschedparam(pthread_attr_t *attr,
					const struct sched_param *param);
int	pthread_attr_getschedparam(const pthread_attr_t *attr,
					struct sched_param *param);
int	pthread_create(pthread_t *thread, const pthread_attr_t *attr,
					void * (*start_routine)(void *),
					void *arg);
int	pthread_once(pthread_once_t *once_control,
					void (*init_routine)(void));
int	pthread_join(pthread_t thread, void **status);
int	pthread_detach(pthread_t thread);
void	pthread_exit(void *value_ptr);
int	pthread_kill(pthread_t thread, int sig);
int	pthread_cancel(pthread_t thread);
int	pthread_setschedparam(pthread_t thread, int policy,
					const struct sched_param *param);
int	pthread_getschedparam(pthread_t thread, int *policy,
					struct sched_param *param);
int	pthread_setcancelstate(int state, int *oldstate);
int	pthread_setcanceltype(int type, int *oldtype);
void	pthread_testcancel(void);
int	pthread_equal(pthread_t t1, pthread_t t2);
int	pthread_atfork(void (*prepare) (void), void (*parent) (void),
						void (*child) (void));
int	pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int	pthread_key_delete(pthread_key_t key);
int	pthread_setspecific(pthread_key_t key, const void *value);
void	*pthread_getspecific(pthread_key_t key);
pthread_t pthread_self(void);
int	pthread_sigmask(int how, const sigset_t *set, sigset_t *oset);

/*
 * function prototypes - synchronization related calls
 */
int	pthread_mutexattr_init(pthread_mutexattr_t *attr);
int	pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int	pthread_mutexattr_setpshared(pthread_mutexattr_t *attr,
					int pshared);
int	pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
					int *pshared);
int	pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr,
					int protocol);
int	pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
					int *protocol);
int	pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr,
					int prioceiling);
int	pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *attr,
					int *ceiling);
int	pthread_mutex_init(pthread_mutex_t *mutex,
					const pthread_mutexattr_t *attr);
int	pthread_mutex_destroy(pthread_mutex_t *mutex);
int	pthread_mutex_lock(pthread_mutex_t *mutex);
int	pthread_mutex_unlock(pthread_mutex_t *mutex);
int	pthread_mutex_trylock(pthread_mutex_t *mutex);
int	pthread_mutex_setprioceiling(pthread_mutex_t *mutex,
					int prioceiling, int *old_ceiling);
int	pthread_mutex_getprioceiling(pthread_mutex_t *mutex,
					int *ceiling);
int	pthread_condattr_init(pthread_condattr_t *attr);
int	pthread_condattr_destroy(pthread_condattr_t *attr);
int	pthread_condattr_setpshared(pthread_condattr_t *attr,
					int pshared);
int	pthread_condattr_getpshared(const pthread_condattr_t *attr,
					int *pshared);
int	pthread_cond_init(pthread_cond_t *cond,
					const pthread_condattr_t *attr);
int	pthread_cond_destroy(pthread_cond_t *cond);
int	pthread_cond_broadcast(pthread_cond_t *cond);
int	pthread_cond_signal(pthread_cond_t *cond);
int	pthread_cond_wait(pthread_cond_t *cond,
					pthread_mutex_t *mutex);
int	pthread_cond_timedwait(pthread_cond_t *cond,
					pthread_mutex_t *mutex,
					const struct timespec *abstime);

#else	/* __STDC__ */

/*
 * function prototypes - thread related calls
 */

int	pthread_attr_init();
int	pthread_attr_destroy();
int	pthread_attr_setstacksize();
int	pthread_attr_getstacksize();
int	pthread_attr_setstackaddr();
int	pthread_attr_getstackaddr();
int	pthread_attr_setdetachstate();
int	pthread_attr_getdetachstate();
int	pthread_attr_setscope();
int	pthread_attr_getscope();
int	pthread_attr_setinheritsched();
int	pthread_attr_getinheritsched();
int	pthread_attr_setschedpolicy();
int	pthread_attr_getschedpolicy();
int	pthread_attr_setschedparam();
int	pthread_attr_getschedparam();
int	pthread_create();
int	pthread_once();
int	pthread_join();
int	pthread_detach();
void	pthread_exit();
int	pthread_kill();
int	pthread_cancel();
int	pthread_setschedparam();
int	pthread_getschedparam();
int	pthread_setcancelstate();
int	pthread_setcanceltype();
void	pthread_testcancel();
int	pthread_equal();
int	pthread_atfork();
int	pthread_key_create();
int	pthread_key_delete();
int	pthread_setspecific();
void	*pthread_getspecific();
pthread_t pthread_self();
int	pthread_sigmask();
/*
 * function prototypes - synchronization related calls
 */
int	pthread_mutexattr_init();
int	pthread_mutexattr_destroy();
int	pthread_mutexattr_setpshared();
int	pthread_mutexattr_getpshared();
int	pthread_mutexattr_setprotocol();
int	pthread_mutexattr_getprotocol();
int	pthread_mutexattr_setprioceiling();
int	pthread_mutexattr_getprioceiling();
int	pthread_mutex_init();
int	pthread_mutex_destroy();
int	pthread_mutex_lock();
int	pthread_mutex_unlock();
int	pthread_mutex_trylock();
int	pthread_mutex_setprioceiling();
int	pthread_mutex_getprioceiling();
int	pthread_condattr_init();
int	pthread_condattr_destroy();
int	pthread_condattr_setpshared();
int	pthread_condattr_getpshared();
int	pthread_cond_init();
int	pthread_cond_destroy();
int	pthread_cond_broadcast();
int	pthread_cond_signal();
int	pthread_cond_wait();
int	pthread_cond_timedwait();

#endif	/* __STDC__ */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _PTHREAD_H */
