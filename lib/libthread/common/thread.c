/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)thread.c	1.146	95/10/06	SMI"

#include <stdio.h>
#include <ucontext.h>
#include <sys/signal.h>
#include <errno.h>
#include "libthread.h"

#ifdef __STDC__
#pragma weak thr_create = _thr_create
#pragma weak thr_join = _thr_join
#pragma weak thr_setconcurrency = _thr_setconcurrency
#pragma weak thr_getconcurrency = _thr_getconcurrency
#pragma weak thr_exit = _thr_exit
#pragma weak thr_self = _thr_self
#pragma weak thr_sigsetmask = _thr_sigsetmask
#pragma weak thr_kill = _thr_kill
#pragma weak thr_suspend = _thr_suspend
#pragma weak thr_continue = _thr_continue
#pragma weak thr_yield = _thr_yield
#pragma weak thr_setprio = _thr_setprio
#pragma weak thr_getprio = _thr_getprio
#pragma weak thr_min_stack = _thr_min_stack
#pragma weak thr_main = _thr_main
#pragma weak thr_stksegment = _thr_stksegment


#pragma weak pthread_exit = _pthread_exit
#pragma weak pthread_kill = _thr_kill
#pragma weak pthread_self = _thr_self
#pragma weak pthread_sigmask = _thr_sigsetmask
#pragma weak pthread_detach = _thr_detach
#pragma weak _pthread_kill = _thr_kill
#pragma weak _pthread_self = _thr_self
#pragma weak _pthread_sigmask = _thr_sigsetmask
#pragma weak _pthread_detach = _thr_detach
#pragma weak __pthread_min_stack = _thr_min_stack

#endif /* __STDC__ */

uthread_t _thread;
#ifdef TLS
#pragma unshared (_thread);
#endif

/*
 * Static functions
 */
static	int _thrp_kill(uthread_t *t, int ix, int sig);
static	int _thrp_suspend(uthread_t *t, thread_t tid,  int ix);
static	int _thrp_continue(uthread_t *t, int ix);
static	int _thrp_setprio(uthread_t *t, int newpri, int ix);

/*
 * Global variables
 */
thrtab_t _allthreads [ALLTHR_TBLSIZ];	/* hash table of all threads */
mutex_t _tidlock = DEFAULTMUTEX;	/* protects access to _lasttid */
thread_t _lasttid = 0;		/* monotonically increasing tid */
sigset_t _pmask;		/* virtual process signal mask */
mutex_t _pmasklock;		/* to protect _pmask updates */
int _first_thr_create = 0;	/* flag to indicate first thr_create call */
lwp_cond_t _suspended;		/* wait for thread to be suspended */


/*
 * A special cancelation cleanup hook for DCE.
 * _cleanuphndlr, when it is not NULL, will contain a callback
 * function to be called before a thread is terminated in _thr_exit()
 * as a result of being canceled.
 */
int _pthread_setcleanupinit(void (*func)(void));
static void (*_cleanuphndlr)(void) = NULL;

/*
 * _pthread_setcleanupinit: sets the cleanup hook.
 *
 * Returns:
 *      0 for success
 *      some errno on error
 */
int
_pthread_setcleanupinit(void (*func)(void))
{
	_cleanuphndlr = func;
	return (0);
}

/*
 * create a thread that begins executing func(arg). if stk is NULL
 * and stksize is zero, then allocate a default sized stack with a
 * redzone.
 */

int
_thr_create(void *stk, size_t stksize, void *(*func)(void *),
			void *arg, long flags, thread_t *new_thread)
{
	/* default priority which is same as parent thread */
	return (_thrp_create(stk, stksize, func, arg,
					flags, new_thread, 0));
}

int
_thrp_create(void *stk, size_t stksize, void *(*func)(), void *arg,
				long flags, thread_t *new_thread, int prio)
{
	register int tid;
	register thrtab_t *tabp;
	register uthread_t *first;
	uthread_t *t;
	int ret;

	TRACE_0(UTR_FAC_THREAD, UTR_THR_CREATE_START, "_thr_create start");
	/*
	TRACE_4(UTR_FAC_THREAD, UTR_THR_CREATE_START,
	    "thread_create start:func %x, flags %x, stk %x stksize %d",
	    (u_long)(func), flags, (u_long)stk, stksize);
	*/

	/*
	 * This flag will leave /dev/zero file opened while allocating
	 * the stack.
	 * This is a Q&D solution, ideally this problem should be solved
	 * as part of grand scheme where single threaded program linked
	 * with libthread should not have any overheads.
	 */
	_first_thr_create = 1;

	/* check for valid parameter combinations */
	if (stk && stksize < MINSTACK)
		return (EINVAL);
	if (stksize && stksize < MINSTACK)
		return (EINVAL);
	if (prio < 0)
		return (EINVAL);

	/* alloc thread local storage */
	if (!_alloc_thread(stk, stksize, &t))
		return (ENOMEM);
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_THC_STK,
				"in _thr_create after stack");
	if (prio)
		t->t_pri = prio;
	else
		t->t_pri = curthread->t_pri;

	t->t_hold = curthread->t_hold;

	ASSERT(!sigismember(&t->t_hold, SIGLWP));
	ASSERT(!sigismember(&t->t_hold, SIGCANCEL));

	/* XXX: if assertions are true, why do we need following 3 calls */

	sigdelset(&t->t_hold, SIGLWP);
	sigdelset(&t->t_hold, SIGCANCEL);

	t->t_usropts = flags;
	t->t_startpc = (int)func;
	if ((flags & (THR_BOUND | THR_SUSPENDED)) == THR_BOUND) {
		t->t_state = TS_RUN;
		t->t_stop = 0;
	} else {
		t->t_state = TS_STOPPED;
		t->t_stop = 1;
	}
	t->t_nosig = 0;
	/*
	 * t_nosig is initialized to    0 for bound threads.
	 * 				1 for unbound threads (see below).
	 * New unbound threads, before they hit _thread_start, always
	 * execute _resume_ret() (see _threadjmp() and _threadjmpsig()).
	 * _resume_ret() is also executed by threads which have _swtch()ed
	 * earlier and are now re-scheduled to resume inside _swtch().
	 * For such threads, _resume_ret() needs to call _sigon(),
	 * which decrements t_nosig to turn signals back on for such threads.
	 * So if a new thread's t_nosig is initialized to 1, by the time it
	 * hits the start_pc, it will have a 0 value in t_nosig since it
	 * would have executed _resume_ret().
	 */

	_lwp_sema_init(&t->t_park, NULL);
	_thread_call(t, (void (*)())func, arg);

	_sched_lock();
	_totalthreads++;
	if (!(flags & THR_DAEMON))
		_userthreads++;
	_sched_unlock();

	/*
	 * allocate tid and add thread to list of all threads.
	 */
	_lmutex_lock(&_tidlock);
	tid = t->t_tid = ++_lasttid;
	_lmutex_unlock(&_tidlock);

	tabp = &(_allthreads[HASH_TID(tid)]);
	_lmutex_lock(&(tabp->lock));
	if (tabp->first == NULL)
		tabp->first = t->t_next = t->t_prev = t;
	else {
		first = tabp->first;
		t->t_prev = first->t_prev;
		t->t_next = first;
		first->t_prev->t_next = t;
		first->t_prev = t;
	}
	_lmutex_unlock(&(tabp->lock));
	if ((flags & THR_BOUND)) {
		if (ret = _new_lwp(t, (void (*)())_thread_start)) {
			if ((t->t_flag & T_ALLOCSTK))
				_thread_free(t);
			return (ret);
		}
	} else {
		t->t_flag |= T_OFFPROC;
		t->t_nosig = 1; /* See Huge Comment above */
		_sched_lock();
		_nthreads++;
		_sched_unlock();
		ITRACE_0(UTR_FAC_TLIB_MISC, UTR_THC_CONT1,
		    "in _thr_create before cont");
		if ((flags & THR_SUSPENDED) == 0) {
			_thr_continue(tid);
		}
		ITRACE_0(UTR_FAC_TLIB_MISC, UTR_THC_CONT2,
		    "in _thr_create after cont");
	}
	if ((flags & THR_NEW_LWP))
		_new_lwp(NULL, _age);
	/*
	TRACE_5(UTR_FAC_THREAD, UTR_THR_CREATE_END,
	    "thread_create end:func %x, flags %x, stk %x stksize %d tid %x",
	    (u_long)(func), flags, (u_long)stk, stksize, tid);
	*/
	TRACE_1(UTR_FAC_THREAD, UTR_THR_CREATE_END,
	    "_thr_create end:id 0x%x", tid);
	if (!new_thread)
		return (0);
	else {
		*new_thread = tid;
		return (0);
	}
}

