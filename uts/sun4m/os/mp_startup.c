/*
 * Copyright (c) 1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mp_startup.c	1.70	95/05/15 SMI"

#include <sys/types.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/class.h>
#include <sys/cmn_err.h>
#include <sys/map.h>
#include <sys/intreg.h>
#include <sys/debug.h>
#include <sys/asm_linkage.h>
#include <sys/x_call.h>
#include <sys/asm_linkage.h>
#include <sys/var.h>
#include <sys/vtrace.h>
#include <sys/promif.h>
#include <sys/systm.h>
#include <sys/machsystm.h>
#include <sys/module.h>
#include <sys/clock.h>
#include <vm/hat_srmmu.h>	/* needed for contexts below */
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>
#include <sys/callb.h>

/*
 * Currently, must statically allocate CPU, and thread structs for all CPUs.
 */
struct cpu	cpus[NCPU];			/* CPU data */
struct cpu	*cpu[NCPU] = {&cpus[0]};	/* pointers to all CPUs */

extern int vac;

#ifdef	MP				/* Around almost entire file */
/*
 * Useful for disabling MP bring-up for an MP capable kernel
 * (a kernel that was built with MP defined)
 */
int use_mp = 1;

int mp_cpus = 0xf;	/* Use all CPU's (4) */

/*
 * This variable is used by the hat layer to decide whether or not
 * critical sections are needed to prevent race conditions.  For sun4m,
 * this variable is set once enough MP initialization has been done in
 * order to allow cross calls.
 */
int flushes_require_xcalls = 0;

static void	mp_startup(void);
static void	cprboot_mp_startup(void);
static void	cprboot_mp_startup_init(int);
static void	mpstart_cpr_callb(void *, int code);
static caddr_t	map_regs(caddr_t, u_int, int);
extern void	vac_allflush(u_int);
extern void	update_itr(int);

caddr_t prom_mailbox[NCPU];		/* XXX - should be part of machcpu? */
u_int	cpu_nodeid[NCPU];		/* XXX - should be in machcpu */


/*
 * Init CPU info - get CPU type info for processor_info system call.
 */
void
init_cpu_info(struct cpu *cp)
{
	register processor_info_t *pi = &cp->cpu_type_info;
	dnode_t		root;
	dnode_t		nodeid;
	unsigned	who;
	unsigned	clock_freq = 0;
	static	char	freq[] = "clock-frequency";

	/*
	 * Get clock-frequency property for the CPU.
	 * Find node for CPU.  The first CPU is initialized before the
	 * cpu_nodeid table is setup.
	 */
	root = prom_nextnode((dnode_t)0);	/* root node */
	for (nodeid = prom_childnode(root);
	    (nodeid != OBP_NONODE) && (nodeid != OBP_BADNODE);
	    nodeid = prom_nextnode(nodeid)) {
		if ((prom_getproplen(nodeid, "mid") == sizeof (who)) &&
		    (prom_getprop(nodeid, "mid", (caddr_t)&who) != -1)) {

			if (MID2CPU(who) == cp->cpu_id) {
				/*
				 * get clock frequency from CPU or root node.
				 * This will be zero if the property is
				 * not there.  Round to nearest megahertz.
				 */
				if (prom_getproplen(nodeid, freq) ==
				    sizeof (clock_freq))
					(void) prom_getprop(nodeid, freq,
					    (caddr_t)&clock_freq);
				break;
			}
		}
	}

	/*
	 * If we didn't find it in the CPU node, look in the root node.
	 */
	if (clock_freq == 0 &&
	    prom_getproplen(root, freq) == sizeof (clock_freq))
		(void) prom_getprop(root, freq, (caddr_t)&clock_freq);

	pi->pi_clock = (clock_freq + 500000) / 1000000;

	strcpy(pi->pi_processor_type, "sparc");
	strcpy(pi->pi_fputypes, "sparc");
}

/*
 * Multiprocessor initialization.
 *
 * Allocate and initialize the cpu structure, startup and idle threads
 * for the specified CPU.
 *
 * If cprboot is set, this procedure is called by the cpr callback routine
 * other than normal boot.
 */
