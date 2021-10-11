/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)rwlock.c	1.31	95/08/25	SMI"

/*
 * synchronization primitives for threads.
 */

#ifdef __STDC__
#pragma weak rwlock_init = _rwlock_init
#pragma weak rwlock_destroy = _rwlock_destroy
#pragma weak rw_rdlock = _rw_rdlock
#pragma weak rw_wrlock = _rw_wrlock
#pragma weak rw_unlock = _rw_unlock
#pragma weak rw_tryrdlock = _rw_tryrdlock
#pragma weak rw_trywrlock = _rw_trywrlock
#endif /* __STDC__ */


#include "libthread.h"

/*
 * The following functions are used to make the rwlocks fork1 safe.
 * The functions  are called from fork1() (see sys/common/fork1.c)
 */
void _rwlsub_lock(void);
void _rwlsub_unlock(void);

typedef struct rwltab {
	int used;	 /* is this lock used? */
	mutex_t    lock; /* lock used for rwlock which hashes here */
} rwltab_t;

/* 
 * All rwlock table size (16) This number can be made
 * larger to increase the number of buckets and increase the
 * concurrency through the rwlock interface.
 */
#define ALLRWL_TBLSIZ 16

/* The rwlock HASH bucket (static to init the locks! and hide it)*/
static rwltab_t _allrwlocks[ALLRWL_TBLSIZ];

#define HASH_RWL(rwlp) ((unsigned int)(rwlp) % ALLRWL_TBLSIZ) 

#if defined(UTRACE) || defined(ITRACE)
#define	TRACE_RW_NAME(x) (((x)->rcv.type & TRACE_TYPE) ? (x)->name : "<noname>")
#include <string.h>
#endif

/*
 * Check if a reader version of the lock is held.
 */
_rw_read_held(rwlock_t *rwlp)
{
	return (rwlp->readers > 0);
}

/*
 * Check if a writer version of the lock is held.
 */
_rw_write_held(rwlock_t *rwlp)
{
	return (rwlp->readers == -1);
}

int
_rwlock_init(rwlock_t *rwlp, int type, void *arg)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	_mutex_init(rwlock, type, arg);
	_cond_init(readers, type, arg);
	_cond_init(writers, type, arg);
	rwlp->type = type;
	rwlp->readers = 0;
	return (0);
}

int
_rwlock_destroy(rwlock_t *rwlp)
{
	return (0);
}

int
_rw_rdlock(rwlock_t *rwlp)
{
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	register rwltab_t *tabp;

       /* 
	* for fork1() safety, for process local rw locks, acquire a lock from
	* a static table, instead of a lock embedded in the rwlock structure.
	*/
	if (rwlp->type == USYNC_THREAD) {
		tabp = &(_allrwlocks[HASH_RWL(rwlp)]);
		tabp->used = 1;
		rwlock = (mutex_t *)(&tabp->lock);
	}
	_mutex_lock(rwlock);
	while ((rwlp->readers == -1) || writers->cond_waiters)
		_cond_wait(readers, rwlock);
	rwlp->readers++;
	ASSERT(rwlp->readers > 0);
	_mutex_unlock(rwlock);
	return (0);
}

int
_lrw_rdlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	_sigoff();
	_lwp_mutex_lock(rwlock);
	while ((rwlp->readers == -1) || writers->cond_waiters)
		__lwp_cond_wait(readers, rwlock);
	rwlp->readers++;
	ASSERT(rwlp->readers > 0);
	_lwp_mutex_unlock(rwlock);
	return (0);
}

int
_rw_wrlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);
	register rwltab_t *tabp;

	if (rwlp->type == USYNC_THREAD) {
		tabp = &(_allrwlocks[HASH_RWL(rwlp)]);
		tabp->used = 1;
		rwlock = (mutex_t *)(&tabp->lock);
	}
	_mutex_lock(rwlock);
	if (writers->cond_waiters) {
		/* This ensures FIFO scheduling of write requests.  */
		_cond_wait(writers, rwlock);
		/* Fall through to the while loop below */
	}
	while (rwlp->readers > 0 || rwlp->readers == -1)
		_cond_wait(writers, rwlock);
	rwlp->readers = -1;
	_mutex_unlock(rwlock);
	return (0);
}

int
_lrw_wrlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	_sigoff();
	_lwp_mutex_lock(rwlock);
	if (writers->cond_waiters) {
		/* This ensures FIFO scheduling of write requests.  */
		__lwp_cond_wait(writers, rwlock);
		/* Fall through to the while loop below */
	}
	while (rwlp->readers > 0 || rwlp->readers == -1)
		__lwp_cond_wait(writers, rwlock);
	rwlp->readers = -1;
	return (0);
}

int
_rw_unlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);
	register rwltab_t *tabp;

	if (rwlp->type == USYNC_THREAD) {
		tabp = &(_allrwlocks[HASH_RWL(rwlp)]);
		tabp->used = 1;
		rwlock = (mutex_t *)(&tabp->lock);
	}
	_mutex_lock(rwlock);
	ASSERT(rwlp->readers == -1 || rwlp->readers > 0);
	if (rwlp->readers == -1) { /* if this is a write lock */
		if (writers->cond_waiters) {
			_cond_signal(writers);
		} else if (readers->cond_waiters)
			_cond_broadcast(readers);
		rwlp->readers = 0;
	} else {
		rwlp->readers--;
		ASSERT(rwlp->readers >= 0);
		if (!rwlp->readers && writers->cond_waiters) {
			/* signal a blocked writer */
			_cond_signal(writers);
		}
	}
	_mutex_unlock(rwlock);
	return (0);
}

