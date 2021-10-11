/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)disp.c	1.90	95/09/11	SMI"


#include <stdio.h>

#ifdef DEBUGT
int _debugt = 1;
#endif

#include "libthread.h"
#include <sys/reg.h>

/*
 * Global variables
 */
lwp_mutex_t _schedlock;		/* protects runqs and sleepqs */
				/* protected by sched_lock() */
dispq_t _dispq[DISPQ_SIZE];	/* the queue of runnable threads */
long 	_dqactmap[MAXRUNWORD];	/* bit map of priority queues */
int 	_nrunnable = 0;		/* number of threads on the run queue */
int 	_nthreads = 0;		/* number of unbound threads */
int 	_totalthreads = 0;	/* total number of threads */
int 	_userthreads = 0;	/* number of user created threads */
int 	_u2bzombies = 0;	/* u threads on their way 2b zombies */
int 	_d2bzombies = 0;	/* dameon ths on their way 2b zombies */
uthread_t *_nidle = NULL;	/* list of idling threads */
int 	_nagewakecnt = 0;	/* number of awakened aging threads */
int	_naging = 0;		/* number of aging threads running */
lwp_cond_t _aging;		/* condition on which threads age */
int 	_nlwps = 0;		/* number of lwps in the pool */
int 	_minlwps = 1;		/* min number of lwps in pool */
int 	_ndie = 0;		/* number of lwps to delete from pool */
int 	_maxpriq = THREAD_MIN_PRIORITY-1; /* index of highest priority dispq */
int 	_minpri = THREAD_MIN_PRIORITY; /* value of lowest priority thread */
int	_nidlecnt = 0;
int	_onprocq_size = 1;

/*
 * Static variables
 */
static	timestruc_t _idletime = {60*5, 0}; /* age for 5 minutes then destroy */

/*
 * Static functions
 */
static	int _onrunq(uthread_t *t);
static	int _park(uthread_t *t);
static	void _idle_deq(uthread_t *t);
static	uthread_t * _disp();



/*
 * quick swtch(). switch to a new thread without saving the
 * current thread's state.
 */
void
_qswtch()
{
	struct thread *next;
	uthread_t *t = curthread;
	long sp;

	ASSERT(MUTEX_HELD(&_schedlock));

	ASSERT(LWPID(t) != 0);
	next = _disp();
	ASSERT(MUTEX_HELD(&_schedlock));
	if (IDLETHREAD(next)) {
		if (_ndie) {
			_ndie--;
			_sched_unlock_nosig();
			_lwp_exit();
		}
		if (t == next)
			return;
		/*
		 * XXX Fix this. Cannot return if _qswtch() called from
		 * thr_exit()
		 */
		_thread_ret(next, _age);
		next->t_state = TS_DISP;
	}
	_sched_unlock_nosig();
	_sigoff();
	sp = next->t_idle->t_sp;
	ASSERT(next->t_state == TS_DISP);
	ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_RESUME_START, "_resume start");
	_resume(next, sp, 1);
	_panic("_qswtch: resume returned");
}

/*
 * when there are more lwps in the pool than there are
 * threads, the idling thread will hang around aging on
 * a timed condition wait. If the idling thread is awakened
 * due to the realtime alarm, then the idling thread and its
 * lwp are deallocated.
 */
void
_age()
{
	timestruc_t ts;
	int err = 0;

	/*
	ITRACE_1(TR_FAC_SYS_LWP, TR_SYS_LWP_CREATE_END2,
	    "lwp_create end2:lwpid 0x%x", lwp_self());
	TRACE_0(UTR_FAC_TRACE, UTR_THR_LWP_MAP, "dummy for thr_lwp mapping");
	*/
	ts.tv_nsec = 0;
	_sched_lock_nosig();
	while (1) {
		_naging++;
		while (!_nrunnable) {
			ts.tv_sec = time(NULL) + _idletime.tv_sec;
			err = _lwp_cond_timedwait(&_aging, &_schedlock, &ts);
			if (_nagewakecnt > 0) {
				_nagewakecnt--;
				continue;
			}
			ASSERT(err != EFAULT);
			if (err == ETIME || err == EAGAIN || _ndie) {
				_nlwps--;
				if (_ndie > 0)
					_ndie--;
				_naging--;
				_sched_unlock_nosig();
				ASSERT(ISBOUND(curthread));
				_thr_exit(0);
			}
		}
		_naging--;
		_qswtch();
	}
}


