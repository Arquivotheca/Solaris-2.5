/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)subr.c	1.56	95/08/29	SMI"


#include "libthread.h"
#include <sys/reg.h>

/*
 * Global variables
 */
uthread_t *_sched_owner;
int _sched_ownerpc;

void
_sched_lock()
{
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_SC_LK_ENTER_START,
	    "_sched_lock enter start");
	_sigoff();
	curthread->t_schedlocked = 1;
	_lwp_mutex_lock(&_schedlock);
	ASSERT(LOCK_HELD(&_schedlock.mutex_lockw));
	_sched_owner = curthread;
	_sched_ownerpc = _getcaller();
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_SC_LK_ENTER_END,
	    "_sched_lock enter end");
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_SC_LK_CS_START,
	    "_sched_lock cs start");
}

void
_sched_unlock()
{
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_SC_LK_CS_END,
	    "_sched_lock cs end");
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_SC_LK_EXIT_START,
	    "_sched_lock exit start");
	ASSERT(LOCK_HELD(&_schedlock.mutex_lockw));
	ASSERT(_sched_owner == curthread);
	ASSERT(LOCK_HELD(&_schedlock.mutex_lockw));
	ASSERT(_sched_owner == curthread);
	_lwp_mutex_unlock(&_schedlock);
	curthread->t_schedlocked = 0;
	_sigon();
	ITRACE_0(UTR_FAC_TLIB_MISC, UTR_SC_LK_EXIT_END,
	    "_sched_lock exit end");
}

void
_sched_lock_nosig()
{
	curthread->t_schedlocked = 1;
	_lwp_mutex_lock(&_schedlock);
	ASSERT(LOCK_HELD(&_schedlock.mutex_lockw));
	_sched_owner = curthread;
	_sched_ownerpc = _getcaller();
}

void
_sched_unlock_nosig()
{
	ASSERT(LOCK_HELD(&_schedlock.mutex_lockw));
	ASSERT(_sched_owner == curthread);
	_sched_owner = NULL;
	_lwp_mutex_unlock(&_schedlock);
	curthread->t_schedlocked = 0;
}

#if defined(ITRACE) || defined(UTRACE)
_trace_sched_lock()
{
	_sigoff();
	curthread->t_schedlocked = 1;
	_lwp_mutex_lock(&_schedlock);
	_sched_owner = curthread;
	_sched_ownerpc = _getcaller();
}

_trace_sched_unlock()
{
	_lwp_mutex_unlock(&_schedlock);
	curthread->t_schedlocked = 0;
	_sigon();
}
#endif


static int _halted = 0;
static void _stackdump(caddr_t v);
static char *exitmsg = "_signotifywait(): bad return; exiting process\n";

void
_panic(char *s)
{
	extern _totalthreads;
	sigset_t ss;
	char buf[500];
	char *bp = buf;
	int sig;

	_halted = 1;
	sprintf(bp, "libthread panic: %s (PID: %d LWP %d)\n",
	    s, getpid(), LWPID(curthread));
	_write(2, bp, strlen(bp));
	_stackdump((caddr_t)_getsp());
	sigfillset(&ss);
	sigdelset(&ss, SIGINT);
	if (curthread->t_schedlocked)
		curthread->t_hold = ss;
	else
		__thr_sigsetmask(SIG_SETMASK, &ss, NULL);
	/*
	 * Keep process idle until sent a SIGINT signal and then
	 * exit.
	 */
	while (1) {
		/*
		 * If this is the aslwp daemon thread that has panic'ed, then
		 * wait using _signotifywait().
		 */
		if (curthread->t_tid == __dynamic_tid) {
			sig = _signotifywait();
			if (sig <= 0) {
				write(2, exitmsg, strlen(exitmsg));
				exit (1);
			} else if (sig == SIGINT)
				exit(1);
			else
				/*
				 * Try to redirect. It might not have any
				 * effect since this is the aslwp and it has
				 * panic'ed implying that the signal could be
				 * directed to a thread which can never run
				 * because there are no lwps available and
				 * the aslwp has panic'ed so they cannot be
				 * created. But try anyway.
				 */
				_sigredirect(sig);
		} else {
			sig = (*__sigtimedwait_trap)(&ss, NULL, NULL);
			if (sig == SIGINT)
				exit(1);
		}
	}
}

int
_assfail(char *a, char *f, int l)
{
	char buf[128];
	char *bp = buf;

	sprintf(bp, "libthread assertion failed: %s, file: %s, line:%d\n",
		a, f, l);
	_write(2, bp, strlen(bp));
	_panic("assertion failed");
}

