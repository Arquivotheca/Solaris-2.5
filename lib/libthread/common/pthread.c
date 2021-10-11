/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	" @(#)pthread.c 1.14 95/08/30 "

#ifdef __STDC__

#pragma weak	pthread_create		= _pthread_create
#pragma weak	pthread_join 		= _pthread_join
#pragma weak	pthread_once		= _pthread_once
#pragma weak	pthread_equal		= _pthread_equal
#pragma weak	pthread_setschedparam	= _pthread_setschedparam
#pragma weak	pthread_getschedparam	= _pthread_getschedparam
#pragma weak	pthread_getspecific	= _pthread_getspecific
#pragma weak	pthread_atfork		= _pthread_atfork

#endif /* __STDC__ */

#include "libpthr.h"
#include "libthread.h"
#include <sched.h>

/*
 * pthread_once related data
 * This structure is exported as pthread_once_t in pthread.h.
 * We export only the size of this structure. so check
 * pthread_once_t in pthread.h before making a change here.
 */
typedef struct  __once {
	mutex_t	mlock;
	union {
		uint8_t		pad8_flag[8];
		uint64_t	pad64_flag;
	} oflag;
} __once_t;

#define	once_flag	oflag.pad8_flag[7]

/*
 * pthread_atfork related data
 * This is internal structure and is used to store atfork handlers.
 */
typedef struct __atfork {
	struct	__atfork *fwd;		/* forward pointer */
	struct	__atfork *bckwd;	/* backward pointer */
	void (*prepare)(void);		/* pre-fork handler */
	void (*child)(void);		/* post-fork child handler */
	void (*parent)(void);		/* post-fork parent handler */
} __atfork_t;

static __atfork_t *_atforklist = NULL;	/* circular Q for fork handlers */
static	mutex_t	_atforklock;		/* protect the above Q */

static __atfork_t *_latforklist = NULL;	/* circular Q for rtld fork handlers */
static	mutex_t _latforklock;		/* protex the above Q */

/* default attribute object for pthread_create with NULL attr pointer */
static thrattr_t	_defattr = {NULL, 0, NULL, PTHREAD_CREATE_JOINABLE,
						PTHREAD_SCOPE_PROCESS};
/*
 * POSIX.1c
 * pthread_create: creates a thread in the current process.
 * calls common _thrp_create() after copying the attributes.
 */
int
_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
					void * (*start_routine)(void *),
					void *arg)
{
	long		flag;
	thrattr_t	*ap;
	int		ret;

	if (attr != NULL) {
		if ((ap = attr->pthread_attrp) == NULL)
			return (EINVAL);
	} else {
		ap = &_defattr;
	}

	flag = ap->scope | ap->detachstate;

	ret = _thrp_create(ap->stkaddr, ap->stksize, start_routine,
				arg, flag, (thread_t *) thread, ap->prio);
	if (ret == ENOMEM)
		/* posix version expects EAGAIN for lack of memory */
		return (EAGAIN);
	else
		return (ret);
}

/*
 * POSIX.1c
 * pthread_once: calls given function only once.
 * it synchronizes via mutex in pthread_once_t structure
 */
int
_pthread_once(pthread_once_t *once_control,
					void (*init_routine)(void))
{
	if (once_control == NULL || init_routine == NULL)
		return (EINVAL);

	_mutex_lock(&((__once_t *)once_control)->mlock);
	if (((__once_t *)once_control)->once_flag == PTHREAD_ONCE_NOTDONE) {

		pthread_cleanup_push(_mutex_unlock,
					&((__once_t *)once_control)->mlock);

		(*init_routine)();

		pthread_cleanup_pop(0);

		((__once_t *)once_control)->once_flag = PTHREAD_ONCE_DONE;
	}
	_mutex_unlock(&((__once_t *)once_control)->mlock);

	return (0);
}

/*
 * POSIX.1c
 * pthread_join: joins the terminated thread.
 * differs from Solaris thr_join(); it does return departed thread's
 * id and hence does not have "departed" argument.
 */
int
_pthread_join(pthread_t thread, void **status)
{
	if (thread == (pthread_t)0)
		return (ESRCH);
	else
		return (_thrp_join((thread_t) thread, NULL, status));
}

/*
 * POSIX.1c
 * pthread_equal: equates two thread ids.
 */
int
_pthread_equal(pthread_t t1, pthread_t t2)
{
	return (t1 == t2);
}

/*
 * POSIX.1c
 * pthread_getspecific: get the tsd vale for a specific key value.
 * It is same as thr_getspecific() except that tsd value is returned
 * by POSIX call whereas thr_* call pass it in an argument.
 */
void *
_pthread_getspecific(pthread_key_t key)
{
	void	*value;

	if (_thr_getspecific((thread_key_t) key, &value) < 0)
		return (NULL);
	else
		return (value);
}

/*
 * POSIX.1c
 * pthread_getschedparam: gets the sched parameters in a struct.
 * Currently, it returns only SCHED_OTHER policy and current
 * priority of target thread. In future, we will implement all
 * the parameters associated with other policies such as
 * SCHED_FIFO and SCHED_RR.
 */
int
_pthread_getschedparam(pthread_t thread, int *policy,
					struct sched_param *param)
{
	if (param != NULL && policy != NULL) {
		*policy = SCHED_OTHER;
		return (_thr_getprio((thread_t) thread,
						&param->sched_priority));
	} else {
		return (EINVAL);
	}

}