/*
 * define the level of concurrency for unbound threads. the thread library
 * will allocate up to "n" lwps for running unbound threads.
 */

int
_thr_setconcurrency(int n)
{
	uthread_t *t;
	int ret;

	TRACE_1(UTR_FAC_THREAD, UTR_THR_SETCONC_START,
	    "_thr_setconcurrency start:conc = %d", n);
	if (n < 0)
		return (EINVAL);
	if (n == 0)
		return (0);
	_sched_lock();
	if (n <= _nlwps) {
		_sched_unlock();
		TRACE_0(UTR_FAC_THREAD, UTR_THR_SETCONC_END,
		    "_thr_setconcurrency end");
		return (0);
	}
	n -= _nlwps;
	_sched_unlock();
	/* add more lwps to the pool */
	while ((n--) > 0) {
		if (ret = _new_lwp(NULL, _age)) {
			TRACE_0(UTR_FAC_THREAD, UTR_THR_SETCONC_END,
			    "thr_setconcurrency end");
			return (ret);
		}
	}
	TRACE_0(UTR_FAC_THREAD, UTR_THR_SETCONC_END, "thr_setconcurrency end");

	return (0);
}

/*
 * _thr_getconcurrency() reports back the size of the LWP pool.
 */
int
_thr_getconcurrency()
{
	TRACE_1(UTR_FAC_THREAD, UTR_THR_GETCONC_START,
	    "_thr_getconcurrency start:conc = %d", _nlwps);
	return (_nlwps);
}

/*
 * wait for a thread to exit.
 * If tid == 0, then wait for any thread.
 * If the threads library ever needs to call this function, it should call
 * _thr_join(), which should be defined just as __thr_continue() is.
 *
 * Note:
 *	a thread must never call resume() with the reaplock mutex held.
 *	this will lead to a deadlock if the thread is then resumed due
 *	to another thread exiting. The zombie thread is placed onto
 *	deathrow by the new thread, which happens to hold the reaplock.
 *
 *
 */
int
_thr_join(thread_t tid, thread_t *departed, void **status)
{
	int ret;

	ret = _thrp_join(tid, departed, status);
	if (ret == EINVAL)
		/* Solaris expects ESRCH for detached threads also */
		return (ESRCH);
	else
		return (ret);
}

int
_thrp_join(thread_t tid, thread_t *departed, void **status)
{
	uthread_t *t;
	int v, ix, xtid;

	TRACE_1(UTR_FAC_THREAD, UTR_THR_JOIN_START,
			"_thr_join start:tid %x", (u_long)tid);
	if (tid == NULL) {
		_reap_lock();
		while (_userthreads > 1 || _u2bzombies || _zombiecnt ||
		    _d2bzombies) {
			while (!_zombies)
				_reap_wait_cancel(&_zombied);
			if (_zombiecnt == 1) {
				t = _zombies;
				_zombies = NULL;
			} else {
				t = _zombies->t_ibackw;
				t->t_ibackw->t_iforw = _zombies;
				_zombies->t_ibackw = t->t_ibackw;
			}
			_zombiecnt--;
			if (t->t_flag & T_INTERNAL)
				continue;
			/*
			 * XXX: For now, leave the following out. It seems that
			 * DAEMON threads should be reclaimable. Of course,
			 * T_INTERNAL threads should not be, as the above
			 * ensures.
			 */
			/*
			if (t->t_usropts & THR_DAEMON)
				continue;
			*/
			xtid = t->t_tid;
			_reap_unlock();
			_lock_bucket((ix = HASH_TID(xtid)));
			if (t == THREAD(xtid))
				goto found;
			_unlock_bucket(ix);
			_reap_lock();
		}
		_reap_unlock();
		ASSERT(_userthreads == 1 && !_u2bzombies && !_zombiecnt &&
		    !_d2bzombies);
		return (EDEADLOCK);
	} else if (tid == curthread->t_tid) {
		return (EDEADLOCK);

	} else {
		_lock_bucket((ix = HASH_TID(tid)));
		if ((t = THREAD(tid)) == (uthread_t *)-1 ||
		    t->t_flag & T_INTERNAL) {
			_unlock_bucket(ix);
			return (ESRCH);
		}
		if (DETACHED(t)) {
			TRACE_1(UTR_FAC_THREAD, UTR_THR_JOIN_END,
			    "_thr_join end:tid %x", NULL);
			_unlock_bucket(ix);
			return (EINVAL);
		}
		_reap_lock();
		while (!(t->t_flag & T_ZOMBIE)) {
			_unlock_bucket(ix);
			_reap_wait_cancel(&_zombied);
			_reap_unlock();
			_lock_bucket(ix);
			if ((t = THREAD(tid)) == (uthread_t *) -1) {
				_unlock_bucket(ix);
				return (ESRCH);
			}
			if (DETACHED(t)) {
				_unlock_bucket(ix);
				return (EINVAL);
			}
			_reap_lock();
		}
		if (_zombiecnt == 1) {
			ASSERT(t == _zombies);
			_zombies = NULL;
		} else {
			t->t_ibackw->t_iforw = t->t_iforw;
			t->t_iforw->t_ibackw = t->t_ibackw;
			if (t == _zombies)
				_zombies = t->t_iforw;
		}
		_zombiecnt--;
		_reap_unlock();
	}
found:
	if (departed != NULL)
		*departed = t->t_tid;
	if (status != NULL)
		*status = t->t_exitstat;
	_thread_destroy(t, ix);
	_unlock_bucket(ix);
	TRACE_1(UTR_FAC_THREAD, UTR_THR_JOIN_END, "_thr_join end:tid %x",
	    (u_long)tid);
	return (0);
}

