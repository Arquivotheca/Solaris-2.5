/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)timers.c	1.51	95/07/19 SMI"

/*	From	kern_time.c 2.20 90/02/01 SMI; from UCB 7.5 7/21/87	*/

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */


#include <sys/param.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/cpuvar.h>
#include <sys/timer.h>
#include <sys/debug.h>

extern int 	hr_clock_lock();
extern void	hr_clock_unlock();

static void	realitexpire(kthread_t *);
static void	realprofexpire(struct proc *);
static int	timer_del_cleanup(timerstr_t *);

/*
 * macro to compare a timeval to a timestruc
 */

#define	TVTSCMP(tvp, tsp, cmp) \
	/* CSTYLED */ \
	((tvp)->tv_sec cmp (tsp)->tv_sec || \
	((tvp)->tv_sec == (tsp)->tv_sec && \
	/* CSTYLED */ \
	(tvp)->tv_usec * 1000 cmp (tsp)->tv_nsec))

/*
 * Time of day and interval timer support.
 *
 * These routines provide the kernel entry points to get and set
 * the time-of-day and per-process interval timers.  Subroutines
 * here provide support for adding and subtracting timeval structures
 * and decrementing interval timers, optionally reloading the interval
 * timers when they expire.
 */

/*
 * SunOS function to generate monotonically increasing time values.
 */
void
uniqtime(struct timeval *tv)
{
	static struct timeval last;
	timestruc_t ts;
	int sec, usec, nsec;

	/*
	 * protect modification of last
	 */
	mutex_enter(&tod_lock);
	gethrestime(&ts);

	/*
	 * Fast algorithm to convert nsec to usec -- see hrt2ts()
	 * in common/os/timers.c for a full description.
	 */
	nsec = ts.tv_nsec;
	usec = nsec + (nsec >> 2);
	usec = nsec + (usec >> 1);
	usec = nsec + (usec >> 2);
	usec = nsec + (usec >> 4);
	usec = nsec - (usec >> 3);
	usec = nsec + (usec >> 2);
	usec = nsec + (usec >> 3);
	usec = nsec + (usec >> 4);
	usec = nsec + (usec >> 1);
	usec = nsec + (usec >> 6);
	usec = usec >> 10;
	sec = ts.tv_sec;

	/*
	 * Try to keep timestamps unique, but don't be obsessive about
	 * it in the face of large differences.
	 */
	if ((sec <= last.tv_sec) &&		/* same or lower seconds, and */
	    ((sec != last.tv_sec) ||		/* either different second or */
	    (usec <= last.tv_usec)) &&		/* lower microsecond, and */
	    ((last.tv_sec - sec) <= 5)) {	/* not way back in time */
		sec = last.tv_sec;
		usec = last.tv_usec + 1;
		if (usec >= MICROSEC) {
			usec -= MICROSEC;
			sec++;
		}
	}
	last.tv_sec = sec;
	last.tv_usec = usec;
	mutex_exit(&tod_lock);

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}


int
gettimeofday(struct timeval *tp)
{
	struct timeval atv;

	if (tp) {
		uniqtime(&atv);
		if (copyout((caddr_t)&atv, (caddr_t)tp, sizeof (atv)))
			return (set_errno(EFAULT));
	}
	return (0);
}

/*
 * Get value of an interval timer.  The process virtual and
 * profiling virtual time timers are kept in the lwp struct, since
 * they can be swapped out.  These are kept internally in the
 * way they are specified externally: in time until they expire.
 *
 * The real time interval timer is kept in the thread struct
 * for the lwp, and its value (it_value) is kept as an absolute
 * time rather than as a delta, so that it is easy to keep
 * periodic real-time signals from drifting.  The same is true
 * for the real time profiling timer, except that it is kept in
 * the proc structure because it affects all lwps in the process.
 *
 * Virtual time timers are processed in the hardclock() routine of
 * kern_clock.c.  The real time timers are processed by timeout
 * routines, called from the softclock() routine.  Since a callout
 * may be delayed in real time due to interrupt processing in the system,
 * it is possible for the real time timeout routines (realitexpire and
 * realprofexpire, given below), to be delayed in real time past when
 * they are supposed to occur.  It does not suffice, therefore, to reload
 * the real timer .it_value from the real time timers .it_interval.
 * Rather, we compute the next time in absolute time the timer should go off.
 */

struct gtimera {
	u_int	which;
	struct itimerval *itv;
};

int xgetitimer(register struct gtimera *uap, rval_t *rvp, int iskaddr);

/* ARGSUSED1 */
int
getitimer(uap, rvp)
	register struct gtimera *uap;
	rval_t	*rvp;
{
	return (xgetitimer(uap, rvp, 0));
}

/* ARGSUSED1 */
int
xgetitimer(register struct gtimera *uap, rval_t *rvp, int iskaddr)
{
	struct proc *p = curproc;
	struct timeval now;
	struct itimerval aitv;

	if (uap->which > 3)
		return (EINVAL);

	mutex_enter(&p->p_lock);
	if (uap->which == ITIMER_REAL || uap->which == ITIMER_REALPROF) {
		uniqtime(&now);
		/*
		 * Convert from absoulte to relative time in .it_value
		 * part of real time timer.  If time for real time timer
		 * has passed return 0, else return difference between
		 * current time and time for the timer to go off.
		 */
		if (uap->which == ITIMER_REAL)
			aitv = curthread->t_realitimer;
		else
			aitv = p->p_rprof_timer;
		if (timerisset(&aitv.it_value))
			/* CSTYLED */
			if (timercmp(&aitv.it_value, &now, <))
				timerclear(&aitv.it_value);
			else
				timevalsub(&aitv.it_value, &now);
	} else {
		aitv = ttolwp(curthread)->lwp_timer[uap->which];
	}
	mutex_exit(&p->p_lock);
	if (iskaddr)
		bcopy((caddr_t)&aitv, (caddr_t)uap->itv,
		    sizeof (struct itimerval));
	else
		if (copyout((caddr_t)&aitv, (caddr_t)uap->itv,
		    sizeof (struct itimerval)))
			return (EFAULT);

	return (0);
}

struct stimera {
	u_int	which;
	struct itimerval *itv;
	struct itimerval *oitv;
};

int xsetitimer(register struct stimera *uap, rval_t *rvp, int iskaddr);

int
setitimer(uap, rvp)
	register struct stimera *uap;
	rval_t	*rvp;
{
	return (xsetitimer(uap, rvp, 0));
}

