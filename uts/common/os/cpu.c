/*
 * Copyright (c) 1991, 1992, 1993, 1994 Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)cpu.c	1.59	95/08/22 SMI"

/*
 * Architecture-independent CPU control functions.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/var.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#include <sys/kstat.h>
#include <sys/uadmin.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/procset.h>
#include <sys/processor.h>
#include <sys/debug.h>

#ifdef MP
extern int	mp_cpu_start(cpu_t *);
extern int	mp_cpu_stop(cpu_t *);

#if bug1139151
extern void return_instr(void);
#endif

static void cpu_add_active_internal(cpu_t *cp);
static void cpu_remove_active(cpu_t *cp);
static void cpu_stat_kstat_create(cpu_t *cp);
static void cpu_info_kstat_create(cpu_t *cp);
#else  /* not MP */
/*
 * Stubs for some architectural interface routines on non-MP machines.
 */
#define	mp_cpu_start(x)		0
#define	mp_cpu_stop(x)		EBUSY
#define	cpu_enable_intr(x)
#define	cpu_disable_intr(x)	0
#endif /* MP */

cpu_t		*cpu_list;		/* list of all CPUs */
cpu_t		*cpu_active;		/* list of active CPUs */
static u_int	cpu_available;		/* set of available CPUs */

/*
 * max_ncpus keeps the max cpus the system can have. Initially
 * it's NCPU, but since most archs scan the devtree for cpus
 * fairly early on during boot, the real max can be known before
 * ncpus is set (useful for early NCPU based allocations).
 */
int max_ncpus = NCPU;

#ifdef	MP

/*
 * values for safe_list.  Pause state that CPUs are in.
 */
#define	PAUSE_IDLE	0		/* normal state */
#define	PAUSE_READY	1		/* paused thread ready to spl */
#define	PAUSE_WAIT	2		/* paused thread is spl-ed high */

/*
 * Variables used in pause_cpus().
 */
static volatile char safe_list[NCPU];

static struct _cpu_pause_info {
	int		cp_spl;		/* spl saved in pause_cpus() */
	volatile int	cp_go;		/* Go signal sent after all ready */
	int		cp_count;	/* # of CPUs to pause */
	ksema_t		cp_sem;		/* synch pause_cpus & cpu_pause */
} cpu_pause_info;

#endif	/* MP */

/*
 * Set affinity for a specified CPU.
 * A reference count is incremented and the affinity is held until the
 * reference count is decremented to zero by affinity_clear().  This is
 * so regions of code requiring affinity can be nested.
 * No locking is required since only the current thread is affected.
 */
void
affinity_set(int cpu_id)
{
	register kthread_id_t t = curthread;
	cpu_t		*cp;

	ASSERT(cpu_id < NCPU);
	cp = cpu[cpu_id];
	ASSERT(cp != NULL);		/* user must provide a good cpu_id */
	/*
	 * If there is already a hard affinity requested, and this affinity
	 * conflicts with that, panic.
	 */
	thread_lock(t);
	if (t->t_affinitycnt > 0 && t->t_bound_cpu != cp) {
		cmn_err(CE_PANIC,
		    "affinity_set: setting %x but already bound to %x\n",
		    (int)cp, (int)t->t_bound_cpu);
	}
	t->t_affinitycnt++;
	t->t_bound_cpu = cp;

	/*
	 * Make sure we're running on the right CPU.
	 */
	if (cp != t->t_cpu) {
		THREAD_TRANSITION(curthread);
		CL_SETRUN(curthread);
		thread_unlock_nopreempt(curthread);
		swtch();
	} else {
		thread_unlock(curthread);
	}
}

/*
 * Decrement the affinity reservation count and if it becomes zero,
 * clear the CPU affinity for the current thread, or set it to the user's
 * software binding request.
 */
void
affinity_clear(void)
{
	kthread_id_t t = curthread;
	register processorid_t binding;

	if (--t->t_affinitycnt == 0) {
		thread_lock(t);
		if ((binding = t->t_bind_cpu) == PBIND_NONE) {
			t->t_bound_cpu = NULL;
		} else {
			t->t_bound_cpu = cpu[binding];
			/*
			 * Make sure the thread is running on the bound CPU.
			 */
			if (CPU != t->t_bound_cpu) {
				THREAD_TRANSITION(curthread);
				CL_SETRUN(curthread);
				thread_unlock_nopreempt(curthread);
				swtch();
				return;		/* already dropped lock */
			}
		}
		thread_unlock(t);
	}
}

