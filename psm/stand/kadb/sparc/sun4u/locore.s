/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)locore.s	1.5	95/07/12 SMI"

/*
 * Assembly language support for Fusion kadb.
 *
 * Fusion differs from other SPARC machines in that the MMU trap handling
 * code is done in software. For this and other reasons, it is simpler to
 * use the PROM's trap table while running in the debugger than to have
 * kadb manage its own trap table. Breakpoint/L1-A traps work by downloading
 * a FORTH 'defer word' to the PROM, which gives it an address to jump to
 * when it gets a trap it doesn't recognize, e.g. a kadb-induced breakpoint
 * (N.B. PROM breakpoints are independent of this scheme and work as before).
 * The PROM saves some of the register state at the time of the breakpoint
 * trap and jumps to 'start' in kadb, where we save off everything else and
 * proceed as usual.
 */

#if defined(lint)
#include <sys/types.h>
#include <sys/t_lock.h>
#endif	/* lint */

#include <sys/param.h>
#include <sys/vmparam.h>
#include <sys/errno.h>
#include <sys/asm_linkage.h>
#include <sys/clock.h>
#include <sys/cpu.h>
#include <sys/intreg.h>
#include <sys/eeprom.h>
#include <sys/debug/debug.h>
#include <sys/mmu.h>
#include <sys/pcb.h>
#include <sys/pte.h>
#include <sys/spitasi.h>
#include <sys/privregs.h>
#include <sys/trap.h>
#include <sys/scb.h>
#include <sys/machparam.h>
#include <sys/machtrap.h>
#include <sys/machthread.h>
#include <sys/spitregs.h>


/*
 * Macros for saving/restoring registers.
 */
#ifdef	RESTORE_GLOBALS
#undef	RESTORE_GLOBALS
#endif
#define	RESTORE_GLOBALS(RP) \
	ld	[RP + R_Y], %g1; \
	mov	%g1, %y; \
	ldx	[RP + R_G1], %g1; \
	ldx	[RP + R_G2], %g2; \
	ldx	[RP + R_G3], %g3; \
	ldx	[RP + R_G4], %g4; \
	ldx	[RP + R_G5], %g5; \
	ldx	[RP + R_G6], %g6; \
	ldx	[RP + R_G7], %g7;

#ifdef	SAVE_WINDOW
#undef	SAVE_WINDOW
#endif
#define	SAVE_WINDOW(SBP) \
	st	%l0, [SBP + 0]; \
	st	%l1, [SBP + 4]; \
	st	%l2, [SBP + 8]; \
	st	%l3, [SBP + 12]; \
	st	%l4, [SBP + 16]; \
	st	%l5, [SBP + 20]; \
	st	%l6, [SBP + 24]; \
	st	%l7, [SBP + 28]; \
	st	%i0, [SBP + 32]; \
	st	%i1, [SBP + 36]; \
	st	%i2, [SBP + 40]; \
	st	%i3, [SBP + 44]; \
	st	%i4, [SBP + 48]; \
	st	%i5, [SBP + 52]; \
	st	%i6, [SBP + 56]; \
	st	%i7, [SBP + 60];

#ifdef	RESTORE_WINDOW
#undef	RESTORE_WINDOW
#endif
#define	RESTORE_WINDOW(SBP) \
	ld	[SBP + 0], %l0; \
	ld	[SBP + 4], %l1; \
	ld	[SBP + 8], %l2; \
	ld	[SBP + 12], %l3; \
	ld	[SBP + 16], %l4; \
	ld	[SBP + 20], %l5; \
	ld	[SBP + 24], %l6; \
	ld	[SBP + 28], %l7; \
	ld	[SBP + 32], %i0; \
	ld	[SBP + 36], %i1; \
	ld	[SBP + 40], %i2; \
	ld	[SBP + 44], %i3; \
	ld	[SBP + 48], %i4; \
	ld	[SBP + 52], %i5; \
	ld	[SBP + 56], %i6; \
	ld	[SBP + 60], %i7;