int
xsetitimer(register struct stimera *uap, rval_t *rvp, int iskaddr)
{
	struct itimerval aitv, *aitvp;
	struct timeval now;
	register struct proc *p = curproc;
	register kthread_t *t;
	register int tmp_id;

	if (uap->which > 3)
		return (EINVAL);

	aitvp = uap->itv;
	if (uap->oitv) {
		uap->itv = uap->oitv;
		if (getitimer((struct gtimera *)uap, rvp))
			return (EFAULT);	/* only possible error */
	}
	if (aitvp == 0)
		return (0);

	if (iskaddr)
		bcopy((caddr_t)aitvp, (caddr_t)&aitv,
		    sizeof (struct itimerval));
	else
		if (copyin((caddr_t)aitvp, (caddr_t)&aitv,
		    sizeof (struct itimerval)))
			return (EFAULT);

	if (itimerfix(&aitv.it_value) ||
	    (itimerfix(&aitv.it_interval) && timerisset(&aitv.it_value))) {
		return (EINVAL);
	}

	mutex_enter(&p->p_lock);
	switch (uap->which) {
	case ITIMER_REAL:
		while (curthread->t_itimerid > 0) {
			/*
			 * Avoid deadlock in callout_delete (called from
			 * untimeout) which may go to sleep (while holding
			 * p_lock). Drop p_lock and re-acquire it after
			 * untimeout returns. Need to clear t_itimerid
			 * while holding lock.
			 */
			tmp_id = curthread->t_itimerid;
			curthread->t_itimerid = 0;
			mutex_exit(&p->p_lock);
			(void) untimeout(tmp_id);
			mutex_enter(&p->p_lock);
		}
		if (timerisset(&aitv.it_value)) {
			uniqtime(&now);
			timevaladd(&aitv.it_value, &now);
			curthread->t_itimerid = realtime_timeout(realitexpire,
			    (caddr_t)curthread, hzto(&aitv.it_value));
		}
		curthread->t_realitimer = aitv;
		break;
	case ITIMER_REALPROF:
		while (p->p_rprof_timerid > 0) {
			tmp_id = p->p_rprof_timerid;
			p->p_rprof_timerid = 0;
			mutex_exit(&p->p_lock);
			(void) untimeout(tmp_id);
			mutex_enter(&p->p_lock);
		}
		if (timerisset(&aitv.it_value)) {
			uniqtime(&now);
			timevaladd(&aitv.it_value, &now);
			p->p_rprof_timerid = realtime_timeout(realprofexpire,
			    (caddr_t)p, hzto(&aitv.it_value));
		}
		p->p_rprof_timer = aitv;

		/* cancel any outstanding ITIMER_PROF */
		t = p->p_tlist;
		do {
			aitvp = &ttolwp(t)->lwp_timer[ITIMER_PROF];
			timerclear(&aitvp->it_interval);
			timerclear(&aitvp->it_value);
		} while ((t = t->t_forw) != p->p_tlist);

		if (timerisset(&aitv.it_value)) {
			/*
			 * Allocate the SIGPROF buffers, if possible.
			 * Sleeping here could lead to deadlock.
			 */
			t = p->p_tlist;
			do {
				if (t->t_rprof == NULL) {
					t->t_rprof = (struct rprof *)
					    kmem_zalloc(sizeof (struct rprof),
					    KM_NOSLEEP);
					aston(t);	/* for trap/syscall */
				}
			} while ((t = t->t_forw) != p->p_tlist);
		}
		break;
	case ITIMER_VIRTUAL:
		ttolwp(curthread)->lwp_timer[ITIMER_VIRTUAL] = aitv;
		break;
	case ITIMER_PROF:
		/* silently ignore if ITIMER_REALPROF is in effect */
		if (p->p_rprof_timerid == 0)
			ttolwp(curthread)->lwp_timer[ITIMER_PROF] = aitv;
		break;
	}
	mutex_exit(&p->p_lock);
	return (0);
}

/*
 * Real interval timer expired:
 * send process whose timer expired an alarm signal.
 * If time is not set up to reload, then just return.
 * Else compute next time timer should go off which is > current time.
 * This is where delay in processing this timeout causes multiple
 * SIGALRM calls to be compressed into one.
 */
static void
realitexpire(t)
	register kthread_t *t;
{
	register struct proc *p = ttoproc(t);

	mutex_enter(&p->p_lock);
	sigtoproc(p, t, SIGALRM, 0);
	if (!timerisset(&t->t_realitimer.it_interval)) {
		timerclear(&t->t_realitimer.it_value);
		t->t_itimerid = 0;
		mutex_exit(&p->p_lock);
		return;
	}

	for (;;) {
		timevaladd(&t->t_realitimer.it_value,
		    &t->t_realitimer.it_interval);
		/* CSTYLED */
		if (TVTSCMP(&t->t_realitimer.it_value, &hrestime, >)) {
			t->t_itimerid = realtime_timeout(realitexpire,
			    (caddr_t)t, hzto(&t->t_realitimer.it_value));
			mutex_exit(&p->p_lock);
			return;
		}
	}
	/* NOTREACHED */
}

/*
 * The realitsexpire routine is called when the timer expires for timers
 * setup via timer_settime().  If the timer is bound to a specific LWP,
 * it puts the signal on the queue for the LWP that created the timer,
 * otherwise the signal is put on the process queue.
 * If the time is not set up to reload, it returns,
 * else it computes the next time the timer should go off
 * and sets the timeout for that time.
 * The pending bit is reset in the timer_func() that is called when the
 * the signal associated with this timer is delivered.
 */

static void
realitsexpire(timerstr_t *timerp)
{
	kthread_t *t;
	struct proc *p;
	timespec_t now;
	timespec_t *valp, *intervalp;

	if (timerp->trs_flags & TRS_PERLWP) {
		t = lwptot(timerp->trs_lwp);
		p = lwptoproc(timerp->trs_lwp);
	} else {
		t = NULL;
		p = timerp->trs_proc;
	}
	mutex_enter(&p->p_lock);
	if (timerp->trs_flags & TRS_PENDING)
		timerp->trs_overrun1++;
	else if (timerp->trs_flags & TRS_SIGNAL) {
		timerp->trs_flags |= TRS_PENDING;
		sigaddqa(p, t, timerp->trs_sigqp);
	}

	valp = &timerp->trs_itimer.it_value;
	intervalp = &timerp->trs_itimer.it_interval;
	if (!timerspecisset(intervalp)) {
		timerspecclear(valp);
		timerp->trs_callout_id = 0;
	} else {
		for (;;) {
			timespecadd(valp, intervalp);
			gethrestime(&now);
			if (timerspeccmp(valp, &now) > 0) {
				timerp->trs_callout_id =
					realtime_timeout(realitsexpire,
					(caddr_t)timerp,
					timespectohz(valp, now));
				break;
			}
		}
	}
	mutex_exit(&p->p_lock);
}

/*
 * Real time profiling interval timer expired:
 * Increment microstate counters for each lwp in the process
 * and ensure that running lwps are kicked into the kernel.
 * If time is not set up to reload, then just return.
 * Else compute next time timer should go off which is > current time,
 * as above.
 */