#ifdef MP
/*
 * This routine is called to place the CPUs in a safe place so that
 * one of them can be taken off line or placed on line.  What we are
 * trying to do here is prevent a thread from traversing the list
 * of active CPUs while we are changing it or from getting placed on
 * the run queue of a CPU that has just gone off line.  We do this by
 * creating a thread with the highest possible prio for each CPU and
 * having it call this routine.  The advantage of this method is that
 * we can eliminate all checks for CPU_ACTIVE in the disp routines.
 * This makes disp faster at the expense of making p_online() slower
 * which is a good trade off.
 */
static void
cpu_pause(volatile char *safe)
{
	int s;
	struct _cpu_pause_info *cpi = &cpu_pause_info;

	ASSERT(curthread->t_bound_cpu != NULL);

	for (;;) {
		*safe = PAUSE_READY;
		lock_mutex_flush();	/* make sure stores are flushed */
		sema_v(&cpi->cp_sem);	/* signal requesting thread */

		/*
		 * Wait here until all pause threads are running.  That
		 * indicates that it's safe to do the spl.  Until
		 * cpu_pause_info.cp_go is set, we don't want to spl
		 * because that might block clock interrupts needed
		 * to preempt threads on other CPUs.
		 */
		while (cpi->cp_go == 0)
#if bug1139151
			return_instr()
#endif
			;
		/*
		 * Even though we are at the highest disp prio, we need
		 * to block out all interrupts below LOCK_LEVEL so that
		 * an intr doesn't come in, wake up a thread, and call
		 * setbackdq/setfrontdq.
		 */
		s = splhigh();
		/*
		 * This cpu is now safe.
		 */
		*safe = PAUSE_WAIT;
		lock_mutex_flush();	/* make sure stores are flushed */

		/*
		 * Now we wait.  When we are allowed to continue, safe will
		 * be set to PAUSE_IDLE.
		 */
		while (*safe != PAUSE_IDLE)
#if bug1139151
			return_instr()
#endif
			;
		(void) splx(s);
		/*
		 * Waiting is at an end. Switch out of cpu_pause
		 * loop and resume useful work.
		 */
		swtch();
	}
}

/*
 * Allow the cpus to start running again.
 */
void
start_cpus()
{
	int i;

	ASSERT(MUTEX_HELD(&cpu_lock));
	for (i = 0; i < NCPU; i++)
		safe_list[i] = PAUSE_IDLE;
	lock_mutex_flush();		/* make sure stores are flushed */
	affinity_clear();
	(void) splx(cpu_pause_info.cp_spl);
	kpreempt_enable();
}

/*
 * Allocate a pause thread for a CPU.
 */
static void
cpu_pause_alloc(cpu_t *cp)
{
	kthread_id_t	t;
	int		cpun = cp->cpu_id;

	/*
	 * Note, v.v_nglobpris will not change value as long as I hold
	 * cpu_lock.
	 */
	t = thread_create(NULL, PAGESIZE, cpu_pause, (caddr_t)&safe_list[cpun],
				0, &p0, TS_STOPPED, v.v_nglobpris - 1);
	if (t == NULL)
		cmn_err(CE_PANIC, "Cannot allocate CPU pause thread");
	thread_lock(t);
	t->t_bound_cpu = cp;
	t->t_disp_queue = &cp->cpu_disp;
	t->t_affinitycnt = 1;
	t->t_preempt = 1;
	thread_unlock(t);
	cp->cpu_pause_thread = t;
}

/*
 * Initialize basic structures for pausing CPUs.
 */
void
cpu_pause_init()
{
	sema_init(&cpu_pause_info.cp_sem, 0, "pause cpus", SEMA_DEFAULT, NULL);
	/*
	 * Create initial CPU pause thread.
	 */
	cpu_pause_alloc(CPU);
}

/*
 * Start the threads used to pause another CPU.
 */
