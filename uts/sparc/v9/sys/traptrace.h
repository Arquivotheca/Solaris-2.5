/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_TRAPTRACE_H
#define	_SYS_TRAPTRACE_H

#pragma ident	"@(#)traptrace.h	1.13	95/06/28 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/machthread.h>

/*
 * Trap tracing. If TRAPTRACE is defined, every trap records info
 * in a circular buffer.  Define TRAPTRACE in Makefile.$ARCH.
 *
 * Trap trace records are XXX words, consisting of the
 * %tick, %tl, %tt, %tpc, %tstate, %sp,
 * and a few other words.
 *
 * Auxilliary entries (not of just a trap), have obvious non-%tt values in
 * the TRAP_ENT_TT field
 */
#define	TRAP_ENT_TL	0
#define	TRAP_ENT_TT	2
#define	TRAP_ENT_TPC	4
#define	TRAP_ENT_TSTATE	8
#define	TRAP_ENT_TICK	16
#define	TRAP_ENT_SP	24
#define	TRAP_ENT_TR	28			/* how far into trap */
#define	TRAP_ENT_F1	32
#define	TRAP_ENT_F2	36
#define	TRAP_ENT_F3	40
#define	TRAP_ENT_F4	44
#define	TRAP_ENT_SIZE	48

#define	TRAP_TSIZE	(170*TRAP_ENT_SIZE)	/* default size is a page */

/*
 * Trap tracing buffer header.
 */

#ifndef _ASM
/*
 * Example buffer header stored in locore.s:
 *
 * (the actual implementation could be .skip TRAPTR_SIZE*NCPU)
 */
typedef union {
    struct {
	caddr_t		vaddr_base;	/* virtual address of top of buffer */
	u_int		last_offset;	/* to "know" what trace completed */
	u_int		offset;		/* current index into buffer (bytes) */
	u_int		limit;		/* upper limit on index */
	u_longlong_t	paddr_base;	/* physical address of buffer */
	u_char		asi;		/* cache for real asi */
	u_int		pad;		/* for nice obp dumps */
	} d;
    char		cache_linesize[64];
} TRAP_TRACE_CTL;

extern TRAP_TRACE_CTL	trap_trace_ctl[];	/* allocated in locore.s */
extern int		trap_trace_bufsize;	/* default buffer size */
extern char		trap_tr0[];		/* prealloc buf for boot cpu */
extern int		trap_freeze;		/* freeze the trap trace */

/*
 * freeze the trap trace
 */
#define	TRAPTRACE_FREEZE	trap_freeze = 1;
#define	TRAPTRACE_UNFREEZE	trap_freeze = 0;

#else /* _ASM */
/*
 * Offsets of words in trap_trace_ctl:
 */
#define	TRAPTR_VBASE	0		/* virtual address of buffer */
#define	TRAPTR_LAST_OFFSET 4		/* last completed trace entry */
#define	TRAPTR_OFFSET	8		/* next trace entry pointer */
#define	TRAPTR_LIMIT	12		/* pointer past end of buffer */
#define	TRAPTR_PBASE	16		/* start of buffer */
#define	TRAPTR_ASIBUF	24		/* cache of current asi */

#define	TRAPTR_SIZE_SHIFT	6	/* shift count -- per CPU indexing */
#define	TRAPTR_SIZE		(1<<TRAPTR_SIZE_SHIFT)

#define	TRAPTR_ASI	ASI_MEM		/* ASI to use for TRAPTR access */

/*
 * TRACE_PTR(ptr, scr1) - get trap trace entry physical pointer.
 *	ptr is the register to receive the trace pointer.
 *	scr1 is a different register to be used as scratch.
 * TRACING now needs a known processor state.  Hence the assertion.
 *	NOTE: this caches and resets %asi
 */
#define	TRACE_PTR(ptr, scr1)				\
	rdpr	%pstate, scr1;				\
	and	scr1, PSTATE_IE | PSTATE_AM, scr1;	\
	/* CSTYLED */					\
	brz,pt	scr1, .+20;				\
	nop;						\
	sethi	%hi(trap_trace_msg), %o0;		\
	call	prom_panic;				\
	or	%o0, %lo(trap_trace_msg), %o0;		\
	CPU_INDEX(scr1);				\
	sll	scr1, TRAPTR_SIZE_SHIFT, scr1;		\
	set	trap_trace_ctl, ptr; 			\
	add	ptr, scr1, scr1;			\
	rd	%asi, ptr;				\
	stb	ptr, [scr1 + TRAPTR_ASIBUF];		\
	ld	[scr1 + TRAPTR_LIMIT], ptr;		\
	/* CSTYLED */					\
	brnz,pt	ptr, .+20;				\
	nop;						\
	sethi	%hi(trap_trace_msg), %o0;		\
	call	prom_panic;				\
	or	%o0, %lo(trap_trace_msg), %o0;		\
	ldx	[scr1 + TRAPTR_PBASE], ptr;		\
	ld	[scr1 + TRAPTR_OFFSET], scr1;		\
	wr	%g0, TRAPTR_ASI, %asi;			\
	add	ptr, scr1, ptr;