static void
mp_startup_init(cpun, cprboot)
	register int cpun;
	int cprboot;
{
	struct cpu *cp;
	kthread_id_t tp;
	caddr_t	sp;
	proc_t *procp;
	extern void idle();
	char buf[100];
	extern pri_t maxclsyspri;
#ifdef	BCOPY_BUF
	extern int bcopy_buf;
#endif
	extern void init_intr_threads(struct cpu *);

	ASSERT((cpun < NCPU && cpu[cpun] == NULL) || cprboot);

	/*
	 * Obtain pointer to the appropriate cpu structure.
	 */
	cp = &cpus[cpun];
	procp = curthread->t_procp;

	/*
	 * Allocate and initialize the startup thread for this CPU.
	 */
	tp = thread_create(NULL, NULL, mp_startup_init, NULL, 0, procp,
	    TS_STOPPED, maxclsyspri);
	if (tp == NULL) {
		cmn_err(CE_PANIC,
	"mp_startup_init: Can't create startup thread for cpu: %d", cpun);
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

	/*
	 * Setup thread to start in mp_startup.
	 */
	sp = tp->t_stk;
	if (cprboot)
		tp->t_pc = (u_int)cprboot_mp_startup - 8;
	else
		tp->t_pc = (u_int)mp_startup - 8;
	tp->t_sp = (u_int)((struct rwindow *)sp - 1);

	cp->cpu_id = cpun;
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
		"mp_startup_init: Can't create idle thread for cpu: %d", cpun);
		/*NOTREACHED*/
	}
	cp->cpu_idle_thread = tp;

	tp->t_preempt = 1;
	tp->t_bound_cpu = cp;
	tp->t_affinitycnt = 1;
	tp->t_cpu = cp;

	cp->cpu_m.in_prom = 0;
	init_cpu_info(cp);

#ifdef	BCOPY_BUF
	cp->cpu_m.bcopy_res = bcopy_buf ? 0 : -1;
#endif

	/*
	 * Initialize per-CPU statistics locks.
	 */
	sprintf(buf, "cpu %d statistics lock", cpun);
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
}

#ifdef SNOOPING
int snooping = 0;
#endif /* SNOOPING */

/*
 * If cprboot is set, this procedure is called by the cpr callback routine
 * other than the nowmal boot.
 */