static void
realprofexpire(p)
	register struct proc *p;
{
	register kthread_t *t;

	mutex_enter(&p->p_lock);
	if ((t = p->p_tlist) == NULL) {
		mutex_exit(&p->p_lock);
		return;
	}
	do {
		register int mstate;

		/*
		 * Attempt to allocate the SIGPROF buffer, but don't sleep.
		 */
		if (t->t_rprof == NULL)
			t->t_rprof = (struct rprof *)
			    kmem_zalloc(sizeof (struct rprof), KM_NOSLEEP);
		if (t->t_rprof == NULL)
			continue;

		thread_lock(t);
		switch (t->t_state) {
		case TS_SLEEP:
			/*
			 * Don't touch the lwp is it is swapped out.
			 */
			if (!(t->t_schedflag & TS_LOAD)) {
				mstate = LMS_SLEEP;
				break;
			}
			switch (mstate = ttolwp(t)->lwp_mstate.ms_prev) {
			case LMS_TFAULT:
			case LMS_DFAULT:
			case LMS_KFAULT:
			case LMS_USER_LOCK:
				break;
			default:
				mstate = LMS_SLEEP;
				break;
			}
			break;
		case TS_RUN:
			mstate = LMS_WAIT_CPU;
			break;
		case TS_ONPROC:
			switch (mstate = t->t_mstate) {
			case LMS_USER:
			case LMS_SYSTEM:
			case LMS_TRAP:
				break;
			default:
				mstate = LMS_SYSTEM;
				break;
			}
			break;
		default:
			mstate = t->t_mstate;
			break;
		}
		t->t_rprof->rp_anystate = 1;
		t->t_rprof->rp_state[mstate]++;
		aston(t);
		/*
		 * force the thread into the kernel
		 * if it is not already there.
		 */
		if (t->t_state == TS_ONPROC &&
		    t->t_cpu != CPU)
			poke_cpu(t->t_cpu->cpu_id);
		thread_unlock(t);
	} while ((t = t->t_forw) != p->p_tlist);

	if (!timerisset(&p->p_rprof_timer.it_interval)) {
		timerclear(&p->p_rprof_timer.it_value);
		p->p_rprof_timerid = 0;
		mutex_exit(&p->p_lock);
		return;
	}

	for (;;) {
		timevaladd(&p->p_rprof_timer.it_value,
		    &p->p_rprof_timer.it_interval);
		/* CSTYLED */
		if (TVTSCMP(&p->p_rprof_timer.it_value, &hrestime, >)) {
			p->p_rprof_timerid = realtime_timeout(realprofexpire,
			    (caddr_t)p, hzto(&p->p_rprof_timer.it_value));
			mutex_exit(&p->p_lock);
			return;
		}
	}
	/* NOTREACHED */
}

/*
 * Check that a proposed value to load into the .it_value or
 * .it_interval part of an interval timer is acceptable, and
 * fix it to have at least minimal value (i.e. if it is less
 * than the resolution of the clock, round it up.)
 */
int
itimerfix(tv)
	struct timeval *tv;
{
	if (tv->tv_sec < 0 || tv->tv_sec > 100000000 ||
	    tv->tv_usec < 0 || tv->tv_usec >= MICROSEC)
		return (EINVAL);
	if (tv->tv_sec == 0 && tv->tv_usec != 0 && tv->tv_usec < usec_per_tick)
		tv->tv_usec = usec_per_tick;
	return (0);
}

/*
 * Same as itimerfix. This routine takes a timespec instead of a timeval.
 */
int
itimerspecfix(timespec_t *tv)
{
	if (tv->tv_sec < 0 || tv->tv_nsec < 0 || tv->tv_nsec >= NANOSEC)
		return (EINVAL);
	if (tv->tv_sec == 0 && tv->tv_nsec != 0 && tv->tv_nsec < nsec_per_tick)
		tv->tv_nsec = nsec_per_tick;
	return (0);
}

/*
 * Decrement an interval timer by a specified number
 * of microseconds, which must be less than a second,
 * i.e. < 1000000.  If the timer expires, then reload
 * it.  In this case, carry over (usec - old value) to
 * reducint the value reloaded into the timer so that
 * the timer does not drift.  This routine assumes
 * that it is called in a context where the timers
 * on which it is operating cannot change in value.
 */
int
itimerdecr(itp, usec)
	register struct itimerval *itp;
	int usec;
{

	if (itp->it_value.tv_usec < usec) {
		if (itp->it_value.tv_sec == 0) {
			/* expired, and already in next interval */
			usec -= itp->it_value.tv_usec;
			goto expire;
		}
		itp->it_value.tv_usec += MICROSEC;
		itp->it_value.tv_sec--;
	}
	itp->it_value.tv_usec -= usec;
	usec = 0;
	if (timerisset(&itp->it_value))
		return (1);
	/* expired, exactly at end of interval */
expire:
	if (timerisset(&itp->it_interval)) {
		itp->it_value = itp->it_interval;
		itp->it_value.tv_usec -= usec;
		if (itp->it_value.tv_usec < 0) {
			itp->it_value.tv_usec += MICROSEC;
			itp->it_value.tv_sec--;
		}
	} else
		itp->it_value.tv_usec = 0;		/* sec is already 0 */
	return (0);
}

/*
 * Add and subtract routines for timevals.
 * N.B.: subtract routine doesn't deal with
 * results which are before the beginning,
 * it just gets very confused in this case.
 * Caveat emptor.
 */
void
timevaladd(t1, t2)
	struct timeval *t1, *t2;
{

	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	timevalfix(t1);
}

void
timevalsub(t1, t2)
	struct timeval *t1, *t2;
{

	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	timevalfix(t1);
}

void
timevalfix(t1)
	struct timeval *t1;
{
	if (t1->tv_usec < 0) {
		t1->tv_sec--;
		t1->tv_usec += MICROSEC;
	}
	if (t1->tv_usec >= MICROSEC) {
		t1->tv_sec++;
		t1->tv_usec -= MICROSEC;
	}
}

/*
 * Same as the routines above. These routines take a timespec instead
 * of a timeval.
 */
void
timespecadd(timespec_t *t1, timespec_t *t2)
{

	t1->tv_sec += t2->tv_sec;
	t1->tv_nsec += t2->tv_nsec;
	timespecfix(t1);
}

void
timespecsub(timespec_t *t1, timespec_t *t2)
{

	t1->tv_sec -= t2->tv_sec;
	t1->tv_nsec -= t2->tv_nsec;
	timespecfix(t1);
}

void
timespecfix(timespec_t *t1)
{

	if (t1->tv_nsec < 0) {
		t1->tv_sec--;
		t1->tv_nsec += NANOSEC;
	} else {
		if (t1->tv_nsec >= NANOSEC) {
			t1->tv_sec++;
			t1->tv_nsec -= NANOSEC;
		}
	}
}

/*
 * NANOFUDGE is used by timespectohz() to relax its calculation
 * just a little bit; if an integer number of ticks take
 * us to less than NANOFUDGE nanoseconds before the
 * destination time, we call it "enough" and do not wait
 * the extra tick.
 */
#ifndef NANOFUDGE
#define	NANOFUDGE	10000
#endif

/*
 * Compute number of hz until specified time.
 * Used to compute third argument to timeout() from an absolute time.
 */
int
hzto(struct timeval *tv)
{
	timespec_t ts, now;
	int s;

	ts.tv_sec = tv->tv_sec;
	ts.tv_nsec = tv->tv_usec * 1000;
	s = hr_clock_lock();
	now = hrestime;
	hr_clock_unlock(s);
	return (timespectohz(&ts, now));
}

/*
 * Compute number of hz until specified time for a given timespec value.
 * Used to compute third argument to timeout() from an absolute time.
 */
int
timespectohz(timespec_t *tv, timespec_t now)
{
	long ticks, sec, nsec;

	/*
	 * Compute number of ticks we will see between now and
	 * the target time; returns "1" if the destination time
	 * is before the next tick, so we always get some delay,
	 * and returns LONG_MAX ticks if we would overflow.
	 */
	sec = tv->tv_sec - now.tv_sec;
	nsec = tv->tv_nsec - now.tv_nsec + nsec_per_tick - 1;

	if (nsec < 0) {
		sec--;
		nsec += NANOSEC;
	} else if (nsec > NANOSEC) {
		sec++;
		nsec -= NANOSEC;
	}


	ticks = nsec / nsec_per_tick;	/* integer ticks in nsec delay */
	nsec -= ticks * nsec_per_tick;	/* nanoseconds remaining */

	/*
	 * Compute ticks, accounting for negative and overflow as above.
	 * Overflow protection kicks in at about 70 weeks for hz=50
	 * and at about 35 weeks for hz=100.
	 */
	if ((sec < 0) || ((sec == 0) && (ticks < 1)))
		ticks = 1;			/* protect vs nonpositive */
	else if (sec > (LONG_MAX - ticks) / hz)
		ticks = LONG_MAX;		/* protect vs overflow */
	else
		ticks += sec * hz;		/* common case */

	return (ticks);
}

