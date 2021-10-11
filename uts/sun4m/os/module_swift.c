/*
 * Copyright (c) 1993, 1994 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)module_swift.c	1.27	95/01/16 SMI"

#include <sys/machparam.h>
#include <sys/module.h>
#include <sys/cpu.h>
#include <sys/pte.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <vm/hat_srmmu.h>
#include <sys/sunddi.h>
#include <sys/debug.h>
#include <sys/openprom.h>

/*
 * Support for modules based on the Sun/Fujitsu microSPARC2 (Swift) CPU
 *
 * The following Swift module versions are supported by this
 * module driver:
 *
 * 	IMPL	VERS	MASK	NAME		COMMENT
 *	0	4	0.0	swift_pg1.0	obsolete
 *	0	4	1.1	swift_pg1.1	proto systems only
 *	0	4	2.0	swift_pg2.3	fcs systems
 *	0	4	2.5	swift_pg2.5	fcs+3 systems
 *	0	4	3.0	swift_pg3.0	proto systems only
 *
 * The following Swift module workarounds are supported by
 * this module driver:
 *
 *	HWBUGID	SWBUGID	COMMENT
 */

extern void	swift_cache_init();
extern void	swift_vac_allflush();
extern void	swift_vac_usrflush();
extern void	swift_vac_ctxflush();
extern void	swift_vac_rgnflush();
extern void	swift_vac_segflush();
extern void	swift_vac_pageflush();
extern void	swift_vac_flush();
extern void	swift_uncache_pt_page();
extern void	swift_turn_cache_on();
extern void	small_sun4m_mmu_getasyncflt();
extern int	small_sun4m_mmu_chk_wdreset();
extern int	small_sun4m_ebe_handler();
extern void	small_sun4m_sys_setfunc();

extern void	srmmu_mmu_flushall();
extern void	srmmu_vacflush();
extern void	srmmu_tlbflush();
extern int	swift_mmu_probe();
static void	swift_mmu_writeptp();
static int	swift_mmu_writepte();
static int	swift_mmu_ltic();
extern int	swift_getversion();
static void	init_swift_idle_cpu();
static void	swift_idle_cpu();

static int		(*cpu_pwr_fn)(dev_info_t *, int, int);
static dev_info_t	*root_node = (dev_info_t *)NULL;
extern void		(*idle_cpu)();		/* defined in disp.c */

extern int	mmu_l3only;
static int	swift_version = 0;

#define	MCR_AP	0x10000	/* AFX page mode control bit in PCR */

#define	SWIFT_KDNC	/* workaround for 1156639 */
#define	SWIFT_KDNX	/* workaround for 1156640 */

#ifdef SWIFT_KDNC
int		swift_kdnc = -1;	/* don't cache kernel data space */
static int	swift_kdnc_inited = 0;	/* startup initialization done flag */
#endif SWIFT_KDNC

#ifdef SWIFT_KDNX
extern void	mmu_flushall();
extern int	swift_mmu_probe_kdnx();
int		swift_kdnx = -1;	/* don't mark non-memory executable */
int		swift_kdnx_inited = 0;	/* startup initialization done flag */
#endif

/*
 * Identify Swift module and get Swift version.
 */

int
swift_module_identify(u_int mcr)
{
	if (((mcr >> 24) & 0xff) == 0x04) {
		/* Now get the version of Swift. */
		swift_version = (swift_getversion() >> 24);
		return (1);
	}

	return (0);
}


/*
 * Setup (attach?) Swift module
 */