static int
cpu_pause_start(processorid_t cpu_id)
{
	int	i;
	int	cpu_count = 0;

	for (i = 0; i < NCPU; i++) {
		cpu_t		*cp;
		kthread_id_t	t;

		cp = cpu[i];
		if (!CPU_IN_SET(cpu_available, i) || (i == cpu_id)) {
			safe_list[i] = PAUSE_WAIT;
			continue;
		}

		/*
		 * Skip CPU if it is quiesced or not yet started.
		 */
		if ((cp->cpu_flags & (CPU_QUIESCED | CPU_READY)) != CPU_READY) {
			safe_list[i] = PAUSE_WAIT;
			continue;
		}

		/*
		 * Start this CPU's pause thread.
		 */
		t = cp->cpu_pause_thread;
		thread_lock(t);
		/*
		 * Reset the priority, since nglobpris may have
		 * changed since the thread was created, if someone
		 * has loaded the RT (or some other) scheduling
		 * class.
		 */
		t->t_pri = v.v_nglobpris - 1;
		THREAD_TRANSITION(t);
		setbackdq(t);
		thread_unlock_nopreempt(t);
		++cpu_count;
	}
	return (cpu_count);
}


/*
 * Pause all of the CPUs except the one we are on by creating a high
 * priority thread bound to those CPUs.
 */
int
pause_cpus(cpu_t *off_cp)
{
	processorid_t	cpu_id;
	int		i;
	struct _cpu_pause_info	*cpi = &cpu_pause_info;

	ASSERT(MUTEX_HELD(&cpu_lock));
	cpi->cp_count = 0;
	cpi->cp_go = 0;
	for (i = 0; i < NCPU; i++)
		safe_list[i] = PAUSE_IDLE;
	kpreempt_disable();

	/*
	 * If running on the cpu that is going offline, get off it.
	 * This is so that it won't be necessary to rechoose a CPU
	 * when done.
	 */
	if (CPU == off_cp)
		cpu_id = off_cp->cpu_next_onln->cpu_id;
	else
		cpu_id = CPU->cpu_id;
	affinity_set(cpu_id);

	/*
	 * Start the pause threads and record how many were started
	 */
	cpi->cp_count = cpu_pause_start(cpu_id);

	/*
	 * Now wait for all CPUs to be running the pause thread.
	 */
	while (cpi->cp_count > 0) {
		sema_p(&cpi->cp_sem);
		--cpi->cp_count;
	}
	cpi->cp_go = 1;			/* all have reached cpu_pause */

	/*
	 * Now wait for all CPUs to spl. (Transition from PAUSE_READY
	 * to PAUSE_WAIT.)
	 */
	for (i = 0; i < NCPU; i++) {
		while (safe_list[i] != PAUSE_WAIT)
#if bug1139151
			return_instr()
#endif
			;
	}
	cpi->cp_spl = splhigh();	/* block dispatcher on this CPU */
	return (1);
}
#endif


/*
 * Check whether cpun is a valid processor id. If it is,
 * return a pointer to the associated CPU structure.
 */
cpu_t *
cpu_get(processorid_t cpun)
{
	if (cpun >= NCPU || cpun < 0 || !CPU_IN_SET(cpu_available, cpun))
		return (NULL);
	return (cpu[cpun]);
}

/*
 * Check offline/online status for the indicated
 * CPU.
 */
int
cpu_status(cpu_t *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	if ((cp->cpu_flags & (CPU_READY | CPU_OFFLINE)) != CPU_READY)
		return (P_OFFLINE);
	else
		return (P_ONLINE);
}

/*
 * Bring the indicated CPU online.
 */
int
cpu_online(cpu_t *cp)
{
	int	error = 0;

	ASSERT(MUTEX_HELD(&cpu_lock));

#ifdef	MP
	/*
	 * Put all the cpus into a known safe place.
	 * No mutexes can be entered while CPUs are paused.
	 */
	(void) pause_cpus(NULL);
	error = mp_cpu_start(cp);	/* arch-dep hook */
	if (error == 0) {
		cpu_add_active_internal(cp);
		cp->cpu_flags &= ~(CPU_QUIESCED | CPU_OFFLINE);
		start_cpus();
		cpu_enable_intr(cp);	/* arch-dep hook */
	} else {
		start_cpus();
	}
	cpu_stat_kstat_create(cp);
#endif	/* MP */
	return (error);
}

/*
 * Take the indicated CPU offline.
 */
