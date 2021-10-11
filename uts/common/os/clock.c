/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma ident   "@(#)clock.c 1.97     95/07/05 SMI"

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/types.h>
#include <sys/tuneable.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/callo.h>
#include <sys/kmem.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/map.h>
#include <sys/swap.h>
#include <sys/vmsystm.h>
#include <sys/class.h>
#include <sys/time.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/spl.h>

#include <vm/anon.h>
#include <vm/rm.h>

extern kmutex_t	delay_lock;

/*
 * clock is called straight from
 * the real time clock interrupt.
 *
 * Functions:
 *	reprime clock
 *	schedule callouts
 *	maintain date
 *	jab the scheduler
 */

extern kcondvar_t	fsflush_cv;
extern int	desfree;
extern int	idleswtch;	/* flag set while idle in pswtch() */
extern int	swapfs_minfree;

extern int (*io_poll[])();	/* driver entry points to poll every tick */

time_t	time;	/* time in seconds since 1970 - for compatibility only */

/* The following variables require no explicit locking */
clock_t	lbolt;		/* time in HZ since last boot */
kcondvar_t lbolt_cv;
int one_sec = 1; /* turned on once every second */
static int fsflushcnt;	/* counter for t_fsflushr */
int	clock_pend;	/* clock pending from the interrupt handler */
lock_t	clock_lock;	/* protects clock_pend */
static int clock_reruns;	 /* how often clock() repeated tick */
int	dosynctodr = 1;	/* patchable; enable/disable sync to TOD chip */
int	tod_needsync = 0;	/* need to sync tod chip with software time */
static int tod_broken = 0;	/* clock chip doesn't work */

/*
 * Similar to dump_timeout, sync_timeout is reset to SYNC_TIMEOUT
 * during a panic, while the sync is progressing. This value is
 * equal to the timeout value for a single scsi poll command.
 */
#define	SYNC_TIMEOUT	(60 * (HZ))

static ulong prev_nbuf = 0;	/* prev number of bwrites by panic_cpu */
static int prev_npg = 0;	/* prev number of busy pages in system */
extern kthread_id_t panic_thread;
extern int page_busy();

#ifdef DEBUG
int	catchmenowcnt;		/* counter for debuging interrupt */
int	catchmestart = 60;	/* counter for debuging interrupt */
int	idlecntdown;
int	idlemsg;
#endif

static void clock_tick(kthread_id_t);

extern kmutex_t cleanup_lock;
extern kcondvar_t cleanup_cv;

#ifdef	KSLICE
int kslice = KSLICE;
#endif
extern int hr_clock_lock();
extern void hr_clock_unlock();

#if defined(GPROF) && defined(i386)
extern void (*kprof_tick)();
#endif

