/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)disp.c	1.109	95/10/03 SMI"	/* from SVr4.0 1.30 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/sysinfo.h>
#include <sys/var.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/procset.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/priocntl.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <sys/bitmap.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/modctl.h>
#include <sys/tblock.h>
#include <sys/vtrace.h>
#include <sys/tnf.h>

#include <vm/as.h>

#ifdef DEBUG
int idlecntdown = 60;
#endif

extern kmutex_t		class_lock;

/* Dispatch queue allocation structure and functions */
struct disp_queue_info {
	disp_t	*dp;
	dispq_t *olddispq;
	dispq_t *newdispq;
	ulong	*olddqactmap;
	ulong	*newdqactmap;
	int	oldnglobpris;
};
static void	disp_dq_alloc(struct disp_queue_info *dptr, int numpris,
    disp_t *dp);
static void	disp_dq_assign(struct disp_queue_info *dptr, int numpris);
static void	disp_dq_free(struct disp_queue_info *dptr);

/* Function to run when UP machine is idle */
static void	generic_idle_cpu();
void		(*idle_cpu)() = generic_idle_cpu;

pri_t	kpreemptpri;	/* priority where kernel preemption applies */
pri_t	intr_pri;	/* interrupt thread priority base level */

disp_lock_t	swapped_lock;	/* lock swapped threads and swap queue */
int	nswapped;	/* total number of swapped threads */
void	disp_swapped_enq(kthread_id_t tp);
static void	disp_swapped_setrun(kthread_id_t tp);
static void	cpu_resched(cpu_t *cp, pri_t tpri, pri_t cpupri);

/*
 * If this is set, only interrupt threads will cause kernel preemptions.
 * This is done by changing the value of kpreemptpri.  kpreemptpri
 * will either be the max sysclass pri + 1 or the min interrupt pri.
 */
int	only_intr_kpreempt;

#ifdef MP
extern void set_idle_cpu(int cpun);
extern void unset_idle_cpu(int cpun);
static void setkpdq(kthread_id_t tp, int borf);
#define	SETKP_BACK	0
#define	SETKP_FRONT	1
#else
#define	set_idle_cpu(cpun)
#define	unset_idle_cpu(cpun)
#endif /* MP */

id_t	initcid;

disp_lock_t	transition_lock;	/* lock on transitioning threads */
disp_lock_t	stop_lock;		/* lock on stopped threads */
disp_lock_t	shuttle_lock;		/* lock on shuttle objects */

static void		cpu_dispqalloc(int numpris);

#ifdef MP
static kthread_id_t	disp_getwork(cpu_t *to);
static kthread_id_t	disp_getbest(disp_t *from);
#else
#define	disp_getwork(for_cpu)	NULL	/* always fails on UP */
#endif /* MP */
static kthread_id_t	disp_ratify(kthread_id_t tp);

void	swtch_to(kthread_id_t);

/*
 * Kernel preemption occurs if a higher-priority thread is runnable with
 * a priority at or above kpreemptpri.
 *
 * So that other processors can watch for such threads, a separate
 * dispatch queue with unbound work above kpreemptpri is maintained.
 */
#ifdef MP
static disp_t	disp_kp_queue;		/* unbound kernel preemption queue */

static void	disp_kp_alloc(pri_t npri);	/* allocate disp_kp_queue */
#else
#define	disp_kp_alloc(npri)
#endif /* MP */

/*
 * Scheduler Initialization
 * 	Called with NULL the first time to initialize all loaded classes,
 *	called with class pointer to initialize for a newly loaded class.
 */
void
dispinit(sclass_t *clp)
{
	register id_t	cid;
	register pri_t	maxglobpri;
	pri_t		cl_maxglobpri;
	int		oldnglobpris;
	int		newnglobpris;

	maxglobpri = -1;
	oldnglobpris = 0;

	/*
	 * Call the class specific initialization functions. We pass the size
	 * of a class specific parameter buffer to each of the initialization
	 * functions to try to catch problems with backward compatibility of
	 * class modules.  For example a new class module running on an old
	 * system which didn't provide sufficiently large parameter buffers
	 * would be bad news.  Class initialization modules can check for
	 * this and take action if they detect a problem.
	 */
	mutex_enter(&cpu_lock);
	if (clp == NULL) {
		/*
		 * Initialize transition lock, which will always be set.
		 */
#ifdef DISP_LOCK_STATS
		disp_lock_stats_init();
#endif /* DISP_LOCK_STATS */
		disp_lock_init(&transition_lock, "transition lock");
		disp_lock_enter_high(&transition_lock);
		disp_lock_init(&stop_lock, "stopped threads lock");
		disp_lock_init(&shuttle_lock, "shuttle lock");
		CPU->cpu_disp.disp_maxrunpri = -1;
#ifdef MP
		disp_lock_init(&disp_kp_queue.disp_lock, "disp kp queue lock");
		CPU->cpu_disp.disp_max_unbound_pri = -1;
		disp_kp_queue.disp_maxrunpri = -1;
		disp_kp_queue.disp_max_unbound_pri = -1;
		disp_kp_queue.disp_cpu = NULL;
#endif /* MP */

		for (cid = 0; cid < nclass; cid++) {
			sclass_t	*sc;

			sc = &sclass[cid];
			if (SCHED_INSTALLED(sc)) {
				(*sc->cl_init)(cid, PC_CLPARMSZ,
					&sc->cl_funcs, &cl_maxglobpri,
					&sc->cl_size);
				if (cl_maxglobpri > maxglobpri)
					maxglobpri = cl_maxglobpri;
			}
		}
		kpreemptpri = (pri_t)v.v_maxsyspri + 1;
		if (maxglobpri == -1)
			return;
	} else {
		maxglobpri = (pri_t)(v.v_nglobpris - LOCK_LEVEL - 1);
		(*clp->cl_init)(clp - sclass, PC_CLPARMSZ, &clp->cl_funcs,
		    &cl_maxglobpri, &clp->cl_size);
		if (cl_maxglobpri > maxglobpri)
			maxglobpri = cl_maxglobpri;

		/*
		 * Save old queue information.  If we're initializing a new
		 * scheduling class which has just been loaded, then
		 * the size of the dispq may have changed.  We need to handle
		 * that here.
		 */
		oldnglobpris = v.v_nglobpris;
	}

	newnglobpris = maxglobpri + 1 + LOCK_LEVEL;

	if (newnglobpris > oldnglobpris) {
		/*
		 * Allocate new kp queue.
		 */
		disp_kp_alloc(newnglobpris);

		/*
		 * Allocate new dispatch queues for each CPU.
		 */
		cpu_dispqalloc(newnglobpris);

		/*
		 * compute new interrupt thread base priority
		 */
		intr_pri = maxglobpri;
		if (only_intr_kpreempt)
			kpreemptpri = intr_pri + 1;

		v.v_nglobpris = newnglobpris;
	}

	/*
	 * Get the init-class CID.
	 * This will load the class if it (TS) is not already loaded
	 * (it isn't), and that will call dispinit recursively,
	 * so drop the cpu_lock first.
	 */
	mutex_exit(&cpu_lock);
	if (clp == NULL) {
		(void) getcid(initclass, &initcid);
	}
}

/*
 * For each CPU, allocate new dispatch queues
 * with the stated number of priorities.
 */
static void
cpu_dispqalloc(int numpris)
{
	cpu_t	*cpup;
	struct disp_queue_info	disp_mem[NCPU];
	int i, num;

	ASSERT(MUTEX_HELD(&cpu_lock));

	/*
	 * This routine must allocate all of the memory before stopping
	 * the cpus because it must not sleep in kmem_alloc while the
	 * CPUs are stopped.  Locks they hold will not be freed until they
	 * are restarted.
	 */
	i = 0;
	cpup = cpu_list;
	do {
		disp_dq_alloc(&disp_mem[i], numpris, &cpup->cpu_disp);
		i++;
		cpup = cpup->cpu_next;
	} while (cpup != cpu_list);
	num = i;

#ifdef MP
	(void) pause_cpus(NULL);
#endif
	for (i = 0; i < num; i++)
		disp_dq_assign(&disp_mem[i], numpris);
#ifdef MP
	start_cpus();
#endif

	/*
	 * I must free all of the memory after starting the cpus because
	 * I can not risk sleeping in kmem_free while the cpus are stopped.
	 */
	for (i = 0; i < num; i++)
		disp_dq_free(&disp_mem[i]);
}

