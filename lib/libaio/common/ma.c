/*
 *	Copyright (c) 1992, Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)ma.c	1.4	95/01/23	SMI"


#include	<sys/mman.h>
#include	<sys/param.h>
#include	<sys/lwp.h>
#include	<synch.h>
#include	<fcntl.h>
#include	<sys/debug.h>


int	_aio_alloc_stack(int, caddr_t *);
void	_aio_free_stack(int, caddr_t);
void	_aio_free_stack_unlocked(int, caddr_t);
static	int	_aio_alloc_chunk(caddr_t, int, caddr_t *);
static	void	_aio_free_chunk(caddr_t, int);

int devzero = 0;

static int DEFAULTSTACKINCR = 0;
static int DEFAULTSTACK = 0;
static int MAXSTACKS = 0;
static int stack_init = 0;

static struct stkcache {
	int size;
	char *next;
} _defaultstkcache;
static lwp_mutex_t _stkcachelock;

__init_stacks(stksz, ncached_stks)
	int stksz;
	int ncached_stks;
{
	int stkincr;

	DEFAULTSTACK = stksz;
	MAXSTACKS = ncached_stks;
	DEFAULTSTACKINCR = ((stkincr = ncached_stks/16)) ? stkincr : 2;
	_aio_alloc_stack(stksz, NULL);
}

/*
 * allocate a stack with redzone. stacks of default size are
 * cached and allocated in increments greater than 1.
 */
int
_aio_alloc_stack(size, sp)
	int size;
	caddr_t *sp;
{
	register int i, j;
	caddr_t addr;
	int err;

	ASSERT(size == DEFAULTSTACK);
	_lwp_mutex_lock(&_stkcachelock);
	if (_defaultstkcache.next == NULL) {
		/* add redzone */
		size += PAGESIZE;
		if (!_aio_alloc_chunk(0, DEFAULTSTACKINCR*size, &addr)) {
			_lwp_mutex_unlock(&_stkcachelock);
			return (0);
		}
		for (i = 0; i < DEFAULTSTACKINCR; i++) {
			/*
			 * invalidate the top stack page.
			 */
			if (mprotect(addr, PAGESIZE, PROT_NONE)) {
				_lwp_mutex_unlock(&_stkcachelock);
				perror("aio_alloc_stack: mprotect 1");
				return (0);
			}
			_aio_free_stack_unlocked(DEFAULTSTACK, addr + PAGESIZE);
			addr += (DEFAULTSTACK + PAGESIZE);
		}
	}
	if (sp) {
		*sp = _defaultstkcache.next;
		_defaultstkcache.next = (caddr_t)(**((long **)sp));
		_defaultstkcache.size -= 1;
	}
	_lwp_mutex_unlock(&_stkcachelock);
	return (1);
}

/*
 * free up stack space. stacks of default size are cached until some
 * high water mark and then they are also freed.
 */
void
_aio_free_stack_unlocked(size, addr)
	int size;
	caddr_t addr;
{
	int err;

	if (size == DEFAULTSTACK) {
		if (_defaultstkcache.size < MAXSTACKS) {
			*(long *)(addr) = (long)_defaultstkcache.next;
			_defaultstkcache.next = addr;
			_defaultstkcache.size += 1;
			return;
		}
	}
	/* include one page for redzone */
	if (munmap(addr - PAGESIZE, size + PAGESIZE)) {
		perror("aio_free_stack: munmap");
	}
}

void
_aio_free_stack(size, addr)
	int size;
	caddr_t addr;
{
	int err;

	if (size == DEFAULTSTACK) {
		_lwp_mutex_lock(&_stkcachelock);
		if (_defaultstkcache.size < MAXSTACKS) {
			*(long *)(addr) = (long)_defaultstkcache.next;
			_defaultstkcache.next = addr;
			_defaultstkcache.size += 1;
			_lwp_mutex_unlock(&_stkcachelock);
			return;
		}
		_lwp_mutex_unlock(&_stkcachelock);
	}
	/* include one page for redzone */
	if (munmap(addr - PAGESIZE, size + PAGESIZE)) {
		perror("aio_free_stack: munmap");
	}
}

static void
_destroy_defaultstk()
{
	caddr_t sp;

	while (_defaultstkcache.size > 0) {
		sp = _defaultstkcache.next;
		_defaultstkcache.next = (caddr_t)(*(long *)sp);
		_defaultstkcache.size -= 1;

		if (munmap(sp - PAGESIZE, DEFAULTSTACK + PAGESIZE)) {
			perror("aio_free_stack: munmap");
			_aiopanic("aio_free_stack");
		}
	}
}

/*
 * allocate a chunk of /dev/zero memory.
 */
static int
_aio_alloc_chunk(at, size, cp)
	caddr_t at;
	int size;
	caddr_t *cp;
{
	caddr_t p;

	if (devzero == 0) {
		if ((devzero = open("/dev/zero", O_RDWR)) == -1) {
			perror("open(/dev/zero)");
			_aiopanic("aio_alloc_chunk");
		}
	}
	if ((*cp = mmap(at, size, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_NORESERVE, devzero, 0)) ==
			(caddr_t) -1) {
		_aiopanic("aio_alloc_chunk: no mem");
	}
	return (1);
}

/*
 * free a chunk of allocated /dev/zero memory.
 */
static void
_aio_free_chunk(addr, size)
	caddr_t addr;
	int size;
{
	if (munmap(addr, size)) {
		perror("munmap");
		_aiopanic("munmap");
	}
}
