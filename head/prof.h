/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_PROF_H
#define	_PROF_H

#pragma ident	"@(#)prof.h	1.9	93/09/27 SMI"	/* SVr4.0 1.10.1.4 */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	MARK
#define	MARK(K)	{}
#else
#undef	MARK

#if defined(__STDC__)

#if #machine(i386) || defined(__i386)
#define	MARK(K)	{\
		asm("	.data"); \
		asm("	.align 4"); \
		asm("."#K".:"); \
		asm("	.long 0"); \
		asm("	.text"); \
		asm("M."#K":"); \
		asm("	movl	$."#K"., %edx"); \
		asm("	call _mcount"); \
		}
#endif

#if #machine(sparc) || defined(__sparc)
#define	MARK(K)	{\
		asm("	.reserve	."#K"., 4, \".bss\", 4"); \
		asm("M."#K":"); \
		asm("	sethi	%hi(."#K".), %o0"); \
		asm("	call	_mcount"); \
		asm("	or	%o0, %lo(."#K".), %o0"); \
		}
#endif

#else	/* __STDC__ */

#if defined(i386) || defined(__i386)
#define	MARK(K)	{\
		asm("	.data"); \
		asm("	.align 4"); \
		asm(".K.:"); \
		asm("	.long 0"); \
		asm("	.text"); \
		asm("M.K:"); \
		asm("	movl	$.K., %edx"); \
		asm("	call _mcount"); \
		}
#endif

#if defined(sparc) || defined(__sparc)
#define	MARK(K)	{\
		asm("	.reserve	.K., 4, \".bss\", 4"); \
		asm("M.K:"); \
		asm("	sethi	%hi(.K.), %o0"); \
		asm("	call	_mcount"); \
		asm("	or	%o0, %lo(.K.), %o0"); \
		}
#endif

#endif	/* __STDC__ */

#endif	/* MARK */

#ifdef	__cplusplus
}
#endif

#endif	/* _PROF_H */
