/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1994, 1995, by Sun Microsystems, Inc.
 * All Rights Reserved.
 */

#ifndef _SYS_TIME_H
#define	_SYS_TIME_H

#pragma ident	"@(#)time.h	2.47	95/08/24 SMI"	/* SVr4.0 1.16	*/

#include <sys/feature_tests.h>

/*
 * Structure returned by gettimeofday(2) system call,
 * and used in other calls.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
#ifndef	_ASM

struct timeval {
	long	tv_sec;		/* seconds */
	long	tv_usec;	/* and microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};

#endif	/* _ASM */
#endif	/* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

#ifdef	__cplusplus
}
#endif

/*
 * <sys/types.h> includes <sys/socket.h> which needs the definition of
 * struct timeval. Hence this include is after those structure definitions.
 */
#ifndef	_ASM
#include <sys/types.h>
#endif	/* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)

#define	DST_NONE	0	/* not on dst */
#define	DST_USA		1	/* USA style dst */
#define	DST_AUST	2	/* Australian style dst */
#define	DST_WET		3	/* Western European dst */
#define	DST_MET		4	/* Middle European dst */
#define	DST_EET		5	/* Eastern European dst */
#define	DST_CAN		6	/* Canada */
#define	DST_GB		7	/* Great Britain and Eire */
#define	DST_RUM		8	/* Rumania */
#define	DST_TUR		9	/* Turkey */
#define	DST_AUSTALT	10	/* Australian style with shift in 1986 */

/*
 * Operations on timevals.
 *
 * NB: timercmp does not work for >= or <=.
 */
#define	timerisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timercmp(tvp, uvp, cmp) \
	/* CSTYLED */ \
	((tvp)->tv_sec cmp (uvp)->tv_sec || \
	((tvp)->tv_sec == (uvp)->tv_sec && \
	/* CSTYLED */ \
	(tvp)->tv_usec cmp (uvp)->tv_usec))

#define	timerclear(tvp)		(tvp)->tv_sec = (tvp)->tv_usec = 0

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2
#define	ITIMER_REALPROF	3

#ifndef	_ASM
struct	itimerval {
	struct	timeval it_interval;	/* timer interval */
	struct	timeval it_value;	/* current value */
};
#endif	/* _ASM */

/*
 *	Definitions for commonly used resolutions.
 */

#define	SEC		1
#define	MILLISEC	1000
#define	MICROSEC	1000000
#define	NANOSEC		1000000000

#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)

#define	__CLOCK_REALTIME0	0	/* wall clock, bound to LWP */
#define	CLOCK_VIRTUAL		1	/* user CPU usage clock */
#define	CLOCK_PROF		2	/* user and system CPU usage clock */
#define	__CLOCK_REALTIME3	3	/* wall clock, not bound */
/*
 * Define CLOCK_REALTIME as per-process if PTHREADS or explicitly requested
 *   NOTE: In the future, per-LWP semantics will be removed and
 *   __CLOCK_REALTIME0 will have per-process semantics (see timer_create(3R))
 */
#if	(_POSIX_C_SOURCE >= 199506L) || defined(_POSIX_PER_PROCESS_TIMER_SOURCE)
#define	CLOCK_REALTIME	__CLOCK_REALTIME3
#else
#define	CLOCK_REALTIME	__CLOCK_REALTIME0
#endif

#define	TIMER_RELTIME	0x0		/* set timer relative */
#define	TIMER_ABSTIME	0x1		/* set timer absolute */

#endif	/* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

#ifndef	_ASM

/*
 * Time expressed in seconds and nanoseconds
 */


#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
typedef struct  timespec {		/* definition per POSIX.4 */
	time_t		tv_sec;		/* seconds */
	long		tv_nsec;	/* and nanoseconds */
} timespec_t;

typedef struct timespec timestruc_t;	/* definition per SVr4 */

#else

typedef	struct	_timespec {
	time_t		_tv_sec;	/* seconds */
	long		_tv_nsec;	/* and nanoseconds */
} timespec_t;