/*
 * POSIX function pthread_detach(). It is being called _thr_detach()
 * just to be compatible with rest of the functionality though there
 * is no thr_detach() API in Solaris.
 */

int
_thr_detach(thread_t tid)
{
	uthread_t *t;
	register int ix;

	TRACE_1(UTR_FAC_THREAD, UTR_THR_DETACH_START,
	    "_thr_detach start:tid 0x%x", (u_long)tid);
	if (tid == (thread_t)0)
		return (ESRCH);

	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1 ||
	    t->t_flag & T_INTERNAL) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	if (DETACHED(t)) {
		TRACE_1(UTR_FAC_THREAD, UTR_THR_DETACH_END,
		    "_thr_detach end:tid %x", NULL);
		_unlock_bucket(ix);
		return (EINVAL);
	}
	/*
	 * The target thread might have exited, but it could have
	 * made to the zombie list or not. If it does, then
	 * we will have to clean the thread as it is done in thr_join.
	 */
	_reap_lock();
	if (t->t_flag & T_ZOMBIE) {
		if (_zombiecnt == 1) {
			ASSERT(t == _zombies);
			_zombies = NULL;
		} else {
			t->t_ibackw->t_iforw = t->t_iforw;
			t->t_iforw->t_ibackw = t->t_ibackw;
			if (t == _zombies)
				_zombies = t->t_iforw;
		}
		_zombiecnt--;
		_reap_unlock();
		_thread_destroy(t, ix);
	} else {
		/*
		 * If it did not make it, then mark it THR_DETACHED so that
		 * when it dies _reapq_add will put it in deathrow queue to
		 * be reaped by reaper later.
		 */
		t->t_usropts |= THR_DETACHED;

		/*
		 * This thread might have just exited and is on the way
		 * to be added to deathrow (we have made it detached).
		 * Before we made it detached, thr_exit may have bumped up
		 * the d2/u2 counts since it was the non-detached thread.
		 * Undo it here, so that it looks as if the thread was
		 * "detached" when it was exited.
		 */
		if (t->t_flag & T_2BZOMBIE) {
			if (t->t_usropts & THR_DAEMON)
				_d2bzombies--;
			else
				_u2bzombies--;
		}

		/*
		 * Wake up threads trying to join target thread.
		 * This thread will not be available for joining.
		 * thr_join will check (after wake up) whether
		 * this thread is valid and non-detached.
		 */
		_cond_broadcast(&_zombied);
		_reap_unlock();
	}
	_unlock_bucket(ix);
	return (0);
}

thread_t
_thr_self()
{
	thread_t tid;

	TRACE_0(UTR_FAC_THREAD, UTR_THR_SELF_START, "_thr_self start");
	tid = curthread->t_tid;
	TRACE_0(UTR_FAC_THREAD, UTR_THR_SELF_END, "_thr_self end");
	return (tid);
}

/*
 * True if "new" unmasks any signal in "old" which is also blocked in "l"
 */
#define	unblocking(new, old, l) (\
	(~((new)->__sigbits[0]) & ((old)->__sigbits[0])\
	& ((l)->__sigbits[0])) || (~((new)->__sigbits[1])\
	& ((old)->__sigbits[1]) & ((l)->__sigbits[1])))

sigset_t __lwpdirsigs = {sigmask(SIGVTALRM)|sigmask(SIGPROF), 0, 0, 0};

int
_thr_sigsetmask(int how, const sigset_t *set, sigset_t *oset)
{
	register sigset_t *t_set;
	sigset_t ot_hold;
	sigset_t pending;
	sigset_t ppending;
	sigset_t sigs_ub;
	sigset_t resend;
	register uthread_t *t = curthread;
	extern void _deliversigs();
	int sig;

	TRACE_2(UTR_FAC_THREAD, UTR_THR_SIGSETMASK_START,
	    "_thr_sigsetmask start:how %d, set 0x%x", how,
	    set != NULL ? set->__sigbits[0] : 0);
	ot_hold = t->t_hold;
	if (oset != NULL)
		*oset = t->t_hold;
	t_set = &t->t_hold;
	if (set != NULL) {
		_sched_lock();
		ASSERT(t->t_flag & T_SIGWAIT ||
		    (t->t_flag & T_TEMPBOUND) || !sigand(&t->t_ssig, t_set));
		/*
		 * If the thread is calling thr_sigsetmask() in the code which
		 * sets up the call to the signal handler, the flag T_TEMPBOUND
		 * will be set. In this case, it is possible to have the mask
		 * block signals in t_ssig - the mask will be restored on
		 * return from the handler processing. Hence the above ASSERT
		 * should include this case.
		 */
		/*
		 * If this thread has been sent signals which are currently
		 * unblocked but are about to be blocked, deliver them here
		 * before blocking them.
		 * This is necessary for 2 reasons :
		 * a) Correct reception of signals unblocked at the time of a
		 *    _thr_kill() by the application(see _thr_exit()
		 *     which calls  _thr_sigsetmask() to block and thus
		 *     flush all such signals)
		 *    Basically, if _thr_kill() returns success, the
		 *    signal must be delivered to the target thread.
		 * b) Guarantee correct reception of a bounced signal.
		 *    The scenario:
		 *    Assume two threads, t1 and t2 in this process. t1 blocks
		 *    SIGFOO but t2 does not. t1's lwp might receive SIGFOO sent
		 *    to the process. t1 bounces the signal to t2 via a
		 *    _thr_kill(). If t2 now blocks SIGFOO using
		 *    _thr_sigsetmask()
		 *    This mask could percolate down to its lwp, resulting in
		 *    SIGFOO pending on t2's lwp, if received after this event.
		 *    If t2 never unblocks SIGFOO, an asynchronously generated
		 *    signal sent to the process is  thus lost.
		 */
		if (how == SIG_BLOCK || how == SIG_SETMASK) {
			while (!sigisempty(&t->t_ssig) &&
			    _blocksent(&t->t_ssig, t_set, (sigset_t *)set)) {
				_sched_unlock();
				_deliversigs(set);
				_sched_lock();
			}
			while ((t->t_flag & T_BSSIG) &&
			    _blocking(&t->t_bsig, t_set, (sigset_t *)set,
				&resend)) {
				sigemptyset(&t->t_bsig);
				t->t_bdirpend = 0;
				t->t_flag &= ~T_BSSIG;
				_sched_unlock();
				_bsigs(&resend);
				_sched_lock();
			}
		}
		switch (how) {
		case SIG_BLOCK:
			t_set->__sigbits[0] |= set->__sigbits[0];
			t_set->__sigbits[1] |= set->__sigbits[1];
			t_set->__sigbits[2] |= set->__sigbits[2];
			t_set->__sigbits[3] |= set->__sigbits[3];
			sigdiffset(t_set, &_cantmask);
			_sched_unlock_nosig();
			break;
		case SIG_UNBLOCK:
			t_set->__sigbits[0] &= ~set->__sigbits[0];
			t_set->__sigbits[1] &= ~set->__sigbits[1];
			t_set->__sigbits[2] &= ~set->__sigbits[2];
			t_set->__sigbits[3] &= ~set->__sigbits[3];
			break;
		case SIG_SETMASK:
			*t_set = *set;
			sigdiffset(t_set, &_cantmask);
			break;
		default:
			break;
		}
		if (how == SIG_UNBLOCK || how == SIG_SETMASK) {
			_sigmaskset(&t->t_psig, t_set, &pending);
			/*
			 * t->t_pending should only be set. it is
			 * possible for a signal to arrive after
			 * we've checked for pending signals. clearing
			 * t->t_pending should only be done when
			 * signals are really disabled like in
			 * _resetsig() or sigacthandler().
			 */
			if (!sigisempty(&pending))
				t->t_pending = 1;
			sigemptyset(&ppending);
			if (!(t->t_usropts & THR_DAEMON))
				_sigmaskset(&_pmask, t_set, &ppending);
			_sched_unlock_nosig();
			if (!sigisempty(&ppending)) {
				sigemptyset(&sigs_ub);
				_lwp_mutex_lock(&_pmasklock);
				_sigunblock(&_pmask, t_set, &sigs_ub);
				_lwp_mutex_unlock(&_pmasklock);
				if (!sigisempty(&sigs_ub)) {
					_sched_lock_nosig();
					sigorset(&t->t_bsig, &sigs_ub);
					t->t_bdirpend = 1;
					_sched_unlock_nosig();
				}
			}
		}
		/*
		 * Is this a  bound thread that uses calls resulting in LWP
		 * directed signals (indicated by the T_LWPDIRSIGS flag)? If so,
		 * is it changing such signals in its mask? If so, push down the
		 * mask, so these LWP directed signals are delivered in keeping
		 * with the state of the the thread's mask.
		 * Also, is this thread temporarily bound because it is running
		 * a user-installed signal handler? If so, its LWP mask and
		 * thread mask are identical and must be kept so while it is
		 * temporarily bound.
		 */
		if (((t->t_flag & T_LWPDIRSIGS) && ISBOUND(t) &&
		    (changesigs(&ot_hold, t_set, &__lwpdirsigs))) ||
		    ISTEMPBOUND(t))
			__sigprocmask(SIG_SETMASK, t_set, NULL);
		_sigon();
	}
	ASSERT(!sigismember(t_set, SIGLWP));
	ASSERT(!sigismember(t_set, SIGCANCEL));
	TRACE_0(UTR_FAC_THREAD, UTR_THR_SIGSETMASK_END,
	    "_thr_sigsetmask end");
	return (0);
}

