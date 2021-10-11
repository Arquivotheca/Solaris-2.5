/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* Merges definitions from ICL version 5.5 (stdarg.h 6.1)	*/

#ifndef	_SYS_VARARGS_H
#define	_SYS_VARARGS_H

#pragma ident	"@(#)varargs.h	1.16	93/11/29 SMI"	/* UCB 4.1 83/05/03 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Many compilation systems depend upon the use of special functions
 * built into the the compilation system to handle variable argument
 * lists and stack allocations.  The method to obtain this in SunOS
 * is to define the feature test macro "__BUILTIN_VA_ARG_INCR" which
 * enables the following special built-in functions:
 *	__builtin_alloca
 *	__builtin_va_alist
 *	__builtin_va_arg_incr
 * It is intended that the compilation system define this feature test
 * macro, not the user of the system.
 *
 * The tests on the processor type are to provide a transitional period
 * for existing compilation systems, and may be removed in a future
 * release.
 */

#if defined(__STDC__)

/*
 * When __STDC__ is defined, this file provides stdarg semantics despite
 * the name of the file.
 */

#ifndef	_VA_LIST
#define	_VA_LIST
typedef void *va_list;
#endif

#if (defined(__BUILTIN_VA_ARG_INCR) || defined(sparc) || defined(__sparc) || \
    defined(i386) || defined(__i386)) && !(defined(lint) || defined(__lint))

#define	va_start(list, name) (void) (list = (va_list) &__builtin_va_alist)
#define	va_arg(list, mode) ((mode *)__builtin_va_arg_incr((mode *)list))[0]

#else

#if __STDC__ != 0	/* -Xc compilation */
#define	va_start(list, name) (void) (list = (void *)((char *)&name + \
	((sizeof (name) + (sizeof (int) - 1)) & ~(sizeof (int) - 1))))
#else
#define	va_start(list, name) (void) (list = (void *)((char *)&...))
#endif	/* __STDC__ != 0 */
#define	va_arg(list, mode) \
	((mode *)(list = (void *)((char *)list + sizeof (mode))))[-1]

#endif	/* (defined(__BUILTIN_VA_ARG_INCR) || ... */

extern void va_end(va_list);
#define	va_end(list) (void)0

#else	/* ! __STDC__ */

/*
 * In the absence of __STDC__, this file provides traditional varargs
 * semantics.
 */

typedef char *va_list;

#if (defined(__BUILTIN_VA_ARG_INCR) || defined(sparc) || defined(__sparc) || \
    defined(i386) || defined(__i386))
#define	va_alist __builtin_va_alist
#endif

#define	va_dcl int va_alist;
#define	va_start(list) list = (char *) &va_alist
#define	va_end(list)

#if (defined(__BUILTIN_VA_ARG_INCR) || defined(sparc) || defined(__sparc) || \
    defined(i386) || defined(__i386)) && !(defined(lint) || defined(__lint))
#define	va_arg(list, mode) ((mode*)__builtin_va_arg_incr((mode *)list))[0]
#else
#define	va_arg(list, mode) ((mode *)(list += sizeof (mode)))[-1]
#endif

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VARARGS_H */