/* save 64-bit state of current window's out regs */
#ifdef SAVE_OUTS
#undef SAVE_OUTS
#endif
#define	SAVE_OUTS(SBP) \
	stx	%o0, [SBP + 0]; \
	stx	%o1, [SBP + 8]; \
	stx	%o2, [SBP + 16]; \
	stx	%o3, [SBP + 24]; \
	stx	%o4, [SBP + 32]; \
	stx	%o5, [SBP + 40]; \
	stx	%o6, [SBP + 48]; \
	stx	%o7, [SBP + 56];

#ifdef RESTORE_OUTS
#undef RESTORE_OUTS
#endif
#define	RESTORE_OUTS(SBP) \
	ldx	[SBP + 0], %o0; \
	ldx	[SBP + 8], %o1; \
	ldx	[SBP + 16], %o2; \
	ldx	[SBP + 24], %o3; \
	ldx	[SBP + 32], %o4; \
	ldx	[SBP + 40], %o5; \
	ldx	[SBP + 48], %o6; \
	ldx	[SBP + 56], %o7;

/*
 * The debug stack. This must be the first thing in the data
 * segment (other than an sccs string) so that we don't stomp
 * on anything important. We get a red zone below this stack
 * for free when the text is write protected.
 */
#define	STACK_SIZE	0x8000
	.seg	".data"
	.align	16
	.global estack

	.skip	STACK_SIZE
estack:				! end (top) of debugger stack

	.align	16
	.global nwindows, nwin_minus_one
	.global saved_fprs	! is debuggee using the FPU?
	.global p1275_cif	! 1275 client interface handler

nwindows:
	.word   8
nwin_minus_one:
	.word   7
saved_fprs:
	.word	0
p1275_cif:
	.word	0
tmpreg:
	.word	0

	.seg	".bss"
	.align	16
	.global	bss_start
bss_start:

	.seg	".text"
#include "assym.s"
!
! REGOFF must be aligned to allow double word access to r_tstate.
!
#if (REGOFF & 7) != 0
ERROR - struct regs not aligned
#endif

/*
 * The debugger vector table.
 */
	.global	dvec
dvec:
	b,a	.enterkadb	! dv_enter
	.word	trap		! dv_trap
	.word	pagesused	! dv_pages
	.word	scbsync		! dv_scbsync
	.word	0

/*
 * Debugger entry point.
 *
 * Inputs:
 *	%o0 - p1275cif pointer
 *	%o1 - dvec (null)
 *	%o2 - bootops vector
 *	%o3 - ELF boot vector
 *
 * Our tasks are:
 *
 *	save parameters
 *	initialize CPU state registers
 *	clear kadblock
 *	dive into main()
 */
	.align	16
	.global	start		! start address of kadb text (sorta)

#ifdef	lint
start() {}
#else

	ENTRY(start)

	! Save the 1275 cif handler and ELF boot vector for later use.

	set	p1275_cif, %g1
	st	%o0, [%g1]
	set	elfbootvec, %g1
	st	%o3, [%g1]

	!
	! Initialize CPU state register
	!
	wrpr	%g0, PSTATE_PEF+PSTATE_AM+PSTATE_PRIV+PSTATE_IE, %pstate

	rdpr	%ver, %g1
	and	%g1, VER_MAXWIN, %g1
	set	nwin_minus_one, %g2
	st	%g1, [%g2]
	inc	%g1
	set	nwindows, %g2
	st	%g1, [%g2]

	! early_startup(p1275cif, dvec, bootops, elfbootvec)

	call	early_startup		! non-machdep startup code
	nop
	call	startup			! do the rest of the startup work.
	nop
	call	main			! enter the debugger.
	nop

	t	ST_KADB_TRAP

	! In the unlikely event we get here, return to the monitor.

	call	prom_exit_to_mon
	clr	%o0

	SET_SIZE(start)
#endif	/* lint */

/*
 * Enter the debugger. This goes out to the kernel (who owns the
 * trap table) and down into the PROM (because we patched the
 * kernel's ST_KADB_TRAP vector with the PROM's).
 */
#ifdef lint
enterkadb() {}
#else

	ENTRY(.enterkadb)
	t	ST_KADB_TRAP
	nop
	retl
	nop
	SET_SIZE(.enterkadb)

#endif	/* lint */


/*
 * Pass control to the client program.
 */
