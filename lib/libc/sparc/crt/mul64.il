!
!	Copyright (c) 1991, by Sun Microsystems, Inc.
!
!	@(#)mul64.il 1.1     91/08/19 SMI
!

	.inline	_umul32x32to64,8
	call	.umul,2
	nop
	mov	%o0,%o2
	mov	%o1,%o0
	mov	%o2,%o1
	.end
