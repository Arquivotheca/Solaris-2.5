/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)trap_table.s	1.74	95/10/05 SMI"

#if !defined(lint)
#include "assym.s"
#endif !lint
#include <sys/asm_linkage.h>
#include <sys/privregs.h>
#include <sys/spitasi.h>
#include <sys/machtrap.h>
#include <sys/machthread.h>
#include <sys/pcb.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/async.h>
#include <sys/intreg.h>
#include <sys/scb.h>
#include <sys/psr_compat.h>
#include <sys/syscall.h>
#include <sys/machparam.h>
#include <sys/traptrace.h>
#include <vm/hat_sfmmu.h>

/*
 * SPARC V9 Trap Table
 *
 * Most of the trap handlers are made from common building
 * blocks, and some are instantiated multiple times within
 * the trap table. So, I build a bunch of macros, then
 * populate the table using only the macros.
 *
 * Many macros branch to sys_trap.  Its calling convention is:
 *	%g1		kernel trap handler
 *	%g2, %g3	args for above
 *	%g4		desire %pil
 */


#ifdef	TRAPTRACE

/*
 * Tracing macro. Adds two instructions if TRAPTRACE is defined.
 */
#define	TT_TRACE(label)							\
	ba	label							;\
	rd	%pc, %g7

#define	TT_TRACE_INS	0

#else

#define	TT_TRACE(label)
#define	TT_TRACE_INS	0

#endif

/*
 * This first set are funneled to trap() with %tt as the type.
 * Trap will then either panic or send the user a signal.
 */
/*
 * NOT is used for traps that just shouldn't happen.
 * It comes in both single and quadruple flavors.
 */
#if !defined(lint)
	.global	trap
#endif !lint
#define	NOT			\
	TT_TRACE(trace_gen)	;\
	set	trap, %g1	;\
	rdpr	%tt, %g2	;\
	ba,pt	%xcc, sys_trap	;\
	sub	%g0, 1, %g4	;\
	.align	32
#define	NOT4	NOT; NOT; NOT; NOT
/*
 * RED is for traps that use the red mode handler.
 * We should neve see these either.
 */
#define	RED	NOT
/*
 * BAD is used for trap vectors we don't have a kernel
 * handler for.  
 * It also comes in single and quadruple versions.
 */
#define	BAD	NOT
#define	BAD4	NOT4


/*
 * TRAP vectors to the trap() function.
 * It's main use is for user errors.
 */
#if !defined(lint)
	.global	trap
#endif !lint
#define	TRAP(arg)		\
	TT_TRACE(trace_gen)	;\
	set	trap, %g1	;\
	mov	arg, %g2	;\
	ba,pt	%xcc, sys_trap	;\
	sub	%g0, 1, %g4	;\
	.align	32

/*
 * SYSCALL is used for system calls.
 */
#define	SYSCALL()			\
	TT_TRACE(trace_gen)		;\
	set	syscall_trap, %g1	;\
	ba,pt	%xcc, sys_trap		;\
	sub	%g0, 1, %g4		;\
	.align	32

#define	FLUSHW()			\
	set	trap, %g1		;\
	mov	T_WIN_OVERFLOW, %g2	;\
	sub	%g0, 1, %g4		;\
	save				;\
	flushw				;\
	restore				;\
	done				;\
	.align	32

/*
 * GOTO just jumps to a label.
 * It's used for things that can be fixed without going thru sys_trap.
 *
 * XXX "nop" is a workaround for mpsas bug (1170530); remove it later
 *
 */
#define	GOTO(label)		\
	.global	label		;\
	ba,a,pt	%xcc, label	;\
	nop			;\
	.empty			;\
	.align	32

/*
 * Privileged traps
 * Takes breakpoint if privileged, calls trap() if not.
 */
#define	PRIV(label)			\
	rdpr	%tstate, %g1		;\
	btst	TSTATE_PRIV, %g1	;\
	bnz	label			;\
	rdpr	%tt, %g2		;\
	set	trap, %g1		;\
	ba,pt	%xcc, sys_trap		;\
	sub	%g0, 1, %g4		;\
	.align	32

/*
 * TRIGGER is for use with external LAs (not done yet).
 */
#define	TRIGGER	NOT

/*
 * Trace traps (not done yet).
 */
/* Was	#define	TRACE(func)	BAD*/
#ifdef TRACE
#define TRACE_TRAP(H)		BAD
#else	/* TRACE */
#define	TRACE_TRAP(H)	\
	BAD;
#endif	/* TRACE */

/*
 * REGISTER WINDOW MANAGEMENT MACROS
 */

/*
 * various convenient units of padding
 */
#define SKIP(n)	.skip 4*(n)

/*
 * CLEAN_WINDOW is the simple handler for cleaning a register window.
 */
#define	CLEAN_WINDOW						\
	CLEAN_WINDOW_TRACE					;\
	rdpr %cleanwin, %l0; inc %l0; wrpr %l0, %cleanwin	;\
	clr %l0; clr %l1; clr %l2; clr %l3			;\
	clr %l4; clr %l5; clr %l6; clr %l7			;\
	clr %o0; clr %o1; clr %o2; clr %o3			;\
	clr %o4; clr %o5; clr %o6; clr %o7			;\
	retry; .align 128

/*
 * If we get an unresolved tlb miss while in a window handler, the
 * fault handler will resume execution at the last instruction of the
 * window hander, instead of delivering the fault to the kernel.
 * Spill handlers use this to spill windows into the wbuf.
 *
 * Note that, while we don't officially support anything but
 * the 32-bit windows, I've included the 64-bit window
 * spill and fill routines, and support for mixed stacks,
 * so we can at least make sure this mostly works.  The mixed
 * handler works by checking %sp, and branching to the
 * correct handler.  This is done by branching back to
 * label 1: for 32b frames, or label 2: for 64b frames;
 * which implies the handler order is: 32b, 64b, mixed.
 */

/*
 * SPILL_32bit spills a 32-bit-wide register window
 * into a 32-bit wide address space via the designated asi.
 * Requires that the stack is eight-byte aligned.
 */
#if !defined(lint)
#define	SPILL_32bit(asi_num, tail)				\
	mov	asi_num, %asi					;\
1:	srl	%sp, 0, %sp					;\
	sta	%l0, [%sp + 0]%asi				;\
	sta	%l1, [%sp + 4]%asi				;\
	sta	%l2, [%sp + 8]%asi				;\
	sta	%l3, [%sp + 12]%asi				;\
	sta	%l4, [%sp + 16]%asi				;\
	sta	%l5, [%sp + 20]%asi				;\
	sta	%l6, [%sp + 24]%asi				;\
	sta	%l7, [%sp + 28]%asi				;\
	sta	%i0, [%sp + 32]%asi				;\
	sta	%i1, [%sp + 36]%asi				;\
	sta	%i2, [%sp + 40]%asi				;\
	sta	%i3, [%sp + 44]%asi				;\
	sta	%i4, [%sp + 48]%asi				;\
	sta	%i5, [%sp + 52]%asi				;\
	sta	%i6, [%sp + 56]%asi				;\
	sta	%i7, [%sp + 60]%asi				;\
	saved							;\
	retry							;\
	SKIP(31-20-TT_TRACE_INS)				;\
	ba,a,pt	%xcc, fault_32bit_/**/tail			;\
	.empty


/*
 * FILL_32bit fills a 32-bit-wide register window
 * into a 32-bit wide address space via the designated asi.
 * Requires that the stack is eight-byte aligned.
 */
#define	FILL_32bit(asi_num, tail)				\
	mov	asi_num, %asi					;\