static void
_idle_deq(uthread_t *t)
{
	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(_nidlecnt > 0);

	t->t_flag &= ~T_IDLE;
	if (--_nidlecnt > 0) {
		t->t_ibackw->t_iforw = t->t_iforw;
		t->t_iforw->t_ibackw = t->t_ibackw;
		if (_nidle == t)
			_nidle = t->t_iforw;
	} else
		_nidle = NULL;
	t->t_iforw = t->t_ibackw = NULL;
}

static void
_idle_enq(uthread_t *t)
{
	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(!ISBOUND(t));
	ASSERT((t->t_iforw == t->t_ibackw) && t->t_iforw == NULL);

	/* temp trace point to benchmark lwp create */
	/*
	ITRACE_1(TR_FAC_SYS_LWP, TR_SYS_LWP_CREATE_END2,
	    "lwp_create end2:lwpid 0x%x", lwp_self());
	*/
	t->t_flag |= T_IDLE;
	_nidlecnt++;
	if (_nidle == NULL) {
		_nidle = t;
		t->t_iforw = t->t_ibackw = t;
	} else {
		_nidle->t_ibackw->t_iforw = t;
		t->t_iforw = _nidle;
		t->t_ibackw = _nidle->t_ibackw;
		_nidle->t_ibackw = t;
	}
}


void
_onproc_deq(uthread_t *t)
{

	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(_onprocq_size > 0);

	if (--_onprocq_size > 0) {
		t->t_backw->t_forw = t->t_forw;
		t->t_forw->t_backw = t->t_backw;
		if (_onprocq == t)
			_onprocq = t->t_forw;
	} else
		_onprocq = NULL;
	t->t_forw = t->t_backw = NULL;
}

void
_onproc_enq(uthread_t *t)
{

	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(!ISBOUND(t));
	ASSERT(t->t_link == NULL);
	ASSERT((t->t_forw == t->t_backw) && t->t_forw == NULL);
	ASSERT(t->t_state == TS_ONPROC || t->t_state == TS_DISP);

	_onprocq_size++;
	if (_onprocq == NULL) {
		_onprocq = t;
		t->t_forw = t->t_backw = t;
	} else {
		_onprocq->t_backw->t_forw = t;
		t->t_forw = _onprocq;
		t->t_backw = _onprocq->t_backw;
		_onprocq->t_backw = t;
	}
}

#ifdef DEBUG
_onprocq_consistent()
{
	uthread_t *x;
	int cnt, v;

	_sched_lock();
	x = _onprocq;
	if (x == NULL)
		cnt = 0;
	else {
		cnt = 1;
		while ((x = x->t_forw) != _onprocq)
			cnt++;
	}
	if (cnt == _onprocq_size)
		v = 1;
	else
		v = 0;
	_sched_unlock();
	return (v);
}
#endif

int
_swtch(int dontsave)
{
	struct thread *t = curthread;
	struct thread *next;
	long sp;
	int qx, ret;
	int oflag;
	sigset_t sigs;
#ifdef DEBUG
	sigset_t lwpmask;
#endif

	ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_START, "_swtch start");
	/*
	 * It is impossible, given the signal delivery mechanism, for a thread
	 * to have received a signal while in the libthread critical section
	 * and to then switch. Hence the following ASSERT.
	 */
	_sched_lock();