/*ARGSUSED*/
void
swift_module_setup(int mcr)
{
	int kdnc = 0;
	int kdnx = 0;
	u_int pcr;
	extern int use_page_coloring;
	extern int do_pg_coloring;
	extern int small_4m;

	/*
	 * Swift systems are small4m machines
	 */
	small_4m = 1;
	small_sun4m_sys_setfunc();

	/*
	 * MCR, CTPR, CTXR
	 */

	/* workaround for bug 1166390 - enable AFX page mode  */
	pcr = mmu_getcr();
	pcr |= MCR_AP;
	mmu_setcr(pcr);

	/* Swift is standard SRMMU */

	/*
	 * TLB PROBE, FLUSH
	 */
	v_mmu_probe = swift_mmu_probe;

	/* Swift is standard SRMMU */

	/* PTE, PTP update */
	v_mmu_writepte = swift_mmu_writepte;
	v_mmu_writeptp = swift_mmu_writeptp;

	/* SFSR, SFAR, EBE, AFSR, AFAR, WDOG */
	v_mmu_handle_ebe = small_sun4m_ebe_handler;
	v_mmu_getasyncflt = small_sun4m_mmu_getasyncflt;
	v_mmu_chk_wdreset = small_sun4m_mmu_chk_wdreset;

	/* Swift is standard SRMMU */

	/* TRCR */
	v_mmu_ltic = swift_mmu_ltic;

	/* PAC INIT, FLUSHALL, PARITY? */

	/* Swift is VAC module */

	/* VAC INIT, FLUSH, CONTROL */
	cache |= (CACHE_VAC | CACHE_VTAG);

	/*
	 * VAC/TLB FLUSH. v_vac_XXX() always flushes the cache.
	 * If you need to flush the TLB, pass FLUSH_TLB as the flags.
	 */
	v_cache_init = swift_cache_init;
	v_vac_usrflush = swift_vac_usrflush;
	v_vac_ctxflush = swift_vac_ctxflush;
	v_vac_rgnflush = swift_vac_rgnflush;
	v_vac_segflush = swift_vac_segflush;
	v_vac_pageflush = swift_vac_pageflush;
	v_vac_flush = swift_vac_flush;
	v_vac_allflush = swift_vac_allflush;
	v_turn_cache_on = swift_turn_cache_on;
	v_uncache_pt_page = swift_uncache_pt_page;

	switch (swift_version) {
	case 0x00:
		/* P1.0 Swifts. See bug id # 1139511 */
		v_mmu_flushseg = srmmu_mmu_flushall;
		v_mmu_flushrgn = srmmu_mmu_flushall;
		v_mmu_flushctx = srmmu_mmu_flushall;
		v_vac_segflush = swift_vac_allflush;
		v_vac_rgnflush = swift_vac_allflush;
		v_vac_ctxflush = swift_vac_allflush;
		mmu_l3only = 1;
		kdnc = 1;
		kdnx = 1;
		break;
	case 0x11:
		/* PG1.1 Swift. */
		mmu_l3only = 1;
		kdnc = 1;
		kdnx = 1;
		break;
	case 0x20:
		/* PG2.0 Swift. */
		mmu_l3only = 1;
		kdnc = 1;
		kdnx = 1;
		break;
	case 0x23:
		/* PG2.3 Swift. */
		mmu_l3only = 1;
		kdnc = 1;
		kdnx = 1;
		break;
	case 0x25:
		/* PG2.5 Swift. */
		kdnx = 1;
		break;
	case 0x30:
		/* PG3.0 Swift. */
		mmu_l3only = 1;
		kdnc = 1;
		kdnx = 1;
		break;
	case 0x31:
		/* PG3.1 Swift. */
		kdnx = 1;
		break;
	case 0x32:
		/* PG3.2 Swift. */
		break;
	default:
		/* Should not get here. */
		break;
	}

	nctxs = 256; /* XXX - why is this here? */

#ifdef SWIFT_KDNC
	if (swift_kdnc == -1)
		swift_kdnc = kdnc;
#endif SWIFT_KDNC

#ifdef SWIFT_KDNX
	if (swift_kdnx == -1) {
		swift_kdnx = kdnx;
		if (swift_kdnx == 1)
			v_mmu_probe = swift_mmu_probe_kdnx;
	}
#endif SWIFT_KDNX

	/*
	 * Indicate if we want page coloring.
	 */
	if (use_page_coloring)
		do_pg_coloring = 0;	/* disable until this works */

	/* Replace generic idle function (in disp.c) with our own */
	idle_cpu = init_swift_idle_cpu;
}

#ifdef	SWIFT_KDNX
/*
 * Remove execute permission from a PTE.
 */
void
swift_kdnx_fix_pte(struct pte *ptep)
{

	switch (ptep->AccessPermissions) {
		/*
		 * ACC 2,4,6 -> 0
		 */
		case MMU_STD_SRXURX:
		case MMU_STD_SXUX:
		case MMU_STD_SRX:
			ptep->AccessPermissions = MMU_STD_SRUR;
			break;
		/*
		 * ACC 3 -> 1
		 */
		case MMU_STD_SRWXURWX:
			ptep->AccessPermissions = MMU_STD_SRWURW;
			break;
		/*
		 * ACC 7 -> 5
		 */
		case MMU_STD_SRWX:
			ptep->AccessPermissions = MMU_STD_SRWUR;
			break;
		default:
			break;
	}
}

/*
 * Cycle thru the mappings within the monitor's virtual address range and
 * remove execute permissions on all mappings to non-main memory space.
 */
void
swift_kdnx_init()
{
	extern struct as kas;
	struct pte *ptep;
	caddr_t addr;
	int level;
	u_int value;

	for (addr = (caddr_t)SUNMON_START; addr < (caddr_t)SUNMON_END;
			addr += PAGESIZE) {
		ptep = srmmu_ptefind_nolock(&kas, addr, &level);
		if (!ptep)
			continue;
		value = *(u_int *)ptep;
		if ((PTE_ETYPE(value) == MMU_ET_PTE) && (value & 0x0f000000))
			swift_kdnx_fix_pte(ptep);
	}
	mmu_flushall();
	swift_kdnx_inited = 1;
}
#endif	SWIFT_KDNX