/*
 * hrt2ts(): convert from hrtime_t to timestruc_t.
 *
 * All this routine really does is:
 *
 *	tsp->sec  = hrt / NANOSEC;
 *	tsp->nsec = hrt % NANOSEC;
 *
 * The black magic below avoids doing a 64-bit by 32-bit integer divide,
 * which is quite expensive.  There's actually much more going on here than
 * it might first appear -- don't try this at home.
 *
 * For the adventuresome, here's an explanation of how it works.
 *
 * Multiplication by a fixed constant is easy -- you just do the appropriate
 * shifts and adds.  For example, to multiply by 10, we observe that
 *
 *	x * 10	= x * (8 + 2)
 *		= (x * 8) + (x * 2)
 *		= (x << 3) + (x << 1).
 *
 * In general, you can read the algorithm right off the bits: the number 10
 * is 1010 in binary; bits 1 and 3 are ones, so x * 10 = (x << 1) + (x << 3).
 *
 * Sometimes you can do better.  For example, 15 is 1111 binary, so the normal
 * shift/add computation is x * 15 = (x << 0) + (x << 1) + (x << 2) + (x << 3).
 * But, it's cheaper if you capitalize on the fact that you have a run of ones:
 * 1111 = 10000 - 1, hence x * 15 = (x << 4) - (x << 0).  [You would never
 * actually perform the operation << 0, since it's a no-op; I'm just writing
 * it that way for clarity.]
 *
 * The other way you can win is if you get lucky with the prime factorization
 * of your constant.  The number 1,000,000,000, which we have to multiply
 * by below, is a good example.  One billion is 111011100110101100101000000000
 * in binary.  If you apply the bit-grouping trick, it doesn't buy you very
 * much, because it's only a win for groups of three or more equal bits:
 *
 * 111011100110101100101000000000 = 1000000000000000000000000000000
 *				  -  000100011001010011011000000000
 *
 * Thus, instead of the 13 shift/add pairs (26 operations) implied by the LHS,
 * we have reduced this to 10 shift/add pairs (20 operations) on the RHS.
 * This is better, but not great.
 *
 * However, we can factor 1,000,000,000 = 2^9 * 5^9 = 2^9 * 125 * 125 * 125,
 * and multiply by each factor.  Multiplication by 125 is particularly easy,
 * since 128 is nearby: x * 125 = (x << 7) - x - x - x, which is just four
 * operations.  So, to multiply by 1,000,000,000, we perform three multipli-
 * cations by 125, then << 9, a total of only 3 * 4 + 1 = 13 operations.
 * This is the algorithm we actually use in both hrt2ts() and ts2hrt().
 *
 * Division is harder; there is no equivalent of the simple shift-add algorithm
 * we used for multiplication.  However, we can convert the division problem
 * into a multiplication problem by pre-computing the binary representation
 * of the reciprocal of the divisor.  For the case of interest, we have
 *
 *	1 / 1,000,000,000 = 1.0001001011100000101111101000001B-30,
 *
 * to 32 bits of precision.  (The notation B-30 means "* 2^-30", just like
 * E-18 means "* 10^-18".)
 *
 * So, to compute x / 1,000,000,000, we just multiply x by the 32-bit
 * integer 10001001011100000101111101000001, then normalize (shift) the
 * result.  This constant has several large bits runs, so the multiply
 * is relatively cheap:
 *
 *	10001001011100000101111101000001 = 10001001100000000110000001000001
 *					 - 00000000000100000000000100000000
 *
 * Again, you can just read the algorithm right off the bits:
 *
 *			sec = hrt;
 *			sec += (hrt << 6);
 *			sec -= (hrt << 8);
 *			sec += (hrt << 13);
 *			sec += (hrt << 14);
 *			sec -= (hrt << 20);
 *			sec += (hrt << 23);
 *			sec += (hrt << 24);
 *			sec += (hrt << 27);
 *			sec += (hrt << 31);
 *			sec >>= (32 + 30);
 *
 * Voila!  The only problem is, since hrt is 64 bits, we need to use 96-bit
 * arithmetic to perform this calculation.  That's a waste, because ultimately
 * we only need the highest 32 bits of the result.
 *
 * The first thing we do is to realize that we don't need to use all of hrt
 * in the calculation.  The lowest 30 bits can contribute at most 1 to the
 * quotient (2^30 / 1,000,000,000 = 1.07...), so we'll deal with them later.
 * The highest 2 bits have to be zero, or hrt won't fit in a timestruc_t.
 * Thus, the only bits of hrt that matter for division are bits 30..61.
 * These 32 bits are just the lower-order word of (hrt >> 30).  This brings
 * us down from 96-bit math to 64-bit math, and our algorithm becomes:
 *
 *			tmp = (ulong_t) (hrt >> 30);
 *			sec = tmp;
 *			sec += (tmp << 6);
 *			sec -= (tmp << 8);
 *			sec += (tmp << 13);
 *			sec += (tmp << 14);
 *			sec -= (tmp << 20);
 *			sec += (tmp << 23);
 *			sec += (tmp << 24);
 *			sec += (tmp << 27);
 *			sec += (tmp << 31);
 *			sec >>= 32;
 *
 * Next, we're going to reduce this 64-bit computation to a 32-bit
 * computation.  We begin by rewriting the above algorithm to use relative
 * shifts instead of absolute shifts.  That is, instead of computing
 * tmp << 6, tmp << 8, tmp << 13, etc, we'll just shift incrementally:
 * tmp <<= 6, tmp <<= 2 (== 8 - 6), tmp <<= 5 (== 13 - 8), etc:
 *
 *			tmp = (ulong_t) (hrt >> 30);
 *			sec = tmp;
 *			tmp <<= 6; sec += tmp;
 *			tmp <<= 2; sec -= tmp;
 *			tmp <<= 5; sec += tmp;
 *			tmp <<= 1; sec += tmp;
 *			tmp <<= 6; sec -= tmp;
 *			tmp <<= 3; sec += tmp;
 *			tmp <<= 1; sec += tmp;
 *			tmp <<= 3; sec += tmp;
 *			tmp <<= 4; sec += tmp;
 *			sec >>= 32;
 *
 * Now for the final step.  Instead of throwing away the low 32 bits at
 * the end, we can throw them away as we go, only keeping the high 32 bits
 * of the product at each step.  So, for example, where we now have
 *
 *			tmp <<= 6; sec = sec + tmp;
 * we will instead have
 *			tmp <<= 6; sec = (sec + tmp) >> 6;
 * which is equivalent to
 *			sec = (sec >> 6) + tmp;
 *
 * The final shift ("sec >>= 32") goes away.
 *
 * All we're really doing here is long multiplication, just like we learned in
 * grade school, except that at each step, we only look at the leftmost 32
 * columns.  The cumulative error is, at most, the sum of all the bits we
 * throw away, which is 2^-32 + 2^-31 + ... + 2^-2 + 2^-1 == 1 - 2^-32.
 * Thus, the final result ("sec") is correct to +/- 1.
 *
 * It turns out to be important to keep "sec" positive at each step, because
 * we don't want to have to explicitly extend the sign bit.  Therefore,
 * starting with the last line of code above, each line that would have read
 * "sec = (sec >> n) - tmp" must be changed to "sec = tmp - (sec >> n)", and
 * the operators (+ or -) in all previous lines must be toggled accordingly.
 * Thus, we end up with:
 *
 *			tmp = (ulong_t) (hrt >> 30);
 *			sec = tmp + (sec >> 6);
 *			sec = tmp - (tmp >> 2);
 *			sec = tmp - (sec >> 5);
 *			sec = tmp + (sec >> 1);
 *			sec = tmp - (sec >> 6);
 *			sec = tmp - (sec >> 3);
 *			sec = tmp + (sec >> 1);
 *			sec = tmp + (sec >> 3);
 *			sec = tmp + (sec >> 4);
 *
 * This yields a value for sec that is accurate to +1/-1, so we have two
 * cases to deal with.  The mysterious-looking "+ 7" in the code below biases
 * the rounding toward zero, so that sec is always less than or equal to
 * the correct value.  With this modified code, sec is accurate to +0/-2, with
 * the -2 case being very rare in practice.  With this change, we only have to
 * deal with one case (sec too small) in the cleanup code.
 *
 * The other modification we make is to delete the second line above
 * ("sec = tmp + (sec >> 6);"), since it only has an effect when bit 31 is
 * set, and the cleanup code can handle that rare case.  This reduces the
 * *guaranteed* accuracy of sec to +0/-3, but speeds up the common cases.
 *
 * Finally, we compute nsec = hrt - (sec * 1,000,000,000).  nsec will always
 * be positive (since sec is never too large), and will at most be equal to
 * the error in sec (times 1,000,000,000) plus the low-order 30 bits of hrt.
 * Thus, nsec < 3 * 1,000,000,000 + 2^30, which is less than 2^32, so we can
 * safely assume that nsec fits in 32 bits.  Consequently, when we compute
 * sec * 1,000,000,000, we only need the low 32 bits, so we can just do 32-bit
 * arithmetic and let the high-order bits fall off the end.
 *
 * Since nsec < 3 * 1,000,000,000 + 2^30 == 4,073,741,824, the cleanup loop:
 *
 *			while (nsec >= NANOSEC) {
 *				nsec -= NANOSEC;
 *				sec++;
 *			}
 *
 * is guaranteed to complete in at most 4 iterations.  In practice, the loop
 * completes in 0 or 1 iteration over 95% of the time.
 *
 * On an SS2, this implementation of hrt2ts() takes 1.7 usec, versus about
 * 35 usec for software division -- about 20 times faster.
 */
