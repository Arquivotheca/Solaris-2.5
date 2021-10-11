!
!	Copyright (c) 1994, by Sun Microsytems, Inc.
!

	.section	".data"
	.align		4
	.global		prb_callinfo
prb_callinfo:
	.word		0		! offset
	.word		2		! shift right
	.word		0x3fffffff	! mask
	
	.section	".text"
	.align		4
	.global		prb_chain_entry
	.global		prb_chain_down
	.global		prb_chain_next
	.global		prb_chain_end
prb_chain_entry:
	save		%sp, -80, %sp
	or		%i0, %g0, %o0
	or		%i1, %g0, %o1
prb_chain_down:		
	call		prb_chain_down
	or		%i2, %g0, %o2
prb_chain_next:	
	call		prb_chain_next
	restore		%g0, %g0, %g0
prb_chain_end:	
	nop
		