typedef	struct _timespec  timestruc_t;	/* definition per SVr4 */
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE_))... */

/*
 * The following has been left in for backward compatibility. Portable
 * applications should not use the structure name timestruc.
 */

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
#define	timestruc	timespec	/* structure name per SVr4 */
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

/*
 * Timer specification
 */
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
typedef struct itimerspec {		/* definition per POSIX.4 */
	struct timespec	it_interval;	/* timer period */
	struct timespec	it_value;	/* timer expiration */
} itimerspec_t;
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) ... */

/*
 * Time expressed as a 64-bit nanosecond counter.
 */
typedef	longlong_t	hrtime_t;

#ifdef _KERNEL

#include <sys/mutex.h>

extern int	usec_per_tick;		/* microseconds per clock tick */
extern int	nsec_per_tick;		/* nanoseconds per clock tick */

typedef struct todinfo {
	int	tod_sec;	/* seconds 0-59 */
	int	tod_min;	/* minutes 0-59 */
	int	tod_hour;	/* hours 0-23 */
	int	tod_dow;	/* day of week 1-7 */
	int	tod_day;	/* day of month 1-31 */
	int	tod_month;	/* month 1-12 */
	int	tod_year;	/* year 70+ */
} todinfo_t;

extern	timestruc_t	hrestime;
extern	longlong_t	timedelta;
extern	int		tod_needsync;
extern	kmutex_t	tod_lock;

extern	timestruc_t	tod_get(void);
extern	void		tod_set(timestruc_t);
extern	todinfo_t	utc_to_tod(time_t);
extern	time_t		tod_to_utc(todinfo_t);
extern	hrtime_t 	gethrtime(void);
extern	void 		gethrestime(timespec_t *);
extern	void		hrt2ts(hrtime_t, timestruc_t *);
extern	hrtime_t	ts2hrt(timestruc_t *);
extern	int		itimerfix(struct timeval *);
extern	int		itimerdecr(struct itimerval *, int);
extern	void		timevaladd(struct timeval *, struct timeval *);
extern	void		timevalsub(struct timeval *, struct timeval *);
extern	void		timevalfix(struct timeval *);

#endif /* _KERNEL */

#if (!defined(_KERNEL) && !defined(_POSIX_C_SOURCE) && \
	!defined(_XOPEN_SOURCE)) || defined(__EXTENSIONS__)
#if defined(__STDC__)
int adjtime(struct timeval *, struct timeval *);
int getitimer(int, struct itimerval *);
int setitimer(int, const struct itimerval *, struct itimerval *);

/*
 * gettimeofday() and settimeofday() were included in SVr4 due to their
 * common use in BSD based applications.  They were to be included exactly
 * as in BSD, with two parameters.  However, AT&T/USL noted that the second
 * parameter was unused and deleted it, thereby making a routine included
 * for compatibility, uncompatible.
 *
 * XSH4.2 (spec 1170) defines gettimeofday and settimeofday to have two
 * parameters.
 *
 * This has caused general disagreement in the application community as to
 * the syntax of these routines.  Solaris defaults to the XSH4.2 definition.
 * The flag _SVID_GETTOD * may be used to force the SVID version.
 */
#if defined(_SVID_GETTOD)
int gettimeofday(struct timeval *);
int settimeofday(struct timeval *);
#else
int gettimeofday(struct timeval *, void *);
int settimeofday(struct timeval *, void *);
#endif

hrtime_t	gethrtime(void);
hrtime_t	gethrvtime(void);
#else
int adjtime();
int getitimer();
int setitimer();
int gettimeofday();
int settimeofday();

hrtime_t	gethrtime();
hrtime_t	gethrvtime();
#endif /* __STDC__ */
#if !defined(_XOPEN_SOURCE)
#include <time.h>
#endif
#endif	/* !defined(_KERNEL) && !defined(_POSIX_C_SOURCE) */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TIME_H */