static void
disp_dq_alloc(struct disp_queue_info *dptr, int numpris, disp_t	*dp)
{
	dptr->newdispq = (dispq_t *)kmem_zalloc(numpris *
	    sizeof (dispq_t), KM_SLEEP);
	dptr->newdqactmap = (ulong *)kmem_zalloc(((numpris /
	    BT_NBIPUL) + 1) * sizeof (long), KM_SLEEP);
	dptr->dp = dp;
}

static void
disp_dq_assign(struct disp_queue_info *dptr, int numpris)
{
	disp_t	*dp;

	dp = dptr->dp;
#ifndef MP
	disp_lock_enter(&dp->disp_lock);
#endif
	dptr->olddispq = dp->disp_q;
	dptr->olddqactmap = dp->disp_qactmap;
	dptr->oldnglobpris = dp->disp_npri;

	ASSERT(dptr->oldnglobpris < numpris);

	if (dptr->olddispq != NULL) {
		bcopy((caddr_t)dptr->olddispq, (caddr_t)dptr->newdispq,
		    dptr->oldnglobpris * sizeof (dispq_t));
		bcopy((caddr_t)dptr->olddqactmap,
		    (caddr_t)dptr->newdqactmap,
		    ((dptr->oldnglobpris / BT_NBIPUL) + 1) *
		    sizeof (long));
	}
	dp->disp_q = dptr->newdispq;
	dp->disp_qactmap = dptr->newdqactmap;
	dp->disp_q_limit = &dptr->newdispq[numpris];
	dp->disp_npri = numpris;
#ifndef MP
	disp_lock_exit(&dp->disp_lock);
#endif
}

static void
disp_dq_free(struct disp_queue_info *dptr)
{
	if (dptr->olddispq != NULL)
		kmem_free(dptr->olddispq,
		    dptr->oldnglobpris * sizeof (dispq_t));
	if (dptr->olddqactmap != NULL)
		kmem_free(dptr->olddqactmap,
		    ((dptr->oldnglobpris / BT_NBIPUL) + 1) * sizeof (long));
}

#ifdef	MP
/*
 * For a newly created CPU, initialize the dispatch queue.
 * This is called before the CPU is known through cpu[] or on any lists.
 */
void
disp_cpu_init(cpu_t *cp)
{
	disp_t	*dp;
	dispq_t	*newdispq;
	ulong	*newdqactmap;

	ASSERT(MUTEX_HELD(&cpu_lock));	/* protect dispatcher queue sizes */

	dp = &cp->cpu_disp;
	dp->disp_cpu = cp;
	dp->disp_maxrunpri = -1;
	dp->disp_max_unbound_pri = -1;
	disp_lock_init(&cp->cpu_thread_lock, "cpu thread lock");
	/*
	 * Allocate memory for the dispatcher queue headers
	 * and the active queue bitmap.
	 */
	newdispq = (dispq_t *)kmem_zalloc(v.v_nglobpris * sizeof (dispq_t),
		KM_SLEEP);
	newdqactmap = (ulong *)kmem_zalloc(((v.v_nglobpris /
		    BT_NBIPUL) + 1) * sizeof (long), KM_SLEEP);
	dp->disp_q = newdispq;
	dp->disp_qactmap = newdqactmap;
	dp->disp_q_limit = &newdispq[v.v_nglobpris];
	dp->disp_npri = v.v_nglobpris;
}

/*
 * Allocate a new, larger kpreempt dispatch queue to replace the old one.
 */
static void
disp_kp_alloc(pri_t npri)
{
	struct disp_queue_info	mem_info;

	ASSERT(MUTEX_HELD(&cpu_lock));

	if (npri <= disp_kp_queue.disp_npri)
		return;
	/*
	 * Allocate memory for the new array.
	 */
	disp_dq_alloc(&mem_info, npri, &disp_kp_queue);

	/*
	 * We need to copy the old structures to the new and free the old.
	 */
	disp_dq_assign(&mem_info, npri);
	disp_dq_free(&mem_info);
}
#endif /* MP */


/*
 * Idle loop.
 */