/*
 * The only difference between the above routine and the following one is
 * that the following routine does not delete the _cantmask set from the
 * signals masked on the thread as a result of the call. So this routine is
 * usable only internally by libthread.
 */
int
__thr_sigsetmask(int how, const sigset_t *set, sigset_t *oset)
{
	register sigset_t *t_set;
	sigset_t ot_hold;
	sigset_t pending;
	sigset_t ppending;
	sigset_t sigs_ub;
	sigset_t resend;
	register uthread_t *t = curthread;
	extern void _deliversigs();
	int sig;

	TRACE_2(UTR_FAC_THREAD, UTR_THR_SIGSETMASK_START,
	    "__thr_sigsetmask start:how %d, set 0x%x", how,
	    set != NULL ? set->__sigbits[0] : 0);
	ot_hold = t->t_hold;
	if (oset != NULL)
		*oset = t->t_hold;
	t_set = &t->t_hold;
	if (set != NULL) {
		_sched_lock();
		ASSERT(t->t_flag & T_SIGWAIT ||
		    (t->t_flag & T_TEMPBOUND) || !sigand(&t->t_ssig, t_set));
		/*
		 * If the thread is calling thr_sigsetmask() in the code which
		 * sets up the call to the signal handler, the flag T_TEMPBOUND
		 * will be set. In this case, it is possible to have the mask
		 * block signals in t_ssig - the mask will be restored on
		 * return from the handler processing. Hence the above ASSERT
		 * should include this case.
		 */
		/*
		 * If this thread has been sent signals which are currently
		 * unblocked but are about to be blocked, deliver them here
		 * before blocking them.
		 * This is necessary for 2 reasons :
		 * a) Correct reception of signals unblocked at the time of a
		 *    _thr_kill() by the application(see _thr_exit()
		 *     which calls  _thr_sigsetmask() to block and thus
		 *     flush all such signals)
		 *    Basically, if _thr_kill() returns success, the
		 *    signal must be delivered to the target thread.
		 * b) Guarantee correct reception of a bounced signal.
		 *    The scenario:
		 *    Assume two threads, t1 and t2 in this process. t1 blocks
		 *    SIGFOO but t2 does not. t1's lwp might receive SIGFOO sent
		 *    to the process. t1 bounces the signal to t2 via a
		 *    _thr_kill(). If t2 now blocks SIGFOO using
		 *    _thr_sigsetmask()
		 *    This mask could percolate down to its lwp, resulting in
		 *    SIGFOO pending on t2's lwp, if received after this event.
		 *    If t2 never unblocks SIGFOO, an asynchronously generated
		 *    signal sent to the process is  thus lost.
		 */
		if (how == SIG_BLOCK || how == SIG_SETMASK) {
			while (!sigisempty(&t->t_ssig) &&
			    _blocksent(&t->t_ssig, t_set, (sigset_t *)set)) {
				_sched_unlock();
				_deliversigs(set);
				_sched_lock();
			}
			while ((t->t_flag & T_BSSIG) &&
			    _blocking(&t->t_bsig, t_set, (sigset_t *)set,
				&resend)) {
				sigemptyset(&t->t_bsig);
				t->t_bdirpend = 0;
				t->t_flag &= ~T_BSSIG;
				_sched_unlock();
				_bsigs(&resend);
				_sched_lock();
			}
		}
		switch (how) {
		case SIG_BLOCK:
			t_set->__sigbits[0] |= set->__sigbits[0];
			t_set->__sigbits[1] |= set->__sigbits[1];
			t_set->__sigbits[2] |= set->__sigbits[2];
			t_set->__sigbits[3] |= set->__sigbits[3];
			_sched_unlock_nosig();
			break;
		case SIG_UNBLOCK:
			t_set->__sigbits[0] &= ~set->__sigbits[0];
			t_set->__sigbits[1] &= ~set->__sigbits[1];
			t_set->__sigbits[2] &= ~set->__sigbits[2];
			t_set->__sigbits[3] &= ~set->__sigbits[3];
			break;
		case SIG_SETMASK:
			*t_set = *set;
			break;
		default:
			break;
		}
		if (how == SIG_UNBLOCK || how == SIG_SETMASK) {
			_sigmaskset(&t->t_psig, t_set, &pending);
			/*
			 * t->t_pending should only be set. it is
			 * possible for a signal to arrive after
			 * we've checked for pending signals. clearing
			 * t->t_pending should only be done when
			 * signals are really disabled like in
			 * _resetsig() or sigacthandler().
			 */
			if (!sigisempty(&pending))
				t->t_pending = 1;
			sigemptyset(&ppending);
			if (!(t->t_usropts & THR_DAEMON))
				_sigmaskset(&_pmask, t_set, &ppending);
			_sched_unlock_nosig();
			if (!sigisempty(&ppending)) {
				sigemptyset(&sigs_ub);
				_lwp_mutex_lock(&_pmasklock);
				_sigunblock(&_pmask, t_set, &sigs_ub);
				_lwp_mutex_unlock(&_pmasklock);
				if (!sigisempty(&sigs_ub)) {
					_sched_lock_nosig();
					sigorset(&t->t_bsig, &sigs_ub);
					t->t_bdirpend = 1;
					_sched_unlock_nosig();
				}
			}
		}
		/*
		 * Is this a  bound thread that uses calls resulting in LWP
		 * directed signals (indicated by the T_LWPDIRSIGS flag)? If so,
		 * is it changing such signals in its mask? If so, push down the
		 * mask, so these LWP directed signals are delivered in keeping
		 * with the state of the the thread's mask.
		 */
		if (((t->t_flag & T_LWPDIRSIGS) && ISBOUND(t) &&
		    (changesigs(&ot_hold, t_set, &__lwpdirsigs))) ||
		    ISTEMPBOUND(t))
			__sigprocmask(SIG_SETMASK, t_set, NULL);
		_sigon();
	}
	ASSERT(!sigismember(t_set, SIGLWP));
	ASSERT(!sigismember(t_set, SIGCANCEL));
	TRACE_0(UTR_FAC_THREAD, UTR_THR_SIGSETMASK_END,
	    "__thr_sigsetmask end");
	return (0);
}

