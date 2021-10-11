/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)underflow.s	1.18	94/11/22 SMI"

#include <sys/asm_linkage.h>
#include <sys/param.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/mmu.h>
#include <sys/machpcb.h>
#include <sys/pte.h>
#include <sys/buserr.h>
#include <sys/machthread.h>
#include <sys/traptrace.h>

/*
 * The sole entry point in this file (_window_overflow) is not callable
 * from C and hence is not of interest to lint.
 */

#if !defined(lint)

#include "assym.s"

	.seg	".text"
	.align	4

VALID	=	(PG_V) >> PG_S_BIT
PTE_VS_MASK =	(PG_V | PG_S) >> PG_S_BIT

/*
 * Window underflow trap handler.
 */
	ENTRY_NP(_window_underflow)

#ifdef TRACE
	CPU_ADDR(%l7, %l4)		! get CPU struct ptr to %l7 using %l4
	!
	! See if event is enabled, using %l4 and %l5 as scratch
	!
	VT_ASM_TEST_FT(TR_FAC_TRAP, TR_KERNEL_WINDOW_UNDERFLOW, %l7, %l4, %l5)
	!
	! We now have: %l7 = cpup, %l4 = event, %l5 = event info, and
	! the condition codes are set (ZF means not enabled)
	!
	bz	9f			! event not enabled
	sll	%l4, 16, %l4		! %l4 = (event << 16), %l5 = info
	or	%l4, %l5, %l4		! %l4 = (event << 16) | info
	st	%l3, [%l7 + CPU_TRACE_SCRATCH]		! save %l3
	st	%l6, [%l7 + CPU_TRACE_SCRATCH + 4]	! save %l6
	!
	! Dump the trace record.  The args are:
	! 	(event << 16) | info, data, cpup, and three scratch registers.
	! In this case, the data is the trapped PC (%l1).
	!
	TRACE_DUMP_1(%l4, %l1, %l7, %l3, %l5, %l6)
	!
	! Trace done, restore saved registers
	!
	ld	[%l7 + CPU_TRACE_SCRATCH], %l3		! restore %l3
	ld	[%l7 + CPU_TRACE_SCRATCH + 4], %l6	! restore %l6
9:
#endif	/* TRACE */

	! wim stored into %l3 by trap vector
	sll	%l3, 1, %l4		! next WIM = rol(WIM, 1, NW)
	srl	%l3, %l6, %l5		! trap vector set %l6 = NW-1
	or	%l5, %l4, %l5
	mov	%l5, %wim		! install it
	btst	PSR_PS, %l0		! (wim delay 1) test for user trap
	bz	wu_user			! (wim delay 2)
	restore				! delay slot

	!
	! Supervisor underflow.
	! We do one more restore to get into the window to be restored.
	! The first one was done in the delay slot coming here.
	! We then restore from the stack.
	!
	restore				! get into window to be restored
wu_stack_res:
	TRACE_UNFL(TT_UF_SYS, %sp, %l1, %l2, %l3)
	RESTORE_WINDOW(%sp)
	save				! get back to original window
	save
	mov	%l0, %psr		! reinstall sup PSR_CC
	nop				! psr delay
	jmp	%l1			! reexecute restore
	rett	%l2

wu_user:
	!
	! User underflow. Window to be restored is a user window.
	! We must check whether the user stack is resident where the window
	! will be restored from, which is pointed to by the windows sp.
	! The sp is the fp of the window which tried to do the restore,
	! so that it is still valid.
	!
	restore				! get into window to be restored

	TRACE_UNFL(TT_UF_USR, %sp, %l1, %l2, %l3)
	!
	! Normally, we would check the alignment, and then probe the top
	! and bottom of the save area on the stack. However we optimize
	! this by checking that both ends of the save area are within a
	! 4k unit (the biggest mask we can generate in one cycle), and
	! the alignment in one shot. This allows us to do one probe to
	! the page map. NOTE: this assumes a page size of at least 4k.
	!
	and	%sp, 0xfff, %l0
#ifdef VA_HOLE
	! check if the sp points into the hole in the address space
	sethi   %hi(hole_shift), %l5    ! hole shift address
	ld      [%l5 + %lo(hole_shift)], %l7
	add     %l0, (14*4), %l0        ! interlock, bottom of save area
	sra     %sp, %l7, %l5
	inc     %l5
	andncc  %l5, 1, %l5
	bz      1f
	andncc  %l0, 0xff8, %g0
	b,a     wu_stack_not_res        ! sp is in the hole
1:
#else
	add	%l0, (14*4), %l0
	andncc	%l0, 0xff8, %g0
#endif VA_HOLE
	bz,a	wu_sp_bot
	lda	[%sp]ASI_PM, %l1	! check for stack page resident
	!
	! Stack is either misaligned or crosses a 4k boundary.
	!
	btst	0x7, %sp		! test sp alignment
	bz	wu_sp_top
	add	%sp, (14*4), %l0	! delay slot, check top of save area

	!
	! A user underflow trap has happened with a misaligned sp.
	! Fake a memory alignment trap.
	!
	save				! get back to orig window
	save
	mov	%l3, %wim		! restore old wim, so regs are dumped
	b	_sys_trap
	mov	T_ALIGNMENT, %l4	! delay slot, fake alignment trap