#ifdef	lint
int
exitto(int *addr) {}
#else

	ENTRY(_exitto)
	save	%sp, -SA(MINFRAME), %sp
	set	bootops, %o0		! pass bootops to callee
	ld	[%o0], %o2
	set	dvec, %o1		! pass dvec address to callee
	set	elfbootvec, %o3		! pass elf bootstrap vector
	ld	[%o3], %o3
	set	p1275_cif, %o0		! pass CIF to callee
	ld	[%o0], %o0
	jmpl	%i0, %o7		! register-indirect call
	mov	%o0, %o4		! 1210378: Pass cif in %o0 and %o4
	ret
	restore
	SET_SIZE(_exitto)

#endif	/* lint */


/*
 * Kadb's breakpoint/L1-A trap handler. The mechanism differs from that
 * used on other architectures, since the PROM owns the trap table.
 * The kernel's trap handler is patched with the PROM's 0x17d and 0x17e
 * trap handlers at kernel startup time. Breakpoints and L1-A traps therefore
 * go into the PROM, which bounces to here (because of the defer word we
 * downloaded at kadb startup time). On entry, the %tba is owned by the
 * kernel, since the PROM restored everything to what it was at the time
 * of the trap. We are called at TL=0, and must only use the normal globals
 * until everything is saved (we can use the globals because they were
 * saved in the defer word).
 *
 *	- acquire the lock
 *	- use the PROM's %tba
 *	- save CPU state, register windows
 *	- idle the other CPUs
 *	- call cmd()
 *	- resume the other CPUs
 *	- restore CPU state, register windows
 *	- release lock
 *	- fake return from trap
 */
#ifdef lint
int
trap(void)
#else

	ENTRY(trap)

	! disable interrupts
	wrpr	%g0, PSTATE_PEF+PSTATE_AM+PSTATE_PRIV, %pstate

	! Try to acquire the mutex.

	set	kadblock, %g1
	ldstub	[%g1], %g2
1:
	tst	%g2			! Is the lock held?
	bnz	2f			! Yes - spin until it's released
	nop
	b,a	4f			! No - onward...
2:
	ldub	[%g1], %g2
3:
	tst	%g2			! Is the lock held?
	bz,a	1b			! lock appears to be free, try again
	ldstub	[%g1], %g2		! delay slot - try to set lock
	ba	3b			! otherwise, spin
	ldub	[%g1], %g2		! delay - reload
4:
	CPU_INDEX(%g1)			! get CPU id
	set	cur_cpuid, %g2
	st	%g1, [%g2]

	! If nofault is set, we've taken a trap within the debugger itself
	! (e.g. peeking at an invalid address). We don't save things in that
	! case, since we would overwrite the save area, which has valid
	! kernel state in it.

	set	nofault, %g1
	ld	[%g1], %g2
	tst	%g2
	bz,a	1f
	nop

	set	kadblock, %g1
	clrb	[%g1]

	! Reinstall the PROM's callback mechanism.

	call	reload_prom_callback
	nop
	
	set	saved_tt, %g3
	ld	[%g3], %g3
	mov	%g3, %o0
	set	saved_pc, %g3
	ld	[%g3], %g3
	mov	%g3, %o1
	set	saved_npc, %g3
	ld	[%g3], %g3
	mov	%g3, %o2

	call	fault			! fault(tt, pc, npc)
	nop
	/* NOTREACHED */		! we longjmp() out of fault()
