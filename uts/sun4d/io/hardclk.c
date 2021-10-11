/*
 * Copyright (c) 1990, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)hardclk.c	1.76	95/05/22 SMI"

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/clock.h>
#include <sys/pte.h>
#include <sys/scb.h>
#include <sys/cpuvar.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/mman.h>
#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/promif.h>
#include <sys/sysmacros.h>
#include <vm/seg_kmem.h>
#include <sys/stat.h>
#include <sys/sunddi.h>

/*
 * Machine-dependent clock routines.
 *	Mostek 48T02 time-of-day for date functions
 *	"tick" timer for kern_clock.c interrupt events
 *	"profile" timer
 */

extern int intr_bwb_cntrl_get(void);
extern int intr_bwb_cntrl_set(int value);
extern int intr_bw_cntrl_get(int cpuid, int whichbus);
extern int intr_bw_cntrl_set(int cpuid, int busid, int value);
extern int intr_prof_setlimit(int cpuid, int whichbus, int value);
extern int intr_prof_getlimit_local(void);
extern void splx();
extern int splclock();

/*
 * External Data
 */
extern struct cpu cpu0;
extern u_int bootbusses;

#ifdef	MOSTEK_WRITE_PROTECTED
extern void mmu_getpte();
extern void mmu_setpte();
#endif	MOSTEK_WRITE_PROTECTED

/*
 * Static Routines:
 */
static caddr_t mapin_tod(u_int cpu_id, ulong_t *vadr);
static void mapout_tod(caddr_t addr, ulong_t vpg);

static void mostek_write(struct mostek48T02 *mostek_regs, todinfo_t tod);
static todinfo_t mostek_read(struct mostek48T02 *mostek_regs);
static u_int mostek_writable(struct mostek48T02 *mostek_regs);
static void mostek_restore(struct mostek48T02 *mostek_regs, u_int saveprot);
static void mostek_sync(todinfo_t tod);

static struct mostek48T02 *system_tod = 0;

#define	CLOCK_RES	1000		/* 1 microsec in nanosecs */

int clock_res = CLOCK_RES;

/*
 * Map in the system TOD clock, sync up all the slacve TOD clocks to
 * the system TOD clock.
 */
void
init_all_tods(void)
{
	int cpu_w_bbus;		/* CPU index to use to map in TOD */

	/*
	 * Make sure the boot CPU has the boot bus semaphore.
	 * If it does not, then mapin the ECSR address of the TOD via
	 * the other CPU on the board.
	 * This is done by XORing the CPU ID with 1. If you are running
	 * on A, this picks B, and vice versa.
	 */
	if (!CPU_IN_SET(bootbusses, CPU->cpu_id)) {
		cpu_w_bbus = (CPU->cpu_id) ^ 1;
	} else {
		cpu_w_bbus = CPU->cpu_id;
	}

	system_tod = (struct mostek48T02 *)(mapin_tod(cpu_w_bbus, NULL) +
		TOD_BYTE_OFFSET);

	mostek_sync(mostek_read(system_tod));
}

static void
mostek_sync(todinfo_t tod)
{
	int cpu_id;

	/* go through all of the CPUs in the system */
	for (cpu_id = 0; cpu_id < NCPU; cpu_id++) {
		/*
		 * Find the ones which own the bootbus but are not the
		 * boot CPU.
		 */
		if ((cpu_id != cpu0.cpu_id) &&
		    (CPU_IN_SET(bootbusses, cpu_id))) {
			struct mostek48T02 *local_tod;
			caddr_t local_tod_map;
			ulong_t	vpg;

			/*
			 * map in the tod. If for some reason it fails to
			 * map in the adress then do not attempt to update
			 * the system clock
			 */
			if ((local_tod_map = mapin_tod(cpu_id, &vpg)) == NULL)
				continue;

			local_tod = (struct mostek48T02 *)(local_tod_map +
				TOD_BYTE_OFFSET);

			/* write it! */
			mostek_write(local_tod, tod);

			/* destroy the mapping */
			mapout_tod(local_tod_map, vpg);
		}
	}
}


static void
mapout_tod(caddr_t addr, ulong_t vpg)
{
	segkmem_mapout(&kvseg, addr, TOD_BYTES);
	rmfree(kernelmap, TOD_PAGES, vpg);
}