1:								;\
	srl	%sp, 0, %sp					;\
	lda	[%sp + 0]%asi, %l0				;\
	lda	[%sp + 4]%asi, %l1				;\
	lda	[%sp + 8]%asi, %l2				;\
	lda	[%sp + 12]%asi, %l3				;\
	lda	[%sp + 16]%asi, %l4				;\
	lda	[%sp + 20]%asi, %l5				;\
	lda	[%sp + 24]%asi, %l6				;\
	lda	[%sp + 28]%asi, %l7				;\
	lda	[%sp + 32]%asi, %i0				;\
	lda	[%sp + 36]%asi, %i1				;\
	lda	[%sp + 40]%asi, %i2				;\
	lda	[%sp + 44]%asi, %i3				;\
	lda	[%sp + 48]%asi, %i4				;\
	lda	[%sp + 52]%asi, %i5				;\
	lda	[%sp + 56]%asi, %i6				;\
	lda	[%sp + 60]%asi, %i7				;\
	restored						;\
	retry							;\
	SKIP(31-20-TT_TRACE_INS)				;\
	ba,a,pt	%xcc, fault_32bit_/**/tail			;\
	.empty


/*
 * SPILL_64bit spills a 64-bit-wide register window
 * into a 64-bit wide address space via the designated asi.
 * Requires that the stack is eight-byte aligned.
 */
#define	SPILL_64bit(asi_num, tail)				\
	mov	asi_num, %asi					;\
2:	stxa	%l0, [%sp + V9BIAS64 + 0]%asi			;\
	stxa	%l1, [%sp + V9BIAS64 + 8]%asi			;\
	stxa	%l2, [%sp + V9BIAS64 + 16]%asi			;\
	stxa	%l3, [%sp + V9BIAS64 + 24]%asi			;\
	stxa	%l4, [%sp + V9BIAS64 + 32]%asi			;\
	stxa	%l5, [%sp + V9BIAS64 + 40]%asi			;\
	stxa	%l6, [%sp + V9BIAS64 + 48]%asi			;\
	stxa	%l7, [%sp + V9BIAS64 + 56]%asi			;\
	stxa	%i0, [%sp + V9BIAS64 + 64]%asi			;\
	stxa	%i1, [%sp + V9BIAS64 + 72]%asi			;\
	stxa	%i2, [%sp + V9BIAS64 + 80]%asi			;\
	stxa	%i3, [%sp + V9BIAS64 + 88]%asi			;\
	stxa	%i4, [%sp + V9BIAS64 + 96]%asi			;\
	stxa	%i5, [%sp + V9BIAS64 + 104]%asi			;\
	stxa	%i6, [%sp + V9BIAS64 + 112]%asi			;\
	stxa	%i7, [%sp + V9BIAS64 + 120]%asi			;\
	saved							;\
	retry							;\
	SKIP(31-19-TT_TRACE_INS)				;\
	ba,a,pt	%xcc, fault_64bit_/**/tail			;\
	.empty


/*
 * FILL_64bit(ASI) fills a 64-bit-wide register window
 * into a 64-bit wide address space via the designated asi.
 * Requires that the stack is eight-byte aligned.
 */
#define	FILL_64bit(asi_num, tail)				\
	mov	asi_num, %asi					;\
2:								;\
	ldxa	[%sp + V9BIAS64 + 0]%asi, %l0			;\
	ldxa	[%sp + V9BIAS64 + 8]%asi, %l1			;\
	ldxa	[%sp + V9BIAS64 + 16]%asi, %l2			;\
	ldxa	[%sp + V9BIAS64 + 24]%asi, %l3			;\
	ldxa	[%sp + V9BIAS64 + 32]%asi, %l4			;\
	ldxa	[%sp + V9BIAS64 + 40]%asi, %l5			;\
	ldxa	[%sp + V9BIAS64 + 48]%asi, %l6			;\
	ldxa	[%sp + V9BIAS64 + 56]%asi, %l7			;\
	ldxa	[%sp + V9BIAS64 + 64]%asi, %i0			;\
	ldxa	[%sp + V9BIAS64 + 72]%asi, %i1			;\
	ldxa	[%sp + V9BIAS64 + 80]%asi, %i2			;\
	ldxa	[%sp + V9BIAS64 + 88]%asi, %i3			;\
	ldxa	[%sp + V9BIAS64 + 96]%asi, %i4			;\
	ldxa	[%sp + V9BIAS64 + 104]%asi, %i5			;\
	ldxa	[%sp + V9BIAS64 + 112]%asi, %i6			;\
	ldxa	[%sp + V9BIAS64 + 120]%asi, %i7			;\
	restored						;\
	retry							;\
	SKIP(31-19-TT_TRACE_INS)				;\
	ba,a,pt	%xcc, fault_64bit_/**/tail			;\
	.empty
#endif !lint

/*
 * SPILL_mixed spills either size window, depending on
 * whether %sp is even or odd, to a 32-bit address space.
 */
#define	SPILL_mixed(asi_num)					\
	btst	1, %sp						;\
	bnz	2b						;\
	mov	asi_num, %asi					;\
	ba,pt	%xcc, 1b					;\
	srl	%sp, 0, %sp					;\
	.align	128


/*
 * FILL_mixed(ASI) fills either size window, depending on
 * whether %sp is even or odd, from a 32-bit address space.
 */
#define	FILL_mixed(asi_num)					\
	btst	1, %sp						;\
	bnz	2b						;\
	mov	asi_num, %asi					;\
	b	1b						;\
	srl	%sp, 0, %sp					;\
	.align	128


/*
 * Floating point disabled.
 */
#define	FP_DISABLED_TRAP		\
	TT_TRACE(trace_gen)		;\
	set	fp_disabled, %g1	;\
	ba,pt	%xcc, sys_trap		;\
	sub	%g0, 1, %g4		;\
	.align	32

/*
 * Floating point exceptions.
 */
#define	FP_IEEE_TRAP			\
	TT_TRACE(trace_gen)		;\
	set	_fp_ieee_exception, %g1	;\
	ba,pt	%xcc, sys_trap		;\
	sub	%g0, 1, %g4		;\
	.align	32

#define	FP_TRAP				\
	TT_TRACE(trace_gen)		;\
	set	_fp_exception, %g1	;\
	ba,pt	%xcc, sys_trap		;\
	sub	%g0, 1, %g4		;\
	.align	32

/*
 * asynchronous traps at
 * level 0 and level 1
 */
#define	ASYNC_TRAP(dori)		\
	rdpr	%tl, %g5		;\
	sub	%g5, 1, %g5		;\
	mov	dori, %g6		;\
	ba,a,pt	%xcc, async_err		;\
	nop				;\
	.empty				;\
	.align	32

#define	ASYNC_ITRAP_TL1			\
	set	.asyncilevel1msg, %g2	;\
	ba,a,pt	%xcc, dis_err_panic1	;\
	.empty				;\
	.align	32

#define	ASYNC_DTRAP_TL1			\
	set	.asyncdlevel1msg, %g2	;\
	ba,a,pt	%xcc, dis_err_panic1	;\
	.empty				;\
	.align	32

#if !defined(lint)
.asyncilevel1msg:
	.seg	".data"
	.asciz	"Async instruction error at tl1";
	.align	4
.asyncdlevel1msg:
	.seg	".data"
	.asciz	"Async data error at tl1";
	.align	4
	.seg	".text"
#endif !lint

/*
 * correctable ECC error traps at
 * level 0 and level 1
 */
#define	CE_TRAP				\
	ba,a,pt	%xcc, ce_err		;\
	nop				;\
	.empty				;\
	.align	32

#ifndef	TRAPTRACE
#define	CE_TRAP_TL1			\
	ldxa	[%g0]ASI_AFSR, %g7	;\
	membar	#Sync			;\
	stxa	%g7, [%g0]ASI_AFSR	;\
	membar	#Sync			;\
	retry				;\
	.empty				;\
	.align	32
#else
#define	CE_TRAP_TL1			\
	set	.celevel1msg, %g2	;\
	ba,a,pt	%xcc, dis_err_panic1	;\
	.empty				;\
	.align	32

.celevel1msg:
	.seg	".data"
	.asciz	"Softerror with trap tracing at tl1";
	.align	4
	.seg	".text"
#endif

/*
 * LEVEL_INTERRUPT is for level N interrupts.
 * VECTOR_INTERRUPT is for the vector trap.
 */
#define	LEVEL_INTERRUPT(level)		\
	ba,pt	%xcc, pil_interrupt	;\
	mov	level, %g4		;\
	.align	32