void
_resetlib()
{
	int i;
	caddr_t sp;

	ASSERT(curthread->t_state == TS_ONPROC);
	curthread->t_preempt = 0;
	curthread->t_lwpid = _lwp_self();
	curthread->t_stop = 0;
	curthread->t_pending = 0;
	curthread->t_bdirpend = 0;
	curthread->t_sig = 0;
	_totalthreads = 1;
	_t0 = curthread;
	_t0->t_tid = 1;
	_lasttid = 1;
	for (i = 0; i < ALLTHR_TBLSIZ; i++) {
		_allthreads[i].first = NULL;
		_allthreads[i].lock.mutex_lockw = 0;
	}
	_allthreads[HASH_TID(1)].first = curthread;
	curthread->t_prev = curthread->t_next = curthread;

	/* initialize dispatch queue */
	for (i = 0; i < DISPQ_SIZE; i++) {
		_dispq[i].dq_first = NULL;
		_dispq[i].dq_last = NULL;
	}
	_maxpriq = -1;
	for (i = 0; i < MAXRUNWORD; i++)
		_dqactmap[i] = 0;

	/* initialize callout processing */
	_co_set = 0;
	_co_tid = 0;
	_calloutcnt = 0;
	_timerset = 0;
	_mutex_init(&_calloutlock, USYNC_THREAD, NULL);
	_calloutp = NULL; /* throw away memory allocated for callouts */

	/* initialize queues for dead threads */
	_zombies = NULL;

	/* initialize SIGWAITING processing */
	_sigwaitingset = 0;

	/* initialize sleep queues */
	for (i = 0; i < NSLEEPQ; i++) {
		_slpq[i].sq_first = NULL;
		_slpq[i].sq_last = NULL;
	}

	/* initialize onprocq maintenance */
	_onprocq = NULL;
	_onprocq_size = 0;
	curthread->t_forw = curthread->t_backw = NULL;
	_userthreads = 1;
	_nidle = NULL;
	_nidlecnt = 0;
	_nthreads = 0;
	_nlwps = 0;
	_u2bzombies = 0;
	_zombiecnt = 0;
	if (!ISBOUND(curthread)) {
		curthread->t_forw = curthread->t_backw = curthread;
		_onprocq = curthread;
		_onprocq_size = 1;
		_nthreads = 1;
		_nlwps = 1;
	}
	_ndie = 0;
	_nrunnable = 0;
	_naging = 0;
	_nagewakecnt = 0;
	_nidle = 0;
	_minlwps = 1;
	/*
	 * Clear pending signal mask for child of a fork1(). For the child of
	 * a fork(), it is not a complete solution to clear the _pmask of
	 * pending signals, since other threads could already be running and
	 * reading _pmask, by the time the caller's clone in the child clears
	 * _pmask. Instead, rely on the lazy clearing of pending signals
	 * described in the comments in sys/common/sigwait.c
	 */
	sigemptyset(&_pmask);
	/*
	 * Free stacks in the default stack cache. All stacks are of the same
	 * size - DEFAULTSTACK bytes + a page of red-zone.
	 */
	while ((sp = _defaultstkcache.next) != NULL) {
		_defaultstkcache.next = (caddr_t)(*(long *)sp);
        	/* include one page for redzone */
        	if (munmap(sp - _lpagesize, DEFAULTSTACK + _lpagesize)) {
                	perror("munmap() failed for default stacks");
                	_panic("_resetlib()");
        	}
	}
	_defaultstkcache.size = 0;
	ASSERT(_defaultstkcache.busy == 0); /* enforced via fork1() wrapper */
}


#ifdef sparc
static void
_stackdump(caddr_t v)
{
	unsigned long addr;
	char buf[20], *bp;
	struct rwindow *sp = (struct rwindow *)v;

	_write(2, "stacktrace:\n", 12);
	(void) _flush_and_tell();
	sp = (struct rwindow *)_getsp();
	while (sp) {
		addr = (unsigned long) sp->rw_rtn;
		buf[19] = '\0';
		bp = &buf[19];
		do {
			*--bp = "0123456789abcdef"[addr%0x10];
			addr /= 0x10;
		} while (addr);
		_write(2, "\t", 1);
		_write(2, bp, strlen(bp));
		_write(2, "\n", 1);
		sp = (struct rwindow *)sp->rw_fp;
	}
}
#else
static void
_stackdump(caddr_t v)
{
}
#endif