again:
	ASSERT(t->t_state == TS_SLEEP || t->t_state == TS_DISP ||
	    t->t_state == TS_RUN || t->t_state == TS_ONPROC ||
	    t->t_state == TS_STOPPED);
	/*
	 * All calls to _swtch() are made with t_nosig at least 1. And the
	 * above call to _sched_lock() increments by another 1. Hence the
	 * following ASSERT.
	 */
	ASSERT(t->t_nosig >= 2);
	if (t->t_state == TS_SLEEP) {
		/*
		 * check t_ssig, T_BSSIG and t_sig. If any one of these is
		 * non-null, interrupt the thread going to sleep and
		 * return.
		 */
		if ((!sigisempty(&t->t_ssig)) ||
		    ((t->t_flag & T_BSSIG) | t->t_sig)) {
			ASSERT(sigisempty(&t->t_bsig) || (t->t_flag & T_BSSIG)
			    || (t->t_flag & T_SIGWAIT));
			_unsleep(t);
			t->t_state = TS_ONPROC;
			if (!ISBOUND(t))
				_onproc_enq(t);
			t->t_flag |= T_INTR;
			_sched_unlock();
                        ASSERT((curthread->t_sig != 0)
			    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
			    && lwpmask.__sigbits[0] == 0
			    && lwpmask.__sigbits[1] == 0
			    && lwpmask.__sigbits[2] == 0
			    && lwpmask.__sigbits[3] == 0));
			ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
			    "_swtch end:end %d", 2);
			return;
		}
		if (ISBOUND(t) || ISTEMPBOUND(t)) {
			ret = _park(t);
			/*
			 * In the following, do not check the underlying LWP
			 * mask if ISTEMPBOUND - this implies that the thread is
			 * in the process of running a user-signal-handler and
			 * so its LWP mask will be cleared eventually before it
			 * switches to another thread.
			 */
                        ASSERT((curthread->t_sig != 0)
			    || ISTEMPBOUND(t)
			    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
			        && lwpmask.__sigbits[0] == 0
			        && lwpmask.__sigbits[1] == 0
			        && lwpmask.__sigbits[2] == 0
			        && lwpmask.__sigbits[3] == 0));
			ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
			    "_swtch end:end %d", 1);
			return (ret);
		}
	} else if (t->t_stop && (t->t_state == TS_DISP ||
	    t->t_state == TS_ONPROC || ISBOUND(t))) {
		ASSERT((t->t_state == TS_ONPROC && OFFPROCQ(t)) ||
		    t->t_state == TS_DISP && ONPROCQ(t));
		if (!ISBOUND(t) && t->t_state == TS_DISP)
			_onproc_deq(t);
		t->t_state = TS_STOPPED;
		/*
		 * Broadcast to anyone waiting for this thread to be suspended
		 */
		_lwp_cond_broadcast(&_suspended);

		if (t->t_sig != 0 || ISTEMPBOUND(t) || (t->t_flag & T_BSSIG) ||
		    ISBOUND(t)) {
			ret = _park(t);
			/*
			 * For following ASSERT, see comment above under the
			 * preceding call to _park().
			 */
                        ASSERT((curthread->t_sig != 0)
			    || ISTEMPBOUND(t)
			    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
			        && lwpmask.__sigbits[0] == 0
			        && lwpmask.__sigbits[1] == 0
			        && lwpmask.__sigbits[2] == 0
			        && lwpmask.__sigbits[3] == 0));
			ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
			    "_swtch end:end %d", 8);
			return (ret);
		}
	} else {
		if (t->t_state == TS_ONPROC) {
			ASSERT(ISBOUND(t) || ONPROCQ(t));
			_sched_unlock();
                        ASSERT((curthread->t_sig != 0)
			    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
			    && lwpmask.__sigbits[0] == 0
			    && lwpmask.__sigbits[1] == 0
			    && lwpmask.__sigbits[2] == 0
			    && lwpmask.__sigbits[3] == 0));
			ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
			    "_swtch end:end %d", 3);
			return;
		}
		if (t->t_state == TS_DISP) {
			ASSERT(ONPROCQ(t) || PREEMPTED(t) || STOPPED(t));
			t->t_state = TS_ONPROC;
			_sched_unlock();
                        ASSERT((curthread->t_sig != 0)
			    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
			    && lwpmask.__sigbits[0] == 0
			    && lwpmask.__sigbits[1] == 0
			    && lwpmask.__sigbits[2] == 0
			    && lwpmask.__sigbits[3] == 0));
			ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
			    "_swtch end:end %d", 4);
			return;
		}
		ASSERT(t->t_state == TS_RUN || t->t_state == TS_STOPPED);
		/*
		 * Again, as in the TS_SLEEP case, check if t_ssig, T_BSSIG
		 * or t_sig is non-NULL. Since this implies that the thread
		 * cannot switch, since it is in the run state, simply put
		 * it back to the ONPROC state and return on the same LWP
		 * after deleting it from the run queue.
		 */
		if ((!sigisempty(&t->t_ssig) || ((t->t_flag & T_BSSIG)
		    || ISTEMPBOUND(t)))
		    && t->t_state == TS_RUN) {
			if (_dispdeq(t) == 1)
				_panic("_swtch: cannot deq from run queue");
			t->t_state = TS_ONPROC;
			if (!ISBOUND(t))
				_onproc_enq(t);
			_sched_unlock();
                        ASSERT((curthread->t_sig != 0)
			    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
			    && lwpmask.__sigbits[0] == 0
			    && lwpmask.__sigbits[1] == 0
			    && lwpmask.__sigbits[2] == 0
			    && lwpmask.__sigbits[3] == 0));
			ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
			    "_swtch end:end %d", 9);
			return;
		}
	}
	ASSERT(t->t_state == TS_SLEEP || t->t_state == TS_RUN || 
	    t->t_state == TS_STOPPED);
	/*
	 * thread's preemption flag is cleared since thread is really
	 * surrendering the "processor" to another thread.
	 */
	t->t_preempt = 0;
	t->t_flag &= ~T_PREEMPT;
	t->t_flag |= T_OFFPROC;
	next = _disp();
	if (IDLETHREAD(next)) {
		ASSERT(!ONPROCQ(t));
		t->t_flag &= ~T_OFFPROC;
		_idle_enq(t);
		ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
		    "_swtch end:end %d", 5);
		_park(t);
		_sched_lock();
		if (ON_IDLE_Q(t))
			_idle_deq(t);
		ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_START, "_swtch start");
		goto again;
	}
	ASSERT(next->t_link == NULL);
	ASSERT(next->t_state == TS_DISP);
	ASSERT((next->t_flag & T_OFFPROC) == 0);
	ASSERT(t->t_nosig > 0);
	if (next != t) {
		sp = next->t_idle->t_sp;
		_sched_unlock_nosig();
		ASSERT((curthread->t_sig != 0)
		    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
		    && lwpmask.__sigbits[0] == 0
		    && lwpmask.__sigbits[1] == 0
		    && lwpmask.__sigbits[2] == 0
		    && lwpmask.__sigbits[3] == 0));
		ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
		    "_swtch end:end %d", 6);
		ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_RESUME_START, "_resume start");
		_resume(next, sp, dontsave);
		ASSERT((curthread->t_sig != 0)
		    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
		    && lwpmask.__sigbits[0] == 0
		    && lwpmask.__sigbits[1] == 0
		    && lwpmask.__sigbits[2] == 0
		    && lwpmask.__sigbits[3] == 0));

	} else {
		t->t_state = TS_ONPROC;
		ASSERT(ONPROCQ(t));
		_sched_unlock();
		ASSERT((curthread->t_sig != 0)
		    || (__sigprocmask(SIG_BLOCK, NULL, &lwpmask) != -1
		    && lwpmask.__sigbits[0] == 0
		    && lwpmask.__sigbits[1] == 0
		    && lwpmask.__sigbits[2] == 0
		    && lwpmask.__sigbits[3] == 0));
		ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_SWTCH_END,
		    "_swtch end:end %d", 7);
	}
	ASSERT(MUTEX_HELD(&curthread->t_lock));
	ASSERT(curthread->t_state == TS_ONPROC);
}

