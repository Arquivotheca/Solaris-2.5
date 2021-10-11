/*
 * Copyright (c) 1990-1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mlsetup.c	1.20	95/03/23 SMI"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/disp.h>
#include <sys/msgbuf.h>
#include <sys/obpdefs.h>
#include <sys/clock.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/stack.h>
#include <sys/machpcb.h>
#include <sys/eeprom.h>
#include <sys/intreg.h>
#include <sys/memerr.h>
#include <sys/auxio.h>
#include <sys/promif.h>
#include <sys/autoconf.h>
#include <sys/reboot.h>
#include <sys/vtrace.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/debug/debug.h>

#define	SUNDDI_IMPL		/* so sunddi.h will not redefine splx() et al */

#include <sys/sunddi.h>

/*
 * External Data:
 */
extern char t0stack;
extern struct _kthread t0;

/*
 * Global Routines:
 * mlsetup()
 */

/*
 * Global Data:
 */

struct cpu	cpu0;			/* first CPU's data */
struct _klwp	lwp0;
struct proc	p0;

/*
 * Configuration parameters set at boot time.
 */
u_int segmask;			/* mask for segment numbers */

/*
 * Magic constants of the implementation, set in fiximp_obp().
 *
 * The default constants shown below correspond to the 4/60.
 * Of course, the machine id or prom info has the final say.
 */
int vac = VAC_WRITETHRU;	/* vac present?  If so, what write policy? */
u_int nctxs = 8;		/* no. of implemented contexts */
int cpu_buserr_type = 0;	/* bus error type (0 = 4/60 style) */
int vac_size = 0x10000;		/* cache size in bytes */
int vac_linesize = 16;		/* size of a cache line */
int vac_hwflush = 0;		/* cache has HW flush */
int Cpudelay = 0;		/* delay loop count/usec */
u_int npmgrps = 128;		/* number of pmgrps in page map */
u_int nsmgrps = 0;		/* number of smgrps in segment map */

static int offdelay = -1;	/* approx mips with cache off */
int ondelay = -1;		/* approx mips with cache on */

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
	register caddr_t addr;
	extern struct classfuncs sys_classfuncs;
	extern pri_t maxclsyspri;
	struct machpcb *mpcb;


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
	t0.t_cpu = &cpu0;
	t0.t_disp_queue = &cpu0.cpu_disp;
	t0.t_bind_cpu = PBIND_NONE;
	t0.t_clfuncs = &sys_classfuncs.thread;
	THREAD_ONPROC(&t0, &cpu0);

	lwp0.lwp_thread = &t0;
	lwp0.lwp_regs = (void *)rp;
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

	cpu0.cpu_thread = &t0;
	cpu0.cpu_dispthread = &t0;
	cpu0.cpu_idle_thread = &t0;
	cpu0.cpu_disp.disp_cpu = &cpu0;
	cpu0.cpu_flags = CPU_READY | CPU_RUNNING | CPU_EXISTS | CPU_ENABLE;
	cpu0.cpu_m.mpcb = mpcb;

#ifdef	TRACE
	cpu0.cpu_trace.event_map = null_event_map;
#endif	/* TRACE */

	/*
	 * Initialize lists of available and active CPUs.
	 */
	cpu_list_init(&cpu0);

	prom_init("kernel", cookie);
	prom_set_preprom(kern_splr_preprom);
	prom_set_postprom(kern_splx_postprom);

	(void) bootflags();

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

	setcputype();
	/*
	 * Now we know precisely which machine we got.
	 * Early cpu properties like npmg are fetched here
	 */
	fiximp_obp();

	setdelay(offdelay);	/* set cache-off delay const */
	/* Now we know how fast we can go */

	segmask = PMGRP_INVALID;

	/*
	 * Map in devices
	 *
	 * XXX	This is somewhat dangerous.  We're simply assuming
	 *	the PROM isn't using the "middle" pmgrp (when there's
	 *	no particular guarantee that it isn't).  Ideally, we'd
	 *	simply share the PROM's mapping to these devices by
	 *	searching the devinfo tree for the "address" property
	 *	of the named devices.
	 */
#ifdef  MMU_3LEVEL
	if (mmu_3level)
		map_setrgnmap((caddr_t)0-SMGRPSIZE, nsmgrps / 2);
