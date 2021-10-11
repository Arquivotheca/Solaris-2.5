/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_CLOCK_H
#define	_SYS_CLOCK_H

#pragma ident	"@(#)clock.h	1.32	95/10/19 SMI"

#include <sys/spl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ECSR_PFN(device_id)	((0xf << 20) + ((device_id) << 12))
#define	CPU_DEVICEID(cpu_id)	((cpu_id) << 3)

#define	TOD_PAGES		(2)
#define	TOD_BYTES		(2 * MMU_PAGESIZE)

#define	TOD_BYTE_OFFSET		(0x1ff8)
#define	TOD_PAGE_OFFSET		(0x280)

/*
 * Definitions for the Mostek 48T02 clock chip. We use this chip as
 * our TOD clock. Clock interrupts are generated by a separate timer
 * circuit.
 */

#define	YRBASE		68	/* 1968 - what year 0 in chip represents */

#define	NANOSEC	1000000000
#define	ADJ_SHIFT 4		/* used in get_hrestime and _level10 */

#ifndef _ASM
struct mostek48T02 {
	volatile u_char	clk_ctrl;	/* ctrl register */
	volatile u_char	clk_sec;	/* counter - seconds 0-59 */
	volatile u_char	clk_min;	/* counter - minutes 0-59 */
	volatile u_char	clk_hour;	/* counter - hours 0-23 */
	volatile u_char	clk_weekday;	/* counter - weekday 1-7 */
	volatile u_char	clk_day;	/* counter - day 1-31 */
	volatile u_char	clk_month;	/* counter - month 1-12 */
	volatile u_char	clk_year;	/* counter - year 0-99 */
};

#ifdef	_KERNEL


#endif	/* _KERNEL */

#endif	/* _ASM */

/*
 * Bit masks for various operations and register limits.
 */
#define	CLK_CTRL_WRITE		0x80
#define	CLK_CTRL_READ		0x40
#define	CLK_CTRL_SIGN		0x20

#define	CLK_STOP		0x80
#define	CLK_KICK		0x80
#define	CLK_FREQT		0x40

#define	CTR_LIMIT_BIT		0x80000000	/* limit bit mask */
#define	CTR_USEC_MASK		0x7FFFFC00	/* counter/limit mask */
#define	CTR_USEC_SHIFT		10		/* counter/limit shift */

/* support functions for TOD clocks on sun4d */
#ifndef _ASM
void init_all_tods(void);
#endif /* _ASM */

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
 * Very frequent fetches of the tick-timer aggravate bugs in the BW.
 * The work-around is to fetch the timer less frequently.  This new
 * version of this macro serializes access via a lock and omits the
 * fetch entirely if the timer was recently read.  A software value of
 * the last read value is maintained for this purpose.  The lock and soft
 * timer value are new and reside in their own cache sub-block.  They are
 * never stored, only swapped.  This avoids excessive write-invalidates
 * which are a performance killer.
 */

/*
 * macro to get high res time in nanoseconds since boot to the register
 * pair outh/outl, using register pair scrh/scrl and nslt for scratch.
 * These must be specified as five distinct registers!
 *
 * WARNING: branches are hand-computed to prevent hidden conflicts with
 * local labels in the caller.  If you ever change these macros, make
 * sure you recompute the branch targets.
 *
 * WARNING: the first three instructions of this macro may execute in the
 * %psr delay of a previous wrpsr instruction.  It is therefore imperative
 * that the first three instructions do not modify the condition codes.
 */
#define	GET_HRTIME(outh, outl, scrh, scrl, nslt)			\
/* 1 */	sethi	%hi(hrtime_base), scrh;		/* time base addr */	\
	ldd	[scrh + %lo(hrtime_base)], outh; /* read time base */	\
	ldstub	[scrh + %lo(soft_hrtime+3)], nslt; /* try lock soft_hrtime */\
	tst	nslt;				/* did we get it? */	\
	/* CSTYLED */							\
	bz,a	. + 6*4;	/* 3f */	/* yes, go read timer */\
	ld	[scrh + %lo(clk10_phyaddr)], scrl; /* delay - BW addr */\
/* 2 */ andcc	nslt, 0xff, %g0;		/* is it clear yet? */	\
	/* CSTYLED */							\
	bnz,a	. - 1*4;	/* 2b */	/* if not, spin */	\
	ld	[scrh + %lo(soft_hrtime)], nslt; /* delay - read soft_hrtime */\
	/* CSTYLED */							\
	b,a	. + 4*4;	/* 4f */	/* use soft_hrtime */	\
/* 3 */ lda	[scrl]0x2f, nslt;		/* read counter */	\
	mov	nslt, scrl;			/* copy counter value */\
	swap	[scrh + %lo(soft_hrtime)], scrl; /* update and unlock */\
/* 4 */ ldd	[scrh + %lo(hrtime_base)], scrh; /* re-read time base */\
	sub	scrl, outl, scrl;		/* low bit diff */	\
	sub	scrh, outh, scrh;		/* high bit diff */	\
	or	scrl, scrh, scrl;		/* non-zero iff diff */	\
	sra	outh, 31, scrh;			/* non-zero iff invalid */\
	orcc	scrl, scrh, %g0;		/* base changed or invalid? */\
	bne	. - 19*4;	/* 1b */	/* yes, try again */	\
	addcc	nslt, nslt, nslt;		/* test & clear limit bit 31 */\
	srl	nslt, 7, scrl;			/* 2048u / 128 = 16u */	\
	sub	nslt, scrl, nslt;		/* 2048u - 16u = 2032u */\
	sub	nslt, scrl, nslt;		/* 2032u - 16u = 2016u */\
	sub	nslt, scrl, nslt;		/* 2016u - 16u = 2000u */\
	bcc	. + 5*4;	/* 5f */	/* limit bit not set */ \
	srl	nslt, 1, nslt;			/* delay: 2000u / 2 = nsec */\
	sethi	%hi(nsec_per_tick), scrh;				\
	ld	[scrh + %lo(nsec_per_tick)], scrh;			\
	add	nslt, scrh, nslt;		/* add 1 tick for limit bit */\