/*
 * Initialize the dispatcher queues.
 */
_dispinit()
{
	int i;

	for (i = 0; i < DISPQ_SIZE; i++)
		_dispq[i].dq_first = _dispq[i].dq_last = NULL;
}

/*
 * Park a thread inside the kernel. The thread is bound to its
 * LWP until it is awakened. A park thread may wake up due to
 * signals. The caller must verify on return from this call whether
 * or not the thread was unparked.
 */
static int
_park(uthread_t *t)
{
	char buf[40];
	int err = 0;

	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(ISBOUND(t) || OFFPROCQ(t));
	ASSERT(t->t_nosig > 0);

	ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_PARK_START, "_park start");
	t->t_flag |= T_PARKED;
	while (ISPARKED(t)) {
		_sched_unlock_nosig();
		/*
		 * interrupt the parking thread if signaled while
		 * asleep or when _lwp_sema_wait() returns EINTR.
		 */
		if (err == EINTR) {
			_sched_lock_nosig();
			/*
			 * XXX Check why nothing done for T_IDLE threads.
			 * e.g. what if t_bpending happens before schedlock
			 * is released above?
			 */
			if (err == EINTR && (t->t_flag & T_IDLE)) {
			    /* do nothing */;
			} else if (t->t_state == TS_SLEEP) {
				if (err == EINTR)
				t->t_flag |= T_INTR;
			    err = EINTR;
			    _unsleep(t);
			    _setrq(t);
			}
			_sched_unlock_nosig();
		}
		err = _lwp_sema_wait(&t->t_park);
		_sched_lock_nosig();
	}
	ASSERT(err == EINTR || !ISPARKED(t));
	t->t_flag &= ~T_PARKED;
	_sched_unlock();
	ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_PARK_END, "_park end");
}