int
_thr_kill(thread_t tid, int sig)
{
	uthread_t *t;
	register int ix;

	TRACE_2(UTR_FAC_THREAD, UTR_THR_KILL_START,
	    "_thr_kill start:tid 0x%x, sig %d", (u_long)tid, sig);
	if (sig >= NSIG || sig < 0 || sig == SIGWAITING ||
				sig == SIGCANCEL || sig == SIGLWP) {
		return (EINVAL);
	}
	if (tid == (thread_t)0)
		return (ESRCH);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1 ||
	    t->t_flag & T_INTERNAL) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	return (_thrp_kill(t, ix, sig));
}

/*
 * XXX- Not used
 * The following functions should be used internally by the threads library,
 * i.e. instead of _thr_kill().
 */
int
__thr_kill(thread_t tid, int sig)
{
	uthread_t *t;
	register int ix;

	TRACE_2(UTR_FAC_THREAD, UTR_THR_KILL_START,
	    "_thr_kill start:tid 0x%x, sig %d", (u_long)tid, sig);

	if (sig >= NSIG || sig < 0 || sig == SIGWAITING) {
		return (EINVAL);
	}
	if (tid == (thread_t)0)
		return (ESRCH);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	return (_thrp_kill(t, ix, sig));
}

static int
_thrp_kill(uthread_t *t, int ix, int sig)
{
	register int rc;
	lwpid_t lwpid;

	ASSERT(MUTEX_HELD(&(_allthreads[ix].lock)));

	if (sig == 0) {
		_unlock_bucket(ix);
		return (0);
	}
	_sched_lock();
	rc = _thrp_kill_unlocked(t, ix, sig, &lwpid);
	if (rc == 0 && lwpid != NULL) {
		/*
		 * XXX: If _lwp_kill() is called with _schedlock held, we *may*
		 * be able to do away with calling _thrp_kill_unlocked() with
		 * the lwpid pointer.
		 */
		rc = _lwp_kill(lwpid, sig);
		_sched_unlock();
		_unlock_bucket(ix);
	} else {
		_sched_unlock();
		_unlock_bucket(ix);
	}
	/*
	 * _thrp_kill_unlocked() called with _schedlock and
	 * _allthreads[ix].lock held.
	 * This is done because this function needs to be called from
	 * _sigredirect() with the 2 locks held.
	 */
	return (rc);
}

int
_thrp_kill_unlocked(uthread_t *t, int ix, int sig, lwpid_t *lwpidp)
{
	register int rc;

	ASSERT(MUTEX_HELD(&(_allthreads[ix].lock)));
	ASSERT(MUTEX_HELD(&_schedlock));

	*lwpidp = 0;

	if (t->t_state == TS_ZOMB) {
		TRACE_0(UTR_FAC_THREAD, UTR_THR_KILL_END,
		    "_thr_kill end:zombie");
		return (ESRCH);
	} else {
		if (sigismember(&t->t_psig, sig)) {
			TRACE_0(UTR_FAC_THREAD, UTR_THR_KILL_END,
			    "_thr_kill end:signal collapsed");
			return (0);
		}
		sigaddset(&t->t_psig, sig);
		t->t_pending = 1;
		if (sigismember(&t->t_hold, sig)) {
			TRACE_0(UTR_FAC_THREAD, UTR_THR_KILL_END,
			    "_thr_kill end:signal masked");
			return (0);
		}
		if ((t != curthread) && (ISBOUND(t) ||
		    (ONPROCQ(t) && t->t_state == TS_ONPROC))) {
			if (ISBOUND(t) && t->t_state == TS_SLEEP) {
				t->t_flag |= T_INTR;
				_unsleep(t);
				_setrq(t);
				if (t->t_flag & T_SIGWAIT)
				/*
				 * At this point, the target thread is
				 * revealed to be a bound thread, in sigwait(),
				 * with the signal unblocked (the above check
				 * against t_hold failed - that is why we are
				 * here). Now, sending it a signal via
				 * _lwp_kill() is a bug. So, just return. The
				 * target thread will wake-up and do the right
				 * thing in sigwait().
				 */
					return (0);
			}
			/*
			 * The following is so the !sigand(t_ssig, t_hold)
			 * assertion in _thr_sigsetmask() stays true for
			 * threads in sigwait.
			 */
			if ((t->t_flag & T_SIGWAIT) == 0) {
				sigaddset(&t->t_ssig, sig);
			}
			ASSERT(LWPID(t) != 0);
			*lwpidp = LWPID(t);
			return (0);
		} else if ((t->t_state == TS_SLEEP)) {
			t->t_flag |= T_INTR;
			_unsleep(t);
			_setrq(t);
		}
	}
	TRACE_0(UTR_FAC_THREAD, UTR_THR_KILL_END, "_thr_kill end");
	return (0);
}

/*
 * stop the specified thread.
 * Define a __thr_suspend() routine, similar to __thr_continue(),
 * if the threads library needs to call _thr_continue() internally.
 */

int
_thr_suspend(thread_t tid)
{
	uthread_t *t;
	register int ix;

	TRACE_1(UTR_FAC_THREAD, UTR_THR_SUSPEND_START,
	    "_thr_suspend start:tid 0x%x", tid);

	if (tid == (thread_t)0)
		return (ESRCH);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1 ||
	    t->t_flag & T_INTERNAL) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	return (_thrp_suspend(t, tid, ix));
}

