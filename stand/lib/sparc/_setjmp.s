/*
 * Copyright (c) 1986, 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)_setjmp.s	1.9	92/07/14 SMI" /* from SunOS 4.1 */

#if defined(lint)

#include <sys/debug/debugger.h>

#else	/* lint */

#include <sys/asm_linkage.h>

#endif	/* lint */

/*
 * _setjmp(buf_ptr)
 * buf_ptr points to a five word array (jmp_buf). In the first is our
 * return address, the second, is the callers SP.
 * The rest is cleared by _setjmp
 *
 *		+----------------+
 *   %i0->	|      pc        |
 *		+----------------+
 *		|      sp        |
 *		+----------------+
 *		|    sigmask     |
 *		+----------------+
 *		|   stagstack    |
 *		|   structure    |
 *		+----------------+
 */

#if defined(lint)

/* ARGSUSED */
int
_setjmp(jmp_buf_ptr buf_ptr)
{ return (0); }

#else	/* lint */

	PCVAL	=	0	! offsets in buf structure
	SPVAL	=	4
	SIGMASK	=	8
	SIGSTACK =	12

	SS_SP	   =	0	! offset in sigstack structure
	SS_ONSTACK =	4

	ENTRY(_setjmp)
	st	%o7, [%o0 + PCVAL] 	! return pc
	st	%sp, [%o0 + SPVAL] 	! save caller's sp
	clr	[%o0 + SIGMASK]		! clear the remainder of the jmp_buf
	clr	[%o0 + SIGSTACK + SS_SP]
	clr	[%o0 + SIGSTACK + SS_ONSTACK]
	retl
	clr	%o0
	SET_SIZE(_setjmp)

#endif	/* lint */

/*
 * _longjmp(buf_ptr, val)
 * buf_ptr points to an array which has been initialized by _setjmp.
 * val is the value we wish to return to _setjmp's caller
 *
 * We will flush our registers by doing (nwindows-1) save instructions.
 * This could be better done as a kernel call. This is necessary to
 * ensure that the registers we want to pick up are stored in the stack.
 * Then, we set fp from the saved fp and make ourselves a stack frame.
 */

#if defined(lint)

/* ARGSUSED */
void
_longjmp(jmp_buf_ptr buf_ptr, int val)
{}

#else	/* lint */

	ENTRY(_longjmp)
	save	%sp, -WINDOWSIZE, %sp
	!
	! flush all register windows to the stack.
	! 
	set	nwindows, %g7
	ld	[%g7], %g7
	sub	%g7, 2, %g6
1:
	deccc	%g6			! all windows done?
	bnz	1b
	save	%sp, -WINDOWSIZE, %sp
	sub	%g7, 2, %g6
2:
	deccc	%g6			! all windows done?
	bnz	2b
	restore				! delay slot, increment CWP

	ld	[%i0 + SPVAL], %fp	! build new stack frame
	sub	%fp, -SA(MINFRAME), %sp	! establish new save area
	ld	[%i0 + PCVAL], %i7	! get new return pc
	ret
	restore	%i1, 0, %o0		! return (val)
	SET_SIZE(_longjmp)

#endif	/* lint */
