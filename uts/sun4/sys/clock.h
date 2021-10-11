/*
 * Copyright (c) 1985, 1990, 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_CLOCK_H
#define	_SYS_CLOCK_H

#pragma ident	"@(#)clock.h	1.25	94/05/09 SMI"
/* From SunOS 4.1.1 sun4/clock.h */

#include <sys/psw.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * There are two types of time of day clock on Sun4 :
 * the Intersil 7170 and the Mostek 48t02.
 */
#define	INTERSIL7170	0
#define	MOSTEK48T02	1

/*
 * Definitions for the Intersil 7170 real-time clock.  This chip
 * is used as the timer chip in addition to being the battery
 * backed up time-of-day device.  This clock is run by UNIX in
 * the 100 hz periodic mode giving interrupts 100 times/second.
 *
 * Reading clk_hsec latches the the time in all the other bytes
 * so you get a consistent value.  To see any byte change, you
 * have to read clk_hsec in between (e.g. you can't loop waiting
 * for clk_sec to reach a certain value without reading clk_hsec
 * each time).
 */

#define	YRBASE		68	/* 1968 - what year 0 in chip represents */

#define	NANOSEC 1000000000
#define	ADJ_SHIFT 4		/* used in get_hrestime and _level10 */

#define	OBIO_CLOCK_ADDR	0xF3000000	/* address of clock in obio space */

#define	CLOCK0_ADDR	(MDEVBASE)	/* virtual address mapped to */

#ifndef _ASM
struct intersil7170 {
	u_char	clk_hsec;	/* counter - hundredths of seconds 0-99 */
	u_char	clk_hour;	/* counter - hours 0-23 (24hr) 1-12 (12hr) */
	u_char	clk_min;	/* counter - minutes 0-59 */
	u_char	clk_sec;	/* counter - seconds 0-59 */
	u_char	clk_mon;	/* counter - month 1-12 */
	u_char	clk_day;	/* counter - day 1-31 */
	u_char	clk_year;	/* counter - year 0-99 */
	u_char	clk_weekday;	/* counter - week day 0-6 */
	u_char	clk_rhsec;	/* RAM - hundredths of seconds 0-99 */
	u_char	clk_rhour;	/* RAM - hours 0-23 (24hr) 1-12 (12hr) */
	u_char	clk_rmin;	/* RAM - minutes 0-59 */
	u_char	clk_rsec;	/* RAM - seconds 0-59 */
	u_char	clk_rmon;	/* RAM - month 1-12 */
	u_char	clk_rday;	/* RAM - day 1-31 */
	u_char	clk_ryear;	/* RAM - year 0-99 */
	u_char	clk_rweekday;	/* RAM - week day 0-6 */
	u_char	clk_intrreg;	/* interrupt status and mask register */
	u_char	clk_cmd;	/* command register */
	u_char	clk_unused[14];
};
#define	CLOCK0 ((volatile struct intersil7170 *)(CLOCK0_ADDR))

#ifdef	_KERNEL

extern void	clkstart(void);
extern void	set_clk_mode(u_char, u_char);

#endif	/* _KERNEL */

#endif /* !_ASM */

/* offsets into structure */
#define	CLK_HSEC	0
#define	CLK_HOUR	1
#define	CLK_MIN		2
#define	CLK_SEC		3
#define	CLK_MON		4
#define	CLK_DAY		5
#define	CLK_YEAR	6
#define	CLK_WEEKDAY	7
#define	CLK_RHSEC	8
#define	CLK_RHOUR	9
#define	CLK_RMIN	10
#define	CLK_RSEC	11
#define	CLK_RMON	12
#define	CLK_RDAY	13
#define	CLK_RYEAR	14
#define	CLK_RWEEKDAY	15
#define	CLK_INTRREG	16
#define	CLK_CMD		17

/*
 * In `alarm' mode the 7170 interrupts when the current
 * counter matches the RAM values.  However, if the ignore
 * bit is on in the RAM counter, that register is not
 * used in the comparision.  Unfortunately, the clk_rhour
 * register uses a different mask bit (because of 12 hour
 * mode) and thus the 2 different defines.
 */
#define	CLK_IGNORE	0x80	/* rmsec, rmin, rsec, rmon, rday, ryear, rdow */
#define	CLK_HOUR_IGNORE	0x40	/* ignore bit for clk_rhour only */

/*
 * Interrupt status and mask register defines,
 * reading this register tells what caused an interrupt
 * and then clears the state.  These can occur
 * concurrently including te RAM compare interrupts.
 */
#define	CLK_INT_INTR	0x80	/* r/o pending interrrupt */
#define	CLK_INT_DAY	0x40	/* r/w periodic day interrupt */
#define	CLK_INT_HOUR	0x20	/* r/w periodic hour interrupt */
#define	CLK_INT_MIN	0x10	/* r/w periodic minute interrupt */
#define	CLK_INT_SEC	0x08	/* r/w periodic second interrupt */
#define	CLK_INT_TSEC	0x04	/* r/w periodic 1/10 second interrupt */
#define	CLK_INT_HSEC	0x02	/* r/w periodic 1/100 second interrupt */
#define	CLK_INT_ALARM	0x01	/* r/w alarm mode - interrupt on time match */