/*
 * TRACE_NEXT(scr1, scr2, scr3) - advance the trap trace pointer.
 *	scr1, scr2, scr3 are scratch registers.
 *	This routine will skip updating the trap pointers if the
 *	global freeze register is set (e.g. in panic).
 *	(we also restore the asi register)
 */
#define	TRACE_NEXT(scr1, scr2, scr3)			\
	CPU_INDEX(scr2);				\
	sll	scr2, TRAPTR_SIZE_SHIFT, scr2;		\
	set	trap_trace_ctl, scr1; 			\
	add	scr1, scr2, scr2;			\
	ldub	[scr2 + TRAPTR_ASIBUF], scr1;		\
	wr	%g0, scr1, %asi;			\
	sethi	%hi(trap_freeze), scr1;			\
	ld	[scr1 + %lo(trap_freeze)], scr1;	\
	/* CSTYLED */					\
	brnz	scr1, .+36; /* skip update on freeze */	\
	ld	[scr2 + TRAPTR_OFFSET], scr1;		\
	ld	[scr2 + TRAPTR_LIMIT], scr3;		\
	st	scr1, [scr2 + TRAPTR_LAST_OFFSET];	\
	add	scr1, TRAP_ENT_SIZE, scr1;		\
	sub	scr3, TRAP_ENT_SIZE, scr3;		\
	cmp	scr1, scr3;				\
	movge	%icc, 0, scr1;				\
	st	scr1, [scr2 + TRAPTR_OFFSET];

/*
 * Trace macro for sys_trap return entries:
 *	prom_rtt, priv_rtt, and user_rtt
 *	%l7 - regs
 *	%l6 - trap %pil for prom_rtt and priv_rtt; THREAD_REG for user_rtt
 */
#define	TRACE_RTT(code, scr1, scr2, scr3, scr4)		\
	rdpr	%pstate, scr4;				\
	andn	scr4, PSTATE_IE | PSTATE_AM, scr3;	\
	wrpr	%g0, scr3, %pstate;			\
	TRACE_PTR(scr1, scr2);				\
	rdpr	%tick, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TICK]%asi;	\
	rdpr	%tl, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TL]%asi;		\
	set	code, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TT]%asi;		\
	ld	[%l7 + PC*4], scr2;			\
	sta	scr2, [scr1 + TRAP_ENT_TPC]%asi;	\
	ldx	[%l7 + TSTATE*4], scr2;			\
	stxa	scr2, [scr1 + TRAP_ENT_TSTATE]%asi;	\
	sta	%sp, [scr1 + TRAP_ENT_SP]%asi;		\
	sta	%l6, [scr1 + TRAP_ENT_TR]%asi;		\
	sta	%l7, [scr1 + TRAP_ENT_F1]%asi;		\
	ld	[THREAD_REG + T_CPU], scr2;		\
	ld	[scr2 + CPU_BASE_SPL], scr2;		\
	sta	scr2, [scr1 + TRAP_ENT_F2]%asi;		\
	mov	MMU_SCONTEXT, scr2;			\
	ldxa	[scr2]ASI_DMMU, scr2;			\
	sta	scr2, [scr1 + TRAP_ENT_F3]%asi;		\
	TRACE_NEXT(scr1, scr2, scr3);			\
	wrpr	%g0, scr4, %pstate

/*
 * Trace macro for spill and fill trap handlers
 *	tl and tt fields indicate which spill handler is entered
 */

