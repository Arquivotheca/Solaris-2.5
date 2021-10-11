/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)hardclk.c	1.57	95/05/22 SMI"

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/clock.h>
#include <sys/intreg.h>
#include <sys/cpuvar.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <vm/as.h>
#include <sys/stat.h>
#include <sys/sunddi.h>

extern int splclock();
extern void splx();

void flush_windows(void);
void srmmu_chgprot(struct as *, caddr_t, u_int, u_int);

/*
 * Machine-dependent clock routines.
 */

#define	CLOCK_RES	1000		/* 1 microsec in nanosecs */

int clock_res = CLOCK_RES;

static int limit10 = 0;	/* for MPSAS and Quickturn Emulation */

/*
 * Start the real-time clock.
 */
void
clkstart(void)
{
	/*
	 * Start counter in a loop to interrupt hz times/second.
	 *
	 * XXX	Note that the clock resets to 500ns, not zero, and
	 *	not 1us like every other platform.
	 */
	v_level10clk_addr->limit10 = (limit10) ? limit10 :
	    ((((1000000 / HZ) << CTR_USEC_SHIFT) & CTR_USEC_MASK)
	    + CTR_USEC_BASE);

	/*
	 * Turn on level 10 clock intr.
	 */
	set_clk_mode(IR_ENA_CLK10, 0);
}

/*
 * Enable/disable interrupts for level 10 and/or 14 clock.
 */

u_int	clk14_config = 0, clk14_avail = 0, clk10_limit = 0;
u_int	clk14_lim[4];