#define	VECTOR_INTERRUPT				\
	ldxa	[%g0]ASI_INTR_RECEIVE_STATUS, %g1	;\
	btst	IRSR_BUSY, %g1				;\
	bnz,pt	%xcc, vec_interrupt			;\
	nop						;\
	ba,a,pt	%xcc, vec_intr_spurious			;\
	.empty						;\
	.align	32

/*
 * MMU Trap Handlers.
 */
#define	USE_ALTERNATE_GLOBALS						\
	rdpr	%pstate, %g5						;\
	wrpr	%g5, PSTATE_MG | PSTATE_AG, %pstate

#define	IMMU_EXCEPTION							\
	USE_ALTERNATE_GLOBALS						;\
	wr	%g0, ASI_IMMU, %asi					;\
	rdpr	%tpc, %g2						;\
	ldxa	[MMU_SFSR]%asi, %g3					;\
	sllx	%g2, 32, %g2						;\
	ba	.mmu_exception_end					;\
	  or	%g2, T_INSTR_EXCEPTION, %g2				;\
	.align	32

#define	DMMU_EXCEPTION							\
	USE_ALTERNATE_GLOBALS						;\
	wr	%g0, ASI_DMMU, %asi					;\
	ldxa	[MMU_TAG_ACCESS]%asi, %g2				;\
	ldxa	[MMU_SFSR]%asi, %g3					;\
	sllx	%g2, 32, %g2						;\
	ba	.mmu_exception_end					;\
	  or	%g2, T_DATA_EXCEPTION, %g2				;\
	.align	32

#define	DMMU_EXC_AG(traptype)						\
	wr	%g0, ASI_DMMU, %asi					;\
	ldxa	[MMU_SFAR]%asi, %g2					;\
	ldxa	[MMU_SFSR]%asi, %g3					;\
	sllx	%g2, 32, %g2						;\
	ba	.mmu_exception_end					;\
	  or	%g2, traptype, %g2					;\
	.align	32

/*
 * This define is meant to be used only in the debug kernel to gather
 * some statistics.  I couldn't embed it within the handlers because 
 * ifdef DEBUG wouldn't work.
 */
#ifdef DEBUG

#define	DEBUG_TSB_CHECK							\
	sethi	%hi(tsb_end), %g7					;\
	ld	[%g7 + %lo(tsb_end)], %g6				;\
	cmp	%g1, %g6						;\
	bg,a	sfmmu_bad_trap						;\
	  nop

#else /* !DEBUG */

#define	DEBUG_TSB_CHECK

#endif /* DEBUG */

/*
 * Load the primary context register and compare this to ctx in tag access
 * register to determine whether to flush using primary ctx or secondary ctx.
 * Requires a membar #Sync before next ld/st.
 * exits with:
 * g2 = tag access register
 * g3 = ctx number
 */
#define	DTLB_DEMAP_ENTRY						\
	mov	MMU_TAG_ACCESS, %g1					;\
	mov	MMU_PCONTEXT, %g5					;\
	ldxa	[%g1]ASI_DMMU, %g2					;\
	sethi	%hi(TAGACC_CTX_MASK), %g4				;\
	or	%g4, %lo(TAGACC_CTX_MASK), %g4				;\
	and	%g2, %g4, %g3			/* g3 = ctx */		;\
	ldxa	[%g5]ASI_DMMU, %g6		/* g6 = primary ctx */	;\
	andn	%g2, %g4, %g1			/* ctx = primary */	;\
	cmp	%g3, %g6						;\
	be,a,pt	%xcc, 1f						;\
	  stxa	%g0, [%g1]ASI_DTLB_DEMAP	/* MMU_DEMAP_PAGE */	;\
	or	%g1, DEMAP_SECOND, %g1					;\
	stxa	%g0, [%g1]ASI_DTLB_DEMAP	/* MMU_DEMAP_PAGE */	;\
1:

#if ((MAX_NCTXS << TSB_CTX_SHIFT) > TSB_ENTRIES)
ERROR - quick hashing in tlb miss handlers of ctx won't work with this tsb size
#endif

#define	DTLB_MISS_TL0							\
	ldxa	[%g0]ASI_DMMU_TSB_8K, %g1	/* g1 = tsbe ptr */	;\
	ldxa	[%g0]ASI_DMMU, %g2		/* MMU_TARGET */	;\
	srlx	%g2, TTARGET_CTX_SHIFT, %g3				;\
	sllx	%g3, TSB_ENTRY_SHIFT + TSB_CTX_SHIFT, %g5		;\
	xor	%g5, %g1, %g1						;\
	DEBUG_TSB_CHECK			/* 5 instr ifdef DEBUG */	;\
	ldda	[%g1]ASI_NQUAD_LD, %g4	/* g4 = tag, g5 = data */	;\
	cmp	%g2, %g4						;\
	bne,a,pn %xcc, 8f		/* br if 8k ptr miss */		;\
	  nop								;\
	stxa	%g5, [%g0]ASI_DTLB_IN					;\
	TT_TRACE(trace_tsbhit)		/* 2 instr ifdef TRAPTRACE */	;\
	retry								;\
8:									;\
	/*								;\
	 * We get here if we couldn't find the 8k mapping in the TSB	;\
	 * We don't look for 64K, 512K, or 4MB mappings in the TSB	;\
	 * because we currently don't support large pages in user	;\
	 * land and for the kernel I might as well go to the hash.	;\
	 * If we ever implement large pages for user adress space	;\
	 * (eg. shared memory) we need to reevaluate this.		;\
	 * g3 = ctx #							;\
	 */								;\
	TT_TRACE(trace_tsbmiss)		/* 2 instr ifdef TRAPTRACE */	;\
	cmp	%g3, %g0						;\
	bne,pt	%icc, sfmmu_utsb_miss					;\
	  nop								;\
	ba,a,pt	%xcc, sfmmu_ktsb_miss					;\
	  nop								;\
	.align 128
	
/* this needs to exactly 32 instructions */
#define	ITLB_MISS_TL0							 \
	ldxa	[%g0]ASI_IMMU_TSB_8K, %g1	/* g1 = tsbe ptr */	;\
	ldxa	[%g0]ASI_IMMU, %g2		/* MMU_TARGET */	;\
	srlx	%g2, TTARGET_CTX_SHIFT, %g3				;\
	sllx	%g3, TSB_ENTRY_SHIFT + TSB_CTX_SHIFT, %g5		;\
	xor	%g5, %g1, %g1						;\
	DEBUG_TSB_CHECK			/* 5 instr ifdef DEBUG */	;\
	ldda	[%g1]ASI_NQUAD_LD, %g4	/* g4 = tag, g5 = data */	;\
	sllx	%g5, TTE_NFO_SHIFT, %g6	/* check nfo bit */		;\
	cmp	%g2, %g4						;\
	bne,a,pn %xcc, 8f		/* br if 8k ptr miss */		;\
	  nop								;\
	brlz,a,pn %g6, .nfo_fault					;\
	  nop								;\
	stxa	%g5, [%g0]ASI_ITLB_IN		/* 8k ptr hit */	;\
	TT_TRACE(trace_tsbhit)		/* 2 instr ifdef TRAPTRACE */	;\
	retry								;\
8:									;\
	/*								;\
	 * We get here if we couldn't find the 8k mapping in the TSB	;\
	 * We don't look for 64K, 512K, or 4MB mappings in the TSB	;\
	 * because we currently don't support large pages in user	;\
	 * land and for the kernel I might as well go to the hash.	;\
	 * If we ever implement large pages for user adress space	;\
	 * (eg. shared memory) we need to reevaluate this.		;\
	 * g3 = ctx #							;\
	 */								;\
	TT_TRACE(trace_tsbmiss)		/* 2 instr ifdef TRAPTRACE */	;\
	cmp	%g3, %g0						;\
	bne,pt	%icc, sfmmu_utsb_miss					;\
	  nop								;\
	ba,a,pt	%xcc, sfmmu_ktsb_miss					;\
	  nop								;\
	.align 128

/*
 * This macro is the first level handler for fast protection faults.
 * It first demaps the tlb entry which generated the fault and then
 * attempts to set the modify bit on the hash.
 */