/* Command register defines */
#define	CLK_CMD_TEST	0x20	/* w/o test mode (vs. normal mode) */
#define	CLK_CMD_INTRENA	0x10	/* w/o interrupt enable (vs. disabled) */
#define	CLK_CMD_RUN	0x08	/* w/o run bit (vs. stop) */
#define	CLK_CMD_24FMT	0x04	/* w/o 24 hour format (vs. 12 hour format) */
#define	CLK_CMD_F4M	0x03	/* w/o using 4.194304MHz crystal frequency */
#define	CLK_CMD_F2M	0x02	/* w/o using 2.097152MHz crystal frequency */
#define	CLK_CMD_F1M	0x01	/* w/o using 1.048576MHz crystal frequency */
#define	CLK_CMD_F32K	0x00	/* w/o using  32.768KHz  crystal frequency */

#define	CLK_CMD_NORMAL	(CLK_CMD_INTRENA|CLK_CMD_RUN|CLK_CMD_24FMT|CLK_CMD_F32K)

/*
 * Definitions for the Mostek 48T02.  This chip is used for the time of
 * day service (battery backed up) and combines the eeprom and TOD.
 * Machines that use this chip also include hi resolution counters which
 * provide the real-time clock.  These counters are normally used to
 * generate 100hz clock interrupts, but are fully programmable for
 * whatever purpose desired.
 */
#define	CLOCK1_ADDR	(MDEVBASE + 0x27F8)
#ifndef	_ASM
struct mostek48T02 {
	volatile u_char	clk_ctrl;
	volatile u_char	clk_sec;
	volatile u_char	clk_min;
	volatile u_char	clk_hour;
	volatile u_char	clk_weekday;
	volatile u_char	clk_day;
	volatile u_char	clk_month;
	volatile u_char	clk_year;
};
#define	CLOCK1 ((struct mostek48T02 *)(CLOCK1_ADDR))
#endif	/* !_ASM */

#define	CLK_CTRL_WRITE		0x80
#define	CLK_CTRL_READ		0x40
#define	CLK_CTRL_SIGN		0x20

#define	CLK_STOP		0x80
#define	CLK_KICK		0x80
#define	CLK_FREQT		0x40

#define	OBIO_COUNTER_ADDR	0xEF000000
#define	COUNTER_ADDR		(MDEVBASE + 0xA000)

#ifndef	_ASM
struct counterregs {
	u_int	counter10;
	u_int	limit10;
	u_int	counter14;
	u_int	limit14;
};
#define	COUNTER ((volatile struct counterregs *)(COUNTER_ADDR))
#endif	/* !+ASM */

#define	CTR_LIMIT_BIT		0x80000000	/* limit bit mask */
#define	CTR_USEC_MASK		0x7FFFFC00	/* counter/limit mask */
#define	CTR_USEC_SHIFT		10		/* counter/limit shift */

/*
 * CLOCK_LOCK() puts a "ff" in the lowest byte of the hres_lock. The
 * higher three bytes are used as a counter. This lock is acquired
 * around "hrestime" and "timedelta". This lock is acquired to make
 * sure that level10 accounts for changes to this variable in that
 * interrupt itself. The level10 interrupt code also acquires this
 * lock.
 *
 * CLOCK_UNLOCK() increments the lower bytes straight, thus clearing the
 * lock and also incrementing the 3 byte counter. This way GET_HRESTIME()
 * can figure out if the value in the lock got changed or not.
 */

#define	HRES_LOCK_OFFSET 3

#define	CLOCK_LOCK()	\
	lock_set_spl(((lock_t *)&hres_lock) + HRES_LOCK_OFFSET, 	\
						ipltospl(LOCK_LEVEL))

#define	CLOCK_UNLOCK(spl)	\
	hres_lock++;		\
	(void) splx(spl)

/*
 * NOTE: the macros below assume that the various time-related variables
 * (hrtime_base, vtrace_time_base, hrestime, timedelta, etc) are all
 * stored together at a 64-byte boundary.  The real motivation is cache
 * performance, but here we take advantage of the side effect that all
 * these variables have the same high 22 address bits -- thus, only one
 * sethi is required.
 */

/*
 * macro to get high res time in nanoseconds since boot to the register
 * pair outh/outl, using register pair scrh/scrl and nslt for scratch.
 * These must be specified as five distinct registers!
 * XXX Not all sun4's have hires hardware clocks.  Perhaps this should
 * XXX do better on those that do.
 */
#define	GET_HRTIME(outh, outl, scrh, scrl, nslt)	\
	sethi	%hi(hrtime_base), scrh;			\
	ldd	[scrh + %lo(hrtime_base)], outh;

/*
 * This macro returns the value of hrestime, hrestime_adj and the counter.
 * It assumes that the adj and hrest are register pairs. This macro
 * is called from trap (0x27) in sparc_subr.s.
 */
#define	GET_HRESTIME(out, scr, scr1, adj, hrest)			\
	sethi	%hi(hrtime_base), scr;		/* scr = hi bits for all */\
	ldd	[scr + %lo(hrestime)], hrest;	/* load hrestime */	\
	ldd	[scr + %lo(hrestime_adj)], adj; /* load hrestime_adj */	\
	clr	out;				/* nslt should be zero */

/*
 * This macro is here to support vtrace 3.x, which is microsecond-based.
 * This will go away with vtrace 4.0.0, which will be nanosecond-based.
 */
#define	GET_VTRACE_TIME(outl, scr1, scr2)	\
	sethi	%hi(vtrace_time_base), outl;	\
	ld	[outl + %lo(vtrace_time_base)], outl;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CLOCK_H */
