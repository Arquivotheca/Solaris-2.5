/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */

#ifndef	_SYS_TIMEB_H
#define	_SYS_TIMEB_H

#pragma ident	"@(#)timeb.h	1.10	95/02/24 SMI"	/* SVr4.0 1.5 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * THIS FILE CONTAINS CODE WHICH IS DESIGNED TO BE
 * PORTABLE BETWEEN DIFFERENT MACHINE ARCHITECTURES
 * AND CONFIGURATIONS. IT SHOULD NOT REQUIRE ANY
 * MODIFICATIONS WHEN ADAPTING XENIX TO NEW HARDWARE.
 */

/*
 * Structure returned by ftime system call
 */
struct timeb {
	time_t	time;		/* time, seconds since the epoch */
	unsigned short millitm;	/* 1000 msec of additional accuracy */
	short	timezone;	/* timezone, minutes west of GMT */
	short	dstflag;	/* daylight savings when appropriate? */
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TIMEB_H */