1:
	! need to switch to the PROM's %tba before we call any C code.

	set	regsave, %g2
	rdpr	%tba, %g3
	st	%g3, [%g2 + R_TBA]
	set	mon_tba, %g3
	ld	[%g3], %g3
	wrpr	%g0, %g3, %tba

	! Save the whole cpu state (all regs, windows) to the
	! register save area. Various privregs were saved in
	! the defer word, so copy them from the saved locations.

	set	saved_pc, %g3
	ld	[%g3], %g3
	st	%g3, [%g2 + R_PC]
	set	saved_npc, %g3
	ld	[%g3], %g3
	st	%g3, [%g2 + R_NPC]
	set	saved_g1, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G1]
	set	saved_g2, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G2]
	set	saved_g3, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G3]
	set	saved_g4, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G4]
	set	saved_g5, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G5]
	set	saved_g6, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G6]
	set	saved_g7, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_G7]

	set	saved_tt, %g3
	ld	[%g3], %g3
	st	%g3, [%g2 + R_TT]
	set	saved_pil, %g3
	ld	[%g3], %g3
	st	%g3, [%g2 + R_PIL]
	rdpr	%cwp, %g3
	st	%g3, [%g2 + R_CWP]
	rdpr	%otherwin, %g3
	st	%g3, [%g2 + R_OTHERWIN]
	rdpr	%cansave, %g3
	st	%g3, [%g2 + R_CANSAVE]
	rdpr	%canrestore, %g3
	st	%g3, [%g2 + R_CANRESTORE]
	rdpr	%cleanwin, %g3
	st	%g3, [%g2 + R_CLEANWIN]
	rdpr	%wstate, %g3
	st	%g3, [%g2 + R_WSTATE]
	set	saved_tstate, %g3
	ldx	[%g3], %g3
	stx	%g3, [%g2 + R_TSTATE]

	! Save the 64-bit state of the current window's out regs,
	! since v8+ programs can make use of them.

	add	%g2, R_OUTS, %g7
	SAVE_OUTS(%g7)

	! Now save all the register windows.

	add	%g2, R_WINDOW, %g7

	! go to window 0 and blast everything to the save area.

	wrpr	%g0, 0, %cwp
	wrpr	%g0, 0, %otherwin
	set	nwin_minus_one, %g5
	ld	[%g5], %g5
	wrpr	%g5, %cleanwin
	sub	%g5, 1, %g5		! cansave+canrestore == nwindows-2
	wrpr	%g5, %cansave
	wrpr	%g0, %canrestore
2:
	SAVE_WINDOW(%g7)
	add	%g7, WINDOWSIZE, %g7
	subcc	%g5, 1, %g5
	bnz	2b
	save

	! We have now exhausted all of cansave's windows and the next
	! save will cause a spill trap. Since there may not be a valid
	! %sp/%o6 in the next window, we reset the winregs by hand.

	SAVE_WINDOW(%g7)
	add	%g7, WINDOWSIZE, %g7
	rdpr	%canrestore, %g4
	sub	%g4, 1, %g4
	wrpr	%g0, %g4, %canrestore	! %canrestore--
	wrpr	%g0, 1, %cansave	! %cansave++
	save
	SAVE_WINDOW(%g7)

	! Now flush the active (i.e. restoreable) window contents to memory.
	! Add %otherwin to %cansave, since those windows have no meaning in
	! this context, and %otherwin+%cansave+%canrestore MUST equal nwin-2.

	set	regsave, %g2
	ld	[%g2 + R_CWP], %g3	! back into saved cwp
	wrpr	%g0, %g3, %cwp
	ld	[%g2 + R_OTHERWIN], %g3
	mov	%g3, %g4
	ld	[%g2 + R_CANSAVE], %g3
	add	%g3, %g4, %g3
	wrpr	%g3, %cansave
	ld	[%g2 + R_CANRESTORE], %g3
	wrpr	%g3, %canrestore
	mov	%sp, %g4
	mov	%fp, %g5
	flushw				! flush all active windows to memory
	mov	%g4, %sp
	mov	%g5, %fp

	! Initialize window registers for the debugger.

	wrpr	%g0, 0, %otherwin
	set	nwin_minus_one, %g5
	ld	[%g5], %g5
	wrpr	%g5, %cleanwin
	sub	%g5, 1, %g5
	wrpr	%g5, %cansave
	wrpr	%g0, %canrestore

	rd	%fprs, %g4
	set	saved_fprs, %g6
	st	%g4, [%g6]		! save fprs
	andcc	%g4, FPRS_FEF, %g4	! fp enabled?
	bz,pt	%icc, 1f		! nope, drive on...
	wr	%g0, FPRS_FEF, %fprs	! enable it

	! Save floating point registers and status. On Fusion, FP traps are
	! precise, so no need to worry about outstanding FP operations.

	set	fpuregs, %g7
	STORE_FPREGS(%g7)
	stx	%fsr, [%g7 + FPU_FSR]
	rd	%fprs, %g4
	stx	%g4, [%g7 + FPU_FPRS]