#define	DTLB_PROT_TL0							 \
	DTLB_DEMAP_ENTRY		/* 13 instructions */		;\
	/*								;\
	 * g2 = tag access register					;\
	 * g3 = ctx number						;\
	 */								;\
	TT_TRACE(trace_dataprot)	/* 2 instr ifdef TRAPTRACE */	;\
	brnz,pt %g3, sfmmu_uprot_trap					;\
	  membar #Sync							;\
	ba,a,pt	%xcc, sfmmu_kprot_trap					;\
	  nop								;\
	.align 128

/* this needs to exactly 32 instructions */
#define	DTLB_MISS_TL1							\
	ldxa	[%g0]ASI_DMMU_TSB_8K, %g1	/* g1 = tsbe ptr */	;\
	ldxa	[%g0]ASI_DMMU, %g2		/* MMU_TARGET */	;\
	srlx	%g2, TTARGET_CTX_SHIFT, %g3				;\
	sllx	%g3, TSB_ENTRY_SHIFT + TSB_CTX_SHIFT, %g5		;\
	xor	%g5, %g1, %g1						;\
	DEBUG_TSB_CHECK			/* 5 instr ifdef DEBUG */	;\
	ldda	[%g1]ASI_NQUAD_LD, %g4	/* g4 = tag, g5 = data */	;\
	cmp	%g2, %g4						;\
	bne,a,pn %xcc, 1f		/* br if 8k ptr miss */		;\
	  nop								;\
	stxa	%g5, [%g0]ASI_DTLB_IN					;\
	TT_TRACE(trace_tsbhit)		/* 2 instr ifdef TRAPTRACE */	;\
	retry								;\
1:									;\
	/*								;\
	 * We get here if we couldn't find the 8k mapping in the TSB	;\
	 * We don't look for 64K, 512K, or 4MB mappings in the TSB	;\
	 * because we currently don't support large pages in user	;\
	 * land and for the kernel I might as well go to the hash.	;\
	 * If we ever implement large pages for user adress space	;\
	 * (eg. shared memory) we need to reevaluate this.		;\
	 * g3 = ctx #							;\
	 */								;\
	TT_TRACE(trace_tsbmiss)		/* 2 instr ifdef TRAPTRACE */	;\
	brz,a,pt  %g3, sfmmu_ktsb_miss	/* assumes KCONTEXT == 0 */	;\
	  nop								;\
	ba,a,pt	%icc, sfmmu_utsb_miss					;\
	  nop								;\
	.align 128

#define	DTLB_PROT_TL1							\
	DTLB_DEMAP_ENTRY		/* 13 instructions */		;\
	/*								;\
	 * g2 = tag access register					;\
	 * g3 = ctx number						;\
	 */								;\
	TT_TRACE(trace_dataprot)	/* 2 instr ifdef TRAPTRACE */	;\
	brz,pn %g3, sfmmu_kprot_trap					;\
	  membar #Sync							;\
	ba,a,pt %icc, sfmmu_uprot_trap					;\
	  nop								;\
	.align 128

#define	DMMU_EXCEPTION_TL1						;\
	USE_ALTERNATE_GLOBALS						;\
	ba,a,pt	%xcc, mmu_trap_tl1					;\
	  nop								;\
	.align 32

#define	MISALIGN_ADDR_TL1						;\
	ba,a,pt	%xcc, mmu_trap_tl1					;\
	  nop								;\
	.align 32
	
#if defined(lint)

struct scb	trap_table;

#else /* lint */

/*
 *========================================================================
 *		SPARC V9 TRAP TABLE
 *
 * The trap table is divided into two halves: the first half is used when
 * taking traps when TL=0; the second half is used when taking traps from
 * TL>0. Note that handlers in the second half of the table might not be able
 * to make the same assumptions as handlers in the first half of the table.
 *
 * Worst case trap nesting so far:
 *
 *	at TL=0 client issues software trap requesting service
 *	at TL=1 nucleus wants a register window
 *	at TL=2 register window clean/spill/fill takes a TLB miss
 *	at TL=3 processing TLB miss
 *	at TL=4 handle asynchronous error
 *
 * Note that a trap from TL=4 to TL=5 places Spitfire in "RED mode".
 *
 *========================================================================
 */
	.section ".text"
	.align	4
	.global trap_table, trap_table0, trap_table1
	.type	trap_table, #function