int
cpu_offline(cpu_t *cp)
{
	cpu_t	*ncp;
	int	intr_enable;
	int	loop_count;
	int	no_quiesce = 0;
	kthread_t *t;
	int	error = 0;

	/*
	 * Handle off-line request.
	 */

	ASSERT(MUTEX_HELD(&cpu_lock));

	/* don't turn off last online CPU */
	if (ncpus_online <= 1 || curthread->t_bound_cpu == cp) {
		return (EBUSY);
	}
#ifdef	MP

	/*
	 * Take the CPU out of interrupt participation so we won't find
	 * bound kernel threads.  If the architecture cannot completely
	 * shut off interrupts on the CPU, don't quiesce it, but don't
	 * run anything but interrupt thread .. this is indicated by
	 * the CPU_OFFLINE flag being on but the CPU_QUIESCE flag being
	 * off.
	 */
	intr_enable = cp->cpu_flags & CPU_ENABLE;
	if (intr_enable)
		no_quiesce = cpu_disable_intr(cp);

	/*
	 * Check for kernel threads bound to that CPU.
	 * Inactive interrupt threads are OK (they'll be in TS_FREE
	 * state).  If test finds some bound threads, wait a few ticks
	 * to give short-lived threads (such as interrupts) chance to
	 * complete.
	 */
	for (loop_count = 0; disp_bound_threads(cp); loop_count++) {
		if (loop_count >= 5) {
			error = EBUSY;	/* some threads still bound */
			break;
		}

		/*
		 * If some threads were assigned, give them
		 * a chance to complete or move.
		 *
		 * This assumes that the clock_thread is not bound
		 * to any CPU, because the clock_thread is needed to
		 * do the delay(1).
		 *
		 * Note: we still hold the cpu_lock while waiting for
		 * the next clock tick.  This is OK since it isn't
		 * needed for anything else except processor_bind(2),
		 * and system initialization.  If we drop the lock,
		 * we would risk another p_online disabling the last
		 * processor.
		 */
		delay(1);
	}

	/*
	 * Call mp_cpu_stop() to perform any special operations
	 * needed for this machine architecture to offline a CPU.
	 */
	if (error == 0)
		error = mp_cpu_stop(cp);	/* arch-dep hook */

	/*
	 * If that all worked, take the CPU offline and decrement
	 * ncpus_online.
	 */
	if (error == 0) {
		/*
		 * Put all the cpus into a known safe place.
		 * No mutexes can be entered while CPUs are paused.
		 */
		(void) pause_cpus(cp);
		ncp = cp->cpu_next_onln;
		/*
		 * Remove the CPU from the list of active CPUs.
		 */
		cpu_remove_active(cp);
		/*
		 * Update any thread that has loose affinity
		 * for the cpu so that it won't end up being placed
		 * on the offline cpu's runqueue.
		 */
		for (t = curthread->t_next; t != curthread; t = t->t_next) {
			ASSERT(t != NULL);
			if (t->t_cpu == cp && t->t_bound_cpu != cp)
				t->t_cpu = disp_lowpri_cpu(ncp);
			ASSERT(t->t_cpu != cp || t->t_bound_cpu == cp);
		}
		ASSERT(curthread->t_cpu != cp);
		cp->cpu_flags |= CPU_OFFLINE;
		disp_cpu_inactive(cp);
		if (!no_quiesce)
			cp->cpu_flags |= CPU_QUIESCED;
		ncpus_online--;
		cp->cpu_type_info.pi_state = P_OFFLINE;
		cp->cpu_state_begin = hrestime.tv_sec;
		start_cpus();
		/*
		 * Remove this CPU's kstat.
		 */
		if (cp->cpu_kstat != NULL) {
			kstat_delete(cp->cpu_kstat);
			cp->cpu_kstat = NULL;
		}
	}

	/*
	 * If we failed, re-enable interrupts.
	 * Do this even if cpu_disable_intr returned an error, because
	 * it may have partially disabled interrupts.
	 */
	if (error && intr_enable)
		cpu_enable_intr(cp);
#endif	/* MP */
	return (error);
}

/*
 * Initialize the CPU lists for the first CPU.
 */
void
cpu_list_init(cpu_t *cp)
{
	cp->cpu_next = cp;
	cp->cpu_prev = cp;
	cpu_list = cp;

	cp->cpu_next_onln = cp;
	cp->cpu_prev_onln = cp;
	cpu_active = cp;

	cp->cpu_seqid = 0;
	CPUSET_ADD(cpu_available, cp->cpu_id);
}

#ifdef MP
/*
 * Insert a CPU into the list of available CPUs.
 */