void
hrt2ts(hrtime_t hrt, timestruc_t *tsp)
{
	u_long sec, nsec, tmp;

	tmp = (u_long) (hrt >> 30);
	sec = tmp - (tmp >> 2);
	sec = tmp - (sec >> 5);
	sec = tmp + (sec >> 1);
	sec = tmp - (sec >> 6) + 7;
	sec = tmp - (sec >> 3);
	sec = tmp + (sec >> 1);
	sec = tmp + (sec >> 3);
	sec = tmp + (sec >> 4);
	tmp = (sec << 7) - sec - sec - sec;
	tmp = (tmp << 7) - tmp - tmp - tmp;
	tmp = (tmp << 7) - tmp - tmp - tmp;
	nsec = (u_long) hrt - (tmp << 9);
	while (nsec >= NANOSEC) {
		nsec -= NANOSEC;
		sec++;
	}
	tsp->tv_sec = sec;
	tsp->tv_nsec = nsec;
}

/*
 * Convert from timestruc_t to hrtime_t.
 *
 * The code below is equivalent to:
 *
 *	hrt = tsp->tv_sec * NANOSEC + tsp->tv_nsec;
 *
 * but requires no integer multiply.
 */
hrtime_t
ts2hrt(timestruc_t *tsp)
{
	hrtime_t hrt;

	hrt = tsp->tv_sec;
	hrt = (hrt << 7) - hrt - hrt - hrt;
	hrt = (hrt << 7) - hrt - hrt - hrt;
	hrt = (hrt << 7) - hrt - hrt - hrt;
	hrt = (hrt << 9) + tsp->tv_nsec;
	return (hrt);
}

struct clocka {
	clockid_t clock_id;
	timespec_t *tp;
};

/*
 * The clock_settime() system call sets the clock to the specified time tp.
 * It adjusts the specified time by the nanosecs that have elapsed since
 * the last clock tick happened.
 */
int
clock_settime(clockid_t clock_id, timespec_t *tp)
{
	timespec_t tspec;
	int s;

	if (suser(CRED())) {
		switch (clock_id) {
		case __CLOCK_REALTIME0:
		case __CLOCK_REALTIME3:
			break;
		default:
			return (set_errno(EINVAL));
		}
		if (copyin((caddr_t)tp, (caddr_t)&tspec, sizeof (timespec_t)))
			return (set_errno(EFAULT));
		if (itimerspecfix(&tspec))
			return (set_errno(EINVAL));

		mutex_enter(&tod_lock);
		tod_set(tspec);
		s = hr_clock_lock();
		hrestime = tspec;
		timedelta = 0;
		hr_clock_unlock(s);
		mutex_exit(&tod_lock);
		return (0);
	} else
		return (set_errno(EPERM));
}

/*
 * The clock_gettime() should never be called for clock_id == CLOCK_REALTIME.
 * For that case, the library should call the gethrestime() trap directly.
 */
int
clock_gettime(clockid_t clock_id, timespec_t *tp)
{
	timespec_t tspec;

	switch (clock_id) {
	case __CLOCK_REALTIME0:
	case __CLOCK_REALTIME3:
		gethrestime(&tspec);
		if (copyout((caddr_t)&tspec, (caddr_t)tp, sizeof (timespec_t)))
			return (set_errno(EFAULT));
		break;
	case CLOCK_VIRTUAL:
	case CLOCK_PROF:
	default:
		return (set_errno(EINVAL));
	}
	return (0);
}

/*
 * Return the resolution of the various clocks.
 * For CLOCK_REALTIME, the clock resolution value is stored in
 * clock_res variable in `arch`/io/hardclk.c.
 */

int
clock_getres(clockid_t clock_id, timespec_t *tp)
{
	timespec_t tspec;

	if (tp != NULL) {
		switch (clock_id) {
		case __CLOCK_REALTIME0:
		case __CLOCK_REALTIME3:
			tspec.tv_sec = 0;
			tspec.tv_nsec = clock_res;
			if (copyout((caddr_t)&tspec, (caddr_t)tp,
						sizeof (timespec_t)))
				return (set_errno(EFAULT));
			break;
		case CLOCK_VIRTUAL:
		case CLOCK_PROF:
		default:
			return (set_errno(EINVAL));
		}
	}
	return (0);
}

/*
 * The timer_func() function is called when the signal delivery happens.
 * Instead of freeing the sigqueue, this function is called. The timer_func
 * function resets the pending bit and transfers the overrun cnt from
 * overrun1 to overrun2. The value of overrun2 is what the user sees via
 * getoverrun().
 */

