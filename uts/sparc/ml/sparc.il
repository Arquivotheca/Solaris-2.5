#ident	"@(#)sparc.il	1.4	95/05/11 SMI"
!
! In-line functions for sparc kernels.
!

! return current thread pointer

	.inline	threadp,0
	mov	%g7, %o0
	.end

! needed by krtld

	.inline doflush,0
	andn	%o0, 3, %o0
	flush	%o0
	.end