static caddr_t
mapin_tod(u_int cpu_id,  ulong_t *vadr)
{
	u_int vpage = rmalloc(kernelmap, TOD_PAGES);
	caddr_t vaddr = (caddr_t)kmxtob(vpage);

	if (vpage == 0)
		return (NULL);

	if (vadr)
		*vadr = vpage;

	segkmem_mapin(&kvseg, vaddr, TOD_BYTES, (PROT_READ | PROT_WRITE),
		(u_int)(ECSR_PFN(CPU_DEVICEID(cpu_id)) + TOD_PAGE_OFFSET), 0);

	return (vaddr);
}

/*
 * change access protections
 */
static u_int
mostek_writable(struct mostek48T02 *mostek_regs)
{
	struct pte pte;
	u_int saveprot = 0;

#ifdef	MOSTEK_WRITE_PROTECTED
	/* write-enable the eeprom */
	mmu_getpte((caddr_t)mostek_regs, &pte);
	saveprot = pte.AccessPermissions;
	pte.AccessPermissions = MMU_STD_SRWUR;
	mmu_setpte((caddr_t)mostek_regs, pte);
#endif	MOSTEK_WRITE_PROTECTED
	return (saveprot);
}

static void
mostek_restore(struct mostek48T02 *mostek_regs, u_int saveprot)
{
#ifdef	MOSTEK_WRITE_PROTECTED
	/* Now write protect it, preserving the new modify/ref bits */
	mmu_getpte((caddr_t)mostek_regs, &pte);
	pte.AccessPermissions = saveprot;
	mmu_setpte((caddr_t)mostek_regs, pte);
#endif	MOSTEK_WRITE_PROTECTED
}

/*
 * mostek_read - access the actual hardware
 */
todinfo_t
mostek_read(struct mostek48T02 *mostek_regs)
{
	todinfo_t tod;
	u_int saveprot = mostek_writable(mostek_regs);

	/*
	 * Turn off updates so we can read the clock cleanly. Then read
	 * all the registers into a temp, and reenable updates.
	 */
	mostek_regs->clk_ctrl |= CLK_CTRL_READ;
	tod.tod_year	= BCD_TO_BYTE(mostek_regs->clk_year) + YRBASE;
	tod.tod_month	= BCD_TO_BYTE(mostek_regs->clk_month & 0x1f);
	tod.tod_day	= BCD_TO_BYTE(mostek_regs->clk_day & 0x3f);
	tod.tod_dow	= BCD_TO_BYTE(mostek_regs->clk_weekday & 0x7);
	tod.tod_hour	= BCD_TO_BYTE(mostek_regs->clk_hour & 0x3f);
	tod.tod_min	= BCD_TO_BYTE(mostek_regs->clk_min & 0x7f);
	tod.tod_sec	= BCD_TO_BYTE(mostek_regs->clk_sec & 0x7f);
	mostek_regs->clk_ctrl &= ~CLK_CTRL_READ;

	mostek_restore(mostek_regs, saveprot);

	return (tod);
}

/*
 * mostek_write - access the actual hardware
 */
static void
mostek_write(struct mostek48T02 *mostek_regs, todinfo_t tod)
{
	struct pte pte;
	unsigned int saveprot;

#ifdef	MOSTEK_WRITE_PROTECTED
	/* write-enable the eeprom */
	mmu_getpte((caddr_t)mostek_regs, &pte);
	saveprot = pte.AccessPermissions;
	pte.AccessPermissions = MMU_STD_SRWUR;
	mmu_setpte((caddr_t)mostek_regs, pte);
#endif	MOSTEK_WRITE_PROTECTED

	mostek_regs->clk_ctrl |= CLK_CTRL_WRITE;	/* allow writes */
	mostek_regs->clk_year		= BYTE_TO_BCD(tod.tod_year - YRBASE);
	mostek_regs->clk_month		= BYTE_TO_BCD(tod.tod_month);
	mostek_regs->clk_day		= BYTE_TO_BCD(tod.tod_day);
	mostek_regs->clk_weekday	= BYTE_TO_BCD(tod.tod_dow);
	mostek_regs->clk_hour		= BYTE_TO_BCD(tod.tod_hour);
	mostek_regs->clk_min		= BYTE_TO_BCD(tod.tod_min);
	mostek_regs->clk_sec		= BYTE_TO_BCD(tod.tod_sec);
	mostek_regs->clk_ctrl &= ~CLK_CTRL_WRITE;	/* load values */

#ifdef	MOSTEK_WRITE_PROTECTED
	/* Now write protect it, preserving the new modify/ref bits */
	mmu_getpte((caddr_t)mostek_regs, &pte);
	pte.AccessPermissions = saveprot;
	mmu_setpte((caddr_t)mostek_regs, pte);
#endif	MOSTEK_WRITE_PROTECTED
}

static u_int deadman_limit = 2;		/* In seconds */
					/* >= 5 seconds it will wrap around */
