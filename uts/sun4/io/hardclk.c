/*
 * Copyright (c) 1987, 1993 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)hardclk.c	1.29	94/06/02 SMI" /* SunOS 4.1.1 1.13 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>

#include <sys/mmu.h>
#include <sys/clock.h>
#include <sys/intreg.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>

#include <fs/fs_subr.h>

int clock_type;

#define	CLOCK_RES	10000000	/* 10 millisecs */

int clock_res = CLOCK_RES;

/*
 * Machine-dependent clock routines.
 */

/*
 * Start the real-time clock. We set things up to interrupt every 1/100 of a
 * second.
 */
void
clkstart()
{
#ifndef SAS
	if (clock_type == INTERSIL7170) {
		/* set 1/100 sec clk int */
		CLOCK0->clk_intrreg = CLK_INT_HSEC;
	} else {
		COUNTER->limit10 =
		    (((1000000 / hz) + 1) << CTR_USEC_SHIFT) & CTR_USEC_MASK;
	}
#endif	/* !SAS */
	/* turn on level 10 clock intr */
	set_clk_mode(IR_ENA_CLK10, 0);
}

/*
 * enable profiling timer
 */
/* ARGSUSED */
void
enable_profiling(int cpuid)
{
	/*
	 * set interval timer to go off ever 10 ms
	 */
	if (clock_type == INTERSIL7170) {
		/* set 1/100 sec clk int */
		CLOCK0->clk_intrreg = CLK_INT_HSEC;
	} else {
		COUNTER->limit14 = (10000 << CTR_USEC_SHIFT) & CTR_USEC_MASK;
	}
	/*
	 * Turn on level 14 clock for profiling
	 */
	set_clk_mode(IR_ENA_CLK14, 0);
}

/*
 * disable profiling timer
 */
/* ARGSUSED */
void
disable_profiling(int cpuid)
{
	/*
	 * turn off level 14 clock
	 */
	set_clk_mode(0, IR_ENA_CLK14);
}

/*
 *  acknowledge the occurence of a profiling interrupt
 */
/* ARGSUSED */
void
clear_profiling_intr(int cpuid)
{
	int s, dummy;
	volatile struct intersil7170 *is;
	volatile struct counterregs *cr;

	if (clock_type == INTERSIL7170) {
		/*
		 * We need to clear the clock chip by reading its interrupt
		 * register and toggle the interrupt enable bit.
		 */
		s = spl7();
		/*
		 * Sun C 2.0 compiler bug,
		 *
		 * dummy = CLOCK0->clk_intrreg;
		 *
		 * is optimized away, despite COUNTER pointing to a volatile.
		 */
		is = CLOCK0;
		dummy = is->clk_intrreg;
		*INTREG &= ~IR_ENA_CLK14;
		*INTREG |= IR_ENA_CLK14;
		(void) splx(s);
	} else {
		/*
		 * Sun C 2.0 compiler bug,
		 *
		 * dummy = COUNTER->limit14;
		 *
		 * is optimized away, despite COUNTER pointing to a volatile.
		 */
		cr = COUNTER;
		dummy = cr->limit14;
	}
#if defined(lint) || defined(__lint)
	dummy = dummy;
#endif
}

/*
 * Set and/or clear the desired clock bits in the interrupt register. We have
 * to be extremely careful that we do it in such a manner that we don't get
 * ourselves lost.
 */
void
set_clk_mode(u_char on, u_char off)
{
	register u_char intreg, dummy;
	register int s;

	/*
	 * make sure that we are only playing w/ clock interrupt register
	 * bits
	 */
	on &= (IR_ENA_CLK14 | IR_ENA_CLK10);
	off &= (IR_ENA_CLK14 | IR_ENA_CLK10);

	/*
	 * Get a copy of current interrupt register, turning off any
	 * undesired bits (aka `off')
	 */
	s = spl7();
	intreg = *INTREG & ~(off | IR_ENA_INT);

	/*
	 * Next we turns off the CLK10 and CLK14 bits to clear the
	 * flip-flops, then we disable clock interrupts. Now we can read the
	 * clock's interrupt register to clear any pending signals there.
	 */
	*INTREG &= ~(IR_ENA_CLK14 | IR_ENA_CLK10);
#ifndef SAS
	if (clock_type == INTERSIL7170) {
		CLOCK0->clk_cmd = (CLK_CMD_NORMAL & ~CLK_CMD_INTRENA);
		dummy = CLOCK0->clk_intrreg;	/* clear clock */
	} else {
		dummy = COUNTER->limit10;
		dummy = COUNTER->limit14;
	}
#endif	/* !SAS */

#if defined(lint) || defined(__lint)
	dummy = dummy;
#endif

	/*
	 * Now we set all the desired bits in the interrupt register, then we
	 * turn the clock back on and finally we can enable all interrupts.
	 */
	*INTREG |= (intreg | on);	/* enable flip-flops */
#ifndef SAS
	if (clock_type == INTERSIL7170) {
		CLOCK0->clk_cmd = CLK_CMD_NORMAL;	/* enable clock intr */
	}
#endif	/* !SAS */
	(void) splx(s);
}