static int
_thrp_suspend(uthread_t *t, thread_t tid,  int ix)
{
	int rc = 0;
	int ostate = curthread->t_state;

	ASSERT(MUTEX_HELD(&(_allthreads[ix].lock)));
	_sched_lock();
	if (STOPPED(t)) {
		_sched_unlock();
		_unlock_bucket(ix);
		TRACE_0(UTR_FAC_THREAD, UTR_THR_SUSPEND_END,
						"_thr_suspend end");
		return (rc);
		/* XXX: if t == curthread , _panic here ? */
	}
	if (_idtot(tid) == (uthread_t *)-1 || t->t_state == TS_ZOMB) {
		_sched_unlock();
		_unlock_bucket(ix);
		TRACE_0(UTR_FAC_THREAD, UTR_THR_SUSPEND_END,
						"_thr_suspend end");
		return (ESRCH);
	}

	t->t_stop = 1;
	if (t == curthread) {
		ASSERT(t->t_state == TS_ONPROC);
		t->t_state = TS_STOPPED;
		if (!ISBOUND(t) && ONPROCQ(t))
			_onproc_deq(t);
		_sched_unlock_nosig();
		_unlock_bucket(ix);
		_swtch(0);
		_sigon();
		TRACE_0(UTR_FAC_THREAD, UTR_THR_SUSPEND_END,
		    "_thr_suspend end");
		return (0);
	} else {
		if (t->t_state == TS_ONPROC) {
			if (!ISBOUND(t) && ONPROCQ(t))
				_onproc_deq(t);
			rc = _lwp_kill(LWPID(t), SIGLWP);
			/*
			 * Wait for thread to be suspended
			 */
			if (rc == 0) {
				thread_t tid;
				/* remember thread ID */
				tid = t->t_tid;

				/*
				 * Wait for thread to stop if it isn't
				 * stopped or sleeping already
				 */
				while (t->t_state != TS_STOPPED &&
					t->t_state != TS_SLEEP) {
					_lwp_cond_wait(&_suspended,
							&_schedlock);
					/*
					 * Check to see if target
					 * thread died while we were
					 * waiting for it to suspend.
					 */
					if (_idtot(tid) ==
					    (uthread_t *)-1 ||
					    t->t_state == TS_ZOMB) {
						rc = ESRCH;
						break;
					}
					/*
					 * If t->t_stop becomes 0, then
					 * thread_continue() must have
					 * been called and the suspend
					 * should be cancelled.
					 */
					if (t->t_stop == 0) {
						rc = ECANCELED;
						break;
					}
				}
			}
		}
	}
	_sched_unlock();
	_unlock_bucket(ix);
	TRACE_0(UTR_FAC_THREAD, UTR_THR_SUSPEND_END, "_thr_suspend end");
	return (rc);
}

/*
 * make the specified thread runnable
 */
int
_thr_continue(thread_t tid)
{
	uthread_t *t;
	register int ix;

	TRACE_1(UTR_FAC_THREAD, UTR_THR_CONTINUE_START,
	    "_thr_continue start:tid 0x%x", tid);
	if (tid == (thread_t)0)
		return (ESRCH);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1 ||
	    t->t_flag & T_INTERNAL) {
		_unlock_bucket(ix);
		return (ESRCH);
	} else if (t->t_state == TS_ZOMB) {	/* Thread is dying */
		if (DETACHED(t)) {
			/*
			 * Return ESRCH to tell user that thread is gone
			 * even though it may not have been reaped yet.
			 */
			_unlock_bucket(ix);
			return (ESRCH);
		} else {
			/*
			 * Return EINVAL to tell user that thread is dying,
			 * but not gone yet and must be joined with to have
			 * its resources reclaimed.
			 */
			_unlock_bucket(ix);
			return (EINVAL);
		}
	}

	return (_thrp_continue(t, ix));
}

/*
 * Call the following function (instead of _thr_continue) inside
 * the threads library.
 */
int
__thr_continue(thread_t tid)
{
	uthread_t *t;
	register int ix;

	TRACE_1(UTR_FAC_THREAD, UTR_THR_CONTINUE_START,
	    "_thr_continue start:tid 0x%x", tid);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	return (_thrp_continue(t, ix));
}

static int
_thrp_continue(uthread_t *t, int ix)
{
	int rc = 0;

	ASSERT(MUTEX_HELD(&(_allthreads[ix].lock)));
	_sched_lock();
	if (!STOPPED(t)) {
		_sched_unlock();
		_unlock_bucket(ix);
		TRACE_0(UTR_FAC_THREAD, UTR_THR_CONTINUE_END,
		    "_thr_continue end");
		return (0);
	}
/*
	ASSERT(OFFPROCQ(t));
*/
	t->t_stop = 0;
	if (t->t_state == TS_STOPPED) {
		if (ISBOUND(t) && !ISPARKED(t)) {
			t->t_state = TS_ONPROC;
			rc = _lwp_continue(LWPID(t));
		}
		else
			_setrq(t);
	} else if (t->t_state == TS_ONPROC) {
		/*
		 * The target thread could be in the window where the
		 * thr_suspend() on the target thread has occurred, and
		 * it has been taken off the onproc queue, and its underlying
		 * LWP has been sent a SIGLWP, but the thread has not yet
		 * received the signal. In this window, the thread has yet
		 * to change its state to TS_STOPPED, since that happens
		 * in the signal handler. So, all that we need to do here
		 * is put it back on the onproc queue, and rely on the
		 * _siglwp() handler which will eventually be invoked on the
		 * target thread, to notice that the t_stop bit has been tuned
		 * off and not do the stopping in _dopreempt().
		 */
		if (OFFPROCQ(t))
			_onproc_enq(t);
	}
	/*
	 * Broadcast to anyone waiting for this thread to be suspended
	 */
	_lwp_cond_broadcast(&_suspended);

	_sched_unlock();
	_unlock_bucket(ix);
	TRACE_0(UTR_FAC_THREAD, UTR_THR_CONTINUE_END, "_thr_continue end");
	return (rc);
}

/*
 * Define a __thr_setprio() function and call it internally (instead of
 * _thr_setprio()), similar to __thr_continue().
 */

int
_thr_setprio(thread_t tid, int newpri)
{
	uthread_t *t;
	register int ix;

	TRACE_2(UTR_FAC_THREAD, UTR_THR_SETPRIO_START,
	    "_thr_setprio start:tid 0x%x, newpri %d", tid, newpri);
	if (tid == (thread_t)0)
		return (ESRCH);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1 ||
	    t->t_flag & T_INTERNAL) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	return (_thrp_setprio(t, newpri, ix));
}

static int
_thrp_setprio(uthread_t *t, int newpri, int ix)
{
	int oldpri, qx;

	if (newpri < THREAD_MIN_PRIORITY || newpri > THREAD_MAX_PRIORITY) {
		_unlock_bucket(ix);
		return (EINVAL);
	}
	if (newpri == t->t_pri) {
		_unlock_bucket(ix);
		return (0);
	}
	if (ISBOUND(t)) {
		oldpri = t->t_pri;
		t->t_pri = newpri;
		_unlock_bucket(ix);
		TRACE_0(UTR_FAC_THREAD, UTR_THR_SETPRIO_END,
						"_thr_setprio end");
		return (0);
	}
	_sched_lock();
	if (t->t_state == TS_ONPROC || t->t_state == TS_DISP) {
		oldpri = t->t_pri;
		t->t_pri = newpri;
		if (newpri < oldpri) {	/* lowering thread's priority */
			if (_maxpriq > newpri)
				_preempt(t, t->t_pri);
		}
	} else {
		oldpri = t->t_pri;
		if (t->t_state == TS_RUN) {
			_dispdeq(t);
			t->t_pri = newpri;
			_setrq(t); /* will do preemption, if required */
		} else if (t->t_state == TS_SLEEP) {
			/*
			 * sleep queues should also be ordered by
			 * priority. changing the priority of a sleeping
			 * thread should also cause the thread to move
			 * on the sleepq like what's done for the runq.
			 * XXX
			 * The same code here for both bound/unbound threads
			 */
			t->t_pri = newpri;
		} else if (t->t_state == TS_STOPPED) {
			t->t_pri = newpri;
		}
	}
	_sched_unlock();
	_unlock_bucket(ix);
	TRACE_0(UTR_FAC_THREAD, UTR_THR_SETPRIO_END, "_thr_setprio end");
	return (0);
}