1:
	call	idle_other_cpus
	nop

	! Clear the lock, now that we know nobody can take it from us.
	! We need to do this before doing anything that could take a
	! trap while we're still holding the lock.

	sethi	%hi(kadblock), %g1
	clrb	[%g1 + %lo(kadblock)]

	call	reload_prom_callback	! reload callback mechanism
	nop
	call	cmd			! enter main cmd loop
	nop
	call	resume_other_cpus	! restart everybody
	nop
	call	reload_prom_callback	! just in case...
	nop

	! Restore the register state

	set	saved_fprs, %g6		! restore %fprs
	ld	[%g6], %g4
	wr	%g4, %fprs
	andcc	%g4, FPRS_FEF, %g0	! fp enabled?
	bz,pt	%icc, 1f		! nope, drive on...
	nop
	set	fpuregs, %g7
	LOAD_FPREGS(%g7)
	ldx	[%g7 + FPU_FSR], %fsr
	ldx	[%g7 + FPU_FPRS], %g4
	wr	%g4, %g0, %fprs
1:
	set	regsave, %g7
	add	%g7, R_WINDOW, %g7

	! Go to window 0 and restore everything from the save area.

	wrpr	%g0, 0, %cwp
	wrpr	%g0, 0, %otherwin
	set	nwin_minus_one, %g5
	ld	[%g5], %g5
	wrpr	%g5, %cleanwin
	sub	%g5, 1, %g5		! cansave+canrestore == nwindows-2
	wrpr	%g5, %cansave
	wrpr	%g0, 0, %canrestore

2:
	RESTORE_WINDOW(%g7)
	add	%g7, WINDOWSIZE, %g7
	subcc	%g5, 1, %g5
	bnz	2b
	save

	! same trick as above
	RESTORE_WINDOW(%g7)
	add	%g7, WINDOWSIZE, %g7
	rdpr	%canrestore, %g4
	sub	%g4, 1, %g4
	wrpr	%g0, %g4, %canrestore	! %canrestore--
	wrpr	%g0, 1, %cansave	! %cansave++
	save
	RESTORE_WINDOW(%g7)

	! Reset window registers to values on trap entry.

	set	regsave, %g2
	ld	[%g2 + R_CWP], %g3
	wrpr	%g3, %cwp
	ld	[%g2 + R_OTHERWIN], %g3
	wrpr	%g3, %otherwin
	ld	[%g2 + R_CANSAVE], %g3
	wrpr	%g3, %cansave
	ld	[%g2 + R_CANRESTORE], %g3
	wrpr	%g3, %canrestore
	ld	[%g2 + R_CLEANWIN], %g3
	wrpr	%g3, %cleanwin
	ld	[%g2 + R_WSTATE], %g3
	wrpr	%g3, %wstate

	! Restore the 64-bit state of the current window's out regs,
	! since v8+ programs can make use of them.

	add	%g2, R_OUTS, %g7
	RESTORE_OUTS(%g7)

	! Figure out which type of trap we're returning from (i.e. either
	! some type of breakpoint or an L1-A/break from the user) and fake
	! a return from TL=1.

	wrpr	%g0, 1, %tl		! TL = 1
	ldx	[%g2 + R_TSTATE], %g3
	wrpr	%g0, %g3, %tstate
	ld	[%g2 + R_PC], %g3	! setup return pc, npc
	wrpr	%g0, %g3, %tpc
	ld	[%g2 + R_NPC], %g3
	wrpr	%g0, %g3, %tnpc
	ld	[%g2 + R_TBA], %g3	! restore kernel's trap table
	wrpr	%g0, %g3, %tba
	ld	[%g2 + R_PIL], %g3
	wrpr	%g0, %g3, %pil

	ld	[%g2 + R_TT], %g3
	wrpr	%g0, %g3, %tt
	wrpr	%g0, 0, %tl		! XXXXXX TL = 0 - needed?
	set	(ST_KADB_TRAP | T_SOFTWARE_TRAP), %g1

	cmp	%g1, %g3
	beq	debugtrap
	nop
	set	tmpreg, %g4
	st	%l3, [%g4]		! use %l3 to restore globals
	set	regsave, %l3
	RESTORE_GLOBALS(%l3)
	set	tmpreg, %l3
	ld	[%l3], %l3
	wrpr	%g0, 1, %tl		! go to TL=1 to simulate trap ret
	retry				! redo instruction
	/* NOTREACHED */