void
clock(void)
{
	extern int	sync_timeout;
	extern int	dump_timeout;
	extern char	*panicstr;
	extern cpu_t	*cpu_list;
	kthread_id_t	t;
#ifdef XENIX_COMPAT
	register int (**pollptr)();	/* pointer to next poll point */
#endif /* XENIX_COMPAT */
	int	pinned_intr;
	int	waiting;
	int	nrunnable, nrunning;
	long w_io, w_swap;
	cpu_t	*cp;
	cpu_sysinfo_t *sysinfop;
	proc_t	*p;
	int	exiting;
	extern void set_anoninfo();
	extern	void	set_freemem();

	set_anoninfo();
	/*
	 * Make sure that 'freemem' do not drift too far from the truth
	 */
	set_freemem();
	/*
	 * If we're panicing, return after checking for a panic timeout.
	 */
	if (panicstr) {
		lbolt++;
		if (sync_timeout) {
			if (--sync_timeout == 0)
				panic("panic sync timeout");

			/*
			 * Allow the sync to continue, while the panic_thread's
			 * cpu is issuing bwrite()'s or the number of busy pages
			 * is decreasing (checked once a second).
			 */
			if (!(lbolt % HZ)) {
				int s;
				cp = panic_thread->t_cpu;
				sysinfop = &(cp->cpu_stat.cpu_sysinfo);

				if (sysinfop->bwrite > prev_nbuf) {
					sync_timeout = SYNC_TIMEOUT;
					prev_nbuf = sysinfop->bwrite;
				} else if ((s = page_busy()) < prev_npg) {
					sync_timeout = SYNC_TIMEOUT;
					prev_npg = s;
				}
			}
		}
		if (dump_timeout && (--dump_timeout == 0))
			panic("panic dump timeout");
		return;
	}

	/*
	 * Did we pin another interrupt thread?
	 */
	pinned_intr = (curthread->t_intr->t_flag & T_INTR_THREAD);

tick_again:

	/*
	 * Count the number of threads waiting for some form
	 * of I/O to complete -- gets added to sysinfo.waiting
	 * To know the state of the system, must add wait counts from
	 * all CPUs.
	 */
	w_io = w_swap = 0;
	nrunnable = 0;
	cp = cpu_list;
	do {
		w_io += cp->cpu_stat.cpu_syswait.iowait;
		w_swap += cp->cpu_stat.cpu_syswait.swap;
		nrunnable += cp->cpu_disp.disp_nrunnable;
	} while ((cp = cp->cpu_next) != cpu_list);
	waiting = w_io + w_swap;

	/*
	 * Do tick processing for all the active threads running in
	 * the system.
	 * pidlock (above) prevents lwp's from disappearing (exiting).
	 * Could defer getting pidlock until now, but that might cause us
	 * to block for awhile, invalidating all the wait states.
	 */
	cp = cpu_list;
	nrunning = 0;
	do {
		klwp_id_t lwp;

		/*
		 * Don't do any tick processing on CPUs that
		 * aren't even in the system or aren't up yet.
		 */
		if ((cp->cpu_flags & CPU_EXISTS) == 0) {
			continue;
		}
		mutex_enter(&pidlock);
		t = cp->cpu_thread;		/* Current running thread */
		if (CPU == cp) {
			/*
			 * 't' will be the clock interrupt thread on this
			 * CPU.
			 * Use the base lwp (if any) on this CPU
			 * (if it was running when the clock interrupt
			 * happened) as the target of the clock tick.
			 */
			lwp = cp->cpu_lwp;	/* Base lwp (if any) */
			if (lwp && !pinned_intr)
				t = lwptot(lwp);
		} else {
			lwp = ttolwp(t);
		}

		p = ttoproc(t);
		mutex_enter(&p->p_lock);
		if (lwp == NULL || (t->t_proc_flag & TP_LWPEXIT)) {
			/*
			 * Thread is exiting so don't do tick processing.
			 * Need to look at t_proc_flag while holding both
			 * pidlock and p_lock.
			 */
			exiting = 1;
		} else {
			exiting = 0;
		}
		mutex_exit(&pidlock);

		/*
		 * Update user, system, and idle cpu times.
		 */
		sysinfop = &cp->cpu_stat.cpu_sysinfo;

		if (cp->cpu_flags & CPU_QUIESCED) {
			sysinfop->cpu[CPU_IDLE]++;
		} else if (cp->cpu_on_intr ||
		    (!exiting && t->t_intr != NULL &&
		    cp->cpu_thread != curthread)) {
			nrunning++;
			sysinfop->cpu[CPU_KERNEL]++;
		} else if (t == curthread && pinned_intr) {
			nrunning++;
			sysinfop->cpu[CPU_KERNEL]++;
		} else if (cp->cpu_dispthread == cp->cpu_idle_thread) {
			if (waiting) {
				/*
				 * Add to the wait times for the CPU.
				 * XXX sysinfo wait times should be
				 * XXX system-wide, not per-CPU.
				 */
				if (w_io)
					sysinfop->wait[W_IO]++;
				if (w_swap)
					sysinfop->wait[W_SWAP]++;
				sysinfop->cpu[CPU_WAIT]++;
			} else {
				sysinfop->cpu[CPU_IDLE]++;
			}
		} else if (exiting) {
			nrunning++;
			sysinfop->cpu[CPU_KERNEL]++;
		} else {
			nrunning++;
			if (lwp->lwp_state == LWP_USER)
				sysinfop->cpu[CPU_USER]++;
			else
				sysinfop->cpu[CPU_KERNEL]++;
			/*
			 * If the current thread running on the CPU is not
			 * an interrupt thread then do tick processing for
			 * it.  We already know it's not exiting.
			 */
			if (!(t->t_flag & T_INTR_THREAD)) {
				clock_t ticks;

				/*
				 * If we haven't done tick processing for this
				 * lwp, then do it now. Since we don't hold the
				 * lwp down on a CPU it can migrate and show up
				 * more than once, hence the lbolt check.
				 * XXX what if LWP is swapped out?
				 */
				if ((ticks = lbolt - t->t_lbolt) != 0) {
					u_short pct = t->t_pctcpu;

					if (--ticks != 0)
						pct = cpu_decay(pct, ticks);
					t->t_pctcpu = cpu_grow(pct, 1);
					t->t_lbolt = lbolt;
					clock_tick(t);
				}
			}
		}

#ifdef KSLICE
		/*
		 * Ah what the heck, give this kid a taste of the real
		 * world and yank the rug out from under it.
		 * But, only if we are running UniProcessor.
		 */
		if ((kslice) && (ncpus == 1)) {
			aston(t);
			cp->cpu_runrun = 1;
			cp->cpu_kprunrun = 1;
		}
#endif
		mutex_exit(&p->p_lock);
	} while ((cp = cp->cpu_next) != cpu_list);

	/*
	 * bump time in ticks
	 *
	 * We rely on there being only one clock thread and hence
	 * don't need a lock to proctect lbolt.
	 */
	lbolt++;

#ifdef XENIX_COMPAT
	/*
	 * XENIX Compatibility Change:
	 *  Call the device driver entries for poll on clock ticks,
	 *  if there are any.  This table (io_poll) is created by
	 *  "cunix" for drivers that contain a "poll" routine.
	 */
	for (pollptr = &io_poll[0];  *pollptr;  pollptr++)
		(**pollptr)();
#endif /* XENIX_COMPAT */

#if defined(GPROF) && defined(i386)
	(*kprof_tick)();
#endif

	/*
	 * Schedule timeout() requests if any are due at this time.
	 */
	callout_schedule(&rt_callout_state);
	callout_schedule(&callout_state);

	if (one_sec) {

		int drift, absdrift;
		timestruc_t tod;

		mutex_enter(&tod_lock);
		tod = tod_get();
		drift = tod.tv_sec - hrestime.tv_sec;
		absdrift = (drift > 0) ? drift : -drift;
		if (tod_needsync || absdrift > 1) {
			int s;
			if (absdrift > 2) {
				if (!tod_broken) {
					s = hr_clock_lock();
					hrestime = tod;
					timedelta = 0;
					tod_needsync = 0;
					hr_clock_unlock(s);
				}
			} else {
				if (tod_needsync || !dosynctodr) {
					gethrestime(&tod);
					tod_set(tod);
					s = hr_clock_lock();
					if (timedelta == 0)
						tod_needsync = 0;
					hr_clock_unlock(s);
				} else {
					s = hr_clock_lock();
					timedelta = (longlong_t)drift * NANOSEC;
					hr_clock_unlock(s);
				}
			}
		}
		one_sec = 0;
		time = hrestime.tv_sec;	/* for crusty old kmem readers */
		mutex_exit(&tod_lock);

		/*
		 * Some drivers still depend on this... XXX
		 */
		wakeup((caddr_t)&lbolt);
		cv_broadcast(&lbolt_cv);

		/*
		 * Make whirl look decent.
		 */
		if (proc_init == (proc_t *)0)
			cmn_err(CE_CONT, "?");
#ifdef DEBUG
		if (idlemsg && --idlecntdown == 0)
			cmn_err(CE_WARN, "System is idle\n");
#endif
		sysinfo.updates++;
		vminfo.freemem += freemem;
		{
			u_long maxswap, resv, free;

			maxswap = MAX(anoninfo.ani_resv, anoninfo.ani_max) +
				(availrmem - swapfs_minfree);
			free = anoninfo.ani_free +
				(availrmem - swapfs_minfree);
			resv = anoninfo.ani_resv;

			vminfo.swap_resv += resv;
			vminfo.swap_alloc += maxswap - free;
			vminfo.swap_avail += maxswap - resv;
			vminfo.swap_free += free;
		}
		if (nrunnable > 0) {
			sysinfo.runque += nrunnable;
			sysinfo.runocc++;
		}
		if (nswapped) {
			sysinfo.swpque += nswapped;
			sysinfo.swpocc++;
		}
		sysinfo.waiting += waiting;
#ifdef DEBUG
		/*
		 * call this routine at regular intervals
		 * to allow debugging.
		 */
		if (--catchmenowcnt <= 0) {
			/* XXX: declare this in some header file */
			extern void catchmenow(void);

			catchmenowcnt = catchmestart;
			catchmenow();
		}
#endif

		/*
		 * Wake up fsflush to write out DELWRI
		 * buffers, dirty pages and other cached
		 * administrative data, e.g. inodes.
		 */
		if (--fsflushcnt <= 0) {
			fsflushcnt = tune.t_fsflushr;
			cv_signal(&fsflush_cv);
		}

		vmmeter(nrunnable + nrunning);

		/*
		 * Wake up the swapper thread if necessary.
		 */
		if (runin ||
		    (runout && (avefree < desfree || wake_sched_sec))) {
			t = &t0;
			thread_lock(t);
			if (t->t_state == TS_STOPPED) {
				runin = runout = 0;
				wake_sched_sec = 0;
				t->t_whystop = 0;
				t->t_whatstop = 0;
				t->t_schedflag &= ~TS_ALLSTART;
				THREAD_TRANSITION(t);
				setfrontdq(t);
			}
			thread_unlock(t);
		}
	}

	/*
	 * Wake up the swapper if any high priority swapped-out threads
	 * became runable during the last tick.
	 */
	if (wake_sched) {
		t = &t0;
		thread_lock(t);
		if (t->t_state == TS_STOPPED) {
			runin = runout = 0;
			wake_sched = 0;
			t->t_whystop = 0;
			t->t_whatstop = 0;
			t->t_schedflag &= ~TS_ALLSTART;
			THREAD_TRANSITION(t);
			setfrontdq(t);
		}
		thread_unlock(t);
	}

	/*
	 * If another hardware clock interrupt happenned during our processing,
	 * repeat everything except charging the pinned-thread tick.
	 * It isn't necessary to set clock_lock for the initial inspection of
	 * clock_pend.  If it gets set just after it is checked, the extra
	 * time will be caught on the next tick.
	 *
	 * NOTE:  lock_set_spl() must be used since the priority will not
	 * be at LOCK_LEVEL if the clock thread blocked trying to acquire
	 * some mutex.  This is due to the fact that thread_unpin() doesn't
	 * set "intr_actv" or "priority for the clock interrupt.
	 */
	if (clock_pend > 0) {
		int s = lock_set_spl(&clock_lock, ipltospl(LOCK_LEVEL));
		clock_reruns++;
		clock_pend--;
		lock_clear_splx(&clock_lock, s);
		goto tick_again;
	}
}