trap_table:
trap_table0:
	/* hardware traps */
	NOT;				/* 000	reserved */
	RED;				/* 001	power on reset */
	RED;				/* 002	watchdog reset */
	RED;				/* 003	externally initiated reset */
	RED;				/* 004	software initiated reset */
	RED;				/* 005	red mode exception */
	NOT; NOT;			/* 006 - 007 reserved */
	IMMU_EXCEPTION;			/* 008	instruction access exception */
	NOT;				/* 009	instruction access MMU miss */
	ASYNC_TRAP(T_INSTR_ERROR);	/* 00A	instruction access error */
	NOT; NOT4;			/* 00B - 00F reserved */
	TRAP(T_UNIMP_INSTR);		/* 010	illegal instruction */
	TRAP(T_PRIV_INSTR);		/* 011	privileged opcode */
	NOT;				/* 012	unimplemented LDD */
	NOT;				/* 013	unimplemented STD */
	NOT4; NOT4; NOT4;		/* 014 - 01F reserved */
	FP_DISABLED_TRAP;		/* 020	fp disabled */
	FP_IEEE_TRAP;			/* 021	fp exception ieee 754 */
	FP_TRAP;			/* 022	fp exception other */
	TRAP(T_TAG_OVERFLOW);		/* 023	tag overflow */
	CLEAN_WINDOW;			/* 024 - 027 clean window */
	TRAP(T_IDIV0);			/* 028	division by zero */
	NOT;				/* 029	internal processor error */
	NOT; NOT; NOT4;			/* 02A - 02F reserved */
	DMMU_EXCEPTION;			/* 030	data access exception */
	NOT;				/* 031	data access MMU miss */
	ASYNC_TRAP(T_DATA_ERROR);	/* 032	data access error */
	NOT;				/* 033	data access protection */
	DMMU_EXC_AG(T_ALIGNMENT);	/* 034	mem address not aligned */
	DMMU_EXC_AG(T_ALIGNMENT);	/* 035	LDDF mem address not aligned */
	DMMU_EXC_AG(T_ALIGNMENT);	/* 036	STDF mem address not aligned */
	DMMU_EXC_AG(T_PRIV_INSTR);	/* 037	privileged action */
	NOT;				/* 038	LDQF mem address not aligned */
	NOT;				/* 039	STQF mem address not aligned */
	NOT; NOT; NOT4;			/* 03A - 03F reserved */
	NOT;				/* 040	async data error */
	LEVEL_INTERRUPT(1);		/* 041	interrupt level 1 */
	LEVEL_INTERRUPT(2);		/* 042	interrupt level 2 */
	LEVEL_INTERRUPT(3);		/* 043	interrupt level 3 */
	LEVEL_INTERRUPT(4);		/* 044	interrupt level 4 */
	LEVEL_INTERRUPT(5);		/* 045	interrupt level 5 */
	LEVEL_INTERRUPT(6);		/* 046	interrupt level 6 */
	LEVEL_INTERRUPT(7);		/* 047	interrupt level 7 */
	LEVEL_INTERRUPT(8);		/* 048	interrupt level 8 */
	LEVEL_INTERRUPT(9);		/* 049	interrupt level 9 */
	LEVEL_INTERRUPT(10);		/* 04A	interrupt level 10 */
	LEVEL_INTERRUPT(11);		/* 04B	interrupt level 11 */
	LEVEL_INTERRUPT(12);		/* 04C	interrupt level 12 */
	LEVEL_INTERRUPT(13);		/* 04D	interrupt level 13 */
	LEVEL_INTERRUPT(14);		/* 04E	interrupt level 14 */
	LEVEL_INTERRUPT(15);		/* 04F	interrupt level 15 */
	NOT4; NOT4; NOT4; NOT4;		/* 050 - 05F reserved */
	VECTOR_INTERRUPT;		/* 060	interrupt vector */
	GOTO(obp_bpt);			/* 061	PA watchpoint */
	GOTO(obp_bpt);			/* 062	VA watchpoint */
	CE_TRAP;			/* 063	corrected ECC error */
	ITLB_MISS_TL0;			/* 064	instruction access MMU miss */
	DTLB_MISS_TL0;			/* 068	data access MMU miss */
	DTLB_PROT_TL0;			/* 06C	data access protection */
	NOT4; NOT4; NOT4; NOT4;		/* 070 - 07F reserved */
	NOT4;				/* 080	spill 0 normal */
	SPILL_32bit(ASI_AIUP,sn0);	/* 084	spill 1 normal */
	SPILL_64bit(ASI_AIUP,sn0);	/* 088	spill 2 normal */
	SPILL_mixed(ASI_AIUP);		/* 08C	spill 3 normal */
	NOT4;				/* 090	spill 4 normal */
	SPILL_32bit(ASI_P,not);		/* 094	spill 5 normal */
	SPILL_64bit(ASI_P,not);		/* 098	spill 6 normal */
	SPILL_mixed(ASI_P);		/* 09C	spill 7 normal */
	NOT4;				/* 0A0	spill 0 other */
	SPILL_32bit(ASI_AIUS,so0);	/* 0A4	spill 1 other */
	SPILL_64bit(ASI_AIUS,so0);	/* 0A8	spill 2 other */
	SPILL_mixed(ASI_AIUS);		/* 0AC	spill 3 other */
	NOT4;				/* 0B0	spill 4 other */
	NOT4;				/* 0B4	spill 5 other */
	NOT4;				/* 0B8	spill 6 other */
	NOT4;				/* 0BC	spill 7 other */
	NOT4;				/* 0C0	fill 0 normal */
	FILL_32bit(ASI_AIUP,fn0);	/* 0C4	fill 1 normal */
	FILL_64bit(ASI_AIUP,fn0);	/* 0C8	fill 2 normal */
	FILL_mixed(ASI_AIUP);		/* 0CC	fill 3 normal */
	NOT4;				/* 0D0	fill 4 normal */
	FILL_32bit(ASI_P,not);		/* 0D4	fill 5 normal */
	FILL_64bit(ASI_P,not);		/* 0D8	fill 6 normal */
	FILL_mixed(ASI_P);		/* 0DC	fill 7 normal */
	NOT4;				/* 0E0	fill 0 other */
	NOT4;				/* 0E4	fill 1 other */
	NOT4;				/* 0E8	fill 2 other */
	NOT4;				/* 0EC	fill 3 other */
	NOT4;				/* 0F0	fill 4 other */
	NOT4;				/* 0F4	fill 5 other */
	NOT4;				/* 0F8	fill 6 other */
	NOT4;				/* 0FC	fill 7 other */
	/* user traps */
	GOTO(syscall_trap_4x);		/* 100	old system call */
	TRAP(T_BREAKPOINT);		/* 101	user breakpoint */
	TRAP(T_DIV0);			/* 102	user divide by zero */
	FLUSHW();			/* 103	flush windows */
	GOTO(.clean_windows);		/* 104	clean windows */
	BAD;				/* 105	range check ?? */
	GOTO(.fix_alignment);		/* 106	do unaligned references */
	BAD;				/* 107	unused */
	SYSCALL();			/* 108	new system call */
	GOTO(set_trap0_addr);		/* 109	set trap0 address */
	BAD; BAD; BAD4;			/* 10A - 10F unused */
	BAD4; BAD4; BAD4; BAD4;		/* 110 - 11F unused */
	GOTO(.getcc);			/* 120	get condition codes */
	GOTO(.setcc);			/* 121	set condition codes */
	GOTO(.getpsr);			/* 122	get psr */
	GOTO(.setpsr);			/* 123	set psr (some fields) */
	GOTO(get_timestamp);		/* 124	get timestamp */
	GOTO(get_virtime);		/* 125	get lwp virtual time */
	PRIV(self_xcall);		/* 126	self xcall */
	GOTO(get_hrestime);		/* 127	get hrestime */
	BAD4; BAD4;			/* 128 - 12F unused */
	TRACE_TRAP(trace_trap_0);	/* 130	trace, no data */
	TRACE_TRAP(trace_trap_1);	/* 131	trace, 1 data word */
	TRACE_TRAP(trace_trap_2);	/* 132	trace, 2 data words */
	TRACE_TRAP(trace_trap_3);	/* 133	trace, 3 data words */
	TRACE_TRAP(trace_trap_4);	/* 134	trace, 4 data words */
	TRACE_TRAP(trace_trap_5);	/* 135	trace, 5 data words */
	BAD;				/* 136	trace, unused */

	TRACE_TRAP(trace_trap_write_buffer);
					/* 137	trace, atomic buf write */
	BAD4; BAD4;			/* 138 - 13F unused */
	BAD4; BAD4; BAD4; BAD4;		/* 140 - 14F unused */
	BAD4; BAD4; BAD4; BAD4;		/* 150 - 15F unused */
	BAD4; BAD4; BAD4; BAD4;		/* 160 - 16F unused */
	BAD4; BAD4; BAD4;		/* 170 - 17B unused */
	BAD;				/* 17C	unused */ 
	PRIV(kadb_bpt);			/* 17D	kadb enter (L1-A) */
	PRIV(kadb_bpt);			/* 17E	kadb breakpoint */
	PRIV(obp_bpt);			/* 17F	obp breakpoint */
	/* reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 180 - 18F reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 190 - 19F reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 1A0 - 1AF reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 1B0 - 1BF reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 1C0 - 1CF reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 1D0 - 1DF reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 1E0 - 1EF reserved */
	NOT4; NOT4; NOT4; NOT4;		/* 1F0 - 1FF reserved */
trap_table1:
	NOT4; NOT4; NOT; NOT;		/* 000 - 009 unused */
	ASYNC_ITRAP_TL1;		/* 00A	instruction access error */
	NOT; NOT4;			/* 00B - 00F unused */
	NOT4; NOT4; NOT4; NOT4;		/* 010 - 01F unused */
	NOT4;				/* 020 - 023 unused */
	CLEAN_WINDOW;			/* 024 - 027 clean window */
	NOT4; NOT4;			/* 028 - 02F unused */
	DMMU_EXCEPTION_TL1;		/* 030 	data access exception */
	NOT;				/* 031 unused */
	ASYNC_DTRAP_TL1;		/* 032	data access error */
	NOT;				/* 033	unused */
	MISALIGN_ADDR_TL1;		/* 034	mem address not aligned */
	NOT; NOT; NOT; NOT4; NOT4	/* 035 - 03F unused */
	NOT4; NOT4; NOT4; NOT4;		/* 040 - 04F unused */
	NOT4; NOT4; NOT4; NOT4;		/* 050 - 05F unused */
	NOT;				/* 060	unused */
	GOTO(obp_bpt);			/* 061	PA watchpoint */
	GOTO(obp_bpt);			/* 062	VA watchpoint */
	CE_TRAP_TL1;			/* 063	corrected ECC error */
	NOT4;				/* 064	unused */
	DTLB_MISS_TL1;			/* 068	data access MMU miss */
	DTLB_PROT_TL1;			/* 06C	data access protection */
	NOT4; NOT4; NOT4; NOT4;		/* 070 - 07F unused */
	NOT4;				/* 080	spill 0 normal */
	SPILL_32bit(ASI_AIUP,sn1);	/* 084	spill 1 normal */
	SPILL_64bit(ASI_AIUP,sn1);	/* 088	spill 2 normal */
	SPILL_mixed(ASI_AIUP);		/* 08C	spill 3 normal */
	NOT4;				/* 090	spill 4 normal */
	SPILL_32bit(ASI_P,not);		/* 094	spill 5 normal */
	SPILL_64bit(ASI_P,not);		/* 098	spill 6 normal */
	SPILL_mixed(ASI_P);		/* 09C	spill 7 normal */
	NOT4;				/* 0A0	spill 0 other */
	SPILL_32bit(ASI_AIUS,so1);	/* 0A4	spill 1 other */
	SPILL_64bit(ASI_AIUS,so1);	/* 0A8	spill 2 other */
	SPILL_mixed(ASI_AIUS);		/* 0AC	spill 3 other */
	NOT4; NOT4; NOT4; NOT4;		/* 0B0 - 0BF reserved */
	NOT4;				/* 0C0	fill 0 normal */
	FILL_32bit(ASI_AIUP,fn1);	/* 0C4	fill 1 normal */
	FILL_64bit(ASI_AIUP,fn1);	/* 0C8	fill 2 normal */
	FILL_mixed(ASI_AIUP);		/* 0CC	fill 3 normal */
	NOT4;				/* 0D0	fill 4 normal */
	FILL_32bit(ASI_P,not);		/* 0D4	fill 5 normal */
	FILL_64bit(ASI_P,not);		/* 0D8	fill 6 normal */
	FILL_mixed(ASI_P);		/* 0DC	fill 7 normal */
	NOT4; NOT4; NOT4; NOT4;		/* 0E0 - 0EF unused */
	NOT4; NOT4; NOT4; NOT4;		/* 0F0 - 0FF unused */
