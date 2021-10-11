/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_TIMES_H
#define	_SYS_TIMES_H

#pragma ident	"@(#)times.h	1.9	92/08/18 SMI"	/* SVr4.0 11.7 */

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Structure returned by times()
 */
struct tms {
	clock_t	tms_utime;		/* user time */
	clock_t	tms_stime;		/* system time */
	clock_t	tms_cutime;		/* user time, children */
	clock_t	tms_cstime;		/* system time, children */
};

#if !defined(_KERNEL)
#if defined(__STDC__)
clock_t times(struct tms *);
#else
clock_t times();
#endif
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TIMES_H */