/*
 * If the threads library ever needs this function, define a
 * __thr_getprio(), just like __thr_continue().
 */
int
_thr_getprio(thread_t tid, int *pri)
{
	uthread_t *t;
	register int ix;

	TRACE_0(UTR_FAC_THREAD, UTR_THR_GETPRIO_START,
					"_thr_getprio start");
	if (tid == (thread_t)0)
		return (ESRCH);
	_lock_bucket((ix = HASH_TID(tid)));
	if ((t = THREAD(tid)) == (uthread_t *)-1 ||
	    t->t_flag & T_INTERNAL) {
		_unlock_bucket(ix);
		return (ESRCH);
	}
	*pri = t->t_pri;
	_unlock_bucket(ix);
	TRACE_0(UTR_FAC_THREAD, UTR_THR_GETPRIO_END, "_thr_getprio end");
	return (0);
}

/*
 * _thr_yield() causes the calling thread to be preempted.
 */
void
_thr_yield()
{
	TRACE_0(UTR_FAC_THREAD, UTR_THR_YIELD_START, "_thr_yield start");
	if (ISBOUND(curthread)) {
		_yield();
		TRACE_0(UTR_FAC_THREAD, UTR_THR_YIELD_END,
						"_thr_yield end");
	} else {
		/*
		 * If there are no threads on run-queue, yield
		 * processor.
		 */
		if (_nrunnable == 0) {
			_yield();
		} else {
			_sched_lock();
			if (ONPROCQ(curthread))
				_onproc_deq(curthread);
			_setrq(curthread);
			_sched_unlock_nosig();
			_swtch(0);
			_sigon();
			TRACE_0(UTR_FAC_THREAD, UTR_THR_YIELD_END,
			"_thr_yield end");
		}
	}
}

/*
 * thr_exit: thread is exiting without calling C++ destructors
 */
void
_thr_exit(void *status)
{
	_thr_exit_common(status, 0);
}

/*
 * pthread_exit: normal thr_exit + C++ desturctors need to be called
 */
void
_pthread_exit(void *status)
{
	_thr_exit_common(status, 1);
}

void
_thr_exit_common(void *status, int ex)
{
	sigset_t set;
	uthread_t *t;
	int cancelpending;

	t = curthread;
	TRACE_4(UTR_FAC_THREAD, UTR_THR_EXIT_START,
	    "_thr_exit start:usropts 0x%x, pc 0x%x, stk 0x%x, lwpid %d",
	    t->t_usropts, (u_long)t->t_pc, (u_long)t->t_stk, LWPID(t));

	cancelpending = t->t_can_pending;
	/*
	 * cancellation is disabled
	 */
	*((int *)&(t->t_can_pending)) = 0;
	t->t_can_state = TC_DISABLE;

	/*
	 * special DCE cancellation cleanup hook.
	 */
	if (_cleanuphndlr != NULL &&
	    cancelpending == TC_PENDING &&
	    (int) status == PTHREAD_CANCELED) {
		(*_cleanuphndlr)();
	}

	_lmutex_lock(&_calloutlock);
	if (t->t_itimer_callo.running)
		while (t->t_itimer_callo.running)
			_cond_wait(&t->t_itimer_callo.waiting, &_calloutlock);
	if (t->t_cv_callo.running)
		while (t->t_cv_callo.running)
			_cond_wait(&t->t_cv_callo.waiting,
							&_calloutlock);
	_lmutex_unlock(&_calloutlock);

	/* remove callout entry */
	_rmcallout(&curthread->t_cv_callo);
	_rmcallout(&curthread->t_itimer_callo);

	maskallsigs(&set);
	/*
	 * Flush all signals sent to this thread, including bounced signals.
	 * After this point, this thread will never be a sig bounce target
	 * even though its state may still be TS_ONPROC.
	 */
	_thr_sigsetmask(SIG_SETMASK, &set, NULL);

	/*
	 * If thr_exit() is called from a signal handler, indicated by the
	 * T_TEMPBOUND flag, clean out any residual state on the
	 * underlying LWP signal mask, so it is clean when it resumes
	 * another thread.
	 */
	if (t->t_flag & T_TEMPBOUND) {
		/*
		 * XXX: Cannot just unblock signals here on the LWP. Any
		 * pending signals on the LWP will then come up violating
		 * the invariant of not receiving signals which are blocked
		 * on the thread on the LWP that receives the signal. So
		 * the pending signal on this LWP will have to be handled
		 * differently: figure out how to do this. Some alternatives:
		 * 	- unblock all signals on thread and then block them
		 *		problem: this violates the calling thread's
		 *			 request to block these signals
		 *	- call sigpending() and then call __sigwait() on them
		 *		problem with this is that sigpending() returns
		 *		signals pending to the whole process, not just
		 *		those on the calling LWP.
		 */
		__sigprocmask(SIG_SETMASK, &_null_sigset, NULL);
	}

	t->t_exitstat = status;

	if (t->t_flag & T_INTERNAL)
		_thrp_exit();
		/*
		 * Does not return. Just call the real thr_exit() if this is
		 * an internal thread, such as the aging thread. Otherwise,
		 * call _tcancel_all() to unwind the stack, popping C++
		 * destructors and cancellation handlers.
		 */
	else {
		/*
		 * If thr_exit is being called from the places where
		 * C++ destructors are to be called such as cancellation
		 * points, then set this flag. It is checked in _t_cancel()
		 * to decide whether _ex_unwind() is to be called or not!
		 */
		if (ex)
			t->t_flag |= T_EXUNWIND;
		_tcancel_all(0);
	}
}

/*
 * An exiting thread becomes a zombie thread. If the thread was created
 * with the THREAD_WAIT flag then it is placed on deathrow waiting to be
 * reaped by a caller() of thread_wait(). otherwise the thread may be
 * freed more quickly.
 */
