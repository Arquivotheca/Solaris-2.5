/*
 * Copyright (c) 1990-1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mlsetup.c	1.25	95/03/23 SMI"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/disp.h>
#include <sys/msgbuf.h>
#include <sys/promif.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/stack.h>
#include <sys/machpcb.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>
#include <sys/memerr.h>
#include <sys/auxio.h>
#include <vm/as.h>
#include <vm/hat_srmmu.h>
#include <sys/iommu.h>
#include <sys/reboot.h>
#include <sys/vtrace.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/debug/debug.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */

#include <sys/sunddi.h>

/*
 * External Routines:
 */
extern void map_wellknown_devices(void);
extern void init_mon_clock(void);

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

int use_bcopy = 1;		/* use hw bcopy */
int vac_linesize;		/* size of a cache line */
int vac_nlines;			/* number of lines in the cache */
int vac_pglines;		/* number of cache lines in a page */
int vac_size;			/* cache size in bytes */
u_int		vac_mask;	/* VAC alignment consistency mask */
int vac_shift;			/* log2(vac_size) for ppmapout() */

/*
 * Static Routines:
 */
static void fiximp_obp(void);
static int getintprop(dnode_t node, char *name, int deflt);
static void kern_splr_preprom(void);
static void kern_splx_postprom(void);

/*
 * Setup routine called right before main(). Interposing this function
 * before main() allows us to call it in a machine-independent fashion.
 */

void
mlsetup(struct regs *rp, void *cookie)
{
	struct machpcb *mpcb;

	extern struct classfuncs sys_classfuncs;
	extern pri_t maxclsyspri;
	extern volatile struct count14 *utimersp;
	extern void setcputype(void);

	extern caddr_t startup_alloc_vaddr;
	extern caddr_t startup_alloc_size;

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
	t0.t_next = &t0;
	t0.t_prev = &t0;
	t0.t_cpu = cpu[0];
	t0.t_disp_queue = &cpu[0]->cpu_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_clfuncs = &sys_classfuncs.thread;
	THREAD_ONPROC(&t0, CPU);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_procp = &p0;
	lwp0.lwp_regs = (void *)rp;
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

	CPU->cpu_thread = &t0;
	CPU->cpu_dispthread = &t0;
	CPU->cpu_idle_thread = &t0;
	CPU->cpu_disp.disp_cpu = CPU;
	CPU->cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
	CPU->cpu_m.mpcb = mpcb;
	CPU->cpu_m.in_prom = 0;

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(CPU);

#ifdef  TRACE
	CPU->cpu_trace.event_map = null_event_map;
#endif  /* TRACE */

	prom_init("kernel", cookie);
	prom_set_preprom(kern_splr_preprom);
	prom_set_postprom(kern_splx_postprom);

	setcputype();
	fiximp_obp();

#ifdef	BCOPY_BUF
	CPU->cpu_m.bcopy_res = bcopy_buf ? 0 : -1;
#endif	/* BCOPY_BUF */

	map_wellknown_devices();
	utimersp = v_counter_addr[0];

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
	 * XXX	Eventually the msgbuf will be mapped at a variable
	 *	physical location.
	 *
	 * Map in the msgbuf to physical pages 2 and 3.
	 *
	 * XXX	prom_map is busted.  It doesn't take the memory mapped here
	 *	off the physical avail list, so we have to do that manually
	 *	in startup().
	 */
	if (prom_map((caddr_t)&msgbuf, 0, mmu_ptob(2), 2 * MMU_PAGESIZE) !=
	    (caddr_t)&msgbuf)
		prom_panic("Can't map msgbuf");

	/*
	 * Need to do logical msgbuf accounting here 'cause running
	 * with lock_stats populates the kernelmap very early in startup.
	 */
	startup_alloc_vaddr += sizeof (msgbuf);
	startup_alloc_size += sizeof (msgbuf);

	init_mon_clock();
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
static void
fiximp_obp(void)
{
	static struct {
		char	*name;
		int	*var;
	} prop[] = {
#ifdef	BCOPY_BUF
		"bcopy?",		&bcopy_buf,
#endif
		"cache-line-size",	&vac_linesize,
		"cache-nlines",		&vac_nlines,
	};
	register dnode_t rootnode, cpunode, vmenode, iommunode;
	auto dnode_t sp[OBP_STACKDEPTH];
	register int i, a;
	pstack_t *stk;

	rootnode = prom_rootnode();
	/*
	 * Find the first 'cpu' node - assume that all the
	 * modules are the same type - at least when we're
	 * determining magic constants.
	 */
	stk = prom_stack_init(sp, OBP_STACKDEPTH);
	cpunode = prom_findnode_bydevtype(rootnode, "cpu", stk);
	prom_stack_fini(stk);

	for (i = 0; i < sizeof (prop) / sizeof (prop[0]); i++) {
		PROM_PRINTF("property '%s' default 0x%x",
			prop[i].name, *prop[i].var);
		if ((a = getintprop(cpunode, prop[i].name, -1)) != -1) {
			*prop[i].var = a;
			PROM_PRINTF(" now set to 0x%x", a);
		}
		PROM_PRINTF("\n");
	}

	/* 'cache' is set early in the module setup code */
	if (cache & CACHE_VAC)
		vac = 1;

	/* turn bcopy off if we don't want to use it */
	if (!use_bcopy)
		bcopy_buf = 0;

	/*
	 * Does this implementation possess an iommu?
	 */
	iom = 0;
	iommunode = prom_childnode(rootnode);
	do {
		if (prom_getnode_byname(iommunode, "iommu"))
			break;
		iommunode = prom_nextnode(iommunode);
	} while (iommunode > (dnode_t)0);

	if (iommunode > (dnode_t)0) {

		iom = 1;
#ifdef	IOC
		/*
		 * Ok, we found an iommu, now do we have a 'vme' node
		 * with a working iocache on it?
		 */
		vme = 0;
		ioc = 0;
		vmenode = prom_childnode(iommunode);
		do {
			if (prom_getnode_byname(vmenode, "vme")) {
				vme = 1;
				break;
			}
			vmenode = prom_nextnode(vmenode);
		} while (vmenode > (dnode_t)0);

		if (vmenode > (dnode_t)0 &&
		    getintprop(vmenode, "iocache?", 0) != 0)
			ioc = 1;
#endif	/* IOC */
	}

	if (vac) {
		vac_pglines = PAGESIZE / vac_linesize;
		vac_size = vac_nlines * vac_linesize;
		vac_mask = MMU_PAGEMASK & (vac_size - 1);
		i = 0; a = vac_size;
		while (a >>= 1)
			++i;
		vac_shift = i;
	}

#ifdef	DEBUG_FIXIMP
	/*
	 * For debugging the property lookup stuff
	 */
	for (i = 0; i < sizeof (prop) / sizeof (prop[0]); i++)
		PROM_PRINTF("%s %x ", prop[i].name, *prop[i].var);
	PROM_PRINTF(
		"vac %x vac_pglines %x vac_size %x vac_mask %x vac_shift %x\n",
		vac, vac_pglines, vac_size, vac_mask, vac_shift);
	PROM_PRINTF("iommu %x iocache %x bcopy_buf %x\n",
		iom, ioc, bcopy_buf);
#endif	/* DEBUG_FIXIMP */
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
