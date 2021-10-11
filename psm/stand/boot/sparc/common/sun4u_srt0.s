/*
 * Copyright (c) 1986-1992, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sun4u_srt0.s	1.11	95/07/12 SMI"

/*
 * srt0.s - standalone startup code
 * Generate the code with a fake a.out header if INETBOOT is defined.
 * inetboot is loaded directly by the PROM, other booters are loaded via
 * a bootblk and don't need the fake a.out header.
 */

#include <sys/asm_linkage.h>
#include <sys/cpu.h>
#include <sys/privregs.h>
#include <sys/stack.h>


#if defined(lint)

/*ARGSUSED*/
void
_start(void *a, void *b, void *c, void *cif_handler)
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
	clr	%o4			! 2	1275 SPARC v9 CIF Handler
	nop				! 3
	nop				! 4
	nop				! 5
	nop				! 6
	nop				! 7
!
! OBP gets control here!
!
! SPARCV9 bindings specify %o4 contains the address of the client interface
! handler.
!
! Problem: the OBP has seen the fake magic number in srt0 and has
! adjusted the executable image to be 0x20 bytes lower in memory,
! since it thinks it has to compensate for the a.out header.
! But there is no real a.out header, so all our memory references
! are 32 bytes off. Fix that here by recopying ourselves back up
! to the addresses we're linked at...but see fakecopy below.
!
!
! NB: until the common startup code, AM may not be set.
!
1:
	set	_start, %l0
	mov	%l0, %o1	! save _start as temp stack address
	add	%l0, 0x20, %l1
	set	_end, %l2
	call	fakecopy	! fakecopy(start, start+0x20, (end-start))
	sub	%l2, %l0, %l2

!
! inetboot common startup, setup temp 32 bit stack, do a save instruction
! and then zero bss
!

5:
	save	%o1, -SA64(MINFRAME64), %sp ! use temp stack setup above
	set	edata, %o0		! Beginning of bss
	set	end, %o1
	call	bzero
	sub	%o1, %o0, %o1		! end - edata = size of bss
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
 * The following variables are machine-dependent and are set in fiximp.
 * Space is allocated there.
 */
	.seg	".data"
	.align	8

_local_p1275cif:
	.word	0

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
! Careful: don't touch %o4 until the save, since it contains the
! address of the IEEE 1275 SPARC v9 CIF handler (linkage to the prom).
!
!
! We cannot write to any symbols until we are relocated.
! Note that with the advent of 5.x boot, we no longer have to
! relocate ourselves, but this code is kept around cuz we *know*
! someone would scream if we did the obvious.
!


#ifndef	INETBOOT

!
! Enter here for all booters loaded by a bootblk program.
! Careful, do not loose value of the SPARC v9 P1275 CIF handler in %o4
! Setup temporary 32 bit stack at _start.
!
! NB: Until the common startup code, AM may not be set.
!

	ENTRY(_start)
	set	_start, %o1
	save	%o1, -SA64(MINFRAME64), %sp	! %i4: 1275 sparcv9 CIF handler
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
	 *  Use our own 32 bit stack now. But, zero it first (do we have to?)
	 */
	set	.ebootstack, %o0
	set	STACK_SIZE, %o1
	sub	%o0, %o1, %o1
1:	dec	4, %o0
	st	%g0, [%o0]
	cmp	%o0, %o1
	bne	1b
	nop

	set	.ebootstack, %o0
	and	%o0, ~(STACK_ALIGN64-1), %o0
	sub	%o0, SA64(MINFRAME64), %sp

	/*
	 * Set the psr into a known state:
	 * Set AM, supervisor mode, interrupt level >= 13, traps enabled
	 */
	wrpr	%g0, PSTATE_PEF+PSTATE_AM+PSTATE_PRIV+PSTATE_IE, %pstate

	sethi	%hi(_local_p1275cif), %o1
	st	%i4, [%o1 + %lo(_local_p1275cif)]
	call	main			! main(prom-cookie) (main(0) - sunmon)
	mov	%i4, %o0		! SPARCV9/CIF 

	call	prom_exit_to_mon	! can't happen .. :-)
	nop
	SET_SIZE(_start)

#endif	/* lint */

/*
 *	exitto is called from main() and It jumps directly to the
 *	just-loaded standalone.  There is NO RETURN from exitto().
 */

#if defined(lint)

/* ARGSUSED */
void
exitto(int (*entrypoint)())
{}

#else	/* lint */

	ENTRY(exitto)
	save	%sp, -SA(MINFRAME), %sp

	sethi	%hi(_local_p1275cif), %o0 ! pass the 1275 CIF handler to callee.
	ld	[%o0 + %lo(_local_p1275cif)], %o0
	clr	%o1			! boot passes no dvec
	set	bootops, %o2		! pass bootops vector to callee
	sethi	%hi(elfbootvec), %o3	! pass elf bootstrap vector
	ld	[%o3 + %lo(elfbootvec)], %o3
	jmpl	%i0, %o7		! call thru register to the standalone
	mov	%o0, %o4		! 1210378: Pass cif in both %o0 & %o4
	/*  there is no return from here */
	SET_SIZE(exitto)

#endif	/* lint */

#ifdef MPSAS

#if defined(lint)

/*
 * void
 * sas_command(cmdstr) - send a "user interface" cmd to SAS
 *
 * FOR SAS only ....
 */
/* ARGSUSED */
int
sas_command(char *cmdstr)
{ return (0); }

#else   /* lint */

#define	ST_SAS_COMMAND	330-256

	ENTRY_NP(sas_command)
	ta	ST_SAS_COMMAND
	nop
	retl
	nop
	SET_SIZE(sas_command)

#endif  /* lint */
#endif /* MPSAS */

/*
 * The interface for a 32-bit client program
 * calling the 64-bit romvec OBP.
 */

#if defined(lint)
#include <sys/promif.h>

/* ARGSUSED */
int
client_handler(void *cif_handler, cell_t **arg_array)
{ return (0); }

#else	/* lint */

	ENTRY(client_handler)
	save	%sp, -SA64(MINFRAME64), %sp	! 32 bit frame, 64 bit sized
	srl	%i0, 0, %i0			! zero extend handler addr
	srl	%i1, 0, %o0			! zero extend first argument.
	srl	%sp, 0, %sp			! zero extend sp
	rdpr	%pstate, %l1			! Get the present pstate value
	wrpr	%l1, PSTATE_AM, %pstate		! Set PSTATE_AM = 0
	jmpl	%i0, %o7			! Call cif handler
	sub	%sp, V9BIAS64, %sp		! delay; Now a 64 bit frame
	add	%sp, V9BIAS64, %sp		! back to a 32-bit frame
	rdpr	%pstate, %l1			! Get the present pstate value
	wrpr	%l1, PSTATE_AM, %pstate		! Set PSTATE_AM = 1
	ret					! Return result ...
	restore %o0, %g0, %o0			! delay; result in %o0

	SET_SIZE(client_handler)

#endif	/* lint */
