/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1990, 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)boot.s	1.11	95/03/03 SMI"

/*
 * Bootstrap routine for ld.so.  Control arrives here either directly from
 * exec() upon invocation of a dynamically linked program specifying ld.so
 * as its interpreter, or from the "a.out compatibility ld.so".  Entry
 * vector at "_rt_boot" used to discriminate between the two.
 *
 * On entry, the stack appears as:
 *
 *	!_______________________!  high addresses
 *	!			!
 *	!	Information	!
 *	!	Block		!
 *	!	(size varies)	!
 *	!_______________________!
 *	!	0 word		!
 *	!_______________________!
 *	!	Auxiliary	!
 *	!	vector		!
 *	!	2 word entries	!
 *	!			!
 *	!_______________________!
 *	!	0 word		!
 *	!_______________________!
 *	!	Environment	!
 *	!	pointers	!
 *	!	...		!
 *	!	(one word each)	!
 *	!_______________________!
 *	!	0 word		!
 *	!_______________________!
 *	!	Argument	! low addresses
 *	!	pointers	!
 *	!	Argc words	!
 *	!_______________________!
 *	!			!
 *	!	Argc		!
 *	!_______________________!<- %sp +64
 *	!			!
 *	!   Window save area	!
 *	!_______________________! <- %sp
 *
 * In the case of being invoked from the a.out compatibility ld.so, the stack
 * has the appearance above but also several frames pushed by the compatibility
 * routine.  An "elf_boot" structure is available in %o1 on entry, which
 * has been "pre-evaluated" to hold much of the information needed from the
 * original stack frame -- we pass this directly into the main portion of the
 * ld.so bootstrap.
 */

#include <sys/asm_linkage.h>
#include <sys/param.h>

#if	defined(lint)

extern	unsigned long	_setup();
extern	void		call_fini();
void
main()
{
	(void) _setup();
	call_fini();
}

#else

#include <link.h>

	.file	"boot.s"
	.seg	".text"
	.global	_rt_boot, _setup, call_fini
	.type	_rt_boot, #function
	.align	4

! Entry vector
!	+0: normal start
!	+4: compatibility start
!	+8: alias start (frame exists)

_rt_boot:
	ba,a	_elf_start
	ba,a	_aout_start
	ba,a	_alias_start

! Start up routines -- the aout_start will have a pointer in %o0 that we'll
! want to save -- the elf can be zeroed.

_elf_start:
	clr	%o0			! 0 in %o0 == ELF
_aout_start:				! (falls through)

! Create a stack frame, perform PIC set up.  If we're a "normal" start, we have
! to determine a bunch of things from our "environment" and construct an ELF
! boot attribute value vector.  Otherwise, it's already been done and we can
! skip it.

	save	%sp, -SA(MINFRAME + (EB_MAX * 8)), %sp
_alias_start:
1:					! PIC prologue
	call	2f
	sethi	%hi(_GLOBAL_OFFSET_TABLE_ + (. - 1b)), %l7
2:
	or	%l7, %lo(_GLOBAL_OFFSET_TABLE_ + (. - 1b)), %l7

! If %i0 (was %o0) is non-zero, we're in compatibility and we can
! skip construction of the ELF boot attribute vector.

	addcc	%i0, %g0, %o0		! set condition codes
	bne	1f			! if non-zero, skip setup
	add	%l7, %o7, %l7		! finish PIC prologue

! %fp points to the root of our ELF bootstrap vector, use it to construct
! the vector and send it to _setup.

	add	%sp, SA(MINFRAME), %o0	! &eb[0] == %sp + frame size
	set	EB_ARGV, %l0		! code for this entry
	st	%l0, [%o0]		!   store it
	add	%fp, 68, %l0		! argument vector is at %fp+68
	st	%l0, [%o0 + 4]		!   store that
	ld	[%fp + 64], %l1		! get argument count
	inc	%l1			! account for last element of 0
	sll	%l1, 2, %l1		! multiply by 4
	add	%l0, %l1, %l0		!   and get address of first env ptr
	st	%l0, [%o0 + 12]		! store it in the vector
	set	EB_ENVP, %l1		! code for environment base
	st	%l1, [%o0 + 8]		!   store it
	set	EB_AUXV, %l1		! get code for auxiliary vector
	st	%l1, [%o0 + 16]		!   store it
2:
	ld	[%l0], %l1		! get an entry
	tst	%l1			! are we at a "0" entry in environment?
	bne	2b			!   no, go back and look again
	add	%l0, 4, %l0		!	incrementing pointer in delay
	st	%l0, [%o0 + 20]		! store aux vector pointer
	set	EB_NULL, %l0		! set up for the last pointer
	st	%l0, [%o0 + 24]		!   and store it

! Call _setup.  Two arguments, the ELF bootstrap vector and our (unrelocated)
! _DYNAMIC address.  The _DYNAMIC address is located in entry 0 of the GOT

1:
	call	_setup			! call it
	ld	[%l7], %o1		! 2nd parameter

! On return, give callee the exit function in %g1, and either jump to the
! target program (normal), or if return value of _setup is "0" we have
! to return to the compatibility bootstrap.  In either case, clear out
! reserved globals.

	ld	[%l7 + call_fini], %g1	! get function address
	mov	%g0, %g2		! clear globals
	mov	%g0, %g3
	restore	%o0, %g0, %l1		! release frame
	tst	%l1			! compatibility return?
	be	1f			! yes,
	mov	%g0, %g4		!   but clear one last global in delay
	jmpl	%l1, %g0		! call main program
	nop
1:
	retl				! compatibility return
	nop

	.size	_rt_boot, . - _rt_boot
#endif
