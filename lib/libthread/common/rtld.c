/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)rtld.c	1.15	95/03/07	SMI"


/*
 * Inform the run-time linker of the address of any necessary mutex functions.
 *
 * In a multi-threaded dynamic application, the run-time linker must insure
 * concurrency when performing such things as function (.plt) binding, and
 * dlopen and dlclose functions.  To be consistent with the threads mechanisms,
 * (and as light weight as possible), the threads .init routine provides for
 * selected mutex function addresses to be passed to ld.so.1.
 */
#include "libthread.h"
#include <sys/link.h>

/*
 * Static functions
 */
static	int _llrw_rdlock();
static	int _llrw_wrlock();
static	int _llrw_unlock();
static	int _bind_guard(int);
static	int _bind_clear(int);

static Ld_concurrency funcs[LC_MAX] = {
	{ LC_RW_RDLOCK,		(int) _llrw_rdlock },
	{ LC_RW_WRLOCK,		(int) _llrw_wrlock },
	{ LC_RW_UNLOCK,		(int) _llrw_unlock },
	{ LC_BIND_GUARD,	(int) _bind_guard },
	{ LC_BIND_CLEAR,	(int) _bind_clear },
	{ LC_ATFORK,		(int) _lpthread_atfork },
	{ LC_THRSELF,		(int) _thr_self },
	{ LC_VERSION,		LC_V_CURRENT},
	{ LC_NULL,		0 }
};


static int
_llrw_rdlock(rwlock_t * _lrw_lock)
{
	return (_lrw_rdlock(_lrw_lock));
}

static int
_llrw_wrlock(rwlock_t * _lrw_lock)
{
	return (_lrw_wrlock(_lrw_lock));
}

static int
_llrw_unlock(rwlock_t * _lrw_lock)
{
	return (_lrw_unlock(_lrw_lock));
}

#ifdef BUILD_STATIC
#pragma	weak	_ld_concurrency
#endif

#pragma	init(set_ld_concurrency)

void
set_ld_concurrency()
{
	void (*	fptr)();

	if ((fptr = _ld_concurrency) != 0)
		(* fptr)(funcs);
}

#pragma	fini(unset_ld_concurrency)

void
unset_ld_concurrency()
{
	void (*	fptr)();

	if ((fptr = _ld_concurrency) != 0)
		(* fptr)((Ld_concurrency *)0);
}

/*
 * consolidation private between libthread and libc.
 * 
 * This initializes libc's global var __threaded and
 * causes libc locking to go "live" in stdio among
 * other places.
 */
#pragma init (_set_threaded)

extern void _libc_set_threaded();

void
_set_threaded()
{
	_libc_set_threaded();
}

/*
 * When ld.so.1 calls one of the mutex functions while binding a .plt its
 * possible that the function itself may require further .plt binding.  To
 * insure that this binding does not cause recursion we set the t_rtldbind
 * flags within the current thread.
 *
 * Note, because we may have .plt bindings occurring prior to %g7 being
 * initialized we must allow for _curthread() == 0.
 *
 * Args:
 *	bindflags:	value of flag(s) to be set in the t_rtldbind field
 *
 * Returns:
 *	0:	if setting the flag(s) results in no change to the t_rtldbind
 *		value.  ie: all the flags were already set.
 *	1:	if a flag(or flags) were not set they have been set.
 */
static int
_bind_guard(int bindflags)
{
	uthread_t 	*thr;
	extern int _nthreads;

	if (_nthreads == 0) /* .init processing - do not need to acquire lock */
		return (0);
	if ((thr = _curthread()) == NULL)
		/*
		 * thr == 0 implies the thread is a bound thread on its
		 * way to die in _lwp_terminate(), just before calling
		 * _lwp_exit(). Should acquire lock in this case - so return 1.
		 */
		return (1);
	else if ((thr->t_rtldbind & bindflags) == bindflags)
		return (0);
	else {
		thr->t_rtldbind |= bindflags;
		return (1);
	}
}

/*
 * Args:
 *	bindflags:	value of flags to be cleared from the t_rtldbind field
 * Returns:
 *	resulting value of t_rtldbind
 *
 * Note: if ld.so.1 needs to query t_rtldbind it can pass in a value
 *	 of '0' to clear_bind() and examine the return code.
 */
static int
_bind_clear(int bindflags)
{
	uthread_t *	thr;
	if (_nthreads > 0 && (thr = _curthread()) != NULL)
		return (thr->t_rtldbind &= ~bindflags);
	else
		return (0);
}
