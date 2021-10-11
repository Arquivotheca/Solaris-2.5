/*
 * Copyright (c) 1986-1992, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sun4x_srt0.s	1.53	95/06/27 SMI"

/*
 * srt0.s - standalone startup code
 * Generate the code with a fake a.out header if INETBOOT is defined.
 * inetboot is loaded directly by the PROM, other booters are loaded via
 * a bootblk and don't need the fake a.out header.
 */

#include <sys/asm_linkage.h>
#include <sys/psr.h>
#include <sys/cpu.h>

#if defined(lint)

/*ARGSUSED*/
void
_start(void *romp)
{}

#else
	.seg	".text"
	.align	8
	.global	end
	.global	edata
	.global	main

#ifdef	INETBOOT

!
!
! This is where network booters (or any program loaded directly by the
! firmware) gets control.  For sunmon, there is no header.
! This is the nastiest program I've ever written. The OBP checks
! for the magic number 0x01030107, and if found it assumes the rest
! of the header is there and adjusts the text accordingly.
! In other words, it starts running this bootblock at the *eighth*
! instruction.
!
! Fortuitously, the a.out magic number is *a no-op in SPARC*
! so it does not discomfit the sunmon prom. We'll use this 
! stunt to distinguish which prom we're running. Remember: as they
! say at Marine World, this is not a "trick," it's a "natural behavior."
!

	ENTRY(_start)
	.word	0x01030107		! sethi	0x30107, %g0 (yecch!)
	!
	! OK, that faked out the forth prom. We have seven more instructions,
	! then we must jump to the forth startup code.
	!
	clr	%o0			! 1
	nop				! 2
	nop				! 3
	nop				! 4
	nop				! 5
	nop				! 6
	nop				! 7
!
! OBP gets control here!
!
! Don't munge %o0 since it contains the romp. Set _start and end
! since we'll need those values for both OBP and sunmon.
!
	sethi	%hi(_start), %o1
	or	%o1, %lo(_start), %o1
	sethi	%hi(end), %o2
	tst	%o0
	bz	1f
	or	%o2, %lo(end), %o2	
!
! Problem: the OBP has seen the fake magic number in srt0 and has
! adjusted the executable image to be 0x20 bytes lower in memory,
! since it thinks it has to compensate for the a.out header.
! But there is no real a.out header, so all our memory references
! are 32 bytes off. Fix that here by recopying ourselves back up
! to the addresses we're linked at...but see fakecopy below.
!
	ld	[%o0 + 4], %o4	! romvec version
	mov	%o1, %l0
	add	%o1, 0x20, %l1
	call	fakecopy	! fakecopy(start, start+0x20, (end-start))
	sub	%o2, %o1, %l2

1:
	save	%o1, -SA(MINFRAME), %sp
	!
	! zero the bss
	!
	sethi	%hi(edata), %o0	! Beginning of bss
	or	%o0, %lo(edata), %o0
	call	bzero
	sub	%i2, %o0, %o1		! end - edata = size of bss
	b	9f			! Goto common code
	nop

!
! Handle the copy from (_start) to (_start+0x20). Note that the "retl"
! adds a 32-byte offset to the return value in order to reset the %pc.
! Note also that the copy loop is duplicated, so the text can move
! by 8 instructions but the same instructions are fetched.
!

1:					! Eight instructions...
	deccc	%l2			! 1
	bl	2f			! 2
	ldub	[%l0 + %l2], %l3	! 3
	ba	1b			! 4
	stb	%l3, [%l1 + %l2]	! 5
2:
	jmpl	%o7 + 8 + 0x20, %g0	! 6
	nop				! 7
	nop				! 8
! Begin in the second half, since
! the loop will get copied 32 bytes up.
	ENTRY(fakecopy)
3:					! ...and again...
	deccc	%l2			! 1
	bl	4f			! 2
	ldub	[%l0 + %l2], %l3	! 3
	ba	3b			! 4
	stb	%l3, [%l1 + %l2]	! 5
4:
	jmpl	%o7 + 8 + 0x20, %g0	! 6
	nop				! 7
	nop				! 8

#endif	/* INETBOOT */

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


#ifndef	INETBOOT

!
! Enter here for all booters loaded by a bootblk program.
! Careful, do not loose value of romp pointer in %o0
!

	ENTRY(_start)
	set	_start, %o1
	save	%o1, -SA(MINFRAME), %sp		! romp in %i0
	!
	! zero the bss
	!
	sethi	%hi(edata), %o0			! Beginning of bss
	or	%o0, %lo(edata), %o0
	set	end, %i2
	call	bzero
	sub	%i2, %o0, %o1			! end - edata = size of bss

#endif	/* INETBOOT */

!
! All booters end up here...
!

9:
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
	
	call	main			! main(romp) or main(0) for sunmon
	mov	%i0, %o0		! 0 = sunmon, other = obp romp

	call	prom_exit_to_mon	! can't happen .. :-)
	nop
	SET_SIZE(_start)

#endif	/* lint */

/*
 *  exitto is called from main() and does 2 things:
 *	it checks for a vac; if it finds one, it turns it off
 *	in an arch-specific way.
 *	It then jumps directly to the just-loaded standalone.
 *	There is NO RETURN from exitto().
 */

#if defined(lint)

/* ARGSUSED */
void
exitto(int (*entrypoint)())
{}

#else	/* lint */

	ENTRY(exitto)
	save	%sp, -SA(MINFRAME), %sp

	set	romp, %o0		! pass the romp to the callee
	clr	%o1			! boot passes no dvec
	set	bootops, %o2		! pass bootops vector to callee
	sethi	%hi(elfbootvec), %o3	! pass elf bootstrap vector
	ld	[%o3 + %lo(elfbootvec)], %o3
	clr	%o4			! 1210381 - no 1275 cif
	jmpl	%i0, %o7		! call thru register to the standalone
	ld	[%o0], %o0
	/*  there is no return from here */
	SET_SIZE(exitto)

#endif	/* lint */