void
start_other_cpus(cprboot)
	int cprboot;
{
	dnode_t nodeid;
	unsigned who;
	int cpuid;
	int delays = 0;
	int mp_setfunc_done = 0;

	extern int procset;
	extern caddr_t cpu_start_addrs[];

	/*
	 * Initialize our own cpu_info.
	 */
	if (!cprboot)
		init_cpu_info(CPU);

	cmn_err(CE_CONT, "!cpu 0 initialization complete - online\n");
	if (!use_mp) {
		cmn_err(CE_CONT, "?***** Not in MP mode\n");
		return;
	}

	/*
	 * perform such initialization as is needed
	 * to be able to take CPUs on- and off-line.
	 */
	cpu_pause_init();

	if (!cprboot)
		xc_init();		/* initialize processor crosscalls */
	cpuid = getprocessorid();

#ifdef SNOOPING
	if (snooping)
		start_snooping(CPU->cpu_id);
#endif /* SNOOPING */

	nodeid = prom_nextnode((dnode_t)0); /* root node */
	for (nodeid = prom_childnode(nodeid);
	    (nodeid != OBP_NONODE) && (nodeid != OBP_BADNODE);
	    nodeid = prom_nextnode(nodeid)) {
		if ((prom_getproplen(nodeid, "mid") == sizeof (who)) &&
		    (prom_getprop(nodeid, "mid", (caddr_t)&who) != -1)) {
			struct prom_reg reg;

			who = MID2CPU(who);
			cpu_nodeid[who] = (u_int) nodeid;
#define	PROM_MAILBOX
#ifdef	PROM_MAILBOX
			if ((prom_getproplen(nodeid, "mailbox") ==
			    sizeof (reg)) && (prom_getprop(nodeid, "mailbox",
			    (caddr_t)&reg) != -1) && !cprboot) {
				prom_mailbox[who] =
				    (caddr_t)map_regs((caddr_t)reg.lo,
					reg.size, (int)reg.hi);
			}
#endif	PROM_MAILBOX

			if (who != cpuid) {
				if ((mp_cpus & (1 << who)) == 0)
					continue;	/* skip this CPU */

				/*
				 * Indicate that for the sun4m architecture
				 * that cache flushes require x-calls.  Note
				 * that x-calls are initiated from code
				 * which may be shared by machines that may
				 * not require x-calls.
				 */
				if (!mp_setfunc_done && !cprboot) {
					mp_setfunc();
					mp_setfunc_done = 1;
					flushes_require_xcalls = 1;
				}

				reg.hi = 0;
				reg.lo = va_to_pa((u_int)contexts);
				reg.size = 0;
				if (cprboot)
					cprboot_mp_startup_init(who);
				else
					mp_startup_init(who, 0);
				/*
				 * It is important to flush the VAC each
				 * time before starting up a CPU, since
				 * the CPUs that will be started do not
				 * have their caches on yet, and in
				 * copy-back (write-back) mode they will
				 * not see data just written.  This has
				 * lead to a variety of bizzare failures.
				 */
				if (vac) {
					XCALL_PROLOG
					vac_allflush(FL_TLB_CACHE);
					XCALL_EPILOG
				}
				(void) prom_startcpu(nodeid, &reg, 0,
				    (caddr_t) cpu_start_addrs[who]);

				while ((procset & (1 << who)) == 0) {
					delay(1); /* wait one clock tick */
					delays++;
					if (delays > 20*HZ) {
					    cmn_err(CE_WARN,
					    "cpu %d failed to start\n", who);
					    break;
					}
				}
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
	extern int procset;

	(void) spl0();				/* enable interrupts */

	mutex_enter(&cpu_lock);

	cp->cpu_flags |= CPU_RUNNING | CPU_READY |
	    CPU_ENABLE | CPU_EXISTS;		/* ready */
	cpu_add_active(cp);

	procset |= 1 << cp->cpu_id;

	mutex_exit(&cpu_lock);

	/*
	 * Because mp_startup() gets fired off after init() starts, we
	 * can't use the '?' trick to do 'boot -v' printing - so we
	 * always direct the 'cpu .. online' messages to the log.
	 */
	cmn_err(CE_CONT, "!cpu %d initialization complete - online\n",
	    cp->cpu_id);

#ifdef SNOOPING
	if (snooping)
		start_snooping(CPU->cpu_id);
#endif /* SNOOPING */

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
	caddr_t	sp;
	extern void idle();

	mp_startup_init(cpun, 1);

	/*
	 * t_lock of idle thread is held when the idle thread is check pointed.
	 * Manually unlock the t_lock of idle loop so that we can resume the
	 * check pointed idle thread.
	 * Also adjust idle thread's PC for re-entry.
	 */
	cp = &cpus[cpun];
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

	(void) spl0();				/* enable interrupts */

	mutex_enter(&cpu_lock);

	/*
	 * The cpu was offlined at suspend time. Put it back to the same state.
	 */
	CPU->cpu_flags |= CPU_RUNNING | CPU_READY | CPU_EXISTS
		| CPU_OFFLINE | CPU_QUIESCED;

	mutex_exit(&cpu_lock);

#ifdef SNOOPING
	if (snooping)
		start_snooping(CPU->cpu_id);
#endif /* SNOOPING */

	/*
	 * Now we are done with the startup thread, so free it up and switch
	 * to idle thread. The thread_exit must be used here because this is the
	 * first thread in the system since boot and normal scheduling
	 * is not ready yet.
	 */
	thread_exit();
	cmn_err(CE_PANIC, "mp_startup: cannot return");
	/*NOTREACHED*/
}

/*ARGSUSED*/
static void
mpstart_cpr_callb(void *arg, int code)
{
	cpu_t *cp;

	switch (code) {
	case CB_CODE_CPR_CHKPT:
		break;

	case CB_CODE_CPR_RESUME:
		/*
		 * All of the non-boot cpus are not ready at this moment, yet
		 * the previous kernel image resumed has cpu_flags ready and
		 * other bits set. It's necessary to clear the cpu_flags to
		 * match the cpu h/w status, otherwise x_calls are not going to
		 * work when using VAC.
		 */
		for (cp = CPU->cpu_next; cp != CPU; cp = cp->cpu_next) {
			cp->cpu_flags = 0;
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
	ASSERT(MUTEX_HELD(&cpu_lock));

	/*
	 * The ITR can be reliably changed only by the CPU which owns it.
	 * To be sure that the CPU we're disabling isn't targeted,
	 * call update_itr() on that CPU.
	 */
	affinity_set(cp->cpu_id);	/* move to the targeted CPU */
	cp->cpu_flags &= ~CPU_ENABLE;
	update_itr(cp->cpu_id);		/* change ITR to next (or best) CPU */
	affinity_clear();
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

#ifdef	PROM_MAILBOX

/*
 * XXX	There must be a better way to do this. It's only used for
 *	mapping in the mailbox registers in start_other_cpus();
 *	This is NOT a driver interface!
 */

#include <sys/vm.h>
#include <sys/mman.h>

/*
 * Given a physical address, a bus type, and a size, return a virtual
 * address that maps the registers.
 * Returns NULL on any error.
 * NOTE: for Sun-4M, the allowable values for "space" are 0..15
 * specifying the upper four bits of the 36-bit physical address,
 * or SPO_VIRTUAL. Other values will result in an error.
 */
static caddr_t
map_regs(caddr_t addr, u_int size, int space)
{
	register caddr_t reg = NULL;
	u_int pageval;
	int offset = (int)addr & PGOFSET;
	long a = 0;
	int extent;

/*
 * XXX	This will go away with the boot stick, so bear with it for now.
 * XXX	The boot stick is here .. so why is this still here?
 */
#define	SPO_VIRTUAL	(-1)

	if (space == SPO_VIRTUAL)
		return (addr);
	else if (space < 16)
		pageval = btop((u_int)addr) + (btop(space<<28)<<4);
	else
		return (0);

	extent = btoc(size + offset);
	if (extent == 0)
		extent = 1;
	a = rmalloc(kernelmap, (size_t)extent);
	if (a == 0)
		panic("out of kernelmap for devices");
	reg = (caddr_t)((int)kmxtob(a) | offset);
	segkmem_mapin(&kvseg, (caddr_t)((int)reg&PAGEMASK),
		(u_int)mmu_ptob(extent), PROT_READ | PROT_WRITE,
		pageval, 0);
	return (reg);
}
#endif	/* PROM_MAILBOX */

#endif	/* MP */