#define	TRACE_32bit_TRAP				\
	rd	%asi, %l3;				\
	TRACE_PTR(%l1, %l2);				\
	rdpr	%tick, %l2;				\
	stxa	%l2, [%l1 + TRAP_ENT_TICK]%asi;		\
	rdpr	%tl, %l2;				\
	stha	%l2, [%l1 + TRAP_ENT_TL]%asi;		\
	rdpr	%tt, %l2;				\
	stha	%l2, [%l1 + TRAP_ENT_TT]%asi;		\
	rdpr	%tpc, %l2;				\
	sta	%l2, [%l1 + TRAP_ENT_TPC]%asi;		\
	rdpr	%tstate, %l2;				\
	stxa	%l2, [%l1 + TRAP_ENT_TSTATE]%asi;	\
	sta	%sp, [%l1 + TRAP_ENT_SP]%asi;		\
	sta	%l3, [%l1 + TRAP_ENT_TR]%asi;		\
	rdpr	%cansave, %l2;				\
	stba	%l2, [%l1 + TRAP_ENT_F1]%asi;		\
	rdpr	%canrestore, %l2;			\
	stba	%l2, [%l1 + TRAP_ENT_F1 + 1]%asi;	\
	rdpr	%otherwin, %l2;				\
	stba	%l2, [%l1 + TRAP_ENT_F1 + 2]%asi;	\
	rdpr	%wstate, %l2;				\
	stba	%l2, [%l1 + TRAP_ENT_F1 + 3]%asi;	\
	sta	%l0, [%l1 + TRAP_ENT_F2]%asi;		\
	TRACE_NEXT(%l1, %l2, %l3)

#define	TRACE_64bit_TRAP				\
	rd	%asi, %l3;				\
	TRACE_PTR(%l1, %l2);				\
	rdpr	%tick, %l2;				\
	stxa	%l2, [%l1 + TRAP_ENT_TICK]%asi;		\
	rdpr	%tl, %l2;				\
	stha	%l2, [%l1 + TRAP_ENT_TL]%asi;		\
	rdpr	%tt, %l2;				\
	stha	%l2, [%l1 + TRAP_ENT_TT]%asi;		\
	sta	%l3, [%l1 + TRAP_ENT_TR]%asi;		\
	rdpr	%tstate, %l2;				\
	stxa	%l2, [%l1 + TRAP_ENT_TSTATE]%asi;	\
	stxa	%sp, [%l1 + TRAP_ENT_SP]%asi;		\
	rdpr	%tpc, %l2;				\
	stxa	%l2, [%l1 + TRAP_ENT_F1]%asi;		\
	TRACE_NEXT(%l1, %l2, %l3)

#ifdef TRAPTRACE

#define	FAULT_32bit_TRACE(scr1, scr2, scr3, type)	\
	TRACE_PTR(scr1, scr2);				\
	rdpr	%tick, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TICK]%asi;	\
	rdpr	%tl, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TL]%asi;		\
	set	type, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TT]%asi;		\
	rdpr	%tpc, scr2;				\
	sta	scr2, [scr1 + TRAP_ENT_TPC]%asi;	\
	rdpr	%tstate, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TSTATE]%asi;	\
	sta	%sp, [scr1 + TRAP_ENT_SP]%asi;		\
	sta	%g0, [scr1 + TRAP_ENT_TR]%asi;		\
	sta	%g0, [scr1 + TRAP_ENT_F1]%asi;		\
	sta	%g4, [scr1 + TRAP_ENT_F2]%asi;		\
	rdpr	%pil, scr2;				\
	sta	scr2, [scr1 + TRAP_ENT_F3]%asi;		\
	sta	%g0, [scr1 + TRAP_ENT_F4]%asi;		\
	TRACE_NEXT(scr1, scr2, scr3)

/*
 * Branches us off to somewhere with more space to maneuver
 */
#define	CLEAN_WINDOW_TRACE				\
	sethi	%hi(win_32bit_trace), %l0;		\
	jmpl	%l0 + %lo(win_32bit_trace), %l0;	\
	nop

#define	WIN_32bit_TRACE					\
	sethi	%hi(win_32bit_trace), %l0;		\
	jmpl	%l0 + %lo(win_32bit_trace), %l0;	\
	nop

#define	WIN_64bit_TRACE					\
	sethi	%hi(win_64bit_trace), %l0;		\
	jmpl	%l0 + %lo(win_64bit_trace), %l0;	\
	nop

/*
 * Trace macro for mmu trap handlers
 *	used by sfmmu_mmu_trap()
 */
