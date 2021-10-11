/*
 * Copyright (c) 1986-1992, Sun Microsystems, Inc.
 */

#pragma ident   "@(#)srt0_boot.s 1.3     94/11/20 SMI"

/*
 * srt0.s - standalone startup code
 * Generate the code with a fake a.out header if INETBOOT is defined.
 * inetboot is loaded directly by the PROM, other booters are loaded via
 * a bootblk and don't need the fake a.out header.
 */

#include <sys/asm_linkage.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/privregs.h>

	.seg	".text"
	.align	8
	.global	end
	.global	edata
	.global	main


/*
 * Initial interrupt priority and how to get there.
 */
#define PSR_PIL_SHIFT	8
#define PSR_PIL_INIT	(13 << PSR_PIL_SHIFT)

/*
 * The following variables are machine-dependent and are set in fiximp.
 * Space is allocated there.
 */
	.seg	".data"
	.align	8


#define STACK_SIZE	0x14000
	.skip	STACK_SIZE
.ebootstack:			! end (top) of boot stack

/*
 * The following variables are more or less machine-independent
 * (or are set outside of fiximp).
 */

	.seg	".text"
	.align	8
	.global	prom_exit_to_mon
	.type	prom_exit_to_mon, #function


! Each standalone program is responsible for its own stack. Our strategy
! is that each program which uses this runtime code creates a stack just
! below its relocation address. Previous windows may (and probably do)
! have frames allocated on the prior stack; leave them alone. Starting with
! this window, allocate our own stack frames for our windows. (Overflows
! or a window flush would then pass seamlessly from our stack to the old.)
! RESTRICTION: A program running at some relocation address must not exec
! another which will run at the very same address: the stacks would collide.
!
! Careful: don't touch %o0 until the save, since it holds the romp
! for Forth PROMs.
!
! We cannot write to any symbols until we are relocated.
! Note that with the advent of 5.x boot, we no longer have to 
! relocate ourselves, but this code is kept around cuz we *know*
! someone would scream if we did the obvious.
!



!
! Enter here for all booters loaded by a bootblk program.
! Careful, do not loose value of romp pointer in %o0
!

	ENTRY(_start)
	set	_start, %o1
	save	%o1, -SA(MINFRAME), %sp		! romp in %i0


!
! All booters end up here...
!

	/*
	 *  Use our own stack now. But, zero it first (do we have to?)
	 */
        set     .ebootstack, %o0
	set	STACK_SIZE, %o1
	sub	%o0, %o1, %o1
1:	dec	4, %o0
	st	%g0, [%o0]
	cmp	%o0, %o1
	bne	1b
	nop

        set     .ebootstack, %o0
        and     %o0, ~(STACK_ALIGN-1), %o0
        sub     %o0, SA(MINFRAME), %sp

	/*
	 * Set the psr into a known state:
	 * supervisor mode, interrupt level >= 13, traps enabled
	 */
	mov	%psr, %o0
	andn	%o0, PSR_PIL, %g1
	set	PSR_S|PSR_PIL_INIT|PSR_ET, %o1
	or	%g1, %o1, %g1		! new psr is ready to go
	mov	%g1, %psr
	nop; nop; nop
	
	! sas save romp and bootops
	! call	early_startup
	! nop

	call	main			! main(romp) or main(0) for sunmon
	mov	%i0, %o0		! 0 = sunmon, other = obp romp

	SET_SIZE(_start)

