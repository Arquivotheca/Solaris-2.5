!
!	"@(#)brk.s 1.13 90/07/13"
!       Copyright (c) 1986 by Sun Microsystems, Inc.
!
	.seg	".text"

#include "SYS.h"
#define ALIGNSIZE	8

#define	SYS_brk		17

	.global .curbrk
	.type	.curbrk,#object
	.size	.curbrk,4

	ENTRY(brk)
	add	%o0, (ALIGNSIZE-1), %o0	! round up new break to a
	andn	%o0, (ALIGNSIZE-1), %o0	! multiple of alignsize
	mov	%o0, %o2		! save new break
	mov	SYS_brk, %g1
	t	8
	CERROR(o5)
#ifdef PIC
	PIC_SETUP(o5)
	ld	[%o5 + .curbrk], %g1
	st	%o2, [%g1]
#else
	sethi	%hi(.curbrk), %g1	! save new break
	st	%o2, [%g1 + %lo(.curbrk)]
#endif
	RET
	SET_SIZE(brk)