void
timer_func(sigqueue_t *sigqp)
{
	timerstr_t *timerp;

	/*
	 * There are two ways to do this. One is to scan the entire list
	 * of timers and figure out which timer this sigq is/was associated
	 * with. Another is to have a back pointer in sigqueue struct. For
	 * performance reasons, latter approach is taken.
	 */
	timerp = (timerstr_t *)sigqp->sq_backptr;
	if (timerp != NULL) {
		timerp->trs_overrun2 = timerp->trs_overrun1;
		timerp->trs_overrun1 = 0;
		timerp->trs_flags &= ~TRS_PENDING;
	}
	else
		panic("timer_func: timer pointer is NULL.");
}

struct tcreatea {
	clockid_t	clock_id;
	struct sigevent	*evp;
	timer_t		*timerid;
};

struct oldsigevent {
	/* structure definition prior to notification attributes member */
	int		_notify;
	int		_signo;
	union sigval	_value;
};

/*
 * The timer_create() system call is called to create a timer struct. For
 * performance reasons, we allocate _TIMER_MAX structs for the LWP when
 * the timer_create() call is made the first time. After allocating the
 * timer and sigqueue structs the user info is copied in.
 */
/* ARGSUSED1 */
int
timer_create(uap, rvp)
	register struct tcreatea *uap;
	rval_t	*rvp;
{
	struct		sigevent evp;
	register	sigqueue_t *sigqp = NULL;
	register	struct cred *cr = CRED();
	int		i;
	int		timerid;
	register	proc_t *p = curproc;
	klwp_t		*cur_lwp = ttolwp(curthread);
	register	timerstr_t *timerp;
	int		signot;

	/* XXX This should be changed later to account for other clocks */
	switch (uap->clock_id) {
	case __CLOCK_REALTIME0:
	case __CLOCK_REALTIME3:
		break;
	default:
		return (EINVAL);
	}

	if (uap->evp != NULL) {
		/*
		 * short copyin() for binary compatibility
		 * fetch oldsigevent to determine how much to copy in.
		 */
		if (copyin((caddr_t)uap->evp, (caddr_t)&evp,
		    sizeof (struct oldsigevent)))
			return (EFAULT);
		switch (evp.sigev_notify) {
		case SIGEV_NONE:
		case SIGEV_SIGNAL:
			/* got it all already */
			break;
		default:
			/* will need to do a full copyin() for new cases */
			return (EINVAL);
		}
	} else {
		switch (uap->clock_id) {
		case __CLOCK_REALTIME0:
		case __CLOCK_REALTIME3:
			evp.sigev_signo = SIGALRM;
			break;
		case CLOCK_VIRTUAL:
			evp.sigev_signo = SIGVTALRM;
			break;
		case CLOCK_PROF:
			evp.sigev_signo = SIGPROF;
			break;
		}
		evp.sigev_notify = SIGEV_SIGNAL;
		evp.sigev_value.sival_ptr = NULL;
	}

	mutex_enter(&p->p_lock);
	while (p->p_itimer == NULL) {
		mutex_exit(&p->p_lock);
		timerp = kmem_zalloc(_TIMER_MAX * sizeof (timerstr_t),
			KM_SLEEP);
		for (i = 0; i < _TIMER_MAX; i++) {
			sigqp = kmem_zalloc(sizeof (sigqueue_t), KM_SLEEP);
			timerp[i].trs_sigqp = sigqp;
		}
		mutex_enter(&p->p_lock);
		if (p->p_itimer != NULL) {
			for (i = 0; i < _TIMER_MAX; i++) {
				kmem_free(timerp[i].trs_sigqp,
				    sizeof (sigqueue_t));
			}
			kmem_free(timerp, _TIMER_MAX * sizeof (timerstr_t));
		} else
			p->p_itimer = timerp;
	}

	for (timerid = -1, i = 0, timerp = p->p_itimer;
	    i < _TIMER_MAX; i++, timerp++) {
		if (!(timerp->trs_flags & TRS_INUSE)) {
			timerid = i;
			timerp->trs_flags |= TRS_INUSE;
			break;
		}
	}

	if (timerid < 0) {
		mutex_exit(&p->p_lock);
		return (EAGAIN);
	}

	sigqp = timerp->trs_sigqp;
	ASSERT(sigqp != NULL);

	sigqp->sq_info.si_signo = evp.sigev_signo;
	sigqp->sq_info.si_value = evp.sigev_value;
	sigqp->sq_info.si_code = SI_TIMER;
	sigqp->sq_info.si_pid = ttoproc(curthread)->p_pid;
	sigqp->sq_info.si_uid = cr->cr_ruid;
	sigqp->sq_func = timer_func;
	sigqp->sq_next = NULL;
	timerp->trs_flags &= ~(TRS_PENDING | TRS_SIGNAL);
	if (evp.sigev_notify == SIGEV_SIGNAL)
		timerp->trs_flags |= TRS_SIGNAL;
	timerp->trs_clock_id = uap->clock_id;
	switch (uap->clock_id) {
	case __CLOCK_REALTIME0:
		timerp->trs_flags |= TRS_PERLWP;
		timerp->trs_lwp = cur_lwp;
		break;
	case __CLOCK_REALTIME3:
		timerp->trs_flags &= ~TRS_PERLWP;
		timerp->trs_proc = p;
		break;
	}
	sigqp->sq_backptr = (void *)timerp;
	mutex_exit(&p->p_lock);
	if (copyout((caddr_t)&timerid, (caddr_t)uap->timerid,
	    sizeof (timer_t))) {
		mutex_enter(&p->p_lock);
		timerp->trs_flags &= ~TRS_INUSE;
		mutex_exit(&p->p_lock);
		return (EFAULT);
	}
	return (0);
}

/*
 * The timer_exit() is called when the process is about to exit.
 * The resources (i.e. the timer structs and the sigqueues)
 * attached to the process are freed.
 */

void
timer_exit(void)
{
	int	i, tid;
	timerstr_t	*itimerp;
	register struct proc	*p = ttoproc(curthread);

	mutex_enter(&p->p_lock);
	for (i = 0, itimerp = p->p_itimer; i < _TIMER_MAX; i++, itimerp++) {
		if (itimerp->trs_flags & TRS_INUSE) {
			/*
			 * If this timer is not on CLOCK_REALTIME,
			 * we will have to do this differently
			 */
			if (timerspecisset(&itimerp->trs_itimer.it_value)) {
				/* if this timer was armed */
				switch (itimerp->trs_clock_id) {
				case __CLOCK_REALTIME0:
				case __CLOCK_REALTIME3:
					while (itimerp->trs_callout_id > 0) {
						tid = itimerp->trs_callout_id;
						itimerp->trs_callout_id = 0;
						mutex_exit(&p->p_lock);
						(void) untimeout(tid);
						mutex_enter(&p->p_lock);
					}
					break;
				default:
					break;
				}
			}
		}
		/* cleanup the timer struct, sigqueue is given away or freed */
		if (itimerp->trs_flags & TRS_PENDING) {
			/* signal pending, give the siginfo structure away */
			itimerp->trs_sigqp->sq_func = NULL;
		} else
			kmem_free(itimerp->trs_sigqp, sizeof (sigqueue_t));
	}
	kmem_free(p->p_itimer, _TIMER_MAX * sizeof (timerstr_t));
	p->p_itimer = NULL;
	mutex_exit(&p->p_lock);
}

