/*
 * Copyright (c) 1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mp_startup.c	1.33	95/05/26 SMI"

#include <sys/types.h>
#include <sys/thread.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/class.h>
#include <sys/cmn_err.h>
#include <sys/map.h>
#include <sys/intreg.h>
#include <sys/debug.h>
#include <sys/x_call.h>
#include <sys/vtrace.h>
#include <sys/var.h>
#include <sys/promif.h>
#include <vm/hat_sfmmu.h>
#include <vm/as.h>
#include <sys/systm.h>
#include <sys/machsystm.h>
#include <sys/callb.h>

#ifdef TRAPTRACE
#include <sys/kmem.h>
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

struct cpu	cpu0;	/* the first cpu data; statically allocate */
struct cpu	*cpus;	/* pointer to other cpus; dynamically allocate */
struct cpu	*cpu[NCPU];		/* pointers to all CPUs */

extern int snooping;
extern int snoop_interval;
extern void deadman();
extern void tickcmpr_reset();
extern void tickint_clnt_add();


#ifdef	MP				/* Around almost entire file */

/* bit mask of cpus ready for x-calls */
cpuset_t cpu_ready_set;

/* bit mask of cpus to bring up */
#ifdef MPSAS
cpuset_t cpu_bringup_set = (u_int)0x1;
#else
cpuset_t cpu_bringup_set = (u_int)CPUSET_ALL;
#endif

/*
 * Useful for disabling MP bring-up for an MP capable kernel
 * (a kernel that was built with MP defined)
 */
int use_mp = 1;			/* set to come up mp */

u_int	cpu_nodeid[NCPU];		/* XXX - should be in machcpu */

static void	mp_startup(void);

/*
 * Callback routines to support cpr
 */
static void	cprboot_mp_startup(void);
static void	cprboot_mp_startup_init(int);
static void	mpstart_cpr_callb(void *, int code);

/*
 * Init CPU info - get CPU type info for processor_info system call.
 */
static void
init_cpu_info(struct cpu *cp)
{
	register processor_info_t *pi = &cp->cpu_type_info;
	int	clock_freq = 0;
	int	cpuid;
	struct cpu_node *cpunode;
	extern char cpu_info_buf[NCPU][CPUINFO_SZ];

	cpuid = cp->cpu_id;
	cp->cpu_m.cpu_info = &cpu_info_buf[cpuid][0];

	/*
	 * Get clock-frequency property from cpunodes[] for the CPU.
	 */
	cpunode = &cpunodes[cpuid];
	clock_freq = cpunode->clock_freq;

	pi->pi_clock = (clock_freq + 500000) / 1000000;

	(void) strcpy(pi->pi_processor_type, "sparc");
	(void) strcpy(pi->pi_fputypes, "sparc");
}

/*
 * Multiprocessor initialization.
 *
 * Allocate and initialize the cpu structure, startup and idle threads
 * for the specified CPU.
 *
 * If cprboot is set, it is called by the cpr callback routine other
 * than normal boot.
 */