/*
 * Wake up a parked thread.
 */
void
_unpark(uthread_t *t)
{
	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(ISPARKED(t));
	/*
	 * The thread can either be bound, or idle, or stuck to this LWP and
	 * hence parked because of pending signal action.
	 */
	ASSERT(ISBOUND(t) || ON_IDLE_Q(t) || ISTEMPBOUND(t) ||
	    t->t_sig != 0 || ((t->t_flag & T_BSSIG) != 0));
	ITRACE_1(UTR_FAC_TLIB_SWTCH, UTR_UNPARK_START,
	    "_unpark start:thread ptr = 0x%x", t);
	if (ON_IDLE_Q(t))
		_idle_deq(t);
	t->t_flag &= ~T_PARKED;
	_lwp_sema_post(&t->t_park);
	ITRACE_0(UTR_FAC_TLIB_SWTCH, UTR_UNPARK_END, "_unpark end");
}

#ifdef OUT
int a, b, c;

_isrunqok(int x, int y, int z)
{
	uthread_t *tx = _allthreads;
	uthread_t *ftx = tx;
	uthread_t *tz;
	int r = 0, onr = 0;
	a = x;
	b = y;
	c = z;
	do {
		if (tx->t_state == TS_RUN) {
			tz = _dispq[tx->t_pri].dq_first;
			r++;
			while (tz != NULL) {
				if (tz == tx) {
					onr++;
					break;
				}
				tz = tz->t_link;
			}
			if (r != onr) _panic("not on runq", tx, tx->t_pri);
		}
	} while ((tx = tx->t_next) != ftx);
}
#endif

#ifdef DEBUG
/*
 * Purely for assertion purposes. Asserts that the thread
 * is on the run queue. Returns 0 on failure, 1 on success.
 */
static int
_onrunq(uthread_t *t)
{
	int i;
	uthread_t *qt;

	ASSERT(MUTEX_HELD(&_schedlock));
	for (i = 0; i <= _maxpriq; i++) {
		for (qt = _dispq[i].dq_first; qt != NULL; qt = qt->t_link)
			if (qt == t)
				return (1); /* success */
	}
	return (0); /* failure */
}
#endif