/*
 * The timer_lwpexit() is called when the LWP is about to exit.
 * The timers attached to the LWP are freed.
 */
void
timer_lwpexit(void)
{
	int	i, tid;
	timerstr_t	*itimerp;
	klwp_t		*lwp = ttolwp(curthread);
	struct proc	*p = ttoproc(curthread);

	mutex_enter(&p->p_lock);
	for (i = 0, itimerp = p->p_itimer; i < _TIMER_MAX; i++, itimerp++) {
		if ((itimerp->trs_flags & (TRS_INUSE | TRS_PERLWP)) ==
		    (TRS_INUSE | TRS_PERLWP) && itimerp->trs_lwp == lwp) {
			(void) timer_del_cleanup(itimerp);
		}
	}
	mutex_exit(&p->p_lock);
}

struct tdeletea {
	timer_t timerid;
};

/*
 * The timer_delete() system call deletes a particular timer struct.
 * The resources attached with the timer are zeroed and not freed.
 */

/* ARGSUSED1 */
int
timer_delete(uap, rvp)
	register struct tdeletea *uap;
	rval_t  *rvp;
{
	register proc_t	*p = curproc;
	timer_t		timerid = uap->timerid;
	timerstr_t	*itimerp;
	int		retval = 0;

	mutex_enter(&p->p_lock);
	if ((timerid < 0) || (timerid >= _TIMER_MAX) ||
	    (p->p_itimer == NULL) ||
	    !(p->p_itimer[timerid].trs_flags & TRS_INUSE)) {
		mutex_exit(&p->p_lock);
		return (EINVAL);
	}

	itimerp = &p->p_itimer[timerid];
	retval = timer_del_cleanup(itimerp);

	mutex_exit(&p->p_lock);
	return (retval);
}

/*
 * Deactivate and release a timer.
 * If the signal is pending, a new sigqueue is allocated and attached
 * and the old one is marked to be freed via kmem_free().
 */
static int
timer_del_cleanup(timerstr_t *itimerp)
{
	sigqueue_t	*sqp;
	int		tmp_id;
	struct proc	*p = curproc;

	ASSERT(MUTEX_HELD(&p->p_lock));
	/*
	 * If this timer is not on CLOCK_REALTIME,
	 * we will have to do this differently
	 */
	if (timerspecisset(&itimerp->trs_itimer.it_value)) {
		/* if this timer was armed */
		switch (itimerp->trs_clock_id) {
		case __CLOCK_REALTIME0:
		case __CLOCK_REALTIME3:
			while ((tmp_id = itimerp->trs_callout_id) > 0) {
				itimerp->trs_callout_id = 0;
				mutex_exit(&p->p_lock);
				(void) untimeout(tmp_id);
				mutex_enter(&p->p_lock);
			}
			break;
		default:
			return (EINVAL);
		}
	}
	/* cleanup the timer struct */
	if (itimerp->trs_flags & TRS_PENDING) {
		/* signal pending, give the current siginfo structure away */
		itimerp->trs_sigqp->sq_func = NULL;
		sqp = (sigqueue_t *)kmem_zalloc(sizeof (sigqueue_t), KM_SLEEP);
	} else {
		sqp = itimerp->trs_sigqp;
		struct_zero((caddr_t)itimerp->trs_sigqp, sizeof (sigqueue_t));
	}
	struct_zero((caddr_t)itimerp, sizeof (timerstr_t));
	itimerp->trs_sigqp = sqp;
	return (0);
}

struct tgettimea {
	timer_t	timerid;
	struct itimerspec *value;
};

/*
 * The timer_gettime() system call stores the itimer values for the
 * timer specified by the timerid.
 */

/* ARGSUSED1 */
int
timer_gettime(uap, rvp)
	register struct tgettimea *uap;
	rval_t  *rvp;
{
	itimerspec_t		aits;
	register struct proc	*p = curproc;
	timespec_t		now;
	register timerstr_t	*timerp;
	timer_t			timerid = uap->timerid;
	int			error = 0;

	mutex_enter(&p->p_lock);
	if ((timerid < 0) || (timerid >= _TIMER_MAX) ||
	    (p->p_itimer == NULL) ||
	    !(p->p_itimer[timerid].trs_flags & TRS_INUSE)) {
		mutex_exit(&p->p_lock);
		return (EINVAL);
	}

	timerp = &p->p_itimer[timerid];
	aits = timerp->trs_itimer;
	switch (timerp->trs_clock_id) {
	case __CLOCK_REALTIME0:
	case __CLOCK_REALTIME3:
		gethrestime(&now);
		if (timerspecisset(&aits.it_value)) {
			if (timerspeccmp(&aits.it_value, &now) < 0)
				timerspecclear(&aits.it_value);
			else
				timespecsub(&aits.it_value, &now);
		}
		break;
	case CLOCK_VIRTUAL:
	case CLOCK_PROF:
	default:
		error = EINVAL;
	}

	mutex_exit(&p->p_lock);
	if (!error) {
		if (copyout((caddr_t)&aits, (caddr_t)uap->value,
			sizeof (struct itimerspec)))
			error = EFAULT;
	}
	return (error);
}

struct tsettimea {
	timer_t	timerid;
	int	flags;
	struct itimerspec *value;
	struct itimerspec *ovalue;
};

/*
 * The timer_settime() system call sets the itimer values for the specified
 * timerid.  If the ovalue is non-NULL, it calls the timer_gettime() to
 * return the values.
 */

int
timer_settime(uap, rvp)
	register struct tsettimea *uap;
	rval_t  *rvp;
{
	itimerspec_t		aits, *aitsp;
	register struct proc	*p = curproc;
	register timerstr_t	*itimerp;
	timespec_t		now;
	struct tgettimea	gap;
	int			tmp_id;
	timer_t			timerid = uap->timerid;
	int			error = 0;

	aitsp = uap->value;
	if (uap->ovalue) {
		gap.timerid = uap->timerid;
		gap.value = uap->ovalue;
		if (timer_gettime(&gap, rvp))
			return (EFAULT);
	}
	if (aitsp == 0)
		return (0);
	if (copyin((caddr_t)aitsp, (caddr_t)&aits, sizeof (itimerspec_t)))
		return (EFAULT);

	if (itimerspecfix(&aits.it_value) ||
			(itimerspecfix(&aits.it_interval) &&
				timerspecisset(&aits.it_value))) {
		return (EINVAL);
	}

	/*
	 * Lets get a time stamp now so that it is closer to the time
	 * when the user made the call. Use this time in timespectohz()
	 * to keep the expiration time close to the user's expected time.
	 */
	gethrestime(&now);
	mutex_enter(&p->p_lock);
	if ((timerid < 0) || (timerid >= _TIMER_MAX) ||
	    (p->p_itimer == NULL) ||
	    !(p->p_itimer[timerid].trs_flags & TRS_INUSE)) {
		mutex_exit(&p->p_lock);
		return (EINVAL);
	}

	itimerp = &p->p_itimer[uap->timerid];
	switch (itimerp->trs_clock_id) {
	case __CLOCK_REALTIME0:
	case __CLOCK_REALTIME3:
		while ((tmp_id = itimerp->trs_callout_id) > 0) {
			itimerp->trs_callout_id = 0;
			mutex_exit(&p->p_lock);
			(void) untimeout(tmp_id);
			mutex_enter(&p->p_lock);
		}
		if (timerspecisset(&aits.it_value)) {
			if (!(uap->flags & TIMER_ABSTIME)) {
				timespecadd(&aits.it_value, &now);
			}
			itimerp->trs_callout_id =
				realtime_timeout(realitsexpire,
				(caddr_t)itimerp, timespectohz(&aits.it_value,
							now));
		}
		itimerp->trs_itimer = aits;
		break;
	case CLOCK_VIRTUAL:
	case CLOCK_PROF:
	default:
		error = EINVAL;
	}
	mutex_exit(&p->p_lock);
	return (error);
}