int
_lrw_unlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	if (rwlp->readers != -1)
		_lwp_mutex_lock(rwlock);
	ASSERT(rwlp->readers == -1 || rwlp->readers > 0);
	if (rwlp->readers == -1) { /* if this is a write lock */
		if (writers->cond_waiters) {
			_lwp_cond_signal(writers);
		} else if (readers->cond_waiters)
			_lwp_cond_broadcast(readers);
		rwlp->readers = 0;
	} else {
		rwlp->readers--;
		ASSERT(rwlp->readers >= 0);
		if (!rwlp->readers && writers->cond_waiters) {
			/* signal a blocked writer */
			_lwp_cond_signal(writers);
		}
	}
	_lwp_mutex_unlock(rwlock);
	_sigon();
	return (0);
}

int
_rw_tryrdlock(rwlock_t *rwlp)
{
	int retval = 0;
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);
	register rwltab_t *tabp;

	if (rwlp->type == USYNC_THREAD) {
		tabp = &(_allrwlocks[HASH_RWL(rwlp)]);
		tabp->used = 1;
		rwlock = (mutex_t *)(&tabp->lock);
	}
	_mutex_lock(rwlock);
	if (rwlp->readers == -1 || writers->cond_waiters)
		retval = EBUSY;
	else {
		rwlp->readers++;
		retval = 0;
	}
	_mutex_unlock(rwlock);
	return (retval);
}

int
_rw_trywrlock(rwlock_t *rwlp)
{
	int retval = 0;
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);
	register rwltab_t *tabp;

	if (rwlp->type == USYNC_THREAD) {
		tabp = &(_allrwlocks[HASH_RWL(rwlp)]);
		tabp->used = 1;
		rwlock = (mutex_t *)(&tabp->lock);
	}
	_mutex_lock(rwlock);
	if (rwlp->readers > 0 || rwlp->readers == -1) {
		retval = EBUSY;
	} else {
		rwlp->readers = -1;
		retval = 0;
	}
	_mutex_unlock(rwlock);
	return (retval);
}

/* 
 * The _rwlsub_[un]lock() functions are called by the thread calling
 * fork1(). These functions acquire/release all the static locks which
 * correspond to the USYNC_THREAD rwlocks. Needs to be as fast as
 * possible - should not heavily penalize programs which do not use
 * rwlocks.
 */
void
_rwlsub_lock(void)
{
	register rwltab_t *tabp;
	register rwltab_t *tabp_end;

	tabp = &(_allrwlocks[0]);
	tabp_end = &(_allrwlocks[ALLRWL_TBLSIZ-1]);
	for (; tabp <= tabp_end; tabp++)
		if (tabp->used != 0)
			_mutex_lock(&(tabp->lock));
}

void 
_rwlsub_unlock(void)
{
	register rwltab_t *tabp;
	register rwltab_t *tabp_end;

	tabp = &(_allrwlocks[0]);
	tabp_end = &(_allrwlocks[ALLRWL_TBLSIZ-1]);
	for (; tabp <= tabp_end; tabp++) {
		ASSERT(tabp->used == 0 || MUTEX_HELD(tabp->lock));
		if (tabp->used != 0)
			_mutex_unlock(&(tabp->lock));
	}
}

#if	defined (ITRACE) || defined (UTRACE)

int
trace_rw_rdlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	trace_mutex_lock(rwlock);
	while (rwlp->readers == -1 || writers->cond_waiters)
		trace_cond_wait(readers, rwlock);
	rwlp->readers++;
	trace_mutex_unlock(rwlock);
	return (0);
}

int
trace_rw_wrlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	trace_mutex_lock(rwlock);
	if (writers->cond_waiters) {
		/* This ensures FIFO scheduling of write requests.  */
		trace_cond_wait(writers, rwlock);
		/* Fall through to the while loop below */
	}
	while (rwlp->readers > 0 || rwlp->readers == -1)
		trace_cond_wait(writers, rwlock);
	rwlp->readers = -1;
	trace_mutex_unlock(rwlock);
	return (0);
}

int
trace_rw_unlock(rwlock_t *rwlp)
{
	mutex_t *rwlock = (mutex_t *)(rwlp->pad1);
	cond_t *readers = (cond_t *)(rwlp->pad2);
	cond_t *writers = (cond_t *)(rwlp->pad3);

	trace_mutex_lock(rwlock);
	ASSERT(rwlp->readers == -1 || rwlp->readers > 0);
	if (rwlp->readers == -1) { /* if this is a write lock */
		if (writers->cond_waiters) {
			trace_cond_signal(&rwlp->wcv);
		} else if (readers->cond_waiters)
			trace_cond_broadcast(readers);
		rwlp->writer = 0;
	} else {
		rwlp->readers--;
		if (!(rwlp->readers) && writers->cond_waiters) {
			/* signal a blocked writer */
			trace_cond_signal(writers);
		}
	}
	trace_mutex_unlock(rwlock);
	return (0);
}

#endif
