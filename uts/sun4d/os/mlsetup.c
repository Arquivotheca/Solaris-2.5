/*
 * Copyright (c) 1990-1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mlsetup.c	1.36	95/03/23 SMI"

#include <sys/types.h>
#include <sys/privregs.h>
#include <sys/systm.h>
#include <sys/disp.h>
#include <sys/msgbuf.h>
#include <sys/promif.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/led.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/stack.h>
#include <sys/machsystm.h>
#include <sys/eeprom.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/reboot.h>
#include <sys/debug/debug.h>
#include <sys/machpcb.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */

#include <sys/sunddi.h>
#include <vm/as.h>
#include <vm/hat_srmmu.h>
#include <sys/vtrace.h>

/*
 * External Routines:
 */
extern void level15_mlsetup(void);
extern void setcpudelay(void);

/*
 * Global Routines:
 * mlsetup()
 */

/*
 * Global Data:
 */

struct _klwp lwp0;
struct proc p0;

/*
 * Static Routines:
 */
static void kern_splr_preprom(void);
static void kern_splx_postprom(void);

/*
 * First FCS OBP is rev. 2.11, which is 0x2000b in romvec.
 */
#define	FIRST_FCS_PROM_ID	(0x2000b)

/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */

void
mlsetup(struct regs *rp, int cpu_index, void *cookie)
{
	extern struct classfuncs sys_classfuncs;
	extern pri_t maxclsyspri;
	extern struct count14 *utimersp;
	extern void setcputype(void);

	extern struct cpu cpu0;
	extern u_int clk10_phyaddr;
	extern int do_stopcpu;
	extern u_int last_cpu_id;

	extern caddr_t startup_alloc_vaddr;
	extern caddr_t startup_alloc_size;

	struct cpu *self = &cpu0;
	struct machpcb *mpcb;

	cpu[cpu_index] = self;
	cpu0.cpu_id = cpu_index;
	last_cpu_id = cpu_index;

	led_set_ecsr(cpu_index, 0x55);		/* indicate kernel is alive */

	/* self->cpu_id = cpu_index; */
	self->cpu_flags = CPU_EXISTS;

	/*
	 * initialize t0
	 */
	t0.t_stk = (caddr_t)rp - MINFRAME;
	t0.t_pri = maxclsyspri - 3;
	t0.t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t0.t_procp = &p0;
	t0.t_lwp = &lwp0;
	t0.t_forw = &t0;
	t0.t_back = &t0;
	t0.t_prev = &t0;
	t0.t_next = &t0;
	t0.t_cpu = self;
	t0.t_disp_queue = &self->cpu_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_clfuncs = &sys_classfuncs.thread;
	THREAD_ONPROC(&t0, self);	/* set t_state to TS_ONPROC */

	lwp0.lwp_thread = &t0;
	lwp0.lwp_regs = (struct regs *)rp;
	lwp0.lwp_procp = &p0;
	t0.t_tid = p0.p_lwpcnt = p0.p_lwprcnt = p0.p_lwptotal = 1;

	mpcb = lwptompcb(&lwp0);
	mpcb->mpcb_fpu.fpu_q = mpcb->mpcb_fpu_q;
	mpcb->mpcb_thread = &t0;
	lwp0.lwp_fpu = (void *)&mpcb->mpcb_fpu;

	p0.p_exec = NULL;
	p0.p_stat = SRUN;
	p0.p_flag = SSYS;
	p0.p_tlist = &t0;
	p0.p_stksize = 2*PAGESIZE;
	p0.p_as = &kas;
	sigorset(&p0.p_ignore, &ignoredefault);

	self->cpu_thread = &t0;
	self->cpu_dispthread = &t0;
	self->cpu_idle_thread = &t0;
	self->cpu_disp.disp_cpu = self;
	self->cpu_flags |= CPU_READY | CPU_RUNNING | CPU_ENABLE;
#ifdef sun4m
	self->cpu_m.in_prom = 0;
#endif sun4m

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(self);

#ifdef  TRACE
	self->cpu_trace.event_map = null_event_map;
#endif  /* TRACE */

	/*
	 * until this routine is executed, calls to prom_xxx
	 * will not work and the kernel will most likely hang.
	 * there should be a better way to handle this.
	 */
	prom_init("kernel", cookie);
	prom_set_preprom(kern_splr_preprom);
	prom_set_postprom(kern_splx_postprom);

	/*
	 * Early version of OBP has a bug in prom_stopcpu(). So we
	 * need to workaround it. See bug 1119958.
	 */
	do_stopcpu = ((u_int)prom_mon_id() >= FIRST_FCS_PROM_ID);

	set_cpu_revision();
	setcputype();

#ifdef sun4m
	fiximp_obp();
#endif sun4m

#ifdef sun4m
#ifdef	BCOPY_BUF
	CPU->cpu_m.bcopy_res = bcopy_buf ? 0 : -1;
#endif	/* BCOPY_BUF */

	map_wellknown_devices();
	utimersp = v_counter_addr[0];
#endif sun4m
	setcpudelay();

	bootflags();

#if !defined(SAS) && !defined(MPSAS)
	/*
	 * If the boot flags say that kadb is there,
	 * test and see if it really is by peeking at DVEC.
	 * If is isn't, we turn off the RB_DEBUG flag else
	 * we call the debugger scbsync() routine.
	 * The kdbx debugger agent does the dvec and scb sync stuff,
	 * and sets RB_DEBUG for debug_enter() later on.
	 */
	if ((boothowto & RB_DEBUG) != 0) {
		if (dvec == NULL || ddi_peeks((dev_info_t *)0,
		    (short *)dvec, (short *)0) != DDI_SUCCESS)
			boothowto &= ~RB_DEBUG;
		else {
			extern trapvec kadb_tcode, trap_kadb_tcode;

			(*dvec->dv_scbsync)();

			/*
			 * Now steal back the traps.
			 * We "know" that kadb steals trap 125 and 126,
			 * and that it uses the same trap code for both.
			 */
			kadb_tcode = scb.user_trap[ST_KADB_TRAP];
			scb.user_trap[ST_KADB_TRAP] = trap_kadb_tcode;
			scb.user_trap[ST_KADB_BREAKPOINT] = trap_kadb_tcode;
		}
	}
#endif

	/*
	 * Map in the msgbuf to physical pages 2 and 3.
	 *
	 * XXX  prom_map is busted.  It doesn't take the memory mapped here
	 *	off the physical avail list, so we have to do that manually
	 *	in startup().
	 */
	if (prom_map((caddr_t)&msgbuf, 0, mmu_ptob(2), 2 * MMU_PAGESIZE) !=
	    (caddr_t)&msgbuf)
		prom_panic("Can't map msgbuf");

	startup_alloc_vaddr += sizeof (msgbuf);
	startup_alloc_size += sizeof (msgbuf);

	clk10_phyaddr = intr_tick_get_phyaddr(cpu0.cpu_id);

	level15_mlsetup();

	/*
	 * Drop the interrupt level to 12 to allow level-14
	 * timer interrupts needed by inetboot and OBP when
	 * called by the kernel to load modules over the net.
	 */
	splzs();
}

/*
 * These routines are called immediately before and
 * immediately after calling into the firmware.  The
 * firmware is significantly confused by preemption -
 * particularly on MP machines - but also on UP's too.
 */

static int saved_spl;

static void
kern_splr_preprom(void)
{
	saved_spl = spl7();
}

static void
kern_splx_postprom(void)
{
	(void) splx(saved_spl);
}