extern int   snooping;		/* deadman timer on/off */


/*
 * Read the current time from the clock chip and convert to UNIX form.
 * Assumes that the year in the clock chip is valid.
 * Must be called with tod_lock held.
 */
timestruc_t
tod_get(void)
{
	timestruc_t ts;

	ASSERT(MUTEX_HELD(&tod_lock));

	if (snooping) {
		enable_profiling(cpu0.cpu_id);
	}

	ts.tv_sec = tod_to_utc(mostek_read(system_tod));
	ts.tv_nsec = 0;

	return (ts);
}

/*
 * Write the specified time into the clock chip.
 * Must be called with tod_lock held.
 */
void
tod_set(timestruc_t ts)
{
	todinfo_t tod = utc_to_tod(ts.tv_sec);

	ASSERT(MUTEX_HELD(&tod_lock));

	mostek_write(system_tod, tod);
	mostek_sync(tod);
}

/*
 * This section provides support for the "tick" timer.
 */
#define	CTR_LIMIT_BIT	0x80000000	/* bit mask */
#define	CTR_USEC_MASK	0x7FFFFC00	/* bit mask */
#define	CTR_USEC_SHIFT	10		/* shift count */

u_int tick_limit_value = (((1000000 / HZ) + 1) << CTR_USEC_SHIFT)
				& CTR_USEC_MASK;

u_int clk10_limit = 0;

#if	0
#define	TICKER	system_tick

typedef	struct {
	u_int limit;
	u_int counter;
	u_int nd_limit;
} tick_timer_t;

tick_timer_t *system_tick = 0;

void
tick_timer_calibrate(ticks)
	int ticks;
{
	TICKER->nd_limit = ticks;
	tick_limit_value = ticks;
}

void
tick_timer_enable()
{
	int s = splclock();
	if (clk10_limit) {	/* is this useful? */
		TICKER->limit = clk10_limit;
	}
	(void) splx(s);
}

void
tick_timer_disable()
{
	int s = splclock();

	clk10_limit = TICKER->limit;
	TICKER->limit = 0;	/* free running */
	(void) splx(s);
}
#endif	0

extern void intr_tick_setlimit_local(int value);

/*
 * clkstart - (re)starts the "tick" timer,
 * which provides interrupts to kern_clock.c.
 */
void
clkstart()
{
	clk10_limit = tick_limit_value;
	intr_tick_setlimit_local(tick_limit_value);
}

/*
 * This section provides support for sharing the "profile" timer
 * with OBP which uses it as a safety net to poll devices.
 */
extern trapvec kclock14_vec;	/* kernel vector */
extern trapvec mon_clock14_vec;	/* monitor vector */

/*
 * read_scb_int - dumb little routine to read from trap vectors
 */
void
read_scb_int(int level, trapvec *vec)
{
	*vec = scb.interrupts[level - 1];
}

/*
 * User timer enable
 */
#define	BW_CNTRL_UTE	(1 << 2)
#define	TIMER_FREERUN(cntrl)	((cntrl & BW_CNTRL_UTE) != 0)
#define	TIMER_SETFREE(cntrl)	(cntrl | BW_CNTRL_UTE)
#define	TIMER_CLRFREE(cntrl)	(cntrl & ~BW_CNTRL_UTE)

#define	BW_WALL_UCEN	(1 << 0)
#define	WALL_FREERUN(frozen)	((frozen & BW_WALL_UCEN) != 0)
#define	WALL_SETFREE(rsvd)	(rsvd | BW_WALL_UCEN)
#define	WALL_CLRFREE(rsvd)	(rsvd & ~BW_WALL_UCEN)

extern u_int intr_usercntrl_get(void);
extern void intr_usercntrl_set(u_int);
extern longlong_t intr_usertimer_get(void);
extern void intr_usertimer_set(longlong_t);
extern hrtime_t hrtime_base;

static u_int clk14_limit = (((1000000 / HZ) + 1) << CTR_USEC_SHIFT);

/*
 * Deadman timer limit (in seconds). The actual math will be done
 * when we are about to reset the profiling clock.
 *
 * IMPORTANT: If deadman timer is turned on then profiling will
 * not work. This stuff is to be used only to debug situation where
 * the system hangs and things like L1-A don't work.
 */

static int wall_broken = 1;