#endif
	map_setsgmap((caddr_t)0-PMGRPSIZE, npmgrps / 2);
	for (addr = (caddr_t)0-PMGRPSIZE; addr != 0; addr += MMU_PAGESIZE)
		map_setpgmap(addr, 0);

	map_setpgmap((caddr_t)COUNTER_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_COUNTER_ADDR));
	map_setpgmap((caddr_t)EEPROM_ADDR,
		PG_V | PG_KR | PGT_OBIO | PG_NC | btop(OBIO_EEPROM_ADDR));
	map_setpgmap((caddr_t)MEMERR_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_MEMERR_ADDR));
	map_setpgmap((caddr_t)AUXIO_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_AUXIO_ADDR));
	map_setpgmap((caddr_t)INTREG_ADDR,
		PG_V | PG_KW | PGT_OBIO | PG_NC | btop(OBIO_INTREG_ADDR));

	/*
	 * Need to map in the msgbuf.  We'll use the region, and segment
	 * that is used for KERNELBASE.  The grody thing here is that
	 * we'll fix the msgbuf to physical pages 2 and 3.  I hate
	 * wiring this stuff down, but we must preserve the msgbuf across
	 * reboots.
	 */
	map_setpgmap((caddr_t)&msgbuf,
		PG_V | PG_KW | PGT_OBMEM | 0x2);
	map_setpgmap((caddr_t)((u_int)&msgbuf + MMU_PAGESIZE),
		PG_V | PG_KW | PGT_OBMEM | 0x3);

	/*
	 * Save the kernel's level 14 interrupt vector code and install
	 * the monitor's. This lets the monitor run the console until we
	 * take it over.
	 */
	kclock14_vec = scb.interrupts[14 - 1];
	start_mon_clock();
#ifdef KDBX
	ka_setup();
#endif /* KDBX */
	(void) splzs();			/* allow hi clock ints but not zs */
}

#ifdef	DEBUG_FIXIMP
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
	/*
	 * Tables of magic constants, for reference purposes.
	 *
	 *	4/60	4/40	4/65	4/20	4/75	4/25	4/50
	 *	SS-1	IPC	SS-1+	SLC	SS-2	ELC	IPX
	 *
	 * "buserr-type"
	 *	0,	0,	0,	0,	1,	1,	1,	1
	 *
	 * #define	K64	0x10000
	 *
	 * "vac-size"
	 *	K64,	K64,	K64,	K64,	K64,	K64,	K64,	K64
	 *
	 * "vac-linesize"
	 *	16,	16,	16,	16,	32,	32,	32,	32
	 *
	 * "vac-hwflush"
	 *	0,	0,	0,	0,	1,	1,	1,	1
	 *
	 * "mmu-nctxs"
	 *	8,	8,	8,	8,	16,	16,	8,	16
	 *
	 * "mmu-npmg"
	 *	128,	128,	128,	128,	256,	256,	256,	256
	 *
	 * "mips-off"
	 *	3,	3,	3,	3,	3,	3,	3,	3
	 *
	 * "mips-on"
	 *	20,	25,	25,	20,	40,	33,	40,	40
	 *
	 *
	 *	SS-1	IPC	SS-1+	SLC	SS-2	ELC	IPX
	 */
	static struct {
		char	*name;
		u_int	*var;
	} prop[] = {
		"buserr-type",	(u_int *)&cpu_buserr_type,
		"vac-size",	(u_int *)&vac_size,
		"vac-linesize",	(u_int *)&vac_linesize,
		"vac-hwflush",	(u_int *)&vac_hwflush,
		"mmu-nctx",	(u_int *)&nctxs,
		"mmu-npmg",	(u_int *)&npmgrps,
		"mips-off",	(u_int *)&offdelay,
		"mips-on",	(u_int *)&ondelay
	};
	register int i, a;
	register dnode_t rootnode;

	rootnode = prom_rootnode();
	for (i = 0; i < (sizeof (prop) / sizeof (prop[0])); i++)
		if ((a = getintprop(rootnode, prop[i].name, -1)) != -1)
			*prop[i].var = a;

	/*
	 * Workaround for bugid 1067719: "vac-hwflush" is mis-named
	 * "vac_hwflush," and it has the wrong value for SS1+ anyway.
	 * (This is the bugfix for 1068462.).
	 *
	 * We infer the existence of the hwflush hardware from the
	 * vac_linesize rather than the cputype.  See add_root_props()
	 * for a similar fix for the 'sun4c-micro-tlb' property.
	 */
	if (vac_linesize == 32 && vac_hwflush == 0)
		vac_hwflush = 1;

	/*
	 * Establish on/off delay defaults in
	 * case the PROM doesn't know what they are.
	 */
	if (offdelay == -1)
		offdelay = 0;

	if (ondelay == -1) {
		if (cputype == CPU_SUN4C_60) {
			ondelay = CPU_MAXMIPS_20MHZ;
		} else {
			ondelay = CPU_MAXMIPS_25MHZ;
		}
	}

	/*
	 * XXX	The prom should have this in the device tree
	 */
	if (vac_size)
		vac = VAC_WRITETHRU;
	else
		vac = NO_VAC;

#ifdef	DEBUG_FIXIMP
	PROM_PRINTF("machine parameters:\n");
	for (i = 0; i < (sizeof (prop) / sizeof (prop[0])); i++)
		PROM_PRINTF("%s 0x%x ", prop[i].name, *prop[i].var);
	PROM_PRINTF("\n");
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