void
cpu_add_unit(cpu_t *cp)
{
	extern void disp_cpu_init(cpu_t *);	/* XXX  - disp.h */

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(cpu_list != NULL);	/* list started in cpu_list_init */

	disp_cpu_init(cp);
	cp->cpu_next = cpu_list;
	cp->cpu_prev = cpu_list->cpu_prev;
	cpu_list->cpu_prev->cpu_next = cp;
	cpu_list->cpu_prev = cp;
	cp->cpu_seqid = ncpus++;
	cpu[cp->cpu_id] = cp;
	CPUSET_ADD(cpu_available, cp->cpu_id);

	/*
	 * allocate a pause thread for this CPU.
	 */
	cpu_pause_alloc(cp);

	/*
	 * So that new CPUs won't have NULL prev_onln and next_onln pointers,
	 * link them into a list of just that CPU.
	 * This is so that disp_lowpri_cpu will work for thread_create in
	 * pause_cpus() when called from the startup thread in a new CPU.
	 */
	cp->cpu_next_onln = cp;
	cp->cpu_prev_onln = cp;
	cpu_info_kstat_create(cp);
}

/*
 * Add a CPU to the list of active CPUs.
 *	This routine must not get any locks, because other CPUs are paused.
 */
static void
cpu_add_active_internal(cpu_t *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(cpu_list != NULL);	/* list started in cpu_list_init */

	ncpus_online++;
	cp->cpu_type_info.pi_state = P_ONLINE;
	cp->cpu_state_begin = hrestime.tv_sec;
	cp->cpu_next_onln = cpu_active;
	cp->cpu_prev_onln = cpu_active->cpu_prev_onln;
	cpu_active->cpu_prev_onln->cpu_next_onln = cp;
	cpu_active->cpu_prev_onln = cp;
}

/*
 * Add a CPU to the list of active CPUs.
 *	This is called from machine-dependent layers when a new CPU is started.
 */
void
cpu_add_active(cpu_t *cp)
{
	(void) pause_cpus(NULL);
	cpu_add_active_internal(cp);
	start_cpus();
	cpu_stat_kstat_create(cp);
}


/*
 * Remove a CPU from the list of active CPUs.
 *	This routine must not get any locks, because other CPUs are paused.
 */
/* ARGSUSED */
static void
cpu_remove_active(cpu_t *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(cp->cpu_next_onln != cp);	/* not the last one */
	ASSERT(cp->cpu_prev_onln != cp);	/* not the last one */

	cp->cpu_prev_onln->cpu_next_onln = cp->cpu_next_onln;
	cp->cpu_next_onln->cpu_prev_onln = cp->cpu_prev_onln;
	if (cpu_active == cp) {
		cpu_active = cp->cpu_next_onln;
	}
	cp->cpu_next_onln = cp;
	cp->cpu_prev_onln = cp;
}

#endif /* MP */

/*
 * Export this CPU's statistics via the kstat mechanism.
 * This is done when a CPU is initialized or placed online via p_online(2).
 */
static void
cpu_stat_kstat_create(cpu_t *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	cp->cpu_kstat = kstat_create("cpu_stat", cp->cpu_id, NULL,
	    "misc", KSTAT_TYPE_RAW, sizeof (cpu_stat_t), KSTAT_FLAG_VIRTUAL);
	if (cp->cpu_kstat != NULL) {
		cp->cpu_kstat->ks_data = (void *)&cp->cpu_stat;
		kstat_install(cp->cpu_kstat);
	}
}

/*
 * Export information about this CPU via the kstat mechanism.
 */
static struct {
	kstat_named_t ci_state;
	kstat_named_t ci_state_begin;
	kstat_named_t ci_cpu_type;
	kstat_named_t ci_fpu_type;
	kstat_named_t ci_clock_MHz;
} cpu_info_template = {
	{ "state",		KSTAT_DATA_CHAR },
	{ "state_begin",	KSTAT_DATA_LONG },
	{ "cpu_type",		KSTAT_DATA_CHAR },
	{ "fpu_type",		KSTAT_DATA_CHAR },
	{ "clock_MHz",		KSTAT_DATA_LONG },
};