#ifdef	SWIFT_KDNC

/*
 * Uncache mappings from etext to end.
 */
void
swift_kdnc_init()
{
	extern char etext[], end[];
	extern struct as kas;
	struct pte *ptep;
	caddr_t addr, end_addr;
	int level;

	addr = MMU_L3_BASE((caddr_t)&etext);
	end_addr = MMU_L3_BASE((caddr_t)&end);
	for (; addr <= end_addr; addr += PAGESIZE) {
		ptep = srmmu_ptefind_nolock(&kas, addr, &level);
		if (!ptep)
			continue;
		ptep->Cacheable = 0;
	}
	mmu_flushall();
	swift_kdnc_inited = 1;
}
#endif	SWIFT_KDNC

/*
 * The Swift writepte function
 */

#define	PFN_C_V_MASK 0xFFFFFF9F /* check pfn, cache bit, acc type, entry type */

int
swift_mmu_writepte(
	struct pte *pte,
	u_int	value,
	caddr_t	addr,
	int	level,
	int	cxn,
	u_int	rmkeep)
{
	u_int old, *ipte;
	int vcache;

#ifdef SWIFT_KDNC
	extern char end[];

	if (swift_kdnc == 1) {
		if (swift_kdnc_inited == 0)
			swift_kdnc_init();

		/* If kernel mapping and writable then don't cache it. */
		if ((addr >= (caddr_t)&end) &&
			(((value >> PTE_PERMSHIFT) & 1) == 1))
			value &= ~0x80;
	}
#endif SWIFT_KDNC

#ifdef SWIFT_KDNX
	if (swift_kdnx == 1) {
		if (swift_kdnx_inited == 0)
			swift_kdnx_init();

		/* If non-memory mapping then remove execute permission. */
		if ((PTE_ETYPE(value) == MMU_ET_PTE) && (value & 0x0f000000))
			swift_kdnx_fix_pte((struct pte *)&value);
	}
#endif	SWIFT_KDNX

	ASSERT((rmkeep & ~PTE_RM_MASK) == 0);
	ipte = (u_int *)pte;
	old = *ipte;

	if (PTE_ETYPE(old) == MMU_ET_PTE && cxn != -1) {
		/*
		 * 'vcache' controls whether we have to flush the vtags.
		 * It's set when we're replacing a valid pte with an
		 * invalid one.
		 */
		vcache = (vac && pte->Cacheable &&
			((old & PFN_C_V_MASK) != (value & PFN_C_V_MASK)));

		if (vcache)
			srmmu_vacflush(level, addr, cxn, FL_TLB_CACHE);
		else
			srmmu_tlbflush(level, addr, cxn, FL_ALLCPUS);
	}
	value |= old & rmkeep;
	(void) swapl(value, (int *)ipte);
	return (old & PTE_RM_MASK);
}

/*
 * The Swift writeptp function.
 */

void
swift_mmu_writeptp(
	struct ptp *ptp,
	u_int	value,
	caddr_t	addr,
	int	level,
	int	cxn)
{
	u_int *iptp, old;

	iptp = (u_int *)ptp;
	old = *iptp;

	/* Install new ptp before TLB flush. */
	(void) swapl(value, (int *)iptp);

	if (PTE_ETYPE(old) == MMU_ET_PTP && cxn != -1) {
		srmmu_vacflush(level, addr, cxn, FL_TLB_CACHE);
	}
}

/*
 * Uncache a page of memory and flush the TLB that maps this page.
 */
void
swift_uncache_pt_page(caddr_t va, u_int pfn)
{
	vac_pageflush(va, KCONTEXT, FL_TLB_CACHE);
}

/*
 * These functions are called when the idle loop can't find anything better
 * to run. It uses the platform specific power manager power routine to
 * power off the CPU. The first function saves the power function look up
 * so then the optimized second function can be used instead.
 */
static void
init_swift_idle_cpu()
{
	int (*pwr_fn)(dev_info_t *, int, int);
	int length = sizeof (pwr_fn);

	if (ddi_prop_op(DDI_DEV_T_ANY, ddi_root_node(),
	    PROP_LEN_AND_VAL_BUF, DDI_PROP_DONTPASS, "platform-pm",
	    (caddr_t)&pwr_fn, &length) == DDI_PROP_SUCCESS) {
		cpu_pwr_fn = pwr_fn;
		root_node = ddi_root_node();
		idle_cpu = swift_idle_cpu;
		(*cpu_pwr_fn)(root_node, 1, 0);
	}
}

static void
swift_idle_cpu()
{
	(*cpu_pwr_fn)(root_node, 1, 0);
}

/*
 * This is a do nothing routine.
 * The routine that used to be here did not work for Swift.
 */
int
swift_mmu_ltic()
{
	return (0);
}
