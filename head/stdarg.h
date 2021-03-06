/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _STDARG_H
#define	_STDARG_H

#pragma ident	"@(#)stdarg.h	1.11	94/01/05 SMI"	/* SVr4.0 1.8	*/

#if defined(__STDC__)

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

#ifndef _VA_LIST
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
#define	va_arg(list, mode) ((mode *)(list = (char *)list + sizeof (mode)))[-1]

#endif	/* (defined(__BUILTIN_VA_ARG_INCR) || ... */

extern void va_end(va_list);
#define	va_end(list) (void)0

#ifdef	__cplusplus
}
#endif

#else	/* __STDC__ */

#include <varargs.h>

#endif	/* __STDC__ */

#endif	/* _STDARG_H */