#define	MMU_TRACE(scr1, scr2, scr3)			\
	rd	%asi, scr3;				\
	TRACE_PTR(scr1, scr2);				\
	rdpr	%tick, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TICK]%asi;	\
	rdpr	%tl, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TL]%asi;		\
	rdpr	%tt, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TT]%asi;		\
	rdpr	%tpc, scr2;				\
	sta	scr2, [scr1 + TRAP_ENT_TPC]%asi;	\
	rdpr	%tstate, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TSTATE]%asi;	\
	sta	%sp, [scr1 + TRAP_ENT_SP]%asi;		\
	wr	%g0, scr3, %asi;			\
	ldxa	[MMU_TAG_ACCESS]%asi, scr2;		\
	wr	%g0, TRAPTR_ASI, %asi;			\
	stxa	scr2, [scr1 + TRAP_ENT_F1]%asi;		\
	rdpr	%cansave, scr2;				\
	stba	scr2, [scr1 + TRAP_ENT_F3]%asi;		\
	rdpr	%canrestore, scr2;			\
	stba	scr2, [scr1 + TRAP_ENT_F3 + 1]%asi;	\
	rdpr	%otherwin, scr2;			\
	stba	scr2, [scr1 + TRAP_ENT_F3 + 2]%asi;	\
	rdpr	%wstate, scr2;				\
	stba	scr2, [scr1 + TRAP_ENT_F3 + 3]%asi;	\
	TRACE_NEXT(scr1, scr2, scr3)

#define	WIN_TRACE_CNT	3

#define	SYSTRAP_TT	0x1300

#define	SYSTRAP_TRACE(scr1, scr2, scr3)			\
	TRACE_PTR(scr1, scr2);				\
	rdpr	%tick, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TICK]%asi;	\
	rdpr	%tl, scr2;				\
	stha	scr2, [scr1 + TRAP_ENT_TL]%asi;		\
	set	SYSTRAP_TT, scr3;			\
	rdpr	%tt, scr2;				\
	or	scr3, scr2, scr2;			\
	stha	scr2, [scr1 + TRAP_ENT_TT]%asi;		\
	rdpr	%tpc, scr2;				\
	sta	scr2, [scr1 + TRAP_ENT_TPC]%asi;	\
	rdpr	%tstate, scr2;				\
	stxa	scr2, [scr1 + TRAP_ENT_TSTATE]%asi;	\
	sta	%g1, [scr1 + TRAP_ENT_SP]%asi;		\
	sta	%g2, [scr1 + TRAP_ENT_TR]%asi;		\
	sta	%g3, [scr1 + TRAP_ENT_F1]%asi;		\
	sta	%g4, [scr1 + TRAP_ENT_F2]%asi;		\
	rdpr	%pil, scr2;				\
	sta	scr2, [scr1 + TRAP_ENT_F3]%asi;	\
	sta	%g0, [scr1 + TRAP_ENT_F4]%asi;		\
	TRACE_NEXT(scr1, scr2, scr3)


#else /* TRAPTRACE */
#define	FAULT_32bit_TRACE(scr1, scr2, scr3, type)
#define	CLEAN_WINDOW_TRACE
#define	WIN_32bit_TRACE
#define	WIN_64bit_TRACE
#define	MMU_TRACE
#define	WIN_TRACE_CNT	0
#define	SYSTRAP_TRACE(scr1, scr2, scr3)

#endif /* TRAPTRACE */

#endif	/* _ASM */

/*
 * Trap trace codes used in place of a %tbr value when more than one
 * entry is made by a trap.  The general scheme is that the trap-type is
 * in the same position as in the TT, and the low-order bits indicate
 * which precise entry is being made.
 */

#define	TT_F32_SN0	0x1084
#define	TT_F64_SN0	0x1088
#define	TT_F32_NT0	0x1094
#define	TT_F64_NT0	0x1098
#define	TT_F32_SO0	0x10A4
#define	TT_F64_SO0	0x10A8
#define	TT_F32_FN0	0x10C4
#define	TT_F64_FN0	0x10C8
#define	TT_F32_SN1	0x1284
#define	TT_F64_SN1	0x1288
#define	TT_F32_NT1	0x1294
#define	TT_F64_NT1	0x1298
#define	TT_F32_SO1	0x12A4
#define	TT_F64_SO1	0x12A8
#define	TT_F32_FN1	0x12C4
#define	TT_F64_FN1	0x12C8

#define	TT_SC_ENTR	0x880	/* enter system call */
#define	TT_SC_RET	0x881	/* system call normal return */

#define	TT_SYS_RTT_PROM	0x5555	/* return from trap to prom */
#define	TT_SYS_RTT_PRIV	0x6666	/* return from trap to privilege */
#define	TT_SYS_RTT_USER	0x7777	/* return from trap to user */

#define	TT_INTR_EXIT	0x8888	/* interrupt thread exit (no pinned thread) */

#define	TT_SERVE_INTR	0x6000	/* SERVE_INTR */
#define	TT_XCALL	0xd000	/* xcall/xtrap */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TRAPTRACE_H */