void
_thrp_exit()
{
	sigset_t set;
	uthread_t *t;
	int uexiting = 0;

	t = curthread;

	/*
	 * Destroy TSD after all signals are blocked. This is to prevent
	 * tsd references in signal handlers after the tsd has been
	 * destroyed.
	 */
	_destroy_tsd();

	/*
	 * Increment count of the non-detached threads which are
	 * available for joining. Mark the condition "on the way"
	 * to zombie queue. It is used by _thr_detach().
	 */
	_reap_lock();
	if (!(t->t_usropts & THR_DAEMON)) {
		if (!(t->t_usropts & THR_DETACHED)) {
			++_u2bzombies;
			t->t_flag |= T_2BZOMBIE;
		}
		uexiting = 1;
	} else if (!(t->t_usropts & THR_DETACHED)) {
		++_d2bzombies;
		t->t_flag |= T_2BZOMBIE;
	}
	_reap_unlock();

	_sched_lock();
	_totalthreads--;
	if (uexiting)
		--_userthreads;
	if (_userthreads == 0) {
		/* last user thread to exit, exit process. */
		_sched_unlock();
		TRACE_5(UTR_FAC_THREAD, UTR_THR_EXIT_END,
		    "_thr_exit(last thread)end:usropts 0x%x, pc 0x%x, \
		    stk 0x%x, lwpid %d, last? %d",
		    t->t_usropts, (u_long)t->t_pc, (u_long)t->t_stk, LWPID(t),
		    1);
		exit(0);
	}

	if (ISBOUND(t)) {
		t->t_state = TS_ZOMB;
		/*
		 * Broadcast to anyone waiting for this thread to be suspended
		 */
		if (t->t_stop)
			_lwp_cond_broadcast(&_suspended);
		_sched_unlock();

		/* block out signals while thread is exiting */
		/* XXX CHECK MASKING - note that set is junk XXX */
		__sigprocmask(SIG_SETMASK, &set, NULL);
		TRACE_5(UTR_FAC_THREAD, UTR_THR_EXIT_END,
		    "_thr_exit end:usropts 0x%x, pc 0x%x, stk 0x%x, \
		    tid 0x%x last? %d",
		    t->t_usropts, (u_long)t->t_pc, (u_long)t->t_stk, LWPID(t),
		    0);
		_lwp_terminate(curthread);
	} else {
		if (ONPROCQ(t)) {
			_onproc_deq(t);
		}
		t->t_state = TS_ZOMB;
		/*
		 * Broadcast to anyone waiting for this thread to be suspended
		 */
		if (t->t_stop)
			_lwp_cond_broadcast(&_suspended);

		_nthreads--;
		TRACE_5(UTR_FAC_THREAD, UTR_THR_EXIT_END,
		    "_thr_exit end:usropts 0x%x, pc 0x%x, stk 0x%x, \
		    lwpid %d, last? %d",
		    t->t_usropts, (u_long)t->t_pc, (u_long)t->t_stk, LWPID(t),
		    0);
		_qswtch();
	}
	_panic("_thr_exit");
}

size_t
_thr_min_stack()
{
	return (MINSTACK);
}

/*
 * _thr_main() returns 1 if the calling thread is the initial thread,
 * 0 otherwise.
 */
int
_thr_main()
{

	if (_t0 == NULL)
		return (-1);
	else
		return (curthread == _t0);
}

/*
 * _thr_errnop() returns the address of the thread specific errno to implement
 * libc's ___errno() function.
 */

int *
_thr_errnop()
{
	return (&curthread->t_errno);
}


#undef lwp_self
lwpid_t
lwp_self()
{
	return (LWPID(curthread));
}

uthread_t *
_idtot(thread_t tid)
{
	uthread_t *next, *first;
	int ix = HASH_TID(tid);
	ASSERT(MUTEX_HELD(&(_allthreads[ix].lock)));

	if ((first = _allthreads[ix].first) != NULL) {
		if (first->t_tid == tid) {
			return (first);
		} else {
			next = first->t_next;
			while (next != first) {
				if (next->t_tid == tid) {
					return (next);
				} else
					next = next->t_next;
			}
		}
	}
	return ((uthread_t *)-1);
}

#if defined(UTRACE) || defined(ITRACE)

thread_t
trace_thr_self()
{
	return (curthread->t_tid);
}
#endif

/*
 * Temporary routines for PCR usage.
 */
caddr_t
_tidtotls(thread_t tid)
{
	caddr_t tls;
	int ix;

	_lock_bucket((ix = HASH_TID(tid)));
	tls = (caddr_t)(THREAD(tid))->t_tls;
	_unlock_bucket(ix);
	return (tls);
}

thread_t
_tlstotid(caddr_t tls)
{
	int ix, i;

	for (i = 1; i <= _lasttid; i++) {
		_lock_bucket((ix = HASH_TID(i)));
		if (tls == (caddr_t)(THREAD(i))->t_tls) {
			_unlock_bucket(ix);
			return (i);
		}
		_unlock_bucket(ix);
	}
}

/*
 * Routines for MT Run Time Checking
 * Return 0 if values in stk are valid.  If not valid, return
 * non-zero errno value.
 */

int
_thr_stksegment(stack_t *stk)
{
	extern uthread_t *_t0;
	ucontext_t uc;

	if (_t0 == NULL)
		return (EAGAIN);
	else {
		/*
		 * If this is the main thread, always get the
		 * the current stack bottom and stack size.
		 * These values can change over the life of the
		 * process.
		 */
		if (_thr_main() == 1) {
			if (getcontext(&uc) == 0) {
				curthread->t_stk = uc.uc_stack.ss_sp +
					uc.uc_stack.ss_size;
				curthread->t_stksize = uc.uc_stack.ss_size;
			} else {
				return (errno);
			}
		} else if (curthread->t_flag & T_INTERNAL) {
			return (EAGAIN);
		}
		stk->ss_sp = curthread->t_stk;
		stk->ss_size = curthread->t_stksize;
		stk->ss_flags = 0;

		return (0);
	}
}

int
__thr_sigredirect(uthread_t *t, int ix, int sig, lwpid_t *lwpidp)
{
	register int rc;

	ASSERT(MUTEX_HELD(&(_allthreads[ix].lock)));
	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT((t->t_flag & T_SIGWAIT) || !sigismember(&t->t_hold, sig));
	ASSERT(t != curthread);

	*lwpidp = 0;

	if (t->t_state == TS_ZOMB) {
		TRACE_0(UTR_FAC_THREAD, UTR_THR_SIGBOUNCE_END,
		    "thr_sigredirect end:zombie");
		return (ESRCH);
	} else if (t->t_state == TS_SLEEP) {
		t->t_flag |= T_INTR;
		_unsleep(t);
		_setrq(t);
	}
	sigaddset(&t->t_bsig, sig);
	t->t_bdirpend = 1;
	/*
	 * Return an lwpid, if
	 * 	this is a bound thread
	 * OR   this thread is ONPROCQ and its state is TS_ONPROC - note that
	 *	the thread could be ONPROCQ but its state may be TS_DISP - this
	 *	is when _disp() picks up this thread, turns off the T_OFFPROC
	 *	flag, puts the thread ONPROCQ, but leaves its state to be
	 *	TS_DISP. The state changes in _resume_ret(), after the lwp that
	 *	picked it up has successfully switched to it. The signal will
	 *	then be received via the call to _sigon() from _resume_ret().
	 * AND this thread is not in sigwait(). If it is in sigwait(), it will
	 *     be picked up by that routine. No need to actually send the
	 *     signal via _lwp_sigredirect().
	 */

	if ((ISBOUND(t) || (ONPROCQ(t) && t->t_state == TS_ONPROC)) &&
	    ((t->t_flag & T_SIGWAIT) == 0)) {
		t->t_flag |= T_BSSIG;
		ASSERT(LWPID(t) != 0);
		*lwpidp = LWPID(t);
		return (0);
	}
	TRACE_0(UTR_FAC_THREAD, UTR_THR_KILL_END, "thr_kill end");
	return (0);
}
