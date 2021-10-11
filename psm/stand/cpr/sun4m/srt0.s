/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)srt0.s 1.4     94/11/20 SMI"

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/asm_linkage.h>
#include <v7/sys/privregs.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/enable.h>
#include <sys/cpu.h>
#include <sys/trap.h>
#include <sys/eeprom.h>
#include <sys/debug/debug.h>

/*
 * code based on kadb locore.s
 */

/*
 * The debug stack. This must be the first thing in the data
 * segment (other than an sccs string) so that we don't stomp
 * on anything important. We get a red zone below this stack
 * for free when the text is write protected.
 */
#define	STACK_SIZE	0x20000
	.seg	".data"
	.global estack
	.skip	STACK_SIZE
estack:					! end (top) of debugger stack
	.skip	33*4			! %f0 - %f31 plus %fsr

first:
	.word	1			! flags fist entry into kadb

/*
 * Current cpuid info
 */
	.global	cur_cpuid
cur_cpuid:
	.word	0

/*
 * The number of windows, set by fiximp when machine is booted.
 */
	.global nwindows
nwindows:
	.word	8

/*
 * The parent callback routine.
 */
	.global release_parent
	release_parent:
	.word   0
			   
	.seg    ".bss"
	.align  16
	.globl  bss_start
bss_start:
						    
	.seg	".text"
	.align	16

#ifdef NOTNOW
/*
 * Trap vector macros.
 */
#define TRAP(H) \
	b (H); mov %psr,%l0; nop; nop;

/*
 * The constant in the last expression must be (nwindows-1).
 * XXX - See fiximp() below.
 */
#define WIN_TRAP(H) \
	mov %psr,%l0;  mov %wim,%l3;  b (H);  mov 7,%l6;

#define SYS_TRAP(T) \
	mov %psr,%l0;  sethi %hi(T),%l3;  b sys_trap;  or %l3,%lo(T),%l3;

#define BAD_TRAP	SYS_TRAP(fault);

/*
 * Trap vector table.
 * This must be the first text in the boot image.
 *
 * When a trap is taken, we vector to DEBUGSTART+(TT*16) and we have
 * the following state:
 *	2) traps are disabled
 *	3) the previous state of PSR_S is in PSR_PS
 *	4) the CWP has been decremented into the trap window
 *	5) the previous pc and npc is in %l1 and %l2 respectively.
 *
 * Registers:
 *	%l0 - %psr immediately after trap
 *	%l1 - trapped pc
 *	%l2 - trapped npc
 *	%l3 - trap handler pointer (sys_trap only)
 *		or current %wim (win_trap only)
 *	%l6 - NW-1, for wim calculations (win_trap only)
 *
 * Note: DEBUGGER receives control at vector 0 (trap).
 */
	.seg	".text"
	.align	16

	.global our_die_routine
	.global _start, scb
_start:
scb:
	TRAP(.enter);				! 00
	BAD_TRAP;				! 01 text fault
	WIN_TRAP(unimpl_instr);			! 02 unimp instruction
	BAD_TRAP;				! 03 priv instruction
	TRAP(fp_disabled);			! 04 fp disabled
	WIN_TRAP(window_overflow);		! 05
	WIN_TRAP(window_underflow);		! 06
	BAD_TRAP;				! 07 alignment
	BAD_TRAP;				! 08 fp exception
	SYS_TRAP(fault);			! 09 data fault
	BAD_TRAP;				! 0A tag_overflow
	BAD_TRAP; BAD_TRAP;			! 0B - 0C
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 0D - 10
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 11 - 14 int 1-4
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 15 - 18 int 5-8
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 19 - 1C int 9-12
	BAD_TRAP; BAD_TRAP;			! 1D - 1E int 13-14
	SYS_TRAP(level15);			! 1F int 15
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 20 - 23
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 24 - 27
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 28 - 2B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 2C - 2F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 30 - 34
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 34 - 37
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 38 - 3B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 3C - 3F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 40 - 44
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 44 - 47
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 48 - 4B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 4C - 4F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 50 - 53
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 54 - 57
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 58 - 5B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 5C - 5F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 60 - 64
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 64 - 67
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 68 - 6B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 6C - 6F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 70 - 74
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 74 - 77
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 78 - 7B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 7C - 7F
	!
	! software traps
	!
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 80 - 83
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 84 - 87
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 88 - 8B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 8C - 8F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 90 - 93
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 94 - 97
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 98 - 9B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 9C - 9F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! A0 - A3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! A4 - A7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! A8 - AB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! AC - AF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! B0 - B3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! B4 - B7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! B8 - BB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! BC - BF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! C0 - C3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! C4 - C7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! C8 - CB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! CC - CF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! D0 - D3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! D4 - D7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! D8 - DB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! DC - DF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! E0 - E3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! E4 - E7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! E8 - EB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! EC - EF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! F0 - F3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! F4 - F7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! F8 - FB
	BAD_TRAP;				! FC 
	TRAP(trap);				! FD enter debugger
	TRAP(trap);				! FE breakpoint
	BAD_TRAP;				! PROM breakpoint trap

/*
 * Debugger vector table.
 * Must follow trap table.
 */
dvec:
	b,a	.enter			! dv_entry
	.word	trap			! dv_trap
	.word	pagesused		! dv_pages
	.word	scbsync			! dv_scbsync
	.word	0

#endif  NOTNOW

	.seg	".text"
	.align	16

	.global _start
_start:
.enter:
	sethi	%hi(first), %o5
	ld	[%o5 + %lo(first)], %o4	! read "first" flag
	tst	%o4			! been through here before?
	bne,a	init			! non-zero --> no
	st	%g0, [%o5 + %lo(first)] ! clear flag


	!
	! We have not been relocated yet.
	!
init:
	mov	%psr, %g1
	bclr	PSR_PIL, %g1		! PIL = 15
	bset	(15 << 8), %g1
	mov	%g1, %psr
	nop; nop; nop

	!
	! Save romp and bootops.
	!
	call	early_startup
	nop
	
	!call main

	call main
	nop

        mov     %psr, %g1
	bclr    PSR_PIL, %g1            ! PIL = 14
	bset    (14 << 8), %g1
	mov     %g1, %psr
	nop;nop;nop
	!
	! In the unlikely event we get here, return to the monitor.
	!
										call    prom_exit_to_mon
											clr     %o0