static int
cpu_info_kstat_update(kstat_t *ksp, int rw)
{
	cpu_t *cp = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	strcpy(cpu_info_template.ci_state.value.c,
	    cp->cpu_type_info.pi_state == P_ONLINE ? "on-line" : "off-line");
	cpu_info_template.ci_state_begin.value.l = cp->cpu_state_begin;
	strncpy(cpu_info_template.ci_cpu_type.value.c,
	    cp->cpu_type_info.pi_processor_type, 15);
	strncpy(cpu_info_template.ci_fpu_type.value.c,
	    cp->cpu_type_info.pi_fputypes, 15);
	cpu_info_template.ci_clock_MHz.value.l = cp->cpu_type_info.pi_clock;
	return (0);
}

static void
cpu_info_kstat_create(cpu_t *cp)
{
	kstat_t *ksp;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ksp = kstat_create("cpu_info", cp->cpu_id, NULL,
		"misc", KSTAT_TYPE_NAMED,
		sizeof (cpu_info_template) / sizeof (kstat_named_t),
		KSTAT_FLAG_VIRTUAL);
	if (ksp != NULL) {
		ksp->ks_data = &cpu_info_template;
		ksp->ks_private = cp;
		ksp->ks_update = cpu_info_kstat_update;
		kstat_install(ksp);
	}
}

void
cpu_kstat_init(cpu_t *cp)
{
	mutex_enter(&cpu_lock);
	cpu_stat_kstat_create(cp);
	cpu_info_kstat_create(cp);
	cp->cpu_type_info.pi_state = P_ONLINE;
	cp->cpu_state_begin = hrestime.tv_sec;
	mutex_exit(&cpu_lock);
}

/*
 * Bind a thread to a CPU as requested.
 */
int
cpu_bind_thread(kthread_id_t tp, struct bind_arg *arg)
{
	processorid_t	binding;

	ASSERT(MUTEX_HELD(&ttoproc(tp)->p_lock));

	thread_lock(tp);

	/*
	 * Record old binding, but change the arg, which was initialized
	 * to PBIND_NONE, only if this thread has a binding.  This avoids
	 * reporting PBIND_NONE for a process when some LWPs are bound.
	 */
	binding = tp->t_bind_cpu;
	if (binding != PBIND_NONE)
		arg->obind = binding;	/* record old binding */

	if (arg->bind != PBIND_QUERY) {
		/*
		 * If this thread/LWP cannot be bound because of permission
		 * problems, just note that and return success so that the
		 * other threads/LWPs will be bound.  This is the way
		 * processor_bind() is defined to work.
		 *
		 * Binding will get EPERM if the thread is of system class
		 * or hasprocperm() fails.
		 */
		if (tp->t_cid == 0 || !hasprocperm(tp->t_cred, CRED())) {
			arg->err = EPERM;
			thread_unlock(tp);
			return (0);
		}
		binding = arg->bind;

		/*
		 * Make sure the CPU being bound to is online.
		 */
		tp->t_bind_cpu = binding;	/* set new binding */

		/*
		 * If there is no system-set reason for affinity, set
		 * the t_bound_cpu field to reflect the binding.
		 */
		if (tp->t_affinitycnt == 0) {
			if (binding == PBIND_NONE) {
				tp->t_bound_cpu = NULL;
			} else {
				register cpu_t	*cp = cpu[binding];

				tp->t_bound_cpu = cp;

#ifdef MP	/* no use on UP, but let user set/clear bindings anyway */
				/*
				 * Make the thread switch to the bound CPU.
				 */
				if (cp != tp->t_cpu) {
					cp = tp->t_cpu;
					cp->cpu_kprunrun = 1;
					cp->cpu_runrun = 1;
					aston(tp);
					if (cp != CPU) {
						poke_cpu(cp->cpu_id);
					}
				}
#endif /* MP */
			}
		}
	}
	thread_unlock(tp);

	return (0);
}

/*
 * Bind all the threads of a process to a CPU.
 */
int
cpu_bind_process(proc_t *pp, struct bind_arg *arg)
{
	kthread_t	*tp;
	kthread_t	*fp;
	int		err = 0;
	int		i;

	ASSERT(MUTEX_HELD(&pidlock));
	mutex_enter(&pp->p_lock);

	tp = pp->p_tlist;
	if (tp != NULL) {
		fp = tp;
		do {
			i = cpu_bind_thread(tp, arg);
			if (err == 0)
				err = i;
		} while ((tp = tp->t_forw) != fp);
	}

	mutex_exit(&pp->p_lock);
	return (err);
}