void
wall_timer_recalibrate(void)
{
	u_int cntrl = intr_bwb_cntrl_get();
	u_int frozen = intr_usercntrl_get();
	hrtime_t hardware = intr_usertimer_get();
	hrtime_t software = hrtime_base;
	u_int recalibrate = 0;

	if (wall_broken) {
		return;
	}

	if (!TIMER_FREERUN(cntrl)) {
		u_int new_mode = TIMER_SETFREE(cntrl);
		intr_bwb_cntrl_set(new_mode);
		recalibrate = 1;
	}

	if (!WALL_FREERUN(frozen)) {
		u_int unfrozen = WALL_SETFREE(frozen);
		intr_usercntrl_set(unfrozen);
		recalibrate = 1;
	}

	if (hardware < software) {
		recalibrate = 1;
	}

	if (recalibrate) {
		prom_printf("wall_timer_recalibrate: %d\n", software);
		intr_usertimer_set(software);
	}
}

hrtime_t
wall_timer_get(void)
{
	hrtime_t now = intr_usertimer_get();
	return (now);
}

/*
 * high res timer
 */

#ifdef NOTUSED
void
hrtinit()
{
	wall_timer_recalibrate();
}
#endif NOTUSED

/*
 * This section provides support for the "profile" timer(s).
 */
void
enable_profiling(cpuid)
	int cpuid;
{
	int s = spl8();	/* should be spl_prof()?! */
	u_int cntrl = intr_bw_cntrl_get(cpuid, 0);
	u_int new_mode = TIMER_CLRFREE(cntrl);

	/* stop hi-res timer firt */
	intr_bw_cntrl_set(cpuid, 0, new_mode);

	/* now start profile timer */
	if (snooping) {
		u_int timer_limit;
		/*
		 * If deadman timer is turned on then set its limit
		 */

		timer_limit = deadman_limit *
		    ((((1000000) + 1) << (CTR_USEC_SHIFT)));

		intr_prof_setlimit(cpuid, 0, timer_limit);
	} else {
		intr_prof_setlimit(cpuid, 0, clk14_limit);
	}

	(void) splx(s);
}

void
disable_profiling(cpuid)
	int cpuid;
{
	u_int cntrl = intr_bw_cntrl_get(cpuid, 0);
	u_int new_mode = TIMER_SETFREE(cntrl);

	intr_prof_setlimit(cpuid, 0, 0);

	/* start the hrt timer */
	intr_bw_cntrl_set(cpuid, 0, new_mode);
}

void
clear_profiling_intr(cpuid)
	int cpuid;
{
	/*
	 * A read to the limit register should clr the int.
	 * Note that this relies on CC/BW is clear by some
	 * else which is true if we got here through sys_trap
	 * in locore.s.
	 */
	(void) intr_prof_getlimit_local();
}

int
enable_profiling_interrupt(dev_info_t *devi, u_int (*intr_handler)())
{
	if (have_fast_profile_intr) {
		if (ddi_add_fastintr(devi, 0, NULL, NULL,
			(u_int (*)())fast_profile_intr) != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "profile_attach: fast attach failed");
			return (DDI_FAILURE);
		}
	} else {
		if (ddi_add_intr(devi, 0, NULL, NULL,
			intr_handler, (caddr_t) 0) != DDI_SUCCESS) {
				return (DDI_FAILURE);
		}
	}
	return (DDI_SUCCESS);
}


int
disable_profiling_interrupt(dev_info_t *devi)
{
	/*
	 * Remove the interrupt vector.
	 */
	ddi_remove_intr(devi, (u_int) 0, NULL);

	return (DDI_SUCCESS);
}

/*
 * at startup, we have the kernel vector installed in
 * the scb, but the hardware is like when the monitor
 * clock is going, so we need to make it all consistent.
 * when we are done, things are turned off, because we
 * can't change the mappings on the scb (yet).
 */
extern trapvec mon_clock14_vec;
extern trapvec kclock14_vec;
int mon_clock_on = 1;	/* disables profiling */

void
start_mon_clock(void)
{
	mon_clock_on = 1;
	enable_profiling(cpu0.cpu_id);
}

void
stop_mon_clock(void)
{
	disable_profiling(cpu0.cpu_id);
	mon_clock_on = 0;
}

/*
 * timer_panic - do timer cleanup needed within panic()
 */
void
timer_panic()
{
	extern void sun4d_stub_nopanic(void);
	int s = splclock();
	sun4d_stub_nopanic();	/* so we'll have something else to ignore */
	(void) splx(s);
}

/*
 * The following wrappers have been added so that locking
 * can be exported to platform-independent clock routines
 * (ie adjtime(), clock_setttime()), via a functional interface.
 */
int
hr_clock_lock()
{
	return (CLOCK_LOCK());
}

void
hr_clock_unlock(int s)
{
	CLOCK_UNLOCK(s);
}