void
set_clk_mode(u_int on, u_int off)
{
	register u_char dummy;
	register int s;
	int	cpuix, i;

	s = splclock();
	/*
	 * Turn off level 10 clock interrupts by setting limit to 0.

	 */
	if (off & IR_ENA_CLK10) {
		clk10_limit = v_level10clk_addr->limit10;
		v_level10clk_addr->limit10 = 0;
	}

	/*
	 * Turn off level 14 clock interrupts by making them user counters.

	 */
	if (off & IR_ENA_CLK14) {
		int new_clk14_avail = 0;
		clk14_config = v_level10clk_addr->config;
		for (i = 0; i < NCPU; i++) {
			if (cpu[i] != NULL &&
			    (cpu[i]->cpu_flags & CPU_EXISTS)) {
				clk14_lim[i] = v_counter_addr[i]->timer_msw;
				new_clk14_avail |= (TMR0_CONFIG << i);
			}
		}
		v_level10clk_addr->config = TMRALL_CONFIG;
		clk14_avail = new_clk14_avail;
	}

	/*
	 * Clear all interrupts.
	 */
	dummy = v_level10clk_addr->limit10;
	for (cpuix = 0; cpuix < NCPU; cpuix++) {
		if (cpu[cpuix] != NULL && (cpu[cpuix]->cpu_flags & CPU_EXISTS))
			dummy = v_counter_addr[cpuix]->timer_msw;
	}
#ifdef lint
	dummy = dummy;
#endif lint

	/*
	 * Turn on level 10 clock interrupts by restoring limit.

	 */
	if (on & IR_ENA_CLK10) {
		if (clk10_limit)
			v_level10clk_addr->limit10 = clk10_limit;
	}

	/*
	 * Turn on level 14 clock interrupts by restoring configuration.
	 */
	if (on & IR_ENA_CLK14) {
		clk14_avail = clk14_config;
		v_level10clk_addr->config = clk14_config;
		for (i = 0; i < NCPU; i++) {
			if (cpu[i] != NULL && (cpu[i]->cpu_flags & CPU_EXISTS))
				v_counter_addr[i]->timer_msw = clk14_lim[i];
		}
	}
	(void) splx(s);
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
 * enable profiling timer
 */
void
enable_profiling(int cpuid)
{
	/* every 10 milli seconds */
	u_int profiling_ticks  = (10000 << CTR_USEC_SHIFT) & CTR_USEC_MASK;

	clk14_avail &= ~(TMR0_CONFIG << cpuid);

	v_level10clk_addr->config =
		v_level10clk_addr->config & ~(TMR0_CONFIG << cpuid);
	v_counter_addr[cpuid]->timer_msw = profiling_ticks;
}

/*
 * disable profiling timer
 */
void
disable_profiling(int cpuid)
{
	v_level10clk_addr->config =
		v_level10clk_addr->config | (TMR0_CONFIG << cpuid);

	clk14_avail |= (TMR0_CONFIG << cpuid);
}

/*
 *  acknowledge the occurence of a profiling interrupt
 */
void
clear_profiling_intr(int cpuid)
{
	volatile int dummy = v_counter_addr[cpuid]->timer_msw;
#ifdef lint
	dummy = dummy;
#endif
}

/* Every 2 second */
int snoop_lim = (2000000 << CTR_USEC_SHIFT) & CTR_USEC_MASK;

/*
 * Set up level14 clock on 'cpuid'
 */
void
start_snooping(cpuid)
int	cpuid;
{
	clk14_avail = 0;
	v_level10clk_addr->config = 0;
	v_counter_addr[cpuid]->timer_msw = snoop_lim;
}

static int olbolt[4];

/*
 * This function is called from the level14 clock handler to
 * make sure the level10 clock handler is still running. If
 * it isn't, we are probably deadlocked somewhere so enter
 * the debugger.
 *
 * We use CPU 0's level 14 clock for this purpose.
 *
 * While making a panic crash dump, interrupts are masked
 * off for a long time.  Wait at least a minute before
 * deciding the system is deadlocked.
 */

#define	DEADMAN_DELAY	30

void
deadman(void)
{
	volatile u_char	dummy;
	long delta;
	extern  int sync_cpus;

	dummy = v_counter_addr[CPU->cpu_id]->timer_msw;	/* Clear interrupt */
#ifdef lint
	dummy = dummy;
#endif
	if (sync_cpus)
		return;

	delta = olbolt[CPU->cpu_id] - lbolt;

	if (delta < 0)				/* Normal */
		olbolt[CPU->cpu_id] = lbolt;
	else if (delta < DEADMAN_DELAY)		/* Count down */
		olbolt[CPU->cpu_id]++;
	else {
		/*
		 * Hang detected!  Save processor state and then try
		 * to enter debugger.  Delay allows other running cpus
		 * to get into their deadman() routines as well.
		 */
		flush_windows();

		DELAY(1000000);

		debug_enter("deadman");		/* We're dead */

		olbolt[CPU->cpu_id] = lbolt;	/* reset timer */
	}
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

#if	!defined(SAS) && !defined(MPSAS)
	/*
	 * The eeprom (which also contains the tod clock) is normally
	 * marked ro; change it to rw temporarily to update todc.
	 * This must be done every time the todc is written since the
	 * prom changes the todc mapping back to ro when it changes
	 * nvram variables (e.g. the eeprom cmd).
	 */
	srmmu_chgprot(&kas, (caddr_t)((u_int)CLOCK & PAGEMASK), PAGESIZE,
		PROT_READ | PROT_WRITE);

	CLOCK->clk_ctrl |= CLK_CTRL_WRITE;	/* allow writes */
	CLOCK->clk_year		= BYTE_TO_BCD(tod.tod_year - YRBASE);
	CLOCK->clk_month	= BYTE_TO_BCD(tod.tod_month);
	CLOCK->clk_day		= BYTE_TO_BCD(tod.tod_day);
	CLOCK->clk_weekday	= BYTE_TO_BCD(tod.tod_dow);
	CLOCK->clk_hour		= BYTE_TO_BCD(tod.tod_hour);
	CLOCK->clk_min		= BYTE_TO_BCD(tod.tod_min);
	CLOCK->clk_sec		= BYTE_TO_BCD(tod.tod_sec);
	CLOCK->clk_ctrl &= ~CLK_CTRL_WRITE;	/* load values */

	/*
	 * Now write protect it, preserving the new modify/ref bits
	 */
	srmmu_chgprot(&kas, (caddr_t)((u_int)CLOCK & PAGEMASK), PAGESIZE,
		PROT_READ);
#endif
}

/*
 * Read the current time from the clock chip and convert to UNIX form.
 * Assumes that the year in the clock chip is valid.
 * Must be called with tod_lock held.
 */
timestruc_t
tod_get(void)
{
	timestruc_t ts;
	todinfo_t tod;

	ASSERT(MUTEX_HELD(&tod_lock));

#if	!defined(SAS) && !defined(MPSAS)

	srmmu_chgprot(&kas, (caddr_t)((u_int)CLOCK & PAGEMASK), PAGESIZE,
		PROT_READ | PROT_WRITE);

	CLOCK->clk_ctrl |= CLK_CTRL_READ;
	tod.tod_year	= BCD_TO_BYTE(CLOCK->clk_year) + YRBASE;
	tod.tod_month	= BCD_TO_BYTE(CLOCK->clk_month & 0x1f);
	tod.tod_day	= BCD_TO_BYTE(CLOCK->clk_day & 0x3f);
	tod.tod_dow	= BCD_TO_BYTE(CLOCK->clk_weekday & 0x7);
	tod.tod_hour	= BCD_TO_BYTE(CLOCK->clk_hour & 0x3f);
	tod.tod_min	= BCD_TO_BYTE(CLOCK->clk_min & 0x7f);
	tod.tod_sec	= BCD_TO_BYTE(CLOCK->clk_sec & 0x7f);
	CLOCK->clk_ctrl &= ~CLK_CTRL_READ;

	srmmu_chgprot(&kas, (caddr_t)((u_int)CLOCK & PAGEMASK), PAGESIZE,
			PROT_READ);
#endif !SAS

	ts.tv_sec = tod_to_utc(tod);
	ts.tv_nsec = 0;

	return (ts);
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