struct tgetoverruna {
	timer_t timerid;
};

/*
 * This returns the overrun count attached with the timer.
 */

int
timer_getoverrun(uap, rvp)
	register struct tgetoverruna *uap;
	rval_t *rvp;
{
	register struct proc	*p = curproc;
	timer_t			timerid = uap->timerid;
	int			error = 0;

	mutex_enter(&p->p_lock);
	if ((timerid < 0) || (timerid >= _TIMER_MAX) ||
	    (p->p_itimer == NULL) ||
	    !(p->p_itimer[timerid].trs_flags & TRS_INUSE)) {
		error = EINVAL;
	}
	if (!error)
		rvp->r_val1 = p->p_itimer[uap->timerid].trs_overrun2;
	mutex_exit(&p->p_lock);
	return (error);
}

int
nanosleep(timespec_t *rqtp, timespec_t *rmtp)
{
	proc_t *p = curproc;
	timespec_t rqtime, rmtime, now;
	long nano_time = 0;
	int ret = 1;
	long rtime;

	if (copyin((caddr_t)rqtp, (caddr_t)&rqtime, sizeof (timespec_t)))
		return (set_errno(EFAULT));
	if (itimerspecfix(&rqtime))
		return (set_errno(EINVAL));

	if (timerspecisset(&rqtime)) {
		gethrestime(&now);
		timespecadd(&rqtime, &now);
		/*
		 * The posix spec requires that we sleep for an inteval
		 * greater than the specifed time.  Since we cannot
		 * predict when the clock interrupt will occur with
		 * respect to the hrestime, we end up checking after
		 * we wake up to make sure that we've slept long enough.
		 */
		while ((rqtime.tv_sec > now.tv_sec) ||
			((rqtime.tv_sec == now.tv_sec) &&
			(rqtime.tv_nsec > now.tv_nsec))) {
			timespec_t wait_time;
			/*
			 *  since we can skew up to 1/16 lbolt rate if
			 *  adj time is going crazy, reduce difference
			 *  since timeout is in clock ticks rather than posix
			 *  realtime, which is really wallclock elapsed time.
			 */
			wait_time = rqtime;
			timespecsub(&wait_time, &now);
			wait_time.tv_sec -= wait_time.tv_sec >> 4;
			wait_time.tv_nsec -= wait_time.tv_nsec >> 4;
			timespecadd(&wait_time, &now);
			nano_time = timespectohz(&wait_time, now);

			/*
			 * Make sure nano_time + lbolt isn't negative
			 */

			if (nano_time < (LONG_MAX - lbolt))
				nano_time += lbolt;

			mutex_enter(&p->p_lock);
			ret = cv_timedwait_sig(&u.u_cv, &p->p_lock, nano_time);
			mutex_exit(&p->p_lock);
			if (ret == 0) {
				break;
			}
			gethrestime(&now);
		}
	}
	if (rmtp) {
		/*
		 * If cv_timedwait_sig() returned due to a signal, and
		 * there is time remaining then set the time remaining.
		 * Else set time remaining to zero
		 */
		if (ret == 0) {
			gethrestime(&now);
			if ((now.tv_sec < rqtime.tv_sec) ||
				((now.tv_sec == rqtime.tv_sec) &&
				(now.tv_nsec < rqtime.tv_nsec))) {
				rmtime = rqtime;
				timespecsub(&rmtime, &now);
			} else {
				rmtime.tv_sec = rmtime.tv_nsec = 0;
			}
		}
		if (copyout((caddr_t)&rmtime, (caddr_t)rmtp,
		    sizeof (timespec_t)))
			return (set_errno(EFAULT));
	}

	if (ret == 0)
		return (set_errno(EINTR));

	return (0);
}

/*
 * Routines to convert standard UNIX time (seconds since Jan 1, 1970)
 * into year/month/day/hour/minute/second format, and back again.
 * Note: these routines require tod_lock held to protect cached state.
 */
static int days_thru_month[64] = {
	0, 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366, 0, 0,
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 0,
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 0,
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 0,
};

todinfo_t saved_tod;
int saved_utc = -60;

todinfo_t
utc_to_tod(time_t utc)
{
	int dse, day, month, year;
	todinfo_t tod;

	ASSERT(MUTEX_HELD(&tod_lock));

	if (utc < 0)			/* should never happen */
		utc = 0;

	saved_tod.tod_sec += utc - saved_utc;
	saved_utc = utc;
	if (saved_tod.tod_sec >= 0 && saved_tod.tod_sec < 60)
		return (saved_tod);	/* only the seconds changed */

	dse = utc / 86400;		/* days since epoch */

	tod.tod_sec = utc % 60;
	tod.tod_min = (utc % 3600) / 60;
	tod.tod_hour = (utc % 86400) / 3600;
	tod.tod_dow = (dse + 4) % 7 + 1;	/* epoch was a Thursday */

	year = dse / 365 + 72;	/* first guess -- always a bit too large */
	do {
		year--;
		day = dse - 365 * (year - 70) - ((year - 69) >> 2);
	} while (day < 0);

	month = ((year & 3) << 4) + 1;
	while (day >= days_thru_month[month + 1])
		month++;

	tod.tod_day = day - days_thru_month[month] + 1;
	tod.tod_month = month & 15;
	tod.tod_year = year;

	saved_tod = tod;
	return (tod);
}

time_t
tod_to_utc(todinfo_t tod)
{
	time_t utc;
	int year = tod.tod_year;
	int month = tod.tod_month + ((year & 3) << 4);

	ASSERT(MUTEX_HELD(&tod_lock));

#ifdef DEBUG
	if (tod.tod_year < 70 || tod.tod_year > 138 ||
	    tod.tod_month < 1 || tod.tod_month > 12 ||
	    tod.tod_day < 1 ||
	    tod.tod_day > days_thru_month[month + 1] - days_thru_month[month] ||
	    tod.tod_hour < 0 || tod.tod_hour > 23 ||
	    tod.tod_min < 0 || tod.tod_min > 59 ||
	    tod.tod_sec < 0 || tod.tod_sec > 59)
		return (0);		/* should never happen */
#endif

	utc = (year - 70);		/* next 3 lines: utc = 365y + y/4 */
	utc += (utc << 3) + (utc << 6);
	utc += (utc << 2) + ((year - 69) >> 2);
	utc += days_thru_month[month] + tod.tod_day - 1;
	utc = (utc << 3) + (utc << 4) + tod.tod_hour;	/* 24 * day + hour */
	utc = (utc << 6) - (utc << 2) + tod.tod_min;	/* 60 * hour + min */
	utc = (utc << 6) - (utc << 2) + tod.tod_sec;	/* 60 * min + sec */

	return (utc);
}