/*
 * Code running at TL>0 does not use soft traps, so
 * we can truncate the table here.
 */
etrap_table:
	.size	trap_table, (.-trap_table)


/*
 * Software trap handlers run directly from the trap table
 */

/*
 * We get to nfo_fault in the case of an instruction miss and tte
 * has no fault bit set.  We go to tl0 to handle it.
 * g5 = tte
 */
.nfo_fault:
#ifdef TRAPTRACE
	membar	#Sync
	sethi	%hi(FLUSH_ADDR), %g6
	flush	%g6
	TRACE_PTR(%g3, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g3 + TRAP_ENT_TICK]%asi
	stxa	%g2, [%g3 + TRAP_ENT_TSTATE]%asi	! tlb tag
	stxa	%g5, [%g3 + TRAP_ENT_F1]%asi		! tsb data
	rdpr	%tpc, %g6
	sta	%g6, [%g3 + TRAP_ENT_TPC]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g3 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	or	%g6, 0x200, %g6
	stha	%g6, [%g3 + TRAP_ENT_TT]%asi
	mov	MMU_TAG_ACCESS, %g4	
	ldxa	[%g4] ASI_IMMU, %g1
	stxa	%g1, [%g3 + TRAP_ENT_SP]%asi		! tag access
	TRACE_NEXT(%g3, %g4, %g5)
#endif TRAPTRACE
	USE_ALTERNATE_GLOBALS
	mov	MMU_TAG_ACCESS, %g4
	ldxa	[%g4] ASI_IMMU, %g2			! arg4 = addr
	set	trap, %g1
	sllx	%g2, 32, %g5
	or	%g5, T_INSTR_MMU_MISS, %g2		! arg2 = type
	clr	%g3
	ba,pt	%xcc, sys_trap
	 mov	-1, %g4

.mmu_exception_end:
#ifndef	ERRATA6_LDXA_WORKS
	or	%g3, %g0, %g3
#endif	/* ERRATA6_LDXA_WORKS */
	set	trap, %g1
	ba,pt	%xcc, sys_trap
	sub	%g0, 1, %g4

.clean_windows:
	rdpr	%canrestore, %g1	! no clean windows when
	wrpr	%g0, %g1, %cleanwin	!  cleanwin is canrestore
	done
	
.fix_alignment:
	CPU_ADDR(%g1, %g2)
	ld	[%g1 + CPU_THREAD], %g1
	ld	[%g1 + T_LWP], %g1
	ld	[%g1 + PCB_FLAGS], %g2
	bset	FIX_ALIGNMENT, %g2
	st	%g2, [%g1 + PCB_FLAGS]
	done

.getcc:
	CPU_ADDR(%g1, %g2)
	stx	%o0, [%g1 + CPU_TMP1]		! save %o0
	stx	%o1, [%g1 + CPU_TMP2]		! save %o1
	mov	%g1, %o1
	rdpr	%tstate, %g1			! get tstate
        srlx	%g1, PSR_TSTATE_CC_SHIFT, %o0	! shift ccr to V8 psr
	set	PSR_ICC, %g2
	and	%o0, %g2, %o0			! mask out the rest
	srl	%o0, PSR_ICC_SHIFT, %o0		! right justify
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_AG, %pstate		! get into normal globals
	mov	%o0, %g1
	ldx	[%o1 + CPU_TMP1], %o0		! restore %o0
	ldx	[%o1 + CPU_TMP2], %o1		! restore %o1
	done

.setcc:
	CPU_ADDR(%g1, %g2)
	stx	%o0, [%g1 + CPU_TMP1]		! save %o0
	stx	%o1, [%g1 + CPU_TMP2]		! save %o1
	rdpr	%pstate, %o0
	wrpr	%o0, PSTATE_AG, %pstate		! get into normal globals
	mov	%g1, %o1
	wrpr	%g0, %o0, %pstate		! back to alternates
	sll	%o1, PSR_ICC_SHIFT, %g2
	set	PSR_ICC, %g3
	and	%g2, %g3, %g2			! mask out rest
	sllx	%g2, PSR_TSTATE_CC_SHIFT, %g2	
	rdpr	%tstate, %g3			! get tstate
	srl	%g3, 0, %g3			! clear upper word
	or	%g3, %g2, %g3			! or in new bits
	wrpr	%g3, %tstate
	ldx	[%g1 + CPU_TMP1], %o0		! restore %o0
	ldx	[%g1 + CPU_TMP2], %o1		! restore %o1
	done

/*
 * getpsr(void)
 * Note that the xcc part of the ccr is not provided.
 * The V8 code shows why the V9 trap is not faster:
 * #define GETPSR_TRAP() \
 *      mov %psr, %i0; jmp %l2; rett %l2+4; nop;
 */

	.type	.getpsr, #function
.getpsr:
	rdpr	%tstate, %g1			! get tstate
        srlx	%g1, PSR_TSTATE_CC_SHIFT, %o0	! shift ccr to V8 psr
	set	PSR_ICC, %g2
	and	%o0, %g2, %o0			! mask out the rest

	rd	%fprs, %g1			! get fprs
	and	%g1, FPRS_FEF, %g2		! mask out dirty upper/lower
	sllx	%g2, PSR_FPRS_FEF_SHIFT, %g2	! shift fef to V8 psr.ef
	or	%o0, %g2, %o0			! or result into psr.ef

        set	V9_PSR_IMPLVER, %g2		! SI assigned impl/ver: 0xef
	or	%o0, %g2, %o0			! or psr.impl/ver
	done
	SET_SIZE(.getpsr)

/*
 * setpsr(newpsr)
 * Note that there is no support for ccr.xcc in the V9 code.
 */

        .type	.setpsr, #function
.setpsr:
	rdpr	%tstate, %g1			! get tstate
!	setx	TSTATE_V8_UBITS, %g2
	or 	%g0, CCR_ICC, %g3
	sllx	%g3, TSTATE_CCR_SHIFT, %g2

	andn	%g1, %g2, %g1			! zero current user bits
	set	PSR_ICC, %g2
	and	%g2, %o0, %g2			! clear all but psr.icc bits
	sllx	%g2, PSR_TSTATE_CC_SHIFT, %g3	! shift to tstate.ccr.icc
        wrpr	%g1, %g3, %tstate		! write tstate

	set	PSR_EF, %g2
        and	%g2, %o0, %g2			! clear all but fp enable bit
	srlx	%g2, PSR_FPRS_FEF_SHIFT, %g4	! shift ef to V9 fprs.fef
	wr	%g0, %g4, %fprs			! write fprs
	done
	SET_SIZE(.setpsr)

