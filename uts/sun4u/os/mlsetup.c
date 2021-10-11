/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mlsetup.c	1.39	95/08/31 SMI"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/disp.h>
#include <sys/msgbuf.h>
#include <sys/promif.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/cpu.h>
#include <sys/stack.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>
#include <vm/as.h>
#include <vm/hat_sfmmu.h>
#include <sys/reboot.h>
#include <sys/sysmacros.h>
#include <sys/vtrace.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/debug/debug.h>
#include <sys/privregs.h>
#include <sys/machpcb.h>

#include <sys/prom_debug.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */

#include <sys/iommu.h>
#include <sys/sunddi.h>

/*
 * External Routines:
 */
extern void map_wellknown_devices(void);

/*
 * External Data:
 */
extern struct _kthread t0;
extern int vme;

/*
 * Global Routines:
 * mlsetup()
 */

/*
 * Global Data:
 */

struct _klwp	lwp0;
struct proc	p0;

int	dcache_size;
int	dcache_linesize;
int	icache_size;
int	icache_linesize;
int	ecache_size;
int	ecache_linesize;
int	itlb_entries;
int	dtlb_entries;
int	vac_size;			/* cache size in bytes */
u_int	vac_mask;	/* VAC alignment consistency mask */
int	vac_shift;			/* log2(vac_size) for ppmapout() */
int	dcache_line_mask;

/*
 * Static Routines:
 */
static int getintprop(dnode_t node, char *name, int deflt);
static void kern_splr_preprom(void);
static void kern_splx_postprom(void);

/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */

void
mlsetup(struct regs *rp, void *cif, struct v9_fpu *fp)
{
	struct machpcb *mpcb;
	u_int phys_msgbuf_hi, phys_msgbuf_lo;
	u_int msgbufsz;

	extern struct classfuncs sys_classfuncs;
	extern pri_t maxclsyspri;
	extern void setcputype(void);
	extern int getprocessorid();
	extern cpuset_t cpu_ready_set;
	extern struct cpu cpu0;

	extern caddr_t startup_alloc_vaddr;
	extern caddr_t startup_alloc_size;

#ifdef	MPSAS
	/*
	 * Arrange to have fake_clockticks called from idle()
	 */
	extern void (*idle_cpu)(), fake_clockticks();

	idle_cpu = fake_clockticks;
#endif

	/*
	 * initialize t0
	 */
	t0.t_stk = (caddr_t)rp - REGOFF;
	t0.t_pri = maxclsyspri - 3;
	t0.t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t0.t_procp = &p0;
	t0.t_lwp = &lwp0;
	t0.t_forw = &t0;
	t0.t_back = &t0;
	t0.t_next = &t0;
	t0.t_prev = &t0;
	t0.t_cpu = &cpu0;			/* loaded by _start */
	t0.t_disp_queue = &cpu0.cpu_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_clfuncs = &sys_classfuncs.thread;
	THREAD_ONPROC(&t0, CPU);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_procp = &p0;
	lwp0.lwp_regs = (void *)rp;
	t0.t_tid = p0.p_lwpcnt = p0.p_lwprcnt = p0.p_lwptotal = 1;

	mpcb = lwptompcb(&lwp0);
	mpcb->mpcb_fpu = fp;
	mpcb->mpcb_fpu->fpu_q = mpcb->mpcb_fpu_q;
	mpcb->mpcb_thread = &t0;
	lwp0.lwp_fpu = (void *)mpcb->mpcb_fpu;

	p0.p_exec = NULL;
	p0.p_stat = SRUN;
	p0.p_flag = SSYS;
	p0.p_tlist = &t0;
	p0.p_stksize = 2*PAGESIZE;
	p0.p_as = &kas;
	sigorset(&p0.p_ignore, &ignoredefault);

	CPU->cpu_thread = &t0;
	CPU->cpu_dispthread = &t0;
	CPU->cpu_idle_thread = &t0;
	CPU->cpu_disp.disp_cpu = CPU;
	CPU->cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
	CPU->cpu_id = getprocessorid();
	CPUSET_ADD(cpu_ready_set, CPU->cpu_id);

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(CPU);

#ifdef  TRACE
	CPU->cpu_trace.event_map = null_event_map;
#endif  /* TRACE */

	prom_init("kernel", cif);
	(void) prom_set_preprom(kern_splr_preprom);
	(void) prom_set_postprom(kern_splx_postprom);

	PRM_INFO("mlsetup: now ok to call prom_printf");

	/*
	 * Claim the physical and virtual resources used by the msgbuf,
	 * then map the msgbuf.  This operation removes the phys and
	 * virtual addresses from the free lists.
	 */
	msgbufsz = roundup(sizeof (struct msgbuf), MMU_PAGESIZE);
	if (prom_claim_virt((u_int)msgbufsz,
	    (caddr_t)&msgbuf) != (caddr_t)&msgbuf)
		prom_panic("Can't claim msgbuf virtual address");
	if (prom_retain("msgbuf", msgbufsz, MMU_PAGESIZE, &phys_msgbuf_hi,
	    &phys_msgbuf_lo) != 0)
		prom_panic("Can't allocate retained msgbuf physical address");
	if (prom_map_phys(-1, (u_int)msgbufsz, (caddr_t)&msgbuf,
	    (u_int)phys_msgbuf_hi, (u_int)phys_msgbuf_lo) != 0)
		prom_panic("Can't map msgbuf");

	PRM_DEBUG(msgbuf);
	PRM_DEBUG(phys_msgbuf_hi);
	PRM_DEBUG(phys_msgbuf_lo);
	/*
	 * Need to do logical msgbuf accounting here 'cause running
	 * with lock_stats populates the kernelmap very early in startup.
	 */
	startup_alloc_vaddr += sizeof (msgbuf);
	startup_alloc_size += sizeof (msgbuf);

	setcputype();
	map_wellknown_devices();
	setcpudelay();
	bootflags();

#if !defined(MPSAS)
	/*
	 * If the boot flags say that kadb is there, test and see
	 * if it really is by peeking at dvec.
	 */
	if ((boothowto & RB_DEBUG) != 0) {
		if (dvec == NULL || ddi_peeks((dev_info_t *)0,
		    (short *)dvec, (short *)0) != DDI_SUCCESS)
			/*
			 * Apparently the debugger isn't really there;
			 * just turn off the flag and drive on.
			 */
			boothowto &= ~RB_DEBUG;
		else {
			/*
			 * We already patched the trap table in locore.s, so
			 * all the following does is allow "boot kadb -d"
			 * to work correctly.
			 */
			(void) (*dvec->dv_scbsync)();
		}
	}
#endif

#ifdef KDBX
	ka_setup();
#endif /* KDBX */
}