/*
 * Handle clock tick processing for a thread.
 * Check for timer action, enforce CPU rlimit, do profiling etc.
 */
void
clock_tick(t)
	kthread_id_t t;
{
	register struct proc *pp;
	register struct user *up;
	register klwp_id_t    lwp;
	register rlim_t rlim_cur;
	struct as *as;
	clock_t	utime;
	clock_t	stime;
	int	poke = 0;		/* notify another CPU */
	int	user_mode;

	/* XXX: declare this in some header file */
	extern int itimerdecr(struct itimerval *, int);


	/* Must be operating on a lwp/thread */
	if ((lwp = ttolwp(t)) == NULL)
		cmn_err(CE_PANIC, "clock_tick: no lwp");

	CL_TICK(t);	/* Class specific tick processing */

	pp = ttoproc(t);

	/* pp->p_lock makes sure that the thread does not exit */
	ASSERT(MUTEX_HELD(&pp->p_lock));

	user_mode = (lwp->lwp_state == LWP_USER);

	/*
	 * Update process times. Should use high res clock and state
	 * changes instead of statistical sampling method. XXX
	 */
	if (user_mode) {
		pp->p_utime++;
		lwp->lwp_utime++;
	} else {
		pp->p_stime++;
		lwp->lwp_stime++;
	}
	up = PTOU(pp);
	as = pp->p_as;
	/*
	 * Update user profiling statistics. Get the pc from the
	 * lwp when the AST happens.
	 */
	if (user_mode && lwp->lwp_prof.pr_scale & ~1) {
		lwp->lwp_oweupc = 1;
		poke = 1;
		aston(t);
	}
	utime = pp->p_utime;
	stime = pp->p_stime;
	/*
	 * If CPU was in user state, process lwp-virtual time
	 * interval timer.
	 */
	if (user_mode &&
	    timerisset(&lwp->lwp_timer[ITIMER_VIRTUAL].it_value) &&
	    itimerdecr(&lwp->lwp_timer[ITIMER_VIRTUAL], usec_per_tick) == 0) {
		poke = 1;
		sigtoproc(pp, t, SIGVTALRM, 0);
	}

	if (timerisset(&lwp->lwp_timer[ITIMER_PROF].it_value) &&
	    itimerdecr(&lwp->lwp_timer[ITIMER_PROF], usec_per_tick) == 0) {
		poke = 1;
		sigtoproc(pp, t, SIGPROF, 0);
	}

	/*
	 * Enforce CPU rlimit.
	 */
	rlim_cur = up->u_rlimit[RLIMIT_CPU].rlim_cur;
	if ((rlim_cur != RLIM_INFINITY) &&
	    ((utime/HZ) + (stime/HZ) > rlim_cur)) {
		poke = 1;
		sigtoproc(pp, NULL, SIGXCPU, 0);
	}

	/*
	 * Update memory usage for the currently running process.
	 */
	up->u_mem += rm_asrss(as);
	/*
	 * Notify the CPU the thread is running on.
	 */
	if (poke && t->t_cpu != CPU)
		poke_cpu(t->t_cpu->cpu_id);
}