debugtrap:
	set	tmpreg, %g4
	st	%l3, [%g4]
	set	regsave, %l3
	RESTORE_GLOBALS(%l3)
	set	tmpreg, %l3
	ld	[%l3], %l3
	wrpr	%g0, 1, %tl		! go to TL=1 to simulate trap ret
	done				! skip over trap
	/* NOTREACHED */
	SET_SIZE(trap)

#endif	/* lint */

/*
 * Perform a trap on the behalf of our (C) caller.
 */
#ifdef	lint
asm_trap(int x) {}
#else

	ENTRY(asm_trap)
	retl
	t	%o0
	SET_SIZE(asm_trap)

#endif	/* lint */

/*
 * Flush all active (i.e. restoreable) windows to memory.
 */
#if defined(lint)
void
flush_windows(void) {}
#else	/* lint */

	ENTRY_NP(flush_windows)
	retl
	flushw
	SET_SIZE(flush_windows)

#endif	/* lint */

/*
 * The interface for a 32-bit client program calling the 64-bit 1275 OBP.
 */
#if defined(lint)
/* ARGSUSED */
int
client_handler(void *cif_handler, void *arg_array) {return (0);}
#else	/* lint */

	ENTRY(client_handler)
	save	%sp, -SA64(MINFRAME64), %sp	! 32 bit frame, 64 bit sized
	srl	%i0, 0, %i0			! zero extend handler addr.
	srl	%i1, 0, %o0			! zero extend first argument.
	set	1f, %o1				! zero extend pc
	jmp	%o1
	srl	%sp, 0, %sp			! zero extend sp
1:
	rdpr	%pstate, %l4			! Get the present pstate value
	wrpr	%l4, PSTATE_AM, %pstate		! Set PSTATE_AM = 0
	jmpl	%i0, %o7			! Call cif handler
	sub	%sp, V9BIAS64, %sp		! delay; Now a 64 bit frame
	add	%sp, V9BIAS64, %sp		! back to a 32-bit frame
	rdpr	%pstate, %l4			! Get the present pstate value
	wrpr	%l4, PSTATE_AM, %pstate		! Set PSTATE_AM = 1
	ret					! Return result ...
	restore	%o0, %g0, %o0			! delay; result in %o0
	SET_SIZE(client_handler)

#endif	/* lint */

/*
 * Misc subroutines.
 */

#ifdef	lint
char *
gettba() {return ((char *)0);}
#else

	ENTRY(gettba)
	rdpr	%tba, %o0
	srl	%o0, 14, %o0
	retl
	sll	%o0, 14, %o0
	SET_SIZE(gettba)

#endif	/* lint */

#ifdef	lint
void
settba(struct scb *t) {}
#else

	ENTRY(settba)
	srl	%o0, 14, %o0
	sll	%o0, 14, %o0
	retl
	wrpr	%o0, %tba
	SET_SIZE(settba)

#endif	/* lint */

#ifdef	lint
char *
getsp(void) {return ((char *)0);}
#else

	ENTRY(getsp)
	retl
	mov	%sp, %o0
	SET_SIZE(getsp)

#endif	/* lint */

/*
 * Flush page containing <addr> from the spitfire I-cache.
 */
#ifdef	lint
void
sf_iflush(int *addr) {}
#else

	ENTRY(sf_iflush)
	set	MMU_PAGESIZE, %o1
	srlx	%o0, MMU_PAGESHIFT, %o0		! Go to begining of page
	sllx	%o0, MMU_PAGESHIFT, %o0
	membar	#StoreStore			! Ensure stores are globally visible
1:
	flush	%o0
	sub	%o1, ICACHE_FLUSHSZ, %o1	! bytes = bytes-8 
	brnz,a,pt %o1, 1b
	add	%o0, ICACHE_FLUSHSZ, %o0	! vaddr = vaddr+8
	retl
	nop
	SET_SIZE(sf_iflush)

#endif