#if defined(DEBUG_FIXIMP) || defined(lint) || defined(__lint)
static int debug_fiximp = 0;
#define	PROM_PRINTF	if (debug_fiximp) prom_printf
#else
#define	PROM_PRINTF
#endif	/* DEBUG_FIXIMP */

static int
getintprop(dnode_t node, char *name, int deflt)
{
	int	value;

	switch (prom_getproplen(node, name)) {
	case 0:
		value = 1;	/* boolean properties */
		break;

	case sizeof (int):
		(void) prom_getprop(node, name, (caddr_t)&value);
		break;

	default:
		value = deflt;
		break;
	}

	return (value);
}

/*
 * Set the magic constants of the implementation
 */
void
fiximp_obp(dnode_t cpunode)
{
#ifdef FIXME
/*
 * There has to be a better way of dealing with the caches.  Come back to
 * this sometime.
 * XXX specially try to remove the need for so many vac related globals.
 * XXX do we need both vac_size and shm_alignment?
 * XXX do we need both vac and cache?
 */
#endif /* FIXME */
	int i, a;
	static struct {
		char	*name;
		int	*var;
	} prop[] = {
		"dcache-size",		&dcache_size,
		"dcache-line-size",	&dcache_linesize,
		"icache-size",		&icache_size,
		"icache-line-size",	&icache_linesize,
		"ecache-size",		&ecache_size,
		"ecache-line-size",	&ecache_linesize,
		"#itlb-entries",	&itlb_entries,
		"#dtlb-entries",	&dtlb_entries,
	};
	extern int shm_alignment;

	for (i = 0; i < sizeof (prop) / sizeof (prop[0]); i++) {
		PROM_PRINTF("property '%s' default 0x%x",
			prop[i].name, *prop[i].var);
		if ((a = getintprop(cpunode, prop[i].name, -1)) != -1) {
			*prop[i].var = a;
			PROM_PRINTF(" now set to 0x%x", a);
		}
		PROM_PRINTF("\n");
	}

	if (cache & CACHE_VAC) {
		if (dcache_size > icache_size) {
			vac_size = dcache_size;
		} else {
			vac_size = icache_size;
		}
		vac_mask = MMU_PAGEMASK & (vac_size - 1);
		i = 0; a = vac_size;
		while (a >>= 1)
			++i;
		vac_shift = i;
		dcache_line_mask = (dcache_size - 1) & ~(dcache_linesize - 1);
		PRM_DEBUG(vac_size);
		PRM_DEBUG(vac_mask);
		PRM_DEBUG(vac_shift);
		PRM_DEBUG(dcache_line_mask);
		shm_alignment = vac_size;
		vac = 1;
	} else {
		shm_alignment = PAGESIZE;
		vac = 0;
	}
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