static void
delay_wakeup(t)
	kthread_id_t	t;
{
	mutex_enter(&delay_lock);
	cv_signal(&t->t_delay_cv);
	mutex_exit(&delay_lock);
}

void
delay(ticks)
	long ticks;
{
	kthread_id_t	t = curthread;
	kmutex_t	*mp = NULL;

	if (ticks <= 0)
		return;
	if (UNSAFE_DRIVER_LOCK_HELD()) {
		mp = &unsafe_driver;
		mutex_exit(mp);
	}
	mutex_enter(&delay_lock);
	(void) timeout(delay_wakeup, (caddr_t)t, ticks);
	(void) cv_wait(&t->t_delay_cv, &delay_lock);
	mutex_exit(&delay_lock);
	if (mp != NULL)
		mutex_enter(mp);
}

/*
 * Initialize the system time, based on the time base which is, e.g. from
 * a filesystem.  A base of -1 means the file system doesn't keep time.
 */
void
clkset(time_t base)
{
	long deltat;
	timestruc_t ts;
	int tod_init = 1;
	int s;

	mutex_enter(&tod_lock);
	ts = tod_get();
	if (ts.tv_sec < 365 * 86400) {
		tod_init = 0;
		if (base == -1)
			ts.tv_sec = (87 - 70) * 365 * 86400;	/* ~1987 */
		else
			ts.tv_sec = base;
		ts.tv_nsec = 0;
	}
	tod_set(ts);
	s = hr_clock_lock();
	hrestime = ts;
	timedelta = 0;
	hr_clock_unlock(s);
	ts = tod_get();
	mutex_exit(&tod_lock);

	if (ts.tv_sec == 0) {
		printf("WARNING: unable to read TOD clock chip");
		dosynctodr = 0;
		tod_broken = 1;
		goto check;
	}

	if (!tod_init) {
		printf("WARNING: TOD clock not initialized");
		goto check;
	}

	if (base == -1)
		return;

	if (base < (87 - 70) * 365 * 86400) {			/* ~1987 */
		printf("WARNING: preposterous time in file system");
		goto check;
	}

	deltat = ts.tv_sec - base;
	/*
	 * See if we gained/lost two or more days;
	 * if so, assume something is amiss.
	 */
	if (deltat < 0)
		deltat = -deltat;
	if (deltat < 2 * 86400)
		return;
	printf("WARNING: clock %s %d days",
	    ts.tv_sec < base ? "lost" : "gained", deltat / 86400);

check:
	printf(" -- CHECK AND RESET THE DATE!\n");
}