/* 5 */	addcc	outl, nslt, outl;		/* add nsec since last tick */\
	addx	outh, %g0, outh;		/* to hrtime_base */

/*
 * This macro return the value of hrestime, hrestime_adj and the counter.
 * It reads the value of hres_lock before and after loading the above
 * values. If the lock value changed in the meanwhile (i.e. level10 is/was
 * being processed on another processor or someone updated the hrestime_adj
 * and/or hrestime), the macro reads the value again.
 *
 * It assumes that the adj and hrest are register pairs. This macro
 * is called from trap (0x27) in sparc_subr.s.
 */
#define	GET_HRESTIME(out, scr, scr1, adj, hrest)			\
/* 1 */	sethi	%hi(hrtime_base), scr;		/* time base addr */	\
	ld	[scr + %lo(hres_lock)], scr1;	/* load clock lock */	\
	ldstub	[scr + %lo(soft_hrtime+3)], out; /* try to lock soft_hrtime */\
	tst	out;				/* did we get it? */	\
	/* CSTYLED */							\
	bz,a	. + 6*4;	/* 3f */	/* yes, go read timer */\
	ld	[scr + %lo(clk10_phyaddr)], adj; /* delay - BW addr */	\
/* 2 */	andcc	out, 0xff, %g0;			/* is it clear yet? */	\
	/* CSTYLED */							\
	bnz,a	. - 1*4;	/* 2b */	/* if not, spin */	\
	ld	[scr + %lo(soft_hrtime)], out;	/* delay - read soft_hrtime */\
	/* CSTYLED */							\
	b,a	. + 4*4;	/* 4f */	/* use soft_hrtime */	\
/* 3 */ lda	[adj]0x2f, out;			/* read counter */	\
	mov	out, adj;			/* copy counter value */\
	swap	[scr + %lo(soft_hrtime)], adj;	/* update and unlock */	\
/* 4 */ addcc	out, out, out;			/* test & clear limit bit 31 */\
	srl	out, 7, adj;			/* 2048u / 128 = 16u */	\
	sub	out, adj, out;			/* 2048u - 16u = 2032u */\
	sub	out, adj, out;			/* 2032u - 16u = 2016u */\
	sub	out, adj, out;			/* 2016u - 16u = 2000u */\
	bcc	. + 5*4;	/* 5f */	/* limit bit not set */ \
	srl	out, 1, out;			/* delay: 2000u / 2 = nsec */\
	sethi	%hi(nsec_per_tick), adj;				\
	ld	[adj + %lo(nsec_per_tick)], adj;			\
	add	out, adj, out;		/* add 1 tick for limit bit */	\
/* 5 */	ld	[scr + %lo(hres_last_tick)], adj;			\
	sub	out, adj, out;						\
	ldd	[scr + %lo(hrestime)], hrest;	/* load hrestime */	\
	ldd	[scr + %lo(hrestime_adj)], adj; /* load hrestime_adj */ \
	ld	[scr + %lo(hres_lock)], scr; /* load clock lock */	\
	andn	scr1, 1, scr1;	/* so cmp can detect lock still held */	\
	cmp	scr1, scr;						\
	bne	. - 30*4;	/* 1b */				\
	nop;

/*
 * This macro is here to support vtrace 3.x, which is microsecond-based.
 * This will go away with vtrace 4.0.0, which will be nanosecond-based.
 */
#define	GET_VTRACE_TIME(outl, scr1, scr2)				\
/* 1 */	sethi	%hi(vtrace_time_base), scr1;	/* time base addr */	\
	ld	[scr1 + %lo(vtrace_time_base)], outl; /* read time base */\
	ld	[scr1 + %lo(clk10_phyaddr)], scr2; /* counter addr */	\
	lda	[scr2]0x2f, scr2;		/* read counter */	\
	ld	[scr1 + %lo(vtrace_time_base)], scr1; /* re-read time base */\
	sub	outl, scr1, scr1;		/* scr1 < 0 iff update */\
	srl	scr1, 31, scr1;			/* scr1 = update ? 1 : 0 */\
	or	scr1, outl, scr1;		/* scr1 & 1 iff update */\
	andcc	scr1, 1, %g0;			/* update in progress? */\
	bnz	. - 9*4;	/* 1b */	/* yes, try again */	\
	addcc	scr2, scr2, scr2;		/* test & clear limit bit 31 */\
	bcc	. + 5*4;	/* 2f */	/* limit bit not set */ \
	srl	scr2, CTR_USEC_SHIFT + 1, scr2;	/* delay: convert to usec */\
	sethi	%hi(usec_per_tick), scr1;				\
	ld	[scr1 + %lo(usec_per_tick)], scr1;			\
	add	scr2, scr1, scr2;		/* add 1 tick for limit bit */\
/* 2 */	add	outl, scr2, outl;		/* add counter value */

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_CLOCK_H */