static void
mp_startup_init(cpuid, cprboot)
	register int cpuid;
	int cprboot;
{
	struct cpu *cp;
	kthread_id_t tp;
	caddr_t	sp;
	proc_t *procp;
	char buf[100];
	struct sfmmu *sfmmup;
	extern pri_t maxclsyspri;
	extern void idle();
	extern void init_intr_threads(struct cpu *);
	static int cpunum;
#ifdef TRAPTRACE
	TRAP_TRACE_CTL	*ctlp;
	caddr_t	newbuf;
#endif /* TRAPTRACE */

	ASSERT((cpuid < NCPU && cpu[cpuid] == NULL) || cprboot);

	/*
	 * Obtain pointer to the appropriate cpu structure.
	 */
	if (cprboot)
		cp = cpu[cpuid];
	else
		cp = &cpus[cpunum];

	procp = curthread->t_procp;

#ifdef TRAPTRACE
	/*
	 * allocate a traptrace buffer for this CPU.
	 */
	ctlp = &trap_trace_ctl[cpuid];
	newbuf = (caddr_t) kmem_zalloc(trap_trace_bufsize, KM_NOSLEEP);
	if (newbuf == NULL) {
		cmn_err(CE_PANIC,
	"mp_startup_init: Can't create traptrace buffer for cpu: %d", cpuid);
		/*NOTREACHED*/
	}
	ctlp->d.vaddr_base = newbuf;
	ctlp->d.offset = ctlp->d.last_offset = 0;
	ctlp->d.limit = trap_trace_bufsize;
	ctlp->d.paddr_base = va_to_pa(newbuf);

#endif /* TRAPTRACE */

	/*
	 * Allocate and initialize the startup thread for this CPU.
	 */
	tp = thread_create(NULL, NULL, mp_startup_init, NULL, 0, procp,
	    TS_STOPPED, maxclsyspri);
	if (tp == NULL) {
		cmn_err(CE_PANIC,
	"mp_startup_init: Can't create startup thread for cpu: %d", cpuid);
		/*NOTREACHED*/
	}

	/*
	 * Set state to TS_ONPROC since this thread will start running
	 * as soon as the CPU comes online.
	 *
	 * All the other fields of the thread structure are setup by
	 * thread_create().
	 */
	THREAD_ONPROC(tp, cp);
	tp->t_preempt = 1;
	tp->t_bound_cpu = cp;
	tp->t_affinitycnt = 1;
	tp->t_cpu = cp;

	sfmmup = astosfmmu(&kas);
	CPUSET_ADD(sfmmup->sfmmu_cpusran, cpuid);

	/*
	 * Setup thread to start in mp_startup.
	 */
	sp = tp->t_stk;
	if (cprboot)
		tp->t_pc = (u_int)cprboot_mp_startup - 8;
	else
		tp->t_pc = (u_int)mp_startup - 8;
	tp->t_sp = (u_int)((struct rwindow *)sp - 1);

	cp->cpu_id = cpuid;
	cp->cpu_thread = tp;
	cp->cpu_lwp = NULL;
	cp->cpu_dispthread = tp;
#ifdef TRACE
	cp->cpu_trace.event_map = null_event_map;
#endif /* TRACE */

	if (cprboot)
		return;

	/*
	 * Now, initialize per-CPU idle thread for this CPU.
	 */
	tp = thread_create(NULL, NBPG, idle, NULL, 0, procp, TS_ONPROC, -1);
	if (tp == NULL) {
		cmn_err(CE_PANIC,
		"mp_startup_init: Can't create idle thread for cpu: %d", cpuid);
		/*NOTREACHED*/
	}
	cp->cpu_idle_thread = tp;

	tp->t_preempt = 1;
	tp->t_bound_cpu = cp;
	tp->t_affinitycnt = 1;
	tp->t_cpu = cp;

	init_cpu_info(cp);

	/*
	 * Initialize per-CPU statistics locks.
	 */
	sprintf(buf, "cpu %d statistics lock", cpuid);
	mutex_init(&cp->cpu_stat.cpu_stat_lock, buf,
	    MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Initialize the interrupt threads for this CPU
	 *
	 * We used to do it in mp_startup() - but that's just wrong
	 * - we might sleep while allocating stuff from segkp - that
	 * would leave us in a yucky state where we'd be handling
	 * interrupts without an interrupt thread .. see 1120597.
	 */
	init_intr_pool(cp);
	init_intr_threads(cp);

	/*
	 * Record that we have another CPU.
	 */
	mutex_enter(&cpu_lock);
	/*
	 * Add CPU to list of available CPUs.  It'll be on the active list
	 * after mp_startup().
	 */
	cpu_add_unit(cp);
	mutex_exit(&cpu_lock);
	cpunum++;
}

/*
 * If cprboot is set, it is called by the cpr callback routine other
 * than normal boot.
 */
void
start_other_cpus(cprboot)
	int cprboot;
{
	extern struct cpu_node cpunodes[];
	dnode_t nodeid;
	int cpuid, mycpuid;
	int delays;
	int i;

	extern caddr_t cpu_startup;

	/*
	 * Initialize our own cpu_info.
	 */
	if (!cprboot) {
		init_cpu_info(CPU);
		cmn_err(CE_CONT, "!cpu 0 initialization complete - online\n");
	}

	CPUSET_ADD(cpu_ready_set, CPU->cpu_id);

	if (!use_mp) {
		cmn_err(CE_CONT, "?***** Not in MP mode\n");
		return;
	}
	/*
	 * perform such initialization as is needed
	 * to be able to take CPUs on- and off-line.
	 */
	if (!cprboot) {
		cpu_pause_init();
		xc_init();		/* initialize processor crosscalls */
	}

	mycpuid = getprocessorid();

	for (i = 0; i < NCPU; i++) {
		if ((nodeid = cpunodes[i].nodeid) <= (dnode_t)0)
			continue;

		cpuid = UPAID2CPU(cpunodes[i].upaid);
		cpu_nodeid[cpuid] = (u_int)nodeid;

		if ((cpuid == mycpuid) ||
		    ((cpu_bringup_set & CPUSET(cpuid)) == 0)) {
			continue;
		}

		if (cprboot)
			cprboot_mp_startup_init(cpuid);
		else
			mp_startup_init(cpuid, 0);

		(void) prom_startcpu(nodeid, (caddr_t)&cpu_startup, cpuid);

		DELAY(50);	/* let's give it a little of time */

		delays = 0;
		while (!CPU_IN_SET(cpu_ready_set, cpuid)) {
			DELAY(0x10000);
			delays++;
			if (delays > 20) {
				cmn_err(CE_WARN,
					"cpu %d node %x failed to start\n",
					cpuid, nodeid);
				break;
			}
		}
	}
	if (!cprboot)
		callb_add(mpstart_cpr_callb, 0, CB_CL_CPR_MPSTART, "mpstart");
}

/*
 * Startup function for 'other' CPUs (besides 0).
 * Resumed from cpu_startup.
 */
static void
mp_startup(void)
{
	struct cpu *cp = CPU;

	cp->cpu_m.mutex_ready = 1;
	(void) spl0();				/* enable interrupts */
	mutex_enter(&cpu_lock);
	CPUSET_ADD(cpu_ready_set, cp->cpu_id);

	cp->cpu_flags |= CPU_RUNNING | CPU_READY |
	    CPU_ENABLE | CPU_EXISTS;		/* ready */
	cpu_add_active(cp);

	mutex_exit(&cpu_lock);

	/*
	 * Because mp_startup() gets fired off after init() starts, we
	 * can't use the '?' trick to do 'boot -v' printing - so we
	 * always direct the 'cpu .. online' messages to the log.
	 */
	cmn_err(CE_CONT, "!cpu %d initialization complete - online\n",
	    cp->cpu_id);

	/* reset TICK_Compare register */
	tickcmpr_reset();
	if (snooping)
		tickint_clnt_add(deadman, snoop_interval);


	/*
	 * Now we are done with the startup thread, so free it up.
	 */
	thread_exit();
	cmn_err(CE_PANIC, "mp_startup: cannot return");
	/*NOTREACHED*/
}

static void
cprboot_mp_startup_init(cpun)
	register int cpun;
{
	struct cpu *cp;
	kthread_id_t tp;
	caddr_t sp;
	extern void idle();

	mp_startup_init(cpun, 1);

	/*
	 * idle thread t_lock is held when the idle thread is suspended.
	 * Manually unlock the t_lock of idle loop so that we can resume
	 * the suspended idle thread.
	 * Also adjust the PC of idle thread for re-retry.
	 */
	cp = cpu[cpun];
	cp->cpu_on_intr = 0;	/* clear the value from previous life */
	lock_clear(&cp->cpu_idle_thread->t_lock);
	tp = cp->cpu_idle_thread;

	sp = tp->t_stk;
	tp->t_sp = (u_int)((struct rwindow *)sp - 1);
	tp->t_pc = (u_int) idle - 8;
}

static void
cprboot_mp_startup(void)
{
	struct cpu *cp = CPU;

	(void) spl0();		/* enable interrupts */

	mutex_enter(&cpu_lock);
	CPUSET_ADD(cpu_ready_set, cp->cpu_id);

	/*
	 * The cpu was offlined at suspend time. Put it back to the same state.
	 */
	CPU->cpu_flags |= CPU_RUNNING | CPU_READY | CPU_EXISTS
		| CPU_OFFLINE | CPU_QUIESCED;

	mutex_exit(&cpu_lock);

	/*
	 * Now we are done with the startup thread, so free it up and switch
	 * to idle thread. thread_exit() must be used here because this is
	 * the firest thread in the system since boot and normal scheduling
	 * is not ready yet.
	 */
	thread_exit();
	cmn_err(CE_PANIC, "cprboot_mp_startup: cannot return");
	/*NOTREACHED*/
}

/*ARGSUSED*/
void
mpstart_cpr_callb(void *arg, int code)
{
	cpu_t	*cp;

	switch (code) {
	case CB_CODE_CPR_CHKPT:
		break;

	case CB_CODE_CPR_RESUME:
		/*
		 * All of the non-boot cpus are not ready at this moment, yet
		 * the previous kernel image resumed has cpu_flags ready and
		 * other bits set. It's necesssary to clear the cpu_flags to
		 * match the cpu h/w status; otherwise x_calls are not going
		 * to work properly.
		 */
		for (cp = CPU->cpu_next; cp != CPU; cp = cp->cpu_next) {
			cp->cpu_flags = 0;
			CPUSET_DEL(cpu_ready_set, cp->cpu_id);
		}

		start_other_cpus(1);
		break;
	}
}

/*
 * Start CPU on user request.
 */
/* ARGSUSED */
int
mp_cpu_start(struct cpu *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	return (0);			/* nothing special to do on this arch */
}

/*
 * Stop CPU on user request.
 */
/* ARGSUSED */
int
mp_cpu_stop(struct cpu *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	return (0);			/* nothing special to do on this arch */
}

/*
 * Take the specified CPU out of participation in interrupts.
 */
int
cpu_disable_intr(struct cpu *cp)
{
#ifdef FIXME
	extern void update_itr(processorid_t);
#endif /* FIXME */

	ASSERT(MUTEX_HELD(&cpu_lock));

	cp->cpu_flags &= ~CPU_ENABLE;
#ifdef FIXME
	update_itr(CPU->cpu_id);	/* change ITR to next (or best) CPU */
#endif /* FIXME */
	return (0);
}

/*
 * Allow the specified CPU to participate in interrupts.
 */
void
cpu_enable_intr(struct cpu *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));

	cp->cpu_flags |= CPU_ENABLE;
}

#endif	/* MP */