/*
 * For Sun-4, we use either the Intersil ICM7170 for both the real time clock
 * and the time-of-day device, or the Mostek MK48T02 for the time-of-day and
 * a separate counter timer service for the real time clock.
 */

/*
 * Write the specified time into the clock chip.
 * Must be called with tod_lock held.
 */
void
tod_set(timestruc_t ts)
{
	todinfo_t tod = utc_to_tod(ts.tv_sec);

	ASSERT(MUTEX_HELD(&tod_lock));

#if !defined(SAS) && !defined(MPSAS)
	if (clock_type == INTERSIL7170) {
		CLOCK0->clk_cmd		= (CLK_CMD_NORMAL & ~CLK_CMD_RUN);
		CLOCK0->clk_year	= tod.tod_year - YRBASE;
		CLOCK0->clk_mon		= tod.tod_month;
		CLOCK0->clk_day		= tod.tod_day;
		CLOCK0->clk_weekday	= tod.tod_dow - 1;
		CLOCK0->clk_hour	= tod.tod_hour;
		CLOCK0->clk_min		= tod.tod_min;
		CLOCK0->clk_sec		= tod.tod_sec;
		CLOCK0->clk_hsec	= ts.tv_nsec / 100000;
		CLOCK0->clk_cmd		= CLK_CMD_NORMAL;
	} else {
		CLOCK1->clk_ctrl |= CLK_CTRL_WRITE;
		CLOCK1->clk_year	= BYTE_TO_BCD(tod.tod_year - YRBASE);
		CLOCK1->clk_month	= BYTE_TO_BCD(tod.tod_month);
		CLOCK1->clk_day		= BYTE_TO_BCD(tod.tod_day);
		CLOCK1->clk_weekday	= BYTE_TO_BCD(tod.tod_dow);
		CLOCK1->clk_hour	= BYTE_TO_BCD(tod.tod_hour);
		CLOCK1->clk_min		= BYTE_TO_BCD(tod.tod_min);
		CLOCK1->clk_sec		= BYTE_TO_BCD(tod.tod_sec);
		CLOCK1->clk_ctrl &= ~CLK_CTRL_WRITE;
	}
#endif	/* !SAS */
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
	if (clock_type == INTERSIL7170) {
		tod.tod_year	= CLOCK0->clk_year + YRBASE;
		tod.tod_month	= CLOCK0->clk_mon;
		tod.tod_day	= CLOCK0->clk_day;
		tod.tod_dow	= CLOCK0->clk_weekday + 1;
		tod.tod_hour	= CLOCK0->clk_hour;
		tod.tod_min	= CLOCK0->clk_min;
		tod.tod_sec	= CLOCK0->clk_sec;
		ts.tv_nsec	= CLOCK0->clk_hsec * 100000;
	} else {
		CLOCK1->clk_ctrl |= CLK_CTRL_READ;
		tod.tod_year	= BCD_TO_BYTE(CLOCK1->clk_year) + YRBASE;
		tod.tod_month	= BCD_TO_BYTE(CLOCK1->clk_month & 0x1f);
		tod.tod_day	= BCD_TO_BYTE(CLOCK1->clk_day & 0x3f);
		tod.tod_dow	= BCD_TO_BYTE(CLOCK1->clk_weekday & 0x7);
		tod.tod_hour	= BCD_TO_BYTE(CLOCK1->clk_hour & 0x3f);
		tod.tod_min	= BCD_TO_BYTE(CLOCK1->clk_min & 0x7f);
		tod.tod_sec	= BCD_TO_BYTE(CLOCK1->clk_sec & 0x7f);
		CLOCK1->clk_ctrl &= ~CLK_CTRL_READ;
		ts.tv_nsec = 0;
	}
#endif	/* !SAS */

	ts.tv_sec = tod_to_utc(tod);

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
