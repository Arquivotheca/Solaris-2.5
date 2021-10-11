/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)sbrk.s	1.18	93/03/02 SMI"	/* SVr4.0 1.6.1.6	*/

/* C-library -- brk, sbrk					*/
/* void *sbrk(int incr);					*/

	.file	"sbrk.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(sbrk,function)
	ANSI_PRAGMA_WEAK(brk,function)

#include "SYS.h"
#ifdef PIC
#include "PIC.h"
#endif

/* Align to 8-bytes - SPARC enhancement				*/
#define ALIGNSIZE	8

#ifndef	DSHLIB
	.global __sbrk_lock
	.global end
	.section	".data"
	.align	4
_nd:
	.word	end
#endif	/* DSHLIB */

	ENTRY(sbrk)
	save	%sp, -SA(MINFRAME), %sp	! acquire the lock for brk/sbrk
#ifdef PIC
	PIC_SETUP(o5)
	ld	[%o5 + __sbrk_lock], %l4
#else
	sethi   %hi(__sbrk_lock), %l4
	add     %lo(__sbrk_lock), %l4, %l4
#endif PIC
	call	_mutex_lock
	mov	%l4, %o0
	call	_sbrk_unlocked
	mov	%i0, %o0
	mov	%o0, %i0
	call	_mutex_unlock
	mov	%l4, %o0
	ret
	restore
	SET_SIZE(sbrk)

/* int brk (void *endds);					*/

	ENTRY(brk)
	save	%sp, -SA(MINFRAME), %sp	! acquire the lock for brk/sbrk
#ifdef PIC
        PIC_SETUP(o5)
        ld      [%o5 + __sbrk_lock], %l4
#else
        sethi   %hi(__sbrk_lock), %l4
        add     %lo(__sbrk_lock), %l4, %l4
#endif PIC
        call    _mutex_lock
        mov     %l4, %o0
	call	_brk_unlocked
	mov	%i0, %o0
        mov     %o0, %i0
        call    _mutex_unlock
        mov     %l4, %o0
	ret
	restore
	SET_SIZE(brk)


	ENTRY_NP(_sbrk_unlocked)

	add	%o0, (ALIGNSIZE-1), %o0	! round up request to align size
	andn	%o0, (ALIGNSIZE-1), %o0
#ifdef PIC
	PIC_SETUP(o5)
	ld	[%o5 + _nd], %g1
	ld	[%g1], %o3
#else
	sethi	%hi(_nd), %o2
	ld	[%o2 + %lo(_nd)], %o3
#endif
	add	%o3, (ALIGNSIZE-1), %o3	! round up _nd to align size
	andn	%o3, (ALIGNSIZE-1), %o3
	add	%o3, %o0, %o0		! new break setting = request + _nd
	mov	%o0, %o4		! save it
	SYSTRAP(brk)
	SYSCERROR
#ifdef PIC
	PIC_SETUP(o5)
	ld	[%o5 + _nd], %g1
	st	%o4, [%g1]
#else
	st	%o4, [%o2 + %lo(_nd)]	! store new break in _nd
#endif
	retl
	mov	%o3, %o0		! return old break

	SET_SIZE(_sbrk_unlocked)

/* int brk (void *endds);					*/

	ENTRY_NP(_brk_unlocked)

	add	%o0, (ALIGNSIZE-1), %o0	! round up new break to a
	andn	%o0, (ALIGNSIZE-1), %o0	! multiple of alignsize
	mov	%o0, %o2		! save new break
	SYSTRAP(brk)
	SYSCERROR
#ifdef PIC
	PIC_SETUP(o5)
	ld	[%o5 + _nd], %g1
	st	%o2, [%g1]
#else
	sethi	%hi(_nd), %g1		! save new break
	st	%o2, [%g1 + %lo(_nd)]
#endif
	RETC

	SET_SIZE(_brk_unlocked)