void
_setrq(uthread_t *t)
{
	uthread_t **pt, *nt;
	register dispq_t *dq;
	int preemptflag = 0;
	int qx;

	ITRACE_4(UTR_FAC_TLIB_DISP, UTR_SETRQ_START,
	    "_setrq start:tid 0x%x pri %d usropts 0x%x _maxpriq %d",
	    t->t_tid, t->t_pri, t->t_usropts, _maxpriq);
	ASSERT(MUTEX_HELD(&_schedlock));
	ASSERT(t->t_link == NULL && t->t_wchan == NULL);
	ASSERT(!_onrunq(t));

	if (t->t_stop) {
		t->t_state = TS_STOPPED;
		if (ISBOUND(t)) {
			ASSERT(ISPARKED(t));
			_unpark(t);
		}

		/*
		 * Broadcast to anyone waiting for this thread to be suspended
		 */
		_lwp_cond_broadcast(&_suspended);

		return;
	}
	if (ISPARKED(t)) {
		if (ISBOUND(t))
			t->t_state = TS_ONPROC;
		else {
			ASSERT(OFFPROCQ(t));
			t->t_state = TS_DISP;
			_onproc_enq(t);
		}
		_unpark(t);
		ITRACE_4(UTR_FAC_TLIB_DISP, UTR_SETRQ_END,
		    "_setrq end:tid 0x%x pri %d usropts 0x%x _maxpriq %d",
		    t->t_tid, t->t_pri, t->t_usropts, _maxpriq);
		return;
	} else if (ISBOUND(t)) {
		t->t_state = TS_ONPROC;
		return;
	}
	ASSERT((t->t_forw == t->t_backw) && t->t_forw == NULL);
	HASH_PRIORITY(t->t_pri, qx);
	t->t_state = TS_RUN;
	if (qx > _maxpriq) {
		_maxpriq = qx;
		if (_maxpriq > _minpri)
			preemptflag = 1;
	}
	dq = &_dispq[qx];
	if (dq->dq_last == NULL) {
		/* queue empty */
		dq->dq_first = dq->dq_last = t;
		_dqactmap[(qx/BPW)] |= 1<<(qx & (BPW-1));
	} else {
		/* add to end of q */
		if (qx < DISPQ_SIZE) {
			dq->dq_last->t_link = t;
			dq->dq_last = t;
		}
#ifdef OUT
		else {
			/*
			 * priorities outside normal range map to last
			 * priority bucket and are inserted in priority
			 * order.
			 */
			register struct thread *prev, *next;

			prev = (struct thread *) dq;
			for (next = dq->dq_first; next != NULL &&
				next->t_pri >= tpri; next = next->t_link)
						prev = next;
			if (prev == (struct thread *)dq) {
				t->t_link = dq->dq_first;
				dq->dq_first = t;
			} else {
				t->t_link = prev->t_link;
				prev->t_link = t;
			}
		}
#endif
	}
	_nrunnable++;
	if (_nidle) {
		_unpark(_nidle);
	} else if (_nagewakecnt < _naging) {
		_nagewakecnt++;
		_lwp_cond_signal(&_aging);
	} else {
		if (!_sigwaitingset)
			_sigwaiting_enabled();
		if (preemptflag)
			_preempt(NULL, t->t_pri);
	}
	ITRACE_4(UTR_FAC_TLIB_DISP, UTR_SETRQ_END,
	    "_setrq end:tid 0x%x pri %d usropts 0x%x _maxpriq %d",
	    t->t_tid, t->t_pri, t->t_usropts, _maxpriq);
}


/*
 * Remove a thread from its dispatch queue. Returns FAILURE (int value 1)
 * when thread wasn't dispatchable.
 */