/*
 * Entry for old 4.x trap (trap 0).
 */
	ENTRY_NP(syscall_trap_4x)
	CPU_ADDR(%g1, %g2)		! load CPU struct addr to %g1 using %g2
	ld	[%g1 + CPU_THREAD], %g2	! load thread pointer
	ld	[%g2 + T_LWP], %g2	! load klwp pointer
	ld	[%g2 + PCB_TRAP0], %g2	! lwp->lwp_pcb.pcb_trap0addr
	brz,pn	%g2, 1f			! has it been set?
	st	%l0, [%g1 + CPU_TMP1]	! delay - save some locals
	mov	%g1, %l0
	st	%l1, [%g1 + CPU_TMP2]
	rdpr	%tnpc, %l1		! save old tnpc
	wrpr	%g0, %g2, %tpc		! setup tpc
	add	%g2, 4, %g2
	wrpr	%g0, %g2, %tnpc		! setup tnpc
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_AG, %pstate	! switch to normal globals
	mov	%l1, %g2		! pass tnpc to user code in %g2
	ld	[%l0 + CPU_TMP2], %l1	! restore locals
	ld	[%l0 + CPU_TMP1], %l0
	retry
1:
	mov	%g1, %l0
	st	%l1, [%g1 + CPU_TMP2]
	rdpr	%pstate, %l1
	wrpr	%l1, PSTATE_AG, %pstate
	!
	! check for old syscall mmap which is the only different one which
	! must be the same.  Others are handled in the compatibility library.
	!
#define OSYS_mmap	71		/* XXX */
	cmp	%g1, OSYS_mmap	! compare to old 4.x mmap
	movz	%icc, SYS_mmap, %g1
	wrpr	%g0, %l1, %pstate
	ld	[%l0 + CPU_TMP2], %l1	! restore locals
	ld	[%l0 + CPU_TMP1], %l0
	SYSCALL();
	SET_SIZE(syscall_trap_4x)

/*
 * Handler for software trap 9.
 * Set trap0 emulation address for old 4.x system call trap.
 * XXX - this should be a system call.
 */
	ENTRY_NP(set_trap0_addr)
	CPU_ADDR(%g1, %g2)		! load CPU struct addr to %g1 using %g2
	ld	[%g1 + CPU_THREAD], %g2	! load thread pointer
	ld	[%g2 + T_LWP], %g2	! load klwp pointer
	st	%l0, [%g1 + CPU_TMP1]	! save some locals
	st	%l1, [%g1 + CPU_TMP2]
	rdpr	%pstate, %l0
	wrpr	%l0, PSTATE_AG, %pstate
	mov	%g1, %l1
	wrpr	%g0, %l0, %pstate
	andn	%l1, 3, %l1		! force alignment
	st	%l1, [%g2 + PCB_TRAP0]	! lwp->lwp_pcb.pcb_trap0addr
	ld	[%g1 + CPU_TMP1], %l0	! restore locals
	ld	[%g1 + CPU_TMP2], %l1
	done
	SET_SIZE(set_trap0_addr)

/*
 * mmu_trap_tl1
 * trap handler for unexpected mmu traps.
 * simply checks if the trap was a user trap from the window handler
 * in which case we go save the state on the pcb.
 * Otherwise, we go to sfmmu_bad_trap
 */
        .type	mmu_trap_tl1, #function
mmu_trap_tl1:
#ifdef	TRAPTRACE
	TRACE_PTR(%g5, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g5 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g5 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g5 + TRAP_ENT_TT]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g5 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g5 + TRAP_ENT_SP]%asi
	rdpr	%tpc, %g6
	sta	%g6, [%g5 + TRAP_ENT_TPC]%asi
	TRACE_NEXT(%g5, %g6, %g7)
#endif /* TRAPTRACE */
	rdpr	%tl, %g7
	sub	%g7, 1, %g6
	wrpr	%g6, %tl
	rdpr	%tt, %g5
	wrpr	%g7, %tl
	and	%g5, WTRAP_TTMASK, %g6
	cmp	%g6, WTRAP_TYPE	
	bne,a,pn %xcc, sfmmu_bad_trap
	 nop
	mov	T_DATA_MMU_MISS, %g5	/* hardwire trap type to dmmu miss */
	mov	MMU_SFAR, %g7
	ldxa	[%g7] ASI_DMMU, %g6
	rdpr	%tpc, %g7
	andn	%g7, WTRAP_ALIGN, %g7	/* 128 byte aligned */
	add	%g7, WTRAP_FAULTOFF, %g7
	wrpr	%g0, %g7, %tnpc	
	done
	SET_SIZE(mmu_trap_tl1)

/*
 * async_err(trap_level, data_or_instruction trap)
 * Steps: 1. Get AFSR, branch to ecc_err if only UE and no other async errors
 * 2. Get AFAR <40:4> 3. Branch to ce code if ecc error 4. Clear AFSR
 * Note we still have to clear the sdb regs if UE && other async error,
 * but we keep to async_error path instead of going to ecc_error path.
 */
        .type	async_err, #function
async_err:
	TT_TRACE(trace_gen)
	ldxa	[%g0]ASI_AFSR, %g2	! save afsr in g2
#ifndef ERRATA6_LDXA_WORKS
	or	%g2, %g0, %g0
#endif
	sllx	%g5, 53, %g5		! move tl to <55:53>
	or	%g2, %g5, %g2		! or into afsr0
	sllx	%g6, 56, %g6		! move dori to <61:56>
	or	%g2, %g6, %g2		! or into afsr0

	ldxa	[%g0]ASI_AFAR, %g3	! save afar in g3

	or	%g0, 1, %g1		! put 1 in g1
	sllx	%g1, 21, %g1		! shift left to <21> afsr UE
	andcc	%g1, %g2, %g0		! check for UE in afsr
	bz,a,pn %icc, 2f		! if !UE skip sdb read/clear
	nop

	set	P_DER_H, %g4		! put P_DER_H in g4
	ldxa	[%g4]ASI_SDB_INTR_R, %g5 ! read sdb upper half into 56
#ifndef ERRATA6_LDXA_WORKS
	or	%g5, %g0, %g0
#endif
	or	%g0, 1, %g6		! put 1 in g6
	sllx	%g6, 9, %g6		! shift g6 to <9> sdb UE
	andcc	%g5, %g6, %g1		! check for UE in upper half
	sllx	%g5, 33, %g5		! shift upper bits to <42:33>
	or	%g2, %g5, %g2		! or with afsr bits
	bz,a	1f			! no error, goto 1f
	nop
	stxa	%g1, [%g4]ASI_SDB_INTR_W ! clear sdb reg UE error bit
	membar	#Sync			! membar sync required
1:
	set	P_DER_L, %g4		! put P_DER_L in g4
	ldxa	[%g4]ASI_SDB_INTR_R, %g5 ! read sdb lower half into g5
#ifndef ERRATA6_LDXA_WORKS
	or	%g5, %g0, %g0
#endif
	andcc	%g5, %g6, %g1		! check for UE in lower half
	sllx	%g5, 43, %g5		! shift upper bits to <52:43>
	or	%g2, %g5, %g2		! or with afsr bits
	bz,a	2f			! no error, goto 2f
	nop
	stxa	%g1, [%g4]ASI_SDB_INTR_W ! clear sdb reg UE error bit
	membar	#Sync			! membar sync required
2:
	or	%g0, 0x1F, %g5		! upper 5 sticky bits of afsr
	sllx	%g5, 7, %g5		! shift left 7 positions
	or	%g5, 0x77, %g5		! lower 7 sticky bits of afsr (!CP)
	sllx	%g5, 21, %g5		! shift sticky bits to <32:21>
	and	%g2, %g5, %g4 		! mask saved afsr into g4
	stxa	%g4, [%g0]ASI_AFSR	! clear masked bits in afsr
	membar	#Sync			! membar sync required

	stxa	%g0, [%g0]ASI_ESTATE_ERR ! disable ecc and other cpu errors
	membar	#Sync			! membar sync required

	set	cpu_async_error, %g1	! put *cpu_async_error() in g1
	ba,pt	%xcc, sys_trap		! goto sys_trap
	or	%g0, 0xf, %g4		! run at pil 15
	SET_SIZE(async_err)
	.align	32

/*
 * ce_err()
 * Steps: 1. GET AFSR  2. Get AFAR <40:4> 3. Get datapath error status
 *	  4. Clear datapath error bit(s) 5. Clear AFSR error bit
 */
        .type	ce_err, #function