/*
 * The following is for computing the percentage of cpu time used recently
 * by an lwp.  The function cpu_decay() is also called from /proc code.
 *
 * exp_x(x):
 * Given x as a 32-bit non-negative scaled integer,
 * Return exp(-x) as a 32-bit scaled integer [0 .. 1].
 *
 * Scaling:
 * The binary point is to the right of the high-order
 * bit of the low-order 16-bit half word.
 */

#define	ESHIFT	15
#define	SSI_ONE	((u_short)1 << ESHIFT)	/* short scaled integer 1 */
#define	LSI_ONE	((long)1 << ESHIFT)	/* long scaled integer 1 */

#ifdef DEBUG
u_long expx_cnt = 0;	/* number of calls to exp_x() */
u_long expx_mul = 0;	/* number of long multiplies in exp_x() */
#endif

static long
exp_x(register long x)
{
	register int i;
	register u_long ul;

#ifdef DEBUG
	expx_cnt++;
#endif
	/*
	 * Defensive programming:
	 * If x is negative, assume that it is really zero
	 * and return the value 1, scaled.
	 */
	if (x <= 0)
		return (LSI_ONE);

	/*
	 * By the formula:
	 *	exp(-x) = exp(-x/2) * exp(-x/2)
	 * we keep halving x until it becomes small enough for
	 * the following approximation to be accurate enough:
	 *	exp(-x) = 1 - x
	 * We reduce x until it is less than 1/4 (the 2 in ESHIFT-2 below).
	 * Our final error will be smaller than 4% .
	 */

	/*
	 * Use a u_long for the shift calculation.
	 */
	ul = x >> (ESHIFT-2);

	/*
	 * Short circuit:
	 * A number this large produces effectively 0 (actually .005).
	 * This way, we will never do more than 5 multiplies.
	 */
	if (ul >= (1 << 5))
		return (0);

	for (i = 0; ul != 0; i++)
		ul >>= 1;
	if (i != 0) {
#ifdef DEBUG
		expx_mul += i;	/* almost never happens */
#endif
		x >>= i;
	}

	/*
	 * Now we compute 1 - x and square it the number of times
	 * that we halved x above to produce the final result:
	 */
	x = LSI_ONE - x;
	while (i--)
		x = (x * x) >> ESHIFT;

	return (x);
}

/*
 * Given the old percent cpu and a time delta in clock ticks,
 * return the new decayed percent cpu:  pct * exp(-tau),
 * where 'tau' is the time delta multiplied by a decay factor.
 * We have chosen the decay factor to make the decay over five
 * seconds be approximately 20%.
 *
 * 'pct' is a 16-bit scaled integer <= 1 (see above)
 */
u_short
cpu_decay(u_short pct, clock_t ticks)
{
	long delta;

	/* avoid overflow on really big values of 'ticks' */
	if ((unsigned)ticks > 0xfffff)
		return (0);

	/* normalize over different system value of HZ */
	delta = (ticks * (25600 / HZ)) >> 4;

	return (((long)pct * (long)exp_x(delta)) >> ESHIFT);
}

/*
 * Given the old percent cpu and a time delta in clock ticks,
 * return the new grown percent cpu:  1 - ( 1 - pct ) * exp(-tau)
 */
u_short
cpu_grow(u_short pct, clock_t ticks)
{
	return (SSI_ONE - cpu_decay(SSI_ONE - pct, ticks));
}
