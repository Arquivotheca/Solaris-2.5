/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)newstubs.h 1.4     94/11/02 SMI"

/* vector table for libthread stubs */

/*
* _thr_stksegment() is a new function that should be added to this
* file if it is again compiled into libc.  lib/libc/${MACH}/Makefile
* currently has the file that includes this file commented out.
*/


#include <sys/errno.h>
#include <signal.h>
#include <synch.h>
#include <thread.h>


typedef struct {
	int	(*_cond_init)(cond_t *, int, void *);
	int	(*_cond_destroy)(cond_t *);
	int	(*_cond_wait)(cond_t *, mutex_t *);
	int	(*_cond_timedwait)(cond_t *, mutex_t *, timestruc_t *);
	int	(*_cond_signal)(cond_t *);
	int	(*_cond_broadcast)(cond_t *);

	int	(*_mutex_init)(mutex_t *, int, void *);
	int	(*_mutex_destroy)(mutex_t *);
	int	(*_mutex_lock)(mutex_t *);
	int	(*_mutex_trylock)(mutex_t *);
	int	(*_mutex_unlock)(mutex_t *);

	int	(*_rwlock_init)(rwlock_t *, int, void *);
	int	(*_rwlock_destroy)(rwlock_t *);
	int	(*_rw_rdlock)(rwlock_t *);
	int	(*_rw_wrlock)(rwlock_t *);
	int	(*_rw_unlock)(rwlock_t *);
	int	(*_rw_tryrdlock)(rwlock_t *);
	int	(*_rw_trywrlock)(rwlock_t *);

	int	(*_sema_init)(sema_t *, unsigned int, int, void *);
	int	(*_sema_destroy)(sema_t *);
	int	(*_sema_wait)(sema_t *);
	int	(*_sema_post)(sema_t *);
	int	(*_sema_trywait)(sema_t *);

	int	(*_thr_create)(void *, size_t, void *(*)(void *),
				void *, long, thread_t *);
	int	(*_thr_join)(thread_t, thread_t *, void **);
	int	(*_thr_setconcurrency)(int);
	int	(*_thr_getconcurrency)(void);
	int	(*_thr_exit)(void *);
	int	(*_thr_kill)(thread_t, int);
	int	(*_thr_suspend)(thread_t);
	int	(*_thr_continue)(thread_t);
	int	(*_thr_yield)(void);
	int	(*_thr_setprio)(thread_t, int);
	int	(*_thr_getprio)(thread_t, int *);
	int	(*_thr_min_stack)(void);
	int	(*_thr_keycreate)(thread_key_t *, void(*)(void *));
	int	(*_thr_setspecific)(thread_key_t, void *);
	int	(*_thr_getspecific)(thread_key_t, void **);
	int	(*_thr_sigsetmask)(int, sigset_t *, sigset_t *);
	int	(*_thr_self)(void);
	int	(*_thr_main)(void);

	int	(*__sema_held)(sema_t *);
	int	(*__rw_read_held)(rwlock_t *);
	int	(*__rw_write_held)(rwlock_t *);
	int	(*__mutex_held)(mutex_t *);
	int	*(*_thr_errnop)(void);
} call_vector;

typedef	void	(*PFrV)();

#define	DEFSTUB0(name) \
	int name(void) { \
	return ((cv->name)()); }

#define	DEFSTUB1(name, TYPE) \
	int name(TYPE x) { \
	return ((cv->name)(x)); }

#define	DEFSTUB2(name, TYPE1, TYPE2) \
	int name(TYPE1 x, TYPE2 y) \
	{ return ((cv->name)(x, y)); }

#define	DEFSTUB3(name, TYPE1, TYPE2, TYPE3) \
	int name(TYPE1 x, TYPE2 y, TYPE3 z) { \
		return ((cv->name)(x, y, z)); }

#define	DEFSTUB4(name, TYPE1, TYPE2, TYPE3, TYPE4) \
	int name(TYPE1 x, TYPE2 y, TYPE3 z, TYPE4 w) { \
		return ((cv->name)(x, y, z, w)); }