/*
 * POSIX.1c
 * pthread_setschedparam: sets the sched parameters for a thread.
 * Currently, it accepts only SCHED_OTHER policy and the
 * priority for target thread. In future, we will implement all
 * the parameters associated with other policies such as
 * SCHED_FIFO and SCHED_RR.
 */
int
_pthread_setschedparam(pthread_t thread, int policy,
					const struct sched_param *param)
{
	if (param != NULL) {
		if (policy == SCHED_OTHER) {
			return (_thr_setprio((thread_t) thread,
						param->sched_priority));
		} else if (policy == SCHED_FIFO || policy == SCHED_RR) {
			return (ENOTSUP);
		}
	}
	return (EINVAL);
}

/*
 * Routine to maintain the atfork queues.  This is called by both
 * _pthread_atfork() & _lpthread_atfork().
 */
static int
_atfork_append(void (*prepare) (void), void (*parent) (void),
		void (*child) (void), __atfork_t ** atfork_q,
		mutex_t *mlockp)
{
	__atfork_t *atfp;

	if ((atfp = (__atfork_t *)malloc(sizeof (__atfork_t))) == NULL) {
		return (ENOMEM);
	}
	atfp->prepare = prepare;
	atfp->child = child;
	atfp->parent = parent;

	_lmutex_lock(mlockp);
	if (*atfork_q == NULL) {
		*atfork_q = atfp;
		atfp->fwd = atfp->bckwd = atfp;
	} else {
		(*atfork_q)->bckwd->fwd = atfp;
		atfp->fwd = *atfork_q;
		atfp->bckwd = (*atfork_q)->bckwd;
		(*atfork_q)->bckwd = atfp;
	}
	_lmutex_unlock(mlockp);

	return (0);
}

/*
 * POSIX.1c
 * pthread_atfork: installs handlers to be called during fork().
 * It allocates memory off the heap and put the handler addresses
 * in circular Q. Once installed atfork handlers can not be removed.
 * There is no POSIX API which allows to "delete" atfork handlers.
 */
int
_pthread_atfork(void (*prepare) (void), void (*parent) (void),
	void (*child) (void))
{
	return (_atfork_append(prepare, parent, child,
	    &_atforklist, &_atforklock));
}

int
_lpthread_atfork(void (*prepare) (void), void (*parent) (void),
	void (*child) (void))
{
	return (_atfork_append(prepare, parent, child,
	    &_latforklist, &_latforklock));
}

static void
_run_prefork(__atfork_t *atfork_q, mutex_t *mlockp)
{
	__atfork_t *atfp, *last;

	_mutex_lock(mlockp);
	if (atfork_q != NULL) {
		atfp = last = atfork_q->bckwd;
		do {
			if (atfp->prepare) {
				pthread_cleanup_push(_mutex_unlock,
						(void *)mlockp);
				(*(atfp->prepare))();
				pthread_cleanup_pop(0);
			}
			atfp = atfp->bckwd;
		} while (atfp != last);
	}
	/* _?atforklock mutex is unlocked by _postfork_child/parent_handler */
}


/*
 * _prefork_handler is called by fork1() before it starts processing.
 * It acquires global atfork lock to protect the circular list.
 * It executes user installed "prepare" routines in LIFO order (POSIX)
 */
void
_prefork_handler()
{
	_run_prefork(_atforklist, &_atforklock);
}

void
_lprefork_handler()
{
	_run_prefork(_latforklist, &_latforklock);
}

static void
_run_postfork_parent(__atfork_t *atfork_q, mutex_t *mlockp)
{
	__atfork_t *atfp;

	/* _atforklock mutex is locked by _prefork_handler */
	ASSERT(MUTEX_HELD(&_atforklock));

	if (atfork_q != NULL) {
		atfp = atfork_q;
		do {
			if (atfp->parent) {
				pthread_cleanup_push(_mutex_unlock,
						(void *)mlockp);
				(*(atfp->parent))();
				pthread_cleanup_pop(0);
			}
			atfp = atfp->fwd;
		} while (atfp != atfork_q);
	}
	_mutex_unlock(mlockp);
}

/*
 * _postfork_parent_handler is called by fork1() after it finishes parent
 * processing. It acquires global atfork lock to protect the circular Q.
 * It executes user installed "parent" routines in FIFO order (POSIX).
 */
void
_postfork_parent_handler()
{
	_run_postfork_parent(_atforklist, &_atforklock);
}

void
_lpostfork_parent_handler()
{
	_run_postfork_parent(_latforklist, &_latforklock);
}

static void
_run_postfork_child(__atfork_t *atfork_q, mutex_t *mlockp)
{
	__atfork_t *atfp;

	/* _atforklock mutex is locked by _prefork_handler */
	ASSERT(MUTEX_HELD(&_atforklock));

	if (atfork_q != NULL) {
		atfp = atfork_q;
		do {
			if (atfp->child) {
				pthread_cleanup_push(_mutex_unlock,
						(void *)mlockp);
				(*(atfp->child))();
				pthread_cleanup_pop(0);
			}
			atfp = atfp->fwd;
		} while (atfp != atfork_q);
	}
	_mutex_unlock(mlockp);
}

/*
 * _postfork_child_handler is called by fork1() after it finishes child
 * processing. It acquires global atfork lock to protect the circular Q.
 * It executes user installed "child" routines in FIFO order (POSIX).
 */
void
_postfork_child_handler()
{
	_run_postfork_child(_atforklist, &_atforklock);
}

void
_lpostfork_child_handler()
{
	_run_postfork_child(_latforklist, &_latforklock);
}
