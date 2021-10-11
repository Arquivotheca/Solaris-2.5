/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SETJMP_H
#define	_SETJMP_H

#pragma ident	"@(#)setjmp.h	1.14	93/09/27 SMI"	/* SVr4.0 1.9.2.9 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _JBLEN

/*
 * The sizes of the jump-buffer (_JBLEN) and the sigjump-buffer
 * (_SIGJBLEN) are defined by the appropriate, processor specific,
 * ABI.
 */
#if defined(__STDC__)

/*
 * Note that the following construct, "#machine()", is a non-standard
 * extension to ANSI-C.  It is maintained here to provide compatibility
 * for existing compilations systems, but should be viewed as transitional
 * and may be removed in a future release.  If it is required that this
 * file not contain this extension, edit this file to remove the offending
 * condition.
 */
#if #machine(i386) || defined(__i386)
#define	_JBLEN		10	/* ABI value */
#define	_SIGJBLEN	128	/* ABI value */
#elif #machine(sparc) || defined(__sparc)
#define	_JBLEN		12	/* ABI value */
#define	_SIGJBLEN	19	/* ABI value */
#else
#error ISA not supported
#endif

#else

#if defined(i386) || defined(__i386)
#define	_JBLEN		10	/* ABI value */
#define	_SIGJBLEN	128	/* ABI value */
#elif defined(sparc) || defined(__sparc)
#define	_JBLEN		12	/* ABI value */
#define	_SIGJBLEN	19	/* ABI value */
#else
/* #error is strictly ansi-C, but works as well as anything for K&R systems. */
#error ISA not supported
#endif

#endif	/* __STDC__ */

typedef int jmp_buf[_JBLEN];

#if defined(__STDC__)
extern int setjmp(jmp_buf);
#pragma unknown_control_flow(setjmp)
extern void longjmp(jmp_buf, int);

#if __STDC__ == 0 || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)
/* non-ANSI standard compilation */

typedef int sigjmp_buf[_SIGJBLEN];

extern int sigsetjmp(sigjmp_buf, int);
#pragma unknown_control_flow(sigsetjmp)
extern void siglongjmp(sigjmp_buf, int);
#endif

#if __STDC__ != 0
#define	setjmp(env)	setjmp(env)
#endif

#else
typedef int sigjmp_buf[_SIGJBLEN];

extern int setjmp();
#pragma unknown_control_flow(setjmp)
extern void longjmp();
extern int sigsetjmp();
#pragma unknown_control_flow(sigsetjmp)
extern void siglongjmp();

#endif  /* __STDC__ */

#endif  /* _JBLEN */

#ifdef	__cplusplus
}
#endif

#endif	/* _SETJMP_H */
