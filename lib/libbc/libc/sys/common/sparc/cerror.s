!
!	"@(#)cerror.s 1.12 90/07/13"
!       Copyright (c) 1986 by Sun Microsystems, Inc.
!
!	Note this routine used to be called cerror, the
!	file name will not change for now. We might go
!	back to the old name.

!	.seg	"text"

#include "SYS.h"

!	.seg	"text"
	.global .cerror
	.global errno

	ENTRY(.cerror)
#ifdef PIC
	PIC_SETUP(o5)
	ld	[%o5 + errno], %g1
	st	%o0, [%g1]
#else
	sethi	%hi(errno), %g1
	st	%o0, [%g1 + %lo(errno)]
#endif
	save	%sp, -SA(MINFRAME), %sp
	call	maperror,0
	nop
	ret
	restore	%g0, -1, %o0

	SET_SIZE(.cerror)