wu_sp_top:
#ifdef VA_HOLE
	sra     %l0, %l7, %l5
	inc     %l5
	andncc  %l5, 1, %l5
	bz,a    1f
	lda     [%l0]ASI_PM, %l1        ! get pme for this address
	b,a     wu_stack_not_res        ! address is in the hole
1:
	srl     %l1, PG_S_BIT, %l1      ! get vws bits
	sra     %sp, %l7, %l5
	inc     %l5
	andncc  %l5, 1, %l5
	bz      1f
	and	%l1, PTE_VS_MASK, %l7	! clear all bits except valid, sys
	b,a     wu_stack_not_res        ! stack page can never be resident
1:
#else
	lda	[%l0]ASI_PM, %l1	! get pme for this address
	srl	%l1, PG_S_BIT, %l1	! get vws bits
	and	%l1, PTE_VS_MASK, %l7	! clear all bits except valid, sys
#endif VA_HOLE

	cmp	%l7, VALID		! is PTE valid and not system page?
	be,a	wu_sp_bot		! yes, check other end
	lda	[%sp]ASI_PM, %l1	! delay slot, check bottom of save area
	b,a	wu_stack_not_res	! stack page not resident

wu_sp_bot:
	srl	%l1, PG_S_BIT, %l1	! get vws bits
	and	%l1, PTE_VS_MASK, %l7	! clear all bits except valid, sys
	cmp	%l7, VALID		! is PTE valid and not system page?
	be	wu_ustack_res
	nop				! extra nop in rare case

	mov	%sp, %l0		! save fault address

wu_stack_not_res:
	!
	! Restore area on user stack is not resident.
	! We punt and fake a page fault so that trap can bring the page in.
	! If the page fault is successful we will reexecute the restore,
	! and underflow with the page now resident.
	!
	CPU_ADDR(%l7, %l6)		! load CPU address into %l7 using %l6
	ld	[%l7 + CPU_THREAD], %l6	! load thread pointer
	ld	[%l7 + CPU_MPCB], %l7	! load mpcb pointer 
	SAVE_GLOBALS(%l7 + MINFRAME)	! mpcb doubles as stack
	mov	%l6, THREAD_REG		! set global thread pointer
	mov	1, FLAG_REG		! set flag reg (%g6) non-zero
	mov	%l7, %g1		! save computed sp
	mov	%l0, %g2		! save fault address
	mov	GENERIC_INVALID, %g3	! compute bus error reg code
	btst	VALID, %l1
	bnz,a	1f
	mov	GENERIC_PROTERR, %g3
1:
	save				! back to last user window
	mov	%psr, %g4		! get CWP
	save				! back to trap window

	!
	! save remaining user state
	!
	mov	%g1, %sp		! setup kernel stack
	SAVE_OUTS(%sp + MINFRAME)
	st	%l0, [%sp + MINFRAME + PSR*4] ! psr
	st	%l1, [%sp + MINFRAME + PC*4] ! pc
	st	%l2, [%sp + MINFRAME + nPC*4] ! npc

	mov	%l3, %wim		! reinstall old wim
	mov	1, %g1			! UWM = 0x01 << CWP
	sll	%g1, %g4, %g1
	st	%g1, [%sp+ MPCB_UWM]	! setup	mpcb->mpcb_uwm  %sp is mpcb
	clr	[%sp + MPCB_WBCNT]	! %sp has mpcb
	wr	%l0, PSR_ET, %psr	! enable traps

	ld	[THREAD_REG + T_PROCP], %o4 ! psr delay
	ld	[%o4 + P_AS], %o4	! psr delay
	mov	%g2, %l3		! psr delay - %l3 = fault addr for trap
	tst	%o4			! no as yet - take long path
	bz	2f
	mov	%g3, %l4		! delay - %l4 = saved be for trap
	mov	%g2, %o1		! fault addr
	call	hat_fault_trap		! hat_fault_trap(hat, addr)
	ld	[%o4 + A_HAT], %o0
	
	tst	%o0
	be	_sys_rtt		! hat layer resolved the fault
	nop

2:
	TRACE_UNFL(TT_UF_FAULT, %g2, %o3, %o4, %o5)
	TRACE_ASM_1 (%o2, TR_FAC_TRAP, TR_TRAP_START, 0, T_DATA_FAULT);
	mov	T_DATA_FAULT, %o0
	add	%sp, MINFRAME, %o1
	mov	%l3, %o2
	mov	%l4, %o3
	call	trap			! trap(T_DATA_FAULT,
	mov	S_READ, %o4		!	rp, addr, be, S_READ)

	b,a	_sys_rtt

	!
	! The user's save area is resident. Restore the window.
	!
wu_ustack_res:
	RESTORE_WINDOW(%sp)
	save				! get back to original window
	save
	!
	! Maintain clean windows. We only need to clean the registers
	! used in underflow as we know this is a user window.
	!
	! This used to be optional, depending on the CLEAN_WINDOWS flag
	! in the PCB.  Now, it takes longer to find the current PCB than
	! to clean the window.
	!
	mov	%l0, %psr		! reinstall system PSR_CC
	mov	%l1, %o6		! put pc, npc in an unobtrusive place
	mov	%l2, %o7
	clr	%l0			! clean the used ones
	clr	%l1
	clr	%l2
	clr	%l3
	clr	%l4
	clr	%l5
	clr	%l6
	clr	%l7
	jmp	%o6			! reexecute restore
	rett	%o7
	SET_SIZE(_window_underflow)

#endif	/* lint */