int
_dispdeq(uthread_t *t)
{
	dispq_t *dq;
	uthread_t **prev, *pt, *tx;
	int qx;

	ASSERT(MUTEX_HELD(&_schedlock));

	if (t->t_state != TS_RUN)
		return (1);
	ASSERT(_nrunnable > 0);
	--_nrunnable;
	HASH_PRIORITY(t->t_pri, qx);
	dq = &_dispq[qx];
	if (dq->dq_last == dq->dq_first) {
		/*
		 * There is only one thread on this priority run
		 * queue. If this thread's priority is equal to
		 * _maxpriq then _maxpriq must be re-adjusted.
		 */
		ASSERT(t == dq->dq_last);
		dq->dq_last = dq->dq_first = NULL;
		t->t_link = NULL;
		_dqactmap[(qx/BPW)] &= ~(1 << (qx & (BPW-1)));
		if (_nrunnable == 0)
			_maxpriq = _minpri-1;
		else {
			if (t->t_pri == _maxpriq) {
				/*
				 *  Alternatively, _maxpriq can be computed from
				 * _dqactmap[].
				 */
				while (_maxpriq-- > _minpri)
					if (_dispq[_maxpriq].dq_last != NULL)
						break;
			}
		}

		return (0);
	}
	prev = &dq->dq_first;
	pt = NULL;
	while (tx = *prev) {
		if (tx == t) {
			if ((*prev = tx->t_link) == NULL)
				dq->dq_last = pt;
			tx->t_link = NULL;
			return (0);
		}
		pt = tx;
		prev = &tx->t_link;
	}
	return (1);
}

#define	QX(hb)	((hb) + BPW * runword)

/*
 * Return the highest priority runnable thread which needs an lwp.
 */
static uthread_t *
_disp()
{
	register uthread_t *t = curthread, **prev, *prev_t;
	register dispq_t *dq;
	register int runword, bitmap, qx, hb;

	ITRACE_0(UTR_FAC_TLIB_DISP, UTR_DISP_START, "_disp_start");
	ASSERT(MUTEX_HELD(&_schedlock));

	if (!_nrunnable)	/* if run q empty */
		goto idle;
	/*
	 * Find the runnable thread with the highest priority.
	 * _dqactmap[] is a bit map of priority queues. This
	 * loop looks through this map for the highest priority
	 * queue with runnable threads.
	 */
	for (runword = _maxpriq/BPW; runword >= 0; runword--) {
		do {
			bitmap = _dqactmap[runword];
			if ((hb = _hibit(bitmap) -1) >= 0) {
				qx = QX(hb);
				dq = &_dispq[qx];
				t = dq->dq_first;
				dq->dq_first = t->t_link;
				t->t_link = NULL;
				if (--_nrunnable == 0) {
					dq->dq_last = NULL;
					_dqactmap[runword] &= ~(1<<hb);
					_maxpriq = _minpri-1;
					/* exit the while */
					hb = -1;
				} else if (dq->dq_first == NULL) {
					dq->dq_last = NULL;
					_dqactmap[runword] &= ~(1<<hb);
					if (qx == _maxpriq) {
						/* re-adjust _maxpriq */
						do {
							bitmap =
							    _dqactmap[runword];
							hb =
							    _hibit(bitmap) - 1;
							if (hb >= 0)
								break;
						} while (runword--);
						if (hb < 0)
							_maxpriq = _minpri-1;
						else
							_maxpriq = QX(hb);
					}
				}
				ASSERT(t->t_state == TS_RUN);
				t->t_state = TS_DISP;
				_onproc_enq(t);
				if (t->t_flag & T_OFFPROC) {
					t->t_flag &= ~T_OFFPROC;
					t->t_lwpid = curthread->t_lwpid;
					ASSERT(LWPID(t) != 0);
					t->t_idle = curthread->t_idle;
					goto out;
				} else {
					if (ISPARKED(t))
						_unpark(t);
				}
			}
		} while (hb >= 0);
	}
idle:
	t = curthread->t_idle;
out:
	ITRACE_0(UTR_FAC_TLIB_DISP, UTR_DISP_END, "_disp_end");
	return (t);
}