ce_err:
	TT_TRACE(trace_gen)
	ldxa	[%g0]ASI_AFSR, %g2	! save afsr in g2
#ifndef ERRATA6_LDXA_WORKS
	or	%g2, %g0, %g0
#endif
	ldxa	[%g0]ASI_AFAR, %g3	! save afar in g3

	set	P_DER_H, %g4		! put P_DER_H in g4
	ldxa	[%g4]ASI_SDB_INTR_R, %g5 ! read sdb upper half into g5
#ifndef ERRATA6_LDXA_WORKS
	or	%g5, %g0, %g0
#endif
	or	%g0, 1, %g6		! put 1 in g6
	sllx	%g6, 8, %g6		! shift g6 to <8> sdb CE
	andcc	%g5, %g6, %g1		! check for CE in upper half
	sllx	%g5, 33, %g5		! shift upper bits to <42:33>
	or	%g2, %g5, %g2		! or with afsr bits
	bz,a	1f			! no error, goto 1f
	nop
	stxa	%g1, [%g4]ASI_SDB_INTR_W ! clear sdb reg error bit
	membar	#Sync			! membar sync required
1:
	set	P_DER_L, %g4		! put P_DER_L in g4
	ldxa	[%g4]ASI_SDB_INTR_R, %g5 ! read sdb lower half into g6
#ifndef ERRATA6_LDXA_WORKS
	or	%g5, %g0, %g0
#endif
	andcc	%g5, %g6, %g1		! check for CE in lower half
	sllx	%g5, 43, %g5		! shift upper bits to <52:43>
	or	%g2, %g5, %g2		! or with afsr bits
	bz,a	2f			! no error, goto 2f
	nop
	stxa	%g1, [%g4]ASI_SDB_INTR_W ! clear sdb reg error bit
	membar	#Sync			! membar sync required
2:
	or	%g0, 1, %g4		! put 1 in g4
	sllx	%g4, 20, %g4		! shift left to <20> afsr CE
	stxa	%g4, [%g0]ASI_AFSR	! use g4 to clear afsr CE error
	membar	#Sync			! membar sync required

	set	cpu_ce_error, %g1	! put *cpu_ce_error() in g1
	rdpr	%pil, %g6		! read pil into %g6
	subcc	%g6, 0xf, %g0
	movneg	%icc, 0xe, %g4
	ba,pt	%xcc, sys_trap		! goto sys_trap
	movge	%icc, 0xf, %g4
	SET_SIZE(ce_err)
	.align	32


/*
 * dis_err_panic1()
 * 1. disable all errors 2. read existing error 3. panic gracefully we hope
 */
        .type	dis_err_panic1, #function
dis_err_panic1:
	stxa	%g0, [%g0]ASI_ESTATE_ERR
	membar	#Sync
	ldxa	[%g0]ASI_AFSR, %g1	! read afsr
	membar	#Sync
	set	P_DER_L, %g4		! put P_DER_L in g4
	ldxa	[%g4]ASI_SDB_INTR_R, %g7 ! read sdb lower half
	membar	#Sync			! membar sync required
	set	P_DER_H, %g4		! put P_DER_L in g4
	ldxa	[%g4]ASI_SDB_INTR_R, %g7 ! read sdb upper half
	membar	#Sync			! membar sync required
	set	sys_tl1_panic, %g1
	ba,pt	%xcc, sys_trap
	sub	%g0, 1, %g4
	SET_SIZE(dis_err_panic1)
	.align	32


/*	
 * These two get overlaid with the associated debugger's
 * breakpoint entry.
 */
	.global	kadb_bpt
	.global	obp_bpt
kadb_bpt:
	NOT
obp_bpt:
	NOT

#endif	/* lint */


#ifdef	TRAPTRACE
/*
 * TRAPTRACE support.
 * labels here are branched to with "rd %pc, %g7" in the delay slot.
 * Return is done by "jmp %g7 + 4".
 */

trace_gen:
	TRACE_PTR(%g3, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g3 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g3 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g3 + TRAP_ENT_TT]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g3 + TRAP_ENT_TSTATE]%asi
	sta	%sp, [%g3 + TRAP_ENT_SP]%asi
	rdpr	%tpc, %g6
	sta	%g6, [%g3 + TRAP_ENT_TPC]%asi
	TRACE_NEXT(%g3, %g4, %g5)
	jmp	%g7 + 4
	nop

trace_spill:
trace_fill:
	jmp	%g7 + 4
	nop

trace_tsbhit:
	membar	#Sync
	sethi	%hi(FLUSH_ADDR), %g6
	flush	%g6
	TRACE_PTR(%g3, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g3 + TRAP_ENT_TICK]%asi
	stxa	%g2, [%g3 + TRAP_ENT_TSTATE]%asi	! tlb tag
	stxa	%g5, [%g3 + TRAP_ENT_F1]%asi		! tsb data
	rdpr	%tpc, %g6
	sta	%g6, [%g3 + TRAP_ENT_TPC]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g3 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g3 + TRAP_ENT_TT]%asi
	mov	MMU_TAG_ACCESS, %g4	
	ldxa	[%g4] ASI_IMMU, %g1
	ldxa	[%g4] ASI_DMMU, %g4
	cmp	%g6, FAST_IMMU_MISS_TT
	movne	%icc, %g4, %g1
	stxa	%g1, [%g3 + TRAP_ENT_SP]%asi		! tag access
	TRACE_NEXT(%g3, %g4, %g5)
	cmp	%g6, FAST_IMMU_MISS_TT
	set	vmhatstat, %g1
	mov	HATSTAT_DTLBMISS, %g4
	move	%icc, HATSTAT_ITLBMISS, %g4
	ld	[%g1 + %g4], %g5
	inc	%g5
	st	%g5, [%g1 + %g4]
	ld	[%g1 + HATSTAT_TSBHIT], %g5
	inc	%g5
	jmp	%g7 + 4
	st	%g5, [%g1 + HATSTAT_TSBHIT]

trace_tsbmiss:
	membar	#Sync
	sethi	%hi(FLUSH_ADDR), %g6
	flush	%g6
	TRACE_PTR(%g5, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g5 + TRAP_ENT_TICK]%asi
	stxa	%g2, [%g5 + TRAP_ENT_TSTATE]%asi	! tlb tag
	stxa	%g4, [%g5 + TRAP_ENT_F1]%asi		! tsb tag
	rdpr	%tpc, %g6
	sta	%g6, [%g5 + TRAP_ENT_TPC]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g5 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	or	%g6, 0x100, %g4
	stha	%g4, [%g5 + TRAP_ENT_TT]%asi
	mov	MMU_TAG_ACCESS, %g4	
	ldxa	[%g4] ASI_IMMU, %g1
	ldxa	[%g4] ASI_DMMU, %g4
	cmp	%g6, FAST_IMMU_MISS_TT
	movne	%icc, %g4, %g1
	stxa	%g1, [%g5 + TRAP_ENT_SP]%asi		! tag access
	TRACE_NEXT(%g5, %g4, %g1)
	cmp	%g6, FAST_IMMU_MISS_TT
	set	vmhatstat, %g1
	mov	HATSTAT_DTLBMISS, %g4
	move	%icc, HATSTAT_ITLBMISS, %g4
	ld	[%g1 + %g4], %g5
	inc	%g5
	jmp	%g7 + 4
	st	%g5, [%g1 + %g4]

trace_dataprot:
	membar	#Sync
	sethi	%hi(FLUSH_ADDR), %g6
	flush	%g6
	TRACE_PTR(%g1, %g6)
	rdpr	%tick, %g6
	stxa	%g6, [%g1 + TRAP_ENT_TICK]%asi
	rdpr	%tpc, %g6
	sta	%g6, [%g1 + TRAP_ENT_TPC]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g1 + TRAP_ENT_TSTATE]%asi
	stxa	%g2, [%g1 + TRAP_ENT_SP]%asi		! tag access reg
	rdpr	%tl, %g6
	stha	%g6, [%g1 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g1 + TRAP_ENT_TT]%asi
	TRACE_NEXT(%g1, %g4, %g5)
	jmp	%g7 + 4
	nop

#endif
