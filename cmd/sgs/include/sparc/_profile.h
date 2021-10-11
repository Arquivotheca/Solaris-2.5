/*
 *	Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)_profile.h	1.4	94/06/03 SMI"

#ifndef	_PROFILE_DOT_H
#define	_PROFILE_DOT_H

/*
 * Size of a dynamic PLT entry, used for profiling of shared objects.
 */
#define	M_DYN_PLT_ENT	0xc


#ifdef	PRF_RTLD
/*
 * Define MCOUNT macros that allow functions within ld.so.1 to collect
 * call count information.  Each function must supply a unique index.
 */
#ifndef	_ASM
#define	PRF_MCOUNT(index, func) \
	if (profile_rtld) { \
		asm("	ld	[%l7 +"#func"], %o2"); \
		asm("	mov	%i7, %o1"); \
		asm("	call	plt_cg_interp"); \
		asm("	mov	"#index", %o0"); \
	}
#else
#define	PRF_MCOUNT(index, func)
#endif
#else
#define	PRF_MCOUNT(index, func)
#endif

#endif
