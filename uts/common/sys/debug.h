/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_DEBUG_H
#define	_SYS_DEBUG_H

#pragma ident	"@(#)debug.h	1.16	94/12/20 SMI"	/* SVr4.0 11.11	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ASSERT(ex) causes a panic or debugger entry if
 * expression ex is not true.  ASSERT()
 * is included only for debugging, and is a no-op in production kernels.
 */

#if defined(__STDC__)
extern int assfail(char *, char *, int);
#if DEBUG
#define	ASSERT(EX) ((void)((EX) || assfail(#EX, __FILE__, __LINE__)))
#else
#define	ASSERT(x)
#endif
#else	/* defined(__STDC__) */
extern int assfail();
#if DEBUG
#define	ASSERT(EX) ((void)((EX) || assfail("EX", __FILE__, __LINE__)))
#else
#define	ASSERT(x)
#endif
#endif	/* defined(__STDC__) */

#ifdef	_KERNEL

extern void abort_sequence_enter(char *);
extern void debug_enter(char *);

#endif	/* _KERNEL */

#ifdef MONITOR
#define	MONITOR(id, w1, w2, w3, w4) monitor(id, w1, w2, w3, w4)
#else
#define	MONITOR(id, w1, w2, w3, w4)
#endif

#if defined(DEBUG) && !defined(sun)
/* CSTYLED */
#define	STATIC
#else
/* CSTYLED */
#define	STATIC static
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DEBUG_H */
