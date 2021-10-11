/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)ma.c	1.22	95/08/16	SMI"


#include "libthread.h"
#include <sys/mman.h>
#include <fcntl.h>

/*
 * Global variables
 */
stkcache_t _defaultstkcache;
mutex_t _stkcachelock = DEFAULTMUTEX;
mutex_t _tsslock;

/*
 * Static variables
 */
static	caddr_t _tmpstkcache = 0;

/*
 * Static functions
 */
static	int _alloc_chunk(caddr_t, int, caddr_t *);
static	void _free_chunk(caddr_t, int);


/*
 * allocate a stack with redzone. stacks of default size are
 * cached and allocated in increments greater than 1.
 */
int
_alloc_stack(int size, caddr_t *sp)
{
	register int i, j;

	if (size == DEFAULTSTACK) {
		_lmutex_lock(&_stkcachelock);
		while (_defaultstkcache.next == NULL) {
			if (_defaultstkcache.busy) {
				_cond_wait(&_defaultstkcache.cv,
					   &_stkcachelock);
				continue;
			}
			_defaultstkcache.busy = 1;
			_lmutex_unlock(&_stkcachelock);
			ITRACE_0(UTR_FAC_TLIB_MISC, UTR_CACHE_MISS,
			    "thread stack cache miss");
			/* add redzone */
			size += _lpagesize;
			if (!_alloc_chunk(0, DEFAULTSTACKINCR*size, sp)) {
				/*
				 * This does not try to reduce demand and retry
				 * but just gets out cleanly - so that threads
				 * waiting for the busy bit are woken up.
				 * Should be fixed later to squeeze more memory
				 * out of what is left. XXX
				 */
				_lmutex_lock(&_stkcachelock);
			    	_defaultstkcache.busy = 0;
			    	_cond_broadcast(&_defaultstkcache.cv);
			    	_lmutex_unlock(&_stkcachelock);
			    	return (0);
			}
			for (i = 0; i < DEFAULTSTACKINCR; i++) {
				/*
				 * invalidate the top stack page.
				 */
				if (mprotect(*sp, _lpagesize, PROT_NONE)) {
					perror("alloc_stack: mprotect 1");
					for (; i < DEFAULTSTACKINCR; i++) {
						/*
						 * from wherever mprotect
						 * failed, free the rest of the 
						 * allocated chunk.
						 */
						 if (munmap(*sp,
						     DEFAULTSTACK+_lpagesize)) {
							perror("munmap");
							_panic("_alloc_stack");
						}
						*sp += (DEFAULTSTACK +
						    _lpagesize);
					
					}
					_lmutex_lock(&_stkcachelock);
					_defaultstkcache.busy = 0;
					_cond_broadcast(&_defaultstkcache.cv);
					_lmutex_unlock(&_stkcachelock);
					return (0);
				}
				_free_stack(*sp + _lpagesize, DEFAULTSTACK);
				*sp += (DEFAULTSTACK + _lpagesize);
			}
			_lmutex_lock(&_stkcachelock);
			_defaultstkcache.busy = 0;
			if (_defaultstkcache.next) {
				_cond_broadcast(&_defaultstkcache.cv);
				break;
			}
		}
#ifdef ITRACE
		else {
			ITRACE_0(UTR_FAC_TLIB_MISC, UTR_CACHE_HIT,
			    "thread stack cache hit");
		}
#endif
		ASSERT(_defaultstkcache.size > 0 &&
		    _defaultstkcache.next != NULL);
		*sp = _defaultstkcache.next;
		_defaultstkcache.next = (caddr_t)(**((long **)sp));
		_defaultstkcache.size -= 1;
		_lmutex_unlock(&_stkcachelock);
		return (1);
	} else {
		/* add redzone */
		size += _lpagesize;
		if (!_alloc_chunk(0, size, sp))
			return (0);
		/*
		 * invalidate the top stack page.
		 */
		if (mprotect(*sp, _lpagesize, PROT_NONE)) {
			perror("alloc_stack: mprotect 2");
			return (0);
		}
		*sp += _lpagesize;
		return (1);
	}
}

/*
 * free up stack space. stacks of default size are cached until some
 * high water mark and then they are also freed.
 */
void
_free_stack(caddr_t addr, int size)
{
	if (size == DEFAULTSTACK) {
		_lmutex_lock(&_stkcachelock);
		if (_defaultstkcache.size < MAXSTACKS) {
			*(long *)(addr) = (long)_defaultstkcache.next;
			_defaultstkcache.next = addr;
			_defaultstkcache.size += 1;
			_lmutex_unlock(&_stkcachelock);
			return;
		}
		_lmutex_unlock(&_stkcachelock);
	}
	/* include one page for redzone */
	if (munmap(addr - _lpagesize, size + _lpagesize)) {
		perror("free_stack: munmap");
		_panic("free_stack");
	}
}


#define	TMPSTKSIZE	256

int
_alloc_tmpstack(caddr_t *stk)
{
	caddr_t addr;
	caddr_t *p, np;
	int i;

	_lmutex_lock(&_tsslock);
	if (!_tmpstkcache) {
		if (!_alloc_chunk(0, _lpagesize, &addr)) {
			_lmutex_unlock(&_tsslock);
			return (0);
		}
		p = &_tmpstkcache;
		np = (caddr_t)addr;
		for (i = 0; i < (_lpagesize/TMPSTKSIZE) - 1; i++) {
			np += TMPSTKSIZE;
			*p = np;
			p = &np;
		}
		*p = NULL;
	}
	*stk = _tmpstkcache;
	_tmpstkcache = *(caddr_t *)_tmpstkcache;
	_lmutex_unlock(&_tsslock);
	return (1);
}

void
_free_tmpstack(caddr_t stk)
{
	_lmutex_lock(&_tsslock);
	*(caddr_t *)stk = _tmpstkcache;
	_tmpstkcache = stk;
	_lmutex_unlock(&_tsslock);
}

/*
 * allocate a chunk of /dev/zero memory.
 */
static int
_alloc_chunk(caddr_t at, int size, caddr_t *cp)
{
	caddr_t p;
	extern _first_thr_create;
	static int opened = 0;
	static int devzero;


	/*
	 * devzero could have 0 as file descriptor, that is why
	 * we have separate flag to indicate the opened file.
	 */
	if (opened == 0) {
		if ((devzero = _open("/dev/zero", O_RDWR)) == -1) {
			perror("open(/dev/zero)");
			_panic("alloc_chunk");
		}
		*cp = (caddr_t) _mmap(at, size,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_PRIVATE|MAP_NORESERVE, devzero, 0);
		if (_first_thr_create == 0)
			_close(devzero);
		else
			opened = 1;
	} else {
		*cp = (caddr_t) _mmap(at, size,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_PRIVATE|MAP_NORESERVE, devzero, 0);
	}
	if (*cp == (caddr_t) -1) {
		perror("_alloc_chunk(): _mmap failed");
		return (0);
	} else
		return (1);
}

/*
 * free a chunk of allocated /dev/zero memory.
 */
static void
_free_chunk(caddr_t addr, int size)
{
	if (munmap(addr, size)) {
		perror("munmap");
		_panic("munmap");
	}
}