void
idle()
{
	struct cpu	*cp = CPU;		/* pointer to this CPU */
#ifdef MP
	processorid_t	cpun = cp->cpu_id;	/* this processor's ID */
	kthread_id_t	t;			/* taken thread */
#endif

	CPU_STAT_ADDQ(cp, cpu_sysinfo.idlethread, 1);
	/*
	 * Uniprocessor version of idle loop.
	 * Do this until notified that we're on an actual multiprocessor.
	 */
#ifdef MP
	while (ncpus == 1) {
#else
	for (;;) {
#endif
#ifndef	bug1139151
		if (cp->cpu_disp.disp_nrunnable != 0)
#else
		if (CPU->cpu_disp.disp_nrunnable != 0)
#endif
			swtch();
		else
			(*idle_cpu)();
	}

#ifdef MP
	/*
	 * Multiprocessor idle loop.
	 */
	for (;;) {
		/*
		 * If CPU is completely quiesced by p_online(2), just wait
		 * here with minimal bus traffic until put online.
		 */
#ifndef	bug1139151
		while (cp->cpu_flags & CPU_QUIESCED)
#else
		while (CPU->cpu_flags & CPU_QUIESCED)
#endif
			;


#ifndef	bug1139151
		if (cp->cpu_disp.disp_nrunnable != 0) {
#else
		if (CPU->cpu_disp.disp_nrunnable != 0) {
#endif
			unset_idle_cpu(cpun);	/* arch-dependent hook */
			swtch();
		} else {
#ifndef bug1139151
			if (cp->cpu_flags & CPU_OFFLINE)
#else
			if (CPU->cpu_flags & CPU_OFFLINE)
#endif
				continue;
			if ((t = disp_getwork(cp)) == NULL)
				continue;
			unset_idle_cpu(cpun);	/* arch-dependent hook */
			swtch_to(t);
		}
		CPU_STAT_ADDQ(cp, cpu_sysinfo.idlethread, 1);
		set_idle_cpu(cpun);	/* arch-dependent hook */
	}
#endif /* MP */
}


/*
 * Preempt the currently running thread in favor of the highest
 * priority thread.  The class of the current thread controls
 * where it goes on the dispatcher queues. If panicking, turn
 * preemption off.
 */
void
preempt()
{
	register kthread_t 	*t = curthread;
	register klwp_t 	*lwp = ttolwp(curthread);

	if (panicstr)
		return;

	TRACE_0(TR_FAC_DISP, TR_PREEMPT_START, "preempt_start");

	thread_lock(t);

	if (t->t_state != TS_ONPROC || t->t_disp_queue != &CPU->cpu_disp) {
		/*
		 * this thread has already been chosen to be run on
		 * another CPU. Clear kprunrun on this CPU since we're
		 * already headed for swtch().
		 */
		CPU->cpu_kprunrun = 0;
		thread_unlock_nopreempt(t);
		TRACE_0(TR_FAC_DISP, TR_PREEMPT_END, "preempt_end");
	} else {
		if (lwp != NULL)
			lwp->lwp_ru.nivcsw++;
		CPU_STAT_ADDQ(CPU, cpu_sysinfo.inv_swtch, 1);
		THREAD_TRANSITION(t);
		CL_PREEMPT(t);
		thread_unlock_nopreempt(t);

		TRACE_0(TR_FAC_DISP, TR_PREEMPT_END, "preempt_end");

		swtch();		/* clears CPU->cpu_runrun via disp() */
	}
}

extern kthread_id_t	thread_unpin();

/*
 * disp() - find the highest priority thread for this processor to run, and
 * set it in TS_ONPROC state so that resume() can be called to run it.
 */
static kthread_id_t
disp()
{
	cpu_t		*cpup;
	disp_t		*dp;
	kthread_id_t	tp;
	dispq_t		*dq;
	int		maxrunword;
	pri_t		pri;

	TRACE_0(TR_FAC_DISP, TR_DISP_START, "disp_start");

	cpup = CPU;
	/*
	 * Find the highest priority loaded, runnable thread.
	 */
	dp = &cpup->cpu_disp;

reschedule:
#ifdef MP
	/*
	 * If there is more important work on the global queue with a better
	 * priority than the maximum on this CPU, take it now.
	 */
	while ((pri = disp_kp_queue.disp_maxrunpri) >= 0 &&
	    pri >= dp->disp_maxrunpri &&
	    (cpup->cpu_flags & CPU_OFFLINE) == 0 &&
	    (tp = disp_getbest(&disp_kp_queue)) != NULL) {
		if (disp_ratify(tp) != NULL) {
			TRACE_3(TR_FAC_DISP, TR_DISP_END,
			    "disp_end:pri %d tid %x (%T)",
			    DISP_PRIO(tp), tp, tp);
			return (tp);
		}
	}
#endif /* MP */

	disp_lock_enter(&dp->disp_lock);
	pri = dp->disp_maxrunpri;

	/*
	 * If there is nothing to run, look at what's runnable on other queues.
	 * Choose the idle thread if the CPU is quiesced.
	 * Note that CPUs that have the CPU_OFFLINE flag set can still run
	 * interrupt threads, which will be the only threads on the CPU's own
	 * queue, but cannot run threads from other queues.
	 */
	if (pri == -1) {
#ifdef	MP
		if (!(cpup->cpu_flags & CPU_OFFLINE)) {
			disp_lock_exit(&dp->disp_lock);
			if ((tp = disp_getwork(cpup)) == NULL) {
				tp = cpup->cpu_idle_thread;
				(void) splhigh();
				THREAD_ONPROC(tp, cpup);
				cpup->cpu_dispthread = tp;
				cpup->cpu_runrun = cpup->cpu_kprunrun = 0;
				cpup->cpu_chosen_level = -1;
			}
		} else {
			disp_lock_exit_high(&dp->disp_lock);
			tp = cpup->cpu_idle_thread;
			THREAD_ONPROC(tp, cpup);
			cpup->cpu_dispthread = tp;
			cpup->cpu_runrun = cpup->cpu_kprunrun = 0;
			cpup->cpu_chosen_level = -1;
		}
#else	/* MP */
		disp_lock_exit_high(&dp->disp_lock);
		tp = cpup->cpu_idle_thread;
		THREAD_ONPROC(tp, cpup);
		cpup->cpu_dispthread = tp;
		cpup->cpu_runrun = cpup->cpu_kprunrun = 0;
		cpup->cpu_chosen_level = -1;
#endif	/* MP */
		TRACE_3(TR_FAC_DISP, TR_DISP_END,
			"disp_end:pri %d tid %x (%T)", DISP_PRIO(tp), tp, tp);
		return (tp);
	}

	dq = &dp->disp_q[pri];
	tp = dq->dq_first;

	ASSERT(tp != NULL);
	ASSERT(tp->t_schedflag & TS_LOAD);	/* thread must be swapped in */

	/*
	 * Found it so remove it from queue.
	 */
	dp->disp_nrunnable--;
	dq->dq_sruncnt--;
	if ((dq->dq_first = tp->t_link) == NULL) {
		ulong	*dqactmap = dp->disp_qactmap;

		ASSERT(dq->dq_sruncnt == 0);
		dq->dq_last = NULL;

		/*
		 * The queue is empty, so the corresponding bit needs to be
		 * turned off in dqactmap.   If nrunnable != 0 just took the
		 * last runnable thread off the
		 * highest queue, so recompute disp_maxrunpri.
		 */
		maxrunword = pri >> BT_ULSHIFT;
		dqactmap[maxrunword] &= ~BT_BIW(pri);

		if (dp->disp_nrunnable == 0) {
#ifdef	MP
			dp->disp_max_unbound_pri = -1;
#endif	/* MP */
			dp->disp_maxrunpri = -1;
		} else {
			int	ipri;

			bt_gethighbit(dqactmap, maxrunword, &ipri);
			dp->disp_maxrunpri = ipri;
#ifdef	MP
			if (ipri < dp->disp_max_unbound_pri)
				dp->disp_max_unbound_pri = ipri;
#endif	/* MP */
		}
	} else {
		tp->t_link = NULL;
	}

	/*
	 * Set TS_DONT_SWAP flag to prevent another processor from swapping
	 * out this thread before we have a chance to run it.
	 * While running, it is protected against swapping by t_lock.
	 */
	tp->t_schedflag |= TS_DONT_SWAP;
	cpup->cpu_dispthread = tp;		/* protected by spl only */
	thread_onproc(tp, cpup);  		/* set t_state to TS_ONPROC */
	disp_lock_exit_high(&dp->disp_lock);	/* drop run queue lock */
	if (tp->t_proc_flag & TP_MSACCT)
		restore_mstate(tp);

	ASSERT(tp != NULL);
	TRACE_3(TR_FAC_DISP, TR_DISP_END,
		"disp_end:pri %d tid %x (%T)", pri, tp, tp);
	if (disp_ratify(tp) == NULL)
		goto reschedule;
	return (tp);
}

/*
 * swtch()
 *	Find best runnable thread and run it.
 *	Called with the current thread already switched to a new state,
 *	on a sleep queue, run queue, stopped, and not zombied.
 *	May be called at any spl level less than or equal to LOCK_LEVEL.
 *	Always drops spl to the base level (spl0()).
 */
void
swtch()
{
	register kthread_id_t	t = curthread;
	register kthread_id_t	next;
	register cpu_t		*cp;

	TRACE_0(TR_FAC_DISP, TR_SWTCH_START, "swtch_start");

	if (t->t_intr != NULL) {
		/*
		 * We are an interrupt thread.  Setup and return
		 * the interrupted thread to be resumed.
		 */
		(void) splhigh();	/* block other scheduler action */
		cp = CPU;		/* now protected against migration */
		ASSERT(cp->cpu_on_intr == 0);	/* not called with PIL > 10 */
		CPU_STAT_ADDQ(cp, cpu_sysinfo.pswitch, 1);
		CPU_STAT_ADDQ(cp, cpu_sysinfo.intrblk, 1);
		next = thread_unpin();
		TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");
		resume_from_intr(next);
	} else {
#ifdef	DEBUG
		if (t->t_state == TS_ONPROC &&
		    t->t_disp_queue->disp_cpu == CPU &&
		    t->t_preempt == 0) {
			thread_lock(t);
			ASSERT(t->t_state != TS_ONPROC ||
			    t->t_disp_queue->disp_cpu != CPU ||
			    t->t_preempt != 0);	/* cannot migrate */
			thread_unlock_nopreempt(t);
		}
#endif	/* DEBUG */
		cp = CPU;
		next = disp();		/* returns with spl high */
		CPU_STAT_ADDQ(cp, cpu_sysinfo.pswitch, 1);
		ASSERT(cp->cpu_on_intr == 0);	/* not called with PIL > 10 */
		if (next != t) {
			t->t_disp_time = lbolt;
			TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");
#ifndef NPROBE
			if (tnf_tracing_active)
				tnf_thread_switch(next);
#endif /* NPROBE */
			resume(next);
			/*
			 * The TR_RESUME_END and TR_SWTCH_END trace points
			 * appear at the end of resume(), because we may not
			 * return here
			 */
		} else {
			TRACE_0(TR_FAC_DISP, TR_SWTCH_END, "swtch_end");
			(void) spl0();
		}
	}
}

/*
 * swtch_from_zombie()
 *	Special case of swtch(), which allows checks for TS_ZOMB to be
 *	eliminated from normal resume.
 *	Find best runnable thread and run it.
 *	Called with the current thread zombied.
 *	Zombies cannot migrate, so CPU references are safe.
 */
void
swtch_from_zombie()
{
	register kthread_id_t	next;

	TRACE_0(TR_FAC_DISP, TR_SWTCH_START, "swtch_start");

	ASSERT(curthread->t_state == TS_ZOMB);

	next = disp();			/* returns with spl high */
	ASSERT(CPU->cpu_on_intr == 0);	/* not called with PIL > 10 */
	CPU_STAT_ADDQ(CPU, cpu_sysinfo.pswitch, 1);
	ASSERT(next != curthread);
	TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");
#ifndef NPROBE
	if (tnf_tracing_active)
		tnf_thread_switch(next);
#endif /* NPROBE */
	resume_from_zombie(next);
	/*
	 * The TR_RESUME_END and TR_SWTCH_END trace points
	 * appear at the end of resume(), because we certainly will not
	 * return here
	 */
}

#if defined(DEBUG) && (defined(DISP_DEBUG) || defined(lint))
static int
thread_on_queue(kthread_t *tp)
{
	cpu_t	*cp;
	cpu_t	*self;
	disp_t	*dp;

	self = CPU;
	cp = self->cpu_next_onln;
	dp = &cp->cpu_disp;
#ifdef	MP
	for (;;)
#endif
	{
		dispq_t		*dq;
		dispq_t		*eq;

		disp_lock_enter_high(&dp->disp_lock);
		for (dq = dp->disp_q, eq = dp->disp_q_limit; dq < eq; ++dq) {
			kthread_t	*rp;

			ASSERT(dq->dq_last == NULL ||
				dq->dq_last->t_link == NULL);
			for (rp = dq->dq_first; rp; rp = rp->t_link)
				if (tp == rp) {
					disp_lock_exit_high(&dp->disp_lock);
					return (1);
				}
		}
		disp_lock_exit_high(&dp->disp_lock);
#ifdef	MP
		if (cp == NULL)
			break;
		if (cp == self) {
			cp = NULL;
			dp = &disp_kp_queue;
		} else {
			cp = cp->cpu_next_onln;
			dp = &cp->cpu_disp;
		}
#endif	/* MP */
	}
	return (0);
}	/* end of thread_on_queue */
#else

#define	thread_on_queue(tp)	0	/* ASSERT must be !thread_on_queue */

#endif  /* DEBUG */

/*
 * like swtch(), but switch to a specified thread taken from another CPU.
 *	called with spl high..
 */
void
swtch_to(kthread_id_t next)
{
	cpu_t			*cp = CPU;

	TRACE_0(TR_FAC_DISP, TR_SWTCH_START, "swtch_start");

	/*
	 * Update context switch statistics.
	 */
	CPU_STAT_ADDQ(cp, cpu_sysinfo.pswitch, 1);

	TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");

	curthread->t_disp_time = lbolt;		/* record last execution time */
#ifndef NPROBE
	if (tnf_tracing_active)
		tnf_thread_switch(next);
#endif /* NPROBE */

	resume(next);
	/*
	 * The TR_RESUME_END and TR_SWTCH_END trace points
	 * appear at the end of resume(), because we may not
	 * return here
	 */
}


#ifdef	MP

#define	CPU_IDLING(pri)	((pri) == -1)

static void
cpu_resched(cpu_t *cp, pri_t tpri, pri_t cpupri)
{
	if (!CPU_IDLING(cpupri) && (cpupri < tpri)) {
		TRACE_2(TR_FAC_DISP, TR_CPU_RESCHED,
		    "CPU_RESCHED:Tpri %d Cpupri %d", tpri, cpupri);
		if (cp->cpu_runrun == 0) {
			cp->cpu_runrun = 1;
			aston(cp->cpu_dispthread);
			if (tpri < kpreemptpri && cp != CPU)
				poke_cpu(cp->cpu_id);
		}
		if (tpri >= kpreemptpri && cp->cpu_kprunrun == 0) {
			cp->cpu_kprunrun = 1;
			if (cp != CPU)
				poke_cpu(cp->cpu_id);
		}
	}
}

#else	/* MP */

static void
cpu_resched(cpu_t *cp, pri_t tpri, pri_t cpupri)
{
	if (cpupri < tpri) {
		TRACE_2(TR_FAC_DISP, TR_CPU_RESCHED,
		    "CPU_RESCHED:Tpri %d Cpupri %d", tpri, cpupri);
		if (cp->cpu_runrun == 0) {
			cp->cpu_runrun = 1;
			aston(cp->cpu_dispthread);
		}
		if (tpri >= kpreemptpri && cp->cpu_kprunrun == 0)
			cp->cpu_kprunrun = 1;
	}
}

#endif	/* MP */


/*
 * To help balance the run queues, setbackdq tries to maintain
 * difference in run queue length between the chosen CPU and the next one
 * of less than MAX_RUNQ_DIFF threads.  For the lowest priority levels, the
 * queue's lengths must match.
 */
#define	Q_MATCH_PRI	16	/* pri below which queue lengths must match */
#define	MAX_RUNQ_DIFF	2	/* maximum runq diff */

/*
 * Put the specified thread on the back of the dispatcher
 * queue corresponding to its current priority.
 *
 * Called with the thread in transition, onproc or stopped state
 * and locked (transition implies locked) and at high spl.
 * Returns with the thread in TS_RUN state and still locked.
 */
void
setbackdq(kthread_id_t tp)
{
	register dispq_t	*dq;
	register disp_t		*dp;
	register cpu_t		*cp;
	register pri_t		tpri;
	register pri_t		cpupri;

	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT((tp->t_schedflag & TS_ALLSTART) == 0);

	if ((tp->t_proc_flag & TP_MSACCT) && tp->t_waitrq == 0)
		tp->t_waitrq = gethrtime();

	ASSERT(!thread_on_queue(tp));	/* make sure tp isn't on a runq */

	/*
	 * If thread is "swapped" or on the swap queue don't
	 * queue it, but wake sched.
	 */
	if ((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD) {
		disp_swapped_setrun(tp);
		return;
	}

	tpri = DISP_PRIO(tp);

#ifndef MP
	cp = tp->t_cpu;
#else
	if (ncpus == 1)
		cp = tp->t_cpu;
	else if ((cp = tp->t_bound_cpu) == NULL) {
		register int	qlen;

		if (tpri >= kpreemptpri) {
			setkpdq(tp, SETKP_BACK);
			return;
		}
		/*
		 * Let scheduling class select the CPU.
		 */
		cp = CL_CPU_CHOOSE(tp, tpri);

		qlen = cp->cpu_disp.disp_q[tpri].dq_sruncnt;
		if (tpri >= Q_MATCH_PRI)
			qlen -= MAX_RUNQ_DIFF;
		if (qlen > 0) {
			cpu_t	*np;

			np = cp->cpu_next_onln;
			if (np->cpu_disp.disp_q[tpri].dq_sruncnt < qlen)
				cp = np;
		}
		ASSERT((cp->cpu_flags & CPU_QUIESCED) == 0);
	}
#endif	/* MP */
	dp = &cp->cpu_disp;
	disp_lock_enter_high(&dp->disp_lock);

	TRACE_4(TR_FAC_DISP, TR_BACKQ, "setbackdq:pri %d cpu %d tid %x (%T)",
		tpri, cp->cpu_id, tp, tp);

#ifndef NPROBE
	/* Kernel probe */
	if (tnf_tracing_active)
		tnf_thread_queue(tp, cp, tpri);
#endif /* NPROBE */

	ASSERT(tpri >= 0 && tpri < dp->disp_npri);
	THREAD_RUN(tp, &dp->disp_lock);		/* set t_state to TS_RUN */
	tp->t_disp_queue = dp;
	tp->t_link = NULL;

	dq = &dp->disp_q[tpri];
	dp->disp_nrunnable++;
	if (dq->dq_sruncnt++ != 0) {
		ASSERT(dq->dq_first != NULL);
		dq->dq_last->t_link = tp;
		dq->dq_last = tp;
	} else {
		ASSERT(dq->dq_first == NULL);
		ASSERT(dq->dq_last == NULL);
		dq->dq_first = dq->dq_last = tp;
		BT_SET(dp->disp_qactmap, tpri);
		if (tpri > dp->disp_maxrunpri) {
			dp->disp_maxrunpri = tpri;
			cpupri = DISP_PRIO(cp->cpu_dispthread);
			cpu_resched(cp, tpri, cpupri);
		}
	}

#ifdef	MP
	if (tpri > dp->disp_max_unbound_pri)
		dp->disp_max_unbound_pri = tpri;
#endif	/* MP */
}

/*
 * Put the specified thread on the front of the dispatcher
 * queue corresponding to its current priority.
 *
 * Called with the thread in transition, onproc or stopped state
 * and locked (transition implies locked) and at high spl.
 * Returns with the thread in TS_RUN state and still locked.
 */
void
setfrontdq(kthread_id_t tp)
{
	register disp_t		*dp;
	register dispq_t	*dq;
	register cpu_t		*cp;
	register pri_t		tpri;
	register pri_t		cpupri;

	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT((tp->t_schedflag & TS_ALLSTART) == 0);

	if ((tp->t_proc_flag & TP_MSACCT) && tp->t_waitrq == 0)
		tp->t_waitrq = gethrtime();

	ASSERT(!thread_on_queue(tp));	/* make sure tp isn't on a runq */

	/*
	 * If thread is "swapped" or on the swap queue don't
	 * queue it, but wake sched.
	 */
	if ((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD) {
		disp_swapped_setrun(tp);
		return;
	}

	tpri = DISP_PRIO(tp);

#ifndef MP
	cp = tp->t_cpu;
#else
	if (ncpus == 1)
		cp = tp->t_cpu;
	else if ((cp = tp->t_bound_cpu) == NULL) {
		if (tpri >= kpreemptpri) {
			setkpdq(tp, SETKP_FRONT);
			return;
		}
		/*
		 * If we are of higher or equal priority than
		 * the highest priority runnable thread of
		 * the current CPU, just pick this CPU.  Otherwise
		 * Let scheduling class select the CPU.
		 */
		cp = tp->t_cpu;
		if (tpri < cp->cpu_disp.disp_maxrunpri)
			cp = CL_CPU_CHOOSE(tp, tpri);
		ASSERT((cp->cpu_flags & CPU_QUIESCED) == 0);
	}
#endif	/* MP */
	dp = &cp->cpu_disp;
	disp_lock_enter_high(&dp->disp_lock);

	TRACE_4(TR_FAC_DISP, TR_FRONTQ, "setfrontdq:pri %d cpu %d tid %x (%T)",
		tpri, cp->cpu_id, tp, tp);

#ifndef NPROBE
	/* Kernel probe */
	if (tnf_tracing_active)
		tnf_thread_queue(tp, cp, tpri);
#endif /* NPROBE */

	ASSERT(tpri >= 0 && tpri < dp->disp_npri);
	THREAD_RUN(tp, &dp->disp_lock);		/* set TS_RUN state and lock */

	tp->t_disp_queue = dp;
	dq = &dp->disp_q[tpri];
	dp->disp_nrunnable++;
	if (dq->dq_sruncnt++ != 0) {
		ASSERT(dq->dq_last != NULL);
		tp->t_link = dq->dq_first;
		dq->dq_first = tp;
	} else {
		ASSERT(dq->dq_last == NULL);
		ASSERT(dq->dq_first == NULL);
		tp->t_link = NULL;
		dq->dq_first = dq->dq_last = tp;
		BT_SET(dp->disp_qactmap, tpri);
		if (tpri > dp->disp_maxrunpri) {
			dp->disp_maxrunpri = tpri;
			cpupri = DISP_PRIO(cp->cpu_dispthread);
			cpu_resched(cp, tpri, cpupri);
		}
	}

#ifdef	MP
	if (tp->t_bound_cpu == NULL && tpri > dp->disp_max_unbound_pri)
		dp->disp_max_unbound_pri = tpri;
#endif	/* MP */
}

#ifdef	MP
/*
 * Put a high-priority unbound thread on the disp_kp_queue
 */
static void
setkpdq(kthread_id_t tp, int borf)
{
	register dispq_t	*dq;
	register disp_t		*dp;
	register cpu_t		*cp;
	register pri_t		tpri;
	register pri_t		cpupri;

	tpri = DISP_PRIO(tp);

	dp = &disp_kp_queue;
	disp_lock_enter_high(&dp->disp_lock);

	TRACE_4(TR_FAC_DISP, TR_FRONTQ, "setkpdq:pri %d borf %d tid %x (%T)",
		tpri, borf, tp, tp);

	ASSERT(tpri >= 0 && tpri < dp->disp_npri);
	THREAD_RUN(tp, &dp->disp_lock);		/* set t_state to TS_RUN */
	tp->t_disp_queue = dp;
	dp->disp_nrunnable++;
	dq = &dp->disp_q[tpri];

	if (dq->dq_sruncnt++ != 0) {
		if (borf == SETKP_BACK) {
			ASSERT(dq->dq_first != NULL);
			tp->t_link = NULL;
			dq->dq_last->t_link = tp;
			dq->dq_last = tp;
		} else {
			ASSERT(dq->dq_last != NULL);
			tp->t_link = dq->dq_first;
			dq->dq_first = tp;
		}
	} else {
		if (borf == SETKP_BACK) {
			ASSERT(dq->dq_first == NULL);
			ASSERT(dq->dq_last == NULL);
			dq->dq_first = dq->dq_last = tp;
		} else {
			ASSERT(dq->dq_last == NULL);
			ASSERT(dq->dq_first == NULL);
			tp->t_link = NULL;
			dq->dq_first = dq->dq_last = tp;
		}
		BT_SET(dp->disp_qactmap, tpri);
		if (tpri > dp->disp_max_unbound_pri)
			dp->disp_max_unbound_pri = tpri;
		if (tpri > dp->disp_maxrunpri)
			dp->disp_maxrunpri = tpri;
	}

	cp = CL_CPU_CHOOSE(tp, tpri);
	disp_lock_enter_high(&cp->cpu_disp.disp_lock);
	ASSERT((cp->cpu_flags & CPU_QUIESCED) == 0);

#ifndef NPROBE
	/* Kernel probe */
	if (tnf_tracing_active)
		tnf_thread_queue(tp, cp, tpri);
#endif /* NPROBE */

	if (cp->cpu_chosen_level < tpri)
		cp->cpu_chosen_level = tpri;
	cpupri = DISP_PRIO(cp->cpu_dispthread);
	cpu_resched(cp, tpri, cpupri);
	disp_lock_exit_high(&cp->cpu_disp.disp_lock);
}
#endif	/* MP */

/*
 * Remove a thread from the dispatcher queue if it is on it.
 * It is not an error if it is not found but we return whether
 * or not it was found in case the caller wants to check.
 */
boolean_t
dispdeq(kthread_id_t tp)
{
	register disp_t		*dp;
	register dispq_t	*dq;
	register kthread_id_t	rp;
	register kthread_id_t	trp;
	register kthread_id_t	*ptp;
	register int		tpri;

	ASSERT(THREAD_LOCK_HELD(tp));

	if (tp->t_state != TS_RUN)
		return (B_FALSE);

	/*
	 * The thread is "swapped" or is on the swap queue and
	 * hence no longer on the run queue, so return true.
	 */
	if ((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD)
		return (B_TRUE);

	tpri = DISP_PRIO(tp);
	dp = tp->t_disp_queue;
	ASSERT(tpri < dp->disp_npri);
	dq = &dp->disp_q[tpri];
	ptp = &dq->dq_first;
	rp = *ptp;
	trp = NULL;

	ASSERT(dq->dq_last == NULL || dq->dq_last->t_link == NULL);

	/*
	 * Search for thread in queue.
	 * Double links would simplify this at the expense of disp/setrun.
	 */
	while (rp != tp && rp != NULL) {
		trp = rp;
		ptp = &trp->t_link;
		rp = trp->t_link;
	}

	if (rp == NULL) {
		cmn_err(CE_PANIC, "dispdeq: thread not on queue");
	}

	/*
	 * Found it so remove it from queue.
	 */
	if ((*ptp = rp->t_link) == NULL)
		dq->dq_last = trp;

	dp->disp_nrunnable--;
	if (--dq->dq_sruncnt == 0) {
		dp->disp_qactmap[tpri >> BT_ULSHIFT] &= ~BT_BIW(tpri);
		if (dp->disp_nrunnable == 0) {
			dp->disp_max_unbound_pri = -1;
			dp->disp_maxrunpri = -1;
		} else if (tpri == dp->disp_maxrunpri) {
			int	ipri;

			bt_gethighbit(dp->disp_qactmap,
				dp->disp_maxrunpri >> BT_ULSHIFT,
				&ipri);
#ifdef	MP
			if (ipri < dp->disp_max_unbound_pri)
				dp->disp_max_unbound_pri = ipri;
#endif	/* MP */
			dp->disp_maxrunpri = ipri;
		}
	}
	tp->t_link = NULL;
	THREAD_TRANSITION(tp);		/* put in intermediate state */
	return (B_TRUE);
}


/*
 * dq_sruninc and dq_srundec are public functions for
 * incrementing/decrementing the sruncnts when a thread on
 * a dispatcher queue is made schedulable/unschedulable by
 * resetting the TS_LOAD flag.
 *
 * The caller MUST have the thread lock and therefore the dispatcher
 * queue lock so that the operation which changes
 * the flag, the operation that checks the status of the thread to
 * determine if it's on a disp queue AND the call to this function
 * are one atomic operation with respect to interrupts.
 */

/*
 * Called by sched AFTER TS_LOAD flag is set on a swapped, runnable thread.
 */
void
dq_sruninc(kthread_id_t t)
{
	ASSERT(t->t_state == TS_RUN);
	ASSERT(t->t_schedflag & TS_LOAD);

	THREAD_TRANSITION(t);
	setfrontdq(t);
}

/*
 * See comment on calling conventions above.
 * Called by sched BEFORE TS_LOAD flag is cleared on a runnable thread.
 */
void
dq_srundec(kthread_id_t t)
{
	ASSERT(t->t_schedflag & TS_LOAD);

	(void) dispdeq(t);
	disp_swapped_enq(t);
}

/*
 * Change the dispatcher lock of thread to the "swapped_lock"
 * and return with thread lock still held.
 *
 * Called with thread_lock held, in transition state, and at high spl.
 */
void
disp_swapped_enq(kthread_id_t tp)
{
	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT(tp->t_schedflag & TS_LOAD);

	switch (tp->t_state) {
	case TS_RUN:
		disp_lock_enter_high(&swapped_lock);
		THREAD_SWAP(tp, &swapped_lock);	/* set TS_RUN state and lock */
		break;
	case TS_ONPROC:
		disp_lock_enter_high(&swapped_lock);
		THREAD_TRANSITION(tp);
		wake_sched_sec = 1;		/* tell clock to wake sched */
		THREAD_SWAP(tp, &swapped_lock);	/* set TS_RUN state and lock */
		break;
	default:
		cmn_err(CE_PANIC, "disp_swapped: tp: %x bad t_state", (int)tp);
	}
}

/*
 * This routine is called by setbackdq/setfrontdq if the thread is
 * not loaded or loaded and on the swap queue.
 *
 * Thread state TS_SLEEP implies that a swapped thread
 * has been woken up and needs to be swapped in by the swapper.
 *
 * Thread state TS_RUN, it implies that the priority of a swapped
 * thread is being increased by scheduling class (e.g. ts_update).
 */
static void
disp_swapped_setrun(kthread_id_t tp)
{
	extern pri_t maxclsyspri;

	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD);

	switch (tp->t_state) {
	case TS_SLEEP:
		disp_lock_enter_high(&swapped_lock);
		/*
		 * Wakeup sched immediately (i.e., next tick) if the
		 * thread priority is above maxclsyspri.
		 */
		if (DISP_PRIO(tp) > maxclsyspri)
			wake_sched = 1;
		else
			wake_sched_sec = 1;
		THREAD_RUN(tp, &swapped_lock); /* set TS_RUN state and lock */
		break;
	case TS_RUN:				/* called from ts_update */
		break;
	default:
		cmn_err(CE_PANIC,
		    "disp_swapped_setrun: tp: %x bad t_state", (int)tp);
	}
}

/*
 * Allocate a cid given a class name if one is not already allocated.
 * Returns 0 if the cid was already exists or if the allocation of a new
 * cid was successful. Nonzero return indicates error.
 */
int
alloc_cid(char *clname, id_t *cidp)
{
	register sclass_t	*clp;

	ASSERT(MUTEX_HELD(&class_lock));

	/*
	 * If the clname doesn't already have a cid, allocate one.
	 */
	if (getcidbyname(clname, cidp) != 0) {
		/*
		 * Allocate a class entry and a lock for it.
		 */
		for (clp = sclass; clp < &sclass[nclass]; clp++)
			if (clp->cl_name[0] == '\0' && clp->cl_lock == NULL)
				break;

		if (clp == &sclass[nclass]) {
			return (ENOSPC);
		}
		*cidp = clp - &sclass[0];
		clp->cl_lock = (krwlock_t *)kmem_alloc(sizeof (krwlock_t),
		    KM_SLEEP);
		clp->cl_name = kmem_alloc(strlen(clname) + 1, KM_SLEEP);
		strcpy(clp->cl_name, clname);
		rw_init(clp->cl_lock, clp->cl_name, RW_DEFAULT, DEFAULT_WT);
	}

	/*
	 * At this point, *cidp will contain the index into the class
	 * array for the given class name.
	 */
	return (0);
}

int
scheduler_load(char *clname, sclass_t *clp)
{
	if (LOADABLE_SCHED(clp)) {
		rw_enter(clp->cl_lock, RW_READER);
		while (!SCHED_INSTALLED(clp)) {
			rw_exit(clp->cl_lock);
			if (modload("sched", clname) == -1)
				return (EINVAL);
			rw_enter(clp->cl_lock, RW_READER);
		}
		rw_exit(clp->cl_lock);
	}
	return (0);
}	/* end of scheduler_load */

/*
 * Get class ID given class name.
 */
int
getcid(char *clname, id_t *cidp)
{
	register sclass_t	*clp;
	register int		retval;

	mutex_enter(&class_lock);
	if ((retval = alloc_cid(clname, cidp)) == 0) {
		clp = &sclass[*cidp];
		clp->cl_count++;

		/*
		 * If it returns zero, it's loaded & locked
		 * or we found a statically installed scheduler
		 * module.
		 * If it returns EINVAL, modload() failed when
		 * it tried to load the module.
		 */
		mutex_exit(&class_lock);
		retval = scheduler_load(clname, clp);
		mutex_enter(&class_lock);

		clp->cl_count--;
		if (retval != 0 && clp->cl_count == 0) {
			/* last guy out of scheduler_load frees the storage */
			kmem_free(clp->cl_name, strlen(clname) + 1);
			kmem_free(clp->cl_lock, sizeof (krwlock_t));
			clp->cl_name = "";
			clp->cl_lock = (krwlock_t *)NULL;
		}
	}
	mutex_exit(&class_lock);
	return (retval);

}

/*
 * Lookup a module by name.
 */
int
getcidbyname(char *clname, id_t *cidp)
{
	register sclass_t	*clp;

	if (*clname == NULL)
		return (EINVAL);

	ASSERT(MUTEX_HELD(&class_lock));

	for (clp = &sclass[0]; clp < &sclass[nclass]; clp++) {
		if (strcmp(clp->cl_name, clname) == 0) {
			*cidp = clp - &sclass[0];
			return (0);
		}
	}
	return (EINVAL);
}

/*
 * Get the scheduling parameters of the thread pointed to by
 * tp into the buffer pointed to by parmsp.
 */
void
parmsget(kthread_id_t tp, pcparms_t *parmsp)
{
	parmsp->pc_cid = tp->t_cid;
	CL_PARMSGET(tp, parmsp->pc_clparms);
}


/*
 * Check the validity of the scheduling parameters in the buffer
 * pointed to by parmsp. If our caller passes us non-NULL process
 * pointers we are also being asked to verify that the requesting
 * process (pointed to by reqpp) has the necessary permissions to
 * impose these parameters on the target process (pointed to by
 * targpp).
 * We check validity before permissions because we assume the user
 * is more interested in finding out about invalid parms than a
 * permissions problem.
 * Note that the format of the parameters may be changed by class
 * specific code which we call.
 */
int
parmsin(pcparms_t *parmsp, kthread_id_t reqtp, kthread_id_t targtp)
{
	register int		error;
	id_t			reqpcid;
	id_t			targpcid;
	register cred_t		*reqpcredp;
	register cred_t		*targpcredp;
	register caddr_t	targpclpp;
	register proc_t		*reqpp = NULL;
	register proc_t		*targpp = NULL;
	extern int 		loaded_classes;

	if (parmsp->pc_cid >= loaded_classes || parmsp->pc_cid < 1)
		return (EINVAL);

	if (reqtp != NULL)
		reqpp = ttoproc(reqtp);
	if (targtp != NULL)
		targpp = ttoproc(targtp);
	if (reqpp != NULL && targpp != NULL) {
		reqpcid = reqtp->t_cid;
		mutex_enter(&reqpp->p_crlock);
		crhold(reqpcredp = reqpp->p_cred);
		mutex_exit(&reqpp->p_crlock);
		targpcid = targtp->t_cid;
		mutex_enter(&targpp->p_crlock);
		crhold(targpcredp = targpp->p_cred);
		mutex_exit(&targpp->p_crlock);
		targpclpp = targtp->t_cldata;
	} else {
		reqpcredp = targpcredp = NULL;
		targpclpp = NULL;
	}

	/*
	 * Call the class specific routine to validate class
	 * specific parameters.  Note that the data pointed to
	 * by targpclpp is only meaningful to the class specific
	 * function if the target process belongs to the class of
	 * the function.
	 */
	error = CL_PARMSIN(&sclass[parmsp->pc_cid], parmsp->pc_clparms,
		reqpcid, reqpcredp, targpcid, targpcredp, targpclpp);
	if (error) {
		if (reqpcredp != NULL) {
			crfree(reqpcredp);
			crfree(targpcredp);
		}
		return (error);
	}

	if (reqpcredp != NULL) {
		/*
		 * Check the basic permissions required for all classes.
		 */
		if (!hasprocperm(targpcredp, reqpcredp)) {
			crfree(reqpcredp);
			crfree(targpcredp);
			return (EPERM);
		}
		crfree(reqpcredp);
		crfree(targpcredp);
	}
	return (0);
}


/*
 * Call the class specific code to do the required processing
 * and permissions checks before the scheduling parameters
 * are copied out to the user.
 * Note that the format of the parameters may be changed by the
 * class specific code.
 */
int
parmsout(pcparms_t *parmsp, kthread_id_t targtp)
{
	register int	error;
	id_t		reqtcid;
	id_t		targtcid;
	register cred_t	*reqpcredp;
	register cred_t	*targpcredp;
	register proc_t	*reqpp = ttoproc(curthread);
	register proc_t	*targpp = ttoproc(targtp);

	reqtcid = curthread->t_cid;
	targtcid = targtp->t_cid;
	mutex_enter(&reqpp->p_crlock);
	crhold(reqpcredp = reqpp->p_cred);
	mutex_exit(&reqpp->p_crlock);
	mutex_enter(&targpp->p_crlock);
	crhold(targpcredp = targpp->p_cred);
	mutex_exit(&targpp->p_crlock);

	error = CL_PARMSOUT(&sclass[parmsp->pc_cid], parmsp->pc_clparms,
		reqtcid, reqpcredp, targtcid, targpcredp, targpp);

	crfree(reqpcredp);
	crfree(targpcredp);
	return (error);
}


/*
 * Set the scheduling parameters of the thread pointed to by
 * targtp to those specified in the pcparms structure pointed
 * to by parmsp.  If reqtp is non-NULL it points to the thread
 * that initiated the request for the parameter change and indicates
 * that our caller wants us to verify that the requesting thread
 * has the appropriate permissions.
 */
int
parmsset(pcparms_t *parmsp, kthread_id_t targtp)
{
	caddr_t			clprocp;
	register int		error;
	register cred_t		*reqpcredp;
	register proc_t		*reqpp = ttoproc(curthread);
	register proc_t		*targpp = ttoproc(targtp);
	id_t			oldcid;

	ASSERT(MUTEX_HELD(&pidlock));
	ASSERT(MUTEX_HELD(&targpp->p_lock));
	if (reqpp != NULL) {
		mutex_enter(&reqpp->p_crlock);
		crhold(reqpcredp = reqpp->p_cred);
		mutex_exit(&reqpp->p_crlock);

		/*
		 * Check basic permissions.
		 */
		mutex_enter(&targpp->p_crlock);
		if (!hasprocperm(targpp->p_cred, reqpcredp)) {
			mutex_exit(&targpp->p_crlock);
			crfree(reqpcredp);
			return (EPERM);
		}
		mutex_exit(&targpp->p_crlock);
	} else {
		reqpcredp = NULL;
	}

	if (parmsp->pc_cid != targtp->t_cid) {
		size_t	bufsz;
		void	*bufp = NULL;
		/*
		 * Target thread must change to new class.
		 */
		clprocp = (caddr_t)targtp->t_cldata;
		oldcid  = targtp->t_cid;

		/*
		 * Pre-allocate scheduling class data.
		 */
		bufsz = sclass[parmsp->pc_cid].cl_size;
		if (bufsz > (size_t)0)
			bufp = kmem_alloc(bufsz, KM_NOSLEEP);
		if (bufp != NULL || bufsz == 0)
			error = CL_ENTERCLASS(targtp, parmsp->pc_cid,
			    parmsp->pc_clparms, reqpcredp, bufp);
		else
			error = ENOMEM;		/* no memory available */
		crfree(reqpcredp);
		if (error) {
			if (bufp)
				kmem_free(bufp, bufsz);
			return (error);
		}
		CL_EXITCLASS(oldcid, clprocp);
	} else {

		/*
		 * Not changing class
		 */
		error = CL_PARMSSET(targtp, parmsp->pc_clparms,
					curthread->t_cid, reqpcredp);
		crfree(reqpcredp);
		if (error)
			return (error);
	}
	return (0);
}


/*
 *	Make a thread give up its processor.  Find the processor on
 *	which this thread is executing, and have that processor
 *	preempt.
 */
void
cpu_surrender(register kthread_id_t tp)
{
	cpu_t	*cpup;
	int	max_pri;

	ASSERT(THREAD_LOCK_HELD(tp));

	if (tp->t_state != TS_ONPROC)
		return;
	cpup = tp->t_disp_queue->disp_cpu;	/* CPU thread dispatched to */
	max_pri = cpup->cpu_disp.disp_maxrunpri; /* best priority of that CPU */

	cpup->cpu_runrun = 1;
	if (max_pri >= kpreemptpri && cpup->cpu_kprunrun == 0) {
		cpup->cpu_kprunrun = 1;
	}
	/*
	 * If this not a voluntary preemption, make the target
	 * thread take an excursion through trap() to do preempt().
	 */
	if (tp != curthread) {
		aston(tp);
#ifdef MP
		if (cpup != CPU)
			poke_cpu(cpup->cpu_id);
#endif	/* MP */
	}
	TRACE_4(TR_FAC_DISP, TR_CPU_SURRENDER,
	    "cpu_surrender:tid %x (%T) pri %d surrenders cpu %d",
	    tp, tp, tp->t_pri, cpup->cpu_id);
}


/*
 * Commit to and ratify a scheduling decision
 */
static kthread_id_t
disp_ratify(kthread_id_t tp)
{
	pri_t	tpri, maxpri;
	cpu_t	*cpup;

	ASSERT(tp != NULL);
	/*
	 * Commit to, then ratify scheduling decision
	 */
	cpup = CPU;
#ifdef	MP
	if (cpup->cpu_runrun != 0)
		cpup->cpu_runrun = 0;
	if (cpup->cpu_kprunrun != 0)
		cpup->cpu_kprunrun = 0;
	if (cpup->cpu_chosen_level != -1)
		cpup->cpu_chosen_level = -1;
	lock_mutex_flush();
#else
	cpup->cpu_runrun = 0;
	cpup->cpu_kprunrun = 0;
	cpup->cpu_chosen_level = -1;
#endif
	tpri = DISP_PRIO(tp);
	maxpri = cpup->cpu_disp.disp_maxrunpri;
#ifdef	MP
	if (maxpri < disp_kp_queue.disp_maxrunpri)
		maxpri = disp_kp_queue.disp_maxrunpri;
#endif
	if (tpri < maxpri) {
		/*
		 * should have done better
		 * put this one back and indicate to try again
		 */
		cpup->cpu_dispthread = curthread;	/* fixup dispthread */
		thread_lock_high(tp);
		THREAD_TRANSITION(tp);
		setfrontdq(tp);
		thread_unlock_nopreempt(tp);

		tp = NULL;
	}
	return (tp);
}

#ifdef MP
/*
 * See if there is any work on the dispatcher queue for other CPUs.
 * If there is, dequeue the best thread and return.
 */
static kthread_id_t
disp_getwork(cpu_t *cp)
{
	cpu_t		*ocp;		/* other CPU */
	cpu_t		*tcp;		/* target CPU */
	kthread_id_t	tp;
	pri_t		maxpri;
	int		s;

	maxpri = -1;
	tcp = NULL;

	while (disp_kp_queue.disp_maxrunpri >= 0) {
		/*
		 * Try to take a thread from the kp_queue.
		 */
		tp = (disp_getbest(&disp_kp_queue));
		if (tp)
			return (disp_ratify(tp));
	}

	s = splhigh();		/* protect the cpu_active list */
	/*
	 * Try to find something to do on another CPU's run queue.
	 * Loop through all other CPUs looking for the one with the highest
	 * priority unbound thread.
	 */
	for (ocp = cp->cpu_next_onln; ocp != cp; ocp = ocp->cpu_next_onln) {
		pri_t pri;

		ASSERT(CPU_ACTIVE(ocp));
		/*
		 * Don't take thread if it is the only runnable thread.
		 * If the CPU is just handling
		 * an interrupt, taking the thread might cause it
		 * to experience unnecessary cache misses.
		 */
		if (ocp->cpu_disp.disp_nrunnable <= 1)
			continue;

		pri = ocp->cpu_disp.disp_max_unbound_pri;
		if (pri > maxpri) {
			maxpri = pri;
			tcp = ocp;
		}
	}
	(void) splx(s);

	/*
	 * If another queue looks good, try to transfer one or more threads
	 * from it to our queue.
	 */
	if (tcp) {
		tp = (disp_getbest(&tcp->cpu_disp));
		if (tp)
			return (disp_ratify(tp));
	}
	return (NULL);
}


/*
 * disp_fix_unbound_pri()
 *	Determines the maximum priority of unbound threads on the queue.
 *	The priority is kept for the queue, but is only increased, never
 *	reduced unless some CPU is looking for something on that queue.
 *
 *	The priority argument is the known upper limit.
 *
 *	Perhaps this should be kept accurately, but that probably means
 *	separate bitmaps for bound and unbound threads.  Since only idled
 *	CPUs will have to do this recalculation, it seems better this way.
 */
static void
disp_fix_unbound_pri(disp_t *dp, pri_t pri)
{
	kthread_id_t	tp;
	dispq_t		*dq;
	ulong		*dqactmap = dp->disp_qactmap;
	ulong		mapword;
	int		wx;
	int		ipri;

	ASSERT(DISP_LOCK_HELD(&dp->disp_lock));

	ASSERT(pri >= 0);			/* checked by caller */

	/*
	 * Start the search at the next lowest priority below the supplied
	 * priority.  This depends on the bitmap implementation.
	 */
	do {
		wx = pri >> BT_ULSHIFT;		/* index of word in map */

		/*
		 * Form mask for all lower priorities in the word.
		 */
		mapword = dqactmap[wx] & (BT_BIW(pri) - 1);

		/*
		 * Get next lower active priority.
		 */
		if (mapword != 0) {
			pri = (wx << BT_ULSHIFT) + highbit(mapword) - 1;
		} else if (wx > 0) {
			bt_gethighbit(dqactmap, wx - 1, &ipri);
			pri = ipri;		/* sign extend */
			if (pri < 0)
				break;
		} else {
			pri = -1;
			break;
		}

		/*
		 * Search the queue for unbound, runnable threads.
		 */
		dq = &dp->disp_q[pri];
		tp = dq->dq_first;

		while (tp != NULL && tp->t_bound_cpu != NULL) {
			tp = tp->t_link;
		}

		/*
		 * If a thread was found, set the priority and return.
		 */
	} while (tp == NULL);

	/*
	 * pri holds the maximum unbound thread priority or -1.
	 */
	if (dp->disp_max_unbound_pri != pri)
		dp->disp_max_unbound_pri = pri;
}

/*
 * disp_getbest() - de-queue the highest priority unbound runnable thread.
 *	returns with the thread unlocked and onproc
 *	but at splhigh (like disp()).
 *	returns NULL if nothing found.
 *
 *	Passed a pointer to a dispatch queue not associated with this CPU.
 */
static kthread_id_t
disp_getbest(disp_t *dp)
{
	kthread_id_t	tp;
	dispq_t		*dq;
	pri_t		pri;
	cpu_t		*cp;

	disp_lock_enter(&dp->disp_lock);

	/*
	 * If there is nothing to run, return NULL.
	 */
	pri = dp->disp_max_unbound_pri;
	if (pri == -1) {
		disp_lock_exit_nopreempt(&dp->disp_lock);
		return (NULL);
	}

	dq = &dp->disp_q[pri];
	tp = dq->dq_first;

	/*
	 * Skip over bound threads.
	 * Bound threads can be here even though disp_max_unbound_pri
	 * indicated this level.  Besides, it not always accurate because it
	 * isn't reduced until another CPU looks for work.
	 * Note that tp could be NULL right away due to this.
	 */
	while (tp != NULL && tp->t_bound_cpu != NULL) {
		tp = tp->t_link;
	}

	/*
	 * If there were no unbound threads on this queue, find the queue
	 * where they are and then return NULL so that other CPUs will be
	 * considered.
	 */
	if (tp == NULL) {
		disp_fix_unbound_pri(dp, pri);
		disp_lock_exit_nopreempt(&dp->disp_lock);
		return (NULL);
	}

	/*
	 * Found a runnable, unbound thread, so remove it from queue.
	 * dispdeq() requires that we have the thread locked, and we do,
	 * by virtue of holding the dispatch queue lock.  dispdeq() will
	 * put the thread in transition state, thereby dropping the dispq
	 * lock.
	 */
#ifdef DEBUG
	{
		boolean_t	thread_was_on_queue;

		thread_was_on_queue = dispdeq(tp);	/* drops disp_lock */
		ASSERT(thread_was_on_queue);
	}
#else /* DEBUG */
	(void) dispdeq(tp);			/* drops disp_lock */
#endif /* DEBUG */

	tp->t_schedflag |= TS_DONT_SWAP;
	if (tp->t_proc_flag & TP_MSACCT)
		restore_mstate(tp);

	/*
	 * Setup thread to run on the current CPU.
	 */
	cp = CPU;

	tp->t_disp_queue = &cp->cpu_disp;

	cp->cpu_dispthread = tp;		/* protected by spl only */

	thread_onproc(tp, cp);			/* set t_state to TS_ONPROC */

	/*
	 * Return with spl high so that swtch() won't need to raise it.
	 * The disp_lock was dropped by dispdeq().
	 */

	return (tp);
}

/*
 * disp_bound_threads - return nonzero if threads are bound to the processor.
 *	Called infrequently.  Keep this simple.
 *	Includes threads that are asleep or stopped but not onproc.
 */
/* ARGSUSED */
int
disp_bound_threads(cpu_t *cp)
{
	int		found = 0;
	kthread_id_t	tp;

	mutex_enter(&pidlock);
	tp = curthread;		/* faster than allthreads */
	do {
		if (tp->t_state & (TS_SLEEP | TS_RUN | TS_STOPPED)) {
			/*
			 * Skip the idle thread for the CPU
			 * we're about to set offline.
			 */
			if (tp == cp->cpu_idle_thread)
				continue;

			/*
			 * Skip the pause thread for the CPU
			 * we're about to set offline.
			 */
			if (tp == cp->cpu_pause_thread)
				continue;

			if (tp->t_bound_cpu == cp) {
				found = 1;
				break;
			}
		}
	} while ((tp = tp->t_next) != curthread && found == 0);
	mutex_exit(&pidlock);
	return (found);
}

/*
 * disp_cpu_inactive - make a CPU inactive by moving all of its unbound
 * threads to other CPUs.
 */
/* ARGSUSED */
void
disp_cpu_inactive(cpu_t *cp)
{
	kthread_id_t	tp;
	disp_t		*dp = &cp->cpu_disp;
	dispq_t		*dq;
	pri_t		pri;
	boolean_t	wasonq;

	disp_lock_enter(&dp->disp_lock);
	while ((pri = dp->disp_max_unbound_pri) != -1) {
		dq = &dp->disp_q[pri];
		tp = dq->dq_first;

		/*
		 * Skip over bound threads.
		 */
		while (tp != NULL && tp->t_bound_cpu != NULL) {
			tp = tp->t_link;
		}

		if (tp == NULL) {
			/* disp_max_unbound_pri must be inaccurate, so fix it */
			disp_fix_unbound_pri(dp, pri);
			continue;
		}

		wasonq = dispdeq(tp);		/* drops disp_lock */
		ASSERT(wasonq);

		setbackdq(tp);
		/*
		 * cp has already been removed from the list of active cpus
		 * and tp->t_cpu has been changed so there is no risk of
		 * tp ending up back on cp.
		 */
		ASSERT(tp->t_cpu != cp);
		thread_unlock(tp);

		disp_lock_enter(&dp->disp_lock);
	}
	disp_lock_exit(&dp->disp_lock);
}
#endif /* MP */

/*
 * disp_lowpri_cpu - find CPU running the lowest priority thread.
 *	The hint passed in is used as a starting point so we don't favor
 *	CPU 0 or any other CPU.  The caller should pass in the most recently
 *	used CPU for the thread.
 *
 *	This function must be called at either high SPL, or with preemption
 *	disabled, so that the "hint" CPU cannot be removed from the online
 *	CPU list while we are traversing it.
 */
cpu_t *
disp_lowpri_cpu(cpu_t *hint)
{
	cpu_t   *bestcpu;
	cpu_t   *cp;
	pri_t   bestpri;
	pri_t   cpupri;

	/*
	 * Scan for a CPU currently running the lowest priority thread.
	 * Cannot get cpu_lock here because it is adaptive.
	 * We do not require lock on CPU list.
	 */
	bestpri = SHRT_MAX;		/* maximum possible priority */
	cp = bestcpu = hint;
	do {
		cpupri = DISP_PRIO(cp->cpu_dispthread);
		if (cp->cpu_disp.disp_maxrunpri > cpupri)
			cpupri = cp->cpu_disp.disp_maxrunpri;
		if (cp->cpu_chosen_level > cpupri)
			cpupri = cp->cpu_chosen_level;
		if (cpupri < bestpri) {
			bestcpu = cp;
			bestpri = cpupri;
		}
	} while ((cp = cp->cpu_next_onln) != hint);

	/*
	 * The chosen CPU could be offline, but not quiesced, in the
	 * case of sys_cpu_choose() on the clock interrupt thread.
	 */
	ASSERT((bestcpu->cpu_flags & CPU_QUIESCED) == 0);
	return (bestcpu);
}

/*
 * This routine provides the generic idle cpu function for all processors.
 * If a processor has some specific code to execute when idle (say, to stop
 * the pipeline and save power) then that routine should be defined in the
 * processors specific code (module_xx.c) and the global variable idle_cpu
 * set to that function.
 */
static void
generic_idle_cpu()
{
}
