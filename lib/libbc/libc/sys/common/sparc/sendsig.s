	.ident	"@(#)sendsig.s 1.1 93/07/23 SMI"

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#include <sys/asm_linkage.h>

!
!	__sendsig(sig, code, scp, addr, sigfunc)
!
!	Call a signal handler in a way that's compatible with statically
!	linked 4.x binaries.
!
!	We have to save our first four arguments to the stack because
!	that's what the 4.x kernel did and that's where the signal handler
!	(__sigtramp, normally) expects to find them.
!
ENTRY_NP(__sendsig)
	save	%sp, -SA(MINFRAME), %sp
	! save i0-i3 to stack, %sp+64 (WINDOWSIZE)
	std	%i0, [%sp + WINDOWSIZE]
	std	%i2, [%sp + WINDOWSIZE + 8]
	mov	%i0, %o0	! pass parameters in %o regs as well, in case
	mov	%i1, %o1	!   we're calling directly into C code (as will
	mov	%i2, %o2	!   happen if the a.out is dynamically linked)
	jmpl	%i4, %o7	! call the signal handler
	mov	%i3, %o3
	ret
	restore
