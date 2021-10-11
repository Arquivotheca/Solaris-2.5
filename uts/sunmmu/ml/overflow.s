/*
 *	Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)overflow.s	1.22	95/02/08 SMI"

/*
 * The sole entry point in this file (_window_overflow) is not callable
 * from C and hence is not of interest to lint.
 */

#if !defined(lint)

#include <sys/asm_linkage.h>
#include <sys/param.h>
#include <sys/machpcb.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/buserr.h>
#include <sys/machthread.h>
#include <sys/traptrace.h>

#include "assym.s"

	.seg	".text"
	.align	4

PROT	= (PG_V | PG_W) >> PG_S_BIT
P_INVAL	= (PG_V) >> PG_S_BIT

/*
 * window overflow trap handler
 *
 * On entry:
 *	%l0, %l1, %l2 = %psr, %pc, %npc
 *	%l3 = %wim
 *	%l6 = nwindows - 1 
 * Register usage:
 *	%l4 = scratch (sometimes saved %g3)
 *	%l5 = saved %g2
 *	%l6 = nwindows - 1, winmask, or saved %g4
 *	%l7 = saved %g1
 *	%g1 = new wim and scratch
 *	%g2 = PCB pointer
 *	%g3 = scratch while in the window to be saved
 *	%g4 = stack pointer (or saved stack pointer for shared window)
 */
	ENTRY_NP(_window_overflow)

#ifdef TRACE
	CPU_ADDR(%l7, %l4)		! get CPU struct ptr to %l7 using %l4
	!
	! See if event is enabled, using %l4 and %l5 as scratch
	!
	VT_ASM_TEST_FT(TR_FAC_TRAP, TR_KERNEL_WINDOW_OVERFLOW, %l7, %l4, %l5)
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

	mov	%g1, %l7		! save %g1
	mov	%g2, %l5		! save %g2
	
	srl	%l3, 1, %g1		! trap vector set %l3 = %wim	
	sll	%l3, %l6, %l4		! trap vector set %l6 = NW - 1
	or	%l4, %g1, %g1		! delay. next WIM = ror(WIM, 1, NW)

	! Load thread pointer into %g2 (%g7 is usually THREAD_REG)
	! Even kernel traps do this for now, because kadb gets in here
	! with its own %g7.


	CPU_ADDR(%g2, %l4)		! get CPU struct ptr to %g2 using %l4
	ld	[%g2 + CPU_MPCB], %g2	! get thread pointer
	tst	%g2			! lwp == 0 for kernel threads
	bz	.wo_kt_window		! skip uwm checking when lwp == 0
	btst	PSR_PS, %l0		! test for user or sup trap
	bz	.wo_user		! user trap 
	nop
	!
	! Overflow from supervisor mode. 
	! if current thread is a simple kernel thread, then all of its
	! register windows reference the kernel thread's stack.
	! Otherwise the current thread has a lwp, and the register window
	! to save might be a user window. This is the case when
	! curmpcb->mpcb_uwm is not zero. curmpcb->lwp_pcb.pcb_uwm has a
	! bit set for each user window which is still in the register file.
	!
	ld	[%g2 + MPCB_UWM], %l4	
	tst	%l4			! if uwm != 0
	bne,a	.wo_user_window		! then branch and execute delay slot
	bclr	%g1, %l4		! UWM & = ~(new WIM)

	!
	! Window to be saved is a supervisor window.
	! Put it on the stack.
	!
.wo_kt_window:
	save				! get into window to be saved
	mov	%g1, %wim		! install new wim
	SAVE_WINDOW(%sp)
	TRACE_OVFL(TT_OV_SYS, %sp, %l1, %l2, %l3);
	restore				! go back to trap window
#ifdef REDCHECK
	!
	! This code tests for a simulated red zone violation.
	! It should be removed in a production system.
	!
	.globl	checkredzone
	sethi	%hi(checkredzone), %g1
	ld	[%g1 + %lo(checkredzone)], %g1
	tst	%g1
	bz	1f
	nop
	CPU_ADDR(%g2, %l4)		! get CPU struct ptr to %g2 using %l4
	ld	[%g2 + CPU_THREAD], %g2	! get thread pointer into %g2
	ld	[%g2 + T_SWAP], %g1	! address of swappable memory
	tst	%g1
	bz	1f
	cmp	%fp, %g1		! lower limit of the red zone
	bl	1f
	nop
	set	PAGESIZE, %l4
	add	%g1, %l4, %g1
	cmp	%fp, %g1		! upper limit of the red zone
	bge	1f
	nop
	wr	%l0, PSR_ET, %psr	! enable traps before calling panic
	nop ; nop ; nop
	restore
	sethi	%hi(checkredzone), %o0	! don't do red zone check
	st	%g0, [%o0 + %lo(checkredzone)]
	set	redpanic, %o0		! while calling panic()
	mov	1, FLAG_REG		! set flag register (%g6) non-zero
	call	panic
	mov	%g2, THREAD_REG		! delay slot, set curthread
redpanic:
	.asciz	"kernel stack overflow"
	.align	4 
1:
#endif /* REDCHECK */
	mov	%l0, %psr		! reinstall system PSR_CC
	mov	%l7, %g1		! restore g1
	mov	%l5, %g2		! restore g2
	jmp	%l1			! reexecute save
	rett	%l2

.wo_user_window:
	!
	! Window to be saved is a user window.
	!

	st	%l4, [%g2 + MPCB_UWM]	! update lwp->pcb.pcb_uwm
	mov	%g3, %l4		! save g3
	mov	%g4, %l6		! save g4
	save				! get into window to be saved
	mov	%g1, %wim		! install new wim
	!
	! Check for saving the shared window.  In this one, the stack pointer
	! is for the kernel stack, and the window must be saved to both the
	! kernel stack and the user stack, using the saved user stack pointer.
	!
	ld	[%g2 + MPCB_SWM], %g4	! get shared window mask
	btst	%g1, %g4		! test for shared window
	bz,a	1f			! not shared window
	mov	%sp, %g4		! delay - use user %sp

	SAVE_WINDOW(%sp)		! save kernel copy of window
	TRACE_OVFL(TT_OV_SHRK, %sp, %g4, %g1, %g3)
	clr	[%g2 + MPCB_SWM]		! clear shared window mask
	b	1f
	ld	[%g2 + MPCB_REGS + O6*4], %g4	! use saved stack pointer to save window

.wo_user:
	!
	! The window to be saved is a user window.
	! We must check whether the user stack is resident where the window
	! will be saved, which is pointed to by the window's sp.
	! We must also check that the sp is aligned to a word boundary.
	!
	mov	%g3, %l4		! save g3
	mov	%g4, %l6		! save g4
	save				! get into window to be saved
	mov	%g1, %wim		! install new wim
	mov	%sp, %g4		! %g4 is address at which to save window
	!
	! Normally, we would check the alignment, and then probe the top
	! and bottom of the save area on the stack. However we optimize
	! this by checking that both ends of the save area are within a
	! 4k unit (the biggest mask we can generate in one cycle), and
	! the alignment in one shot. This allows us to do one probe to
	! the page map. NOTE: this assumes a page size of at least 4k.
	!
1:

	and	%g4, 0xfff, %g1
#ifdef VA_HOLE
        ! check if the sp (in %g4) points into the hole in the address space
        sethi   %hi(hole_shift), %g3    ! hole shift address
        ld      [%g3 + %lo(hole_shift)], %g3
        add     %g1, (14*4), %g1        ! interlock, bottom of save area
        sra     %g4, %g3, %g3
        inc     %g3
        andncc  %g3, 1, %g3
        bz      1f
        andncc  %g1, 0xff8, %g0
        b,a     .wo_stack_not_res       ! stack page is in the hole
1:
#else
	add	%g1, (14*4), %g1
	andncc	%g1, 0xff8, %g0
#endif VA_HOLE
	bz,a	.wo_sp_bot
	lda	[%g4] ASI_PM, %g1	! check for stack page resident
	!
	! Stack is either misaligned or crosses a 4k boundary.
	!
	btst	0x7, %g4		! test sp alignment
	bz	.wo_sp_top
	add	%g4, (14*4), %g1	! delay slot, check top of save area

	!
	! Misaligned sp. If this is a userland trap fake a memory alignment
	! trap. Otherwise, put the window in the window save buffer so that
	! we can catch it again later.
	!
	mov	%psr, %g1		! get psr (we are not in trap window)
	btst	PSR_PS, %g1		! test for user or sup trap
	bnz	.wo_save_to_buf		! sup trap, save window in uarea buf
	nop

	restore				! get back to orig window
	mov	%l6, %g4		! restore g4
	mov	%l4, %g3		! restore g3
	mov	%l5, %g2		! restore g2
	mov	%l7, %g1		! restore g1
	mov	%l3, %wim		! restore old wim, so regs are dumped
	b	_sys_trap
	mov	T_ALIGNMENT, %l4	! delay slot, fake alignment trap

.wo_sp_top:

#ifdef VA_HOLE
        sethi   %hi(hole_shift), %g3    ! hole shift address
        ld      [%g3 + %lo(hole_shift)], %g3
        sra     %g1, %g3, %g3
        inc     %g3
        andncc  %g3, 1, %g3
        bz,a    1f
        lda     [%g1]ASI_PM, %g1        ! get pme for this address
        b,a     .wo_stack_not_res       ! stack page can never be resident
1:
        sethi   %hi(hole_shift), %g3    ! hole shift address
        ld      [%g3 + %lo(hole_shift)], %g3
        srl     %g1, PG_S_BIT, %g1      ! get vws bits
        sra     %g4, %g3, %g3
        inc     %g3
        andncc  %g3, 1, %g3
        bz,a    1f
        cmp     %g1, PROT               ! look for valid, writeable, user
        b,a     .wo_stack_not_res       ! stack page can never be resident
1:
#else
	lda	[%g1]ASI_PM, %g1	! get pme for this address
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	cmp	%g1, PROT		! look for valid, writeable, user
#endif VA_HOLE
	be,a	.wo_sp_bot
	lda	[%g4]ASI_PM, %g1	! delay slot, check bottom of save area

	b	.wo_stack_not_res	! stack page not resident
	bset	1, %g4			! note that this is top of save area

.wo_sp_bot:
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	cmp	%g1, PROT		! look for valid, writeable, user
	be	.wo_ustack_res
	nop				! extra nop

.wo_stack_not_res:
	!
	! The stack save area for user window is not resident.
	!

	mov	%psr, %g1		! get psr (we are not in trap window)
	btst	PSR_PS, %g1		! test for user or sup trap
	bnz,a	.wo_save_to_buf		! sup trap, save window in uarea buf
	bclr	1, %g4			! no need to know which end failed

	btst	1, %g4			! reconstruct fault address
	bz,a	1f			! top of save area?
	mov	%g4, %g1		! no, use sp
	bclr	1, %g4			! correct the sp, clr flag
	add	%g4, (15*4), %g1	! yes, add appropriate amount
1:
	!
	! We first save the window in the first window buffer in the u area.
	! Then we fake a user data fault. If the fault succeeds, we will
	! reexecute the save and overflow again, but this time the page
	! will be resident
	!
	st	%g4, [%g2 + MPCB_SPBUF]	! save sp
	SAVE_WINDOW(%g2 + MPCB_WBUF)
	TRACE_OVFL(TT_OV_BUF, %g4, %l1, %l2, %l3);
	restore				! get back into original window
	mov	%l4, %g3		! restore g3
	mov	%l6, %g4		! restore g4
	!
	! Set the save buffer ptr to next buffer
	!
	mov	1, %l4
	st	%l4, [%g2 + MPCB_WBCNT]	! lwp->lwp_pcb.pcb_wbcnt = 1
	!
	! Compute the user window mask (mpcb->mpcb_uwm), which is a mask
	! of which windows contain user data. In this case it is all the
	! registers except the one at the old WIM and the one we just saved.
	!
	sethi	%hi(winmask), %l6
	ld	[%l6 + %lo(winmask)], %l6 ! mask has zeros for valid windows
	!
	! note that %l6 no longer holds NW-1 
	!
	mov	%wim, %l4		! get new WIM
	or	%l4, %l3, %l4		! or in old WIM
	not	%l4
	andn	%l4, %l6, %l4		! apply WINMASK
	st	%l4, [%g2 + MPCB_UWM]	! lwp->lwp_pcb.pcb_uwm = ~(OWIM|NWIM)

	mov	%g2, %sp		! mpcb doubles as initial stack
	ld	[%g2 + MPCB_THREAD], %l7	! get thread pointer
	mov	%g1, %l4		! save fault address, arg to _trap
	mov	%l7, %g1		! restore %g1
	mov	%l5, %g2		! restore %g2
	SAVE_GLOBALS(%sp + MINFRAME)
	mov	%l7, THREAD_REG		! finally set thread pointer for trap
	mov	1, FLAG_REG		! set flag register (%g6) non-zero
	SAVE_OUTS(%sp + MINFRAME)
	st	%l0, [%sp + MINFRAME + PSR*4] ! psr
	st	%l1, [%sp + MINFRAME + PC*4] ! pc
	st	%l2, [%sp + MINFRAME + nPC*4] ! npc
#ifdef VA_HOLE
        sethi   %hi(hole_shift), %g1    ! hole shift address
        ld      [%g1 + %lo(hole_shift)], %g1
        sra     %l4, %g1, %g1
        inc     %g1
        andncc  %g1, 1, %g1
        bz,a    2f
        lda     [%l4]ASI_PM, %g1        ! get pme for this address
        b       1f                      ! stack page in the hole
        mov     GENERIC_INVALID, %l3
2:
#else
	lda	[%l4]ASI_PM, %g1	! compute proper bus error reg
#endif VA_HOLE
	mov	GENERIC_INVALID, %l3
	srl	%g1, PG_S_BIT, %g1
	btst	P_INVAL, %g1
	bnz,a	1f
	mov	GENERIC_PROTERR, %l3
1:
	wr	%l0, PSR_ET, %psr	! enable traps

	ld	[THREAD_REG + T_PROCP], %o4
	ld	[%o4 + P_AS], %o4	! no as yet, must take long path
	tst	%o4
	bz	2f
	mov	%l4, %o1		! fault addr
	call	hat_fault_trap		! hat_fault_trap(hat, addr)
	ld	[%o4 + A_HAT], %o0

	tst	%o0
	be	_sys_rtt		! hat layer resolved the fault
	nop

2:
	mov	T_DATA_FAULT, %o0
	add	%sp, MINFRAME, %o1
	mov	%l4, %o2
	mov	%l3, %o3
	call	trap			! trap(T_DATA_FAULT,
	mov	S_WRITE, %o4		!	rp, addr, be, S_WRITE)

	b,a	_sys_rtt		! return

.wo_save_to_buf:
	!
	! The user's stack is not accessable while trying to save a user window
	! during a supervisor overflow. We save the window in the PCB to
	! be processed when we return to the user.
	!

	ld	[%g2 + MPCB_THREAD], %g3
	mov	1, %g1
	stb	%g1, [%g3 + T_ASTFLAG] ! set AST so WBCNT will be seen
	ld	[%g2 + MPCB_WBCNT], %g1	! pcb_wbcnt*4 is offset into pcb_spbuf
	add	%g1, 1, %g3		! increment pcb_wbcnt to reflect save
	st	%g3, [%g2 + MPCB_WBCNT]
	sll	%g1, 2, %g3		! 
	add	%g2, %g3, %g3		! save sp in mpcb_wbcnt*4 + pcb+spbuf
	st	%g4, [%g3 + MPCB_SPBUF]	! save user's sp from %g4
	sll	%g1, 6, %g1		! mpcb_wbcnt*64 is offset into pcb_wbuf
	add	%g2, MPCB_WBUF, %g3	! calculate mpcb_wbuf
	add	%g1, %g3, %g1		! save window to mpcb_wbuf + 64*mpcb_wbcnt
	SAVE_WINDOW(%g1)
	TRACE_OVFL(TT_OV_BUFK, %g1, %l1, %l2, %l3);
	restore				! get back to orig window
	mov	%l6, %g4		! restore g4
	mov	%l4, %g3		! restore g3

	!
	! Return to supervisor. Rett will not underflow since traps
	! were never disabled.
	!
	mov	%l0, %psr		! reinstall system PSR_CC
	mov	%l7, %g1		! restore g1
	mov	%l5, %g2		! restore g2
	jmp	%l1			! reexecute save
	rett	%l2

	!
	! The user's save area is resident. Save the window.
	!
.wo_ustack_res:
	SAVE_WINDOW(%g4)
	TRACE_OVFL(TT_OV_USR, %g4, %l1, %l2, %l3);
	restore				! go back to trap window
	mov	%l6, %g4		! restore g4
	mov	%l4, %g3		! restore g3
.wo_out:
	ld	[%g2 + MPCB_FLAGS], %l3	! check for clean window maintenance
	mov	%l7, %g1		! restore g1
	mov	%l5, %g2		! restore g2
	btst	CLEAN_WINDOWS, %l3
	bz	1f
	mov	%l0, %psr		! reinstall system PSR_CC

	!
	! Maintain clean windows.
	!
	mov	%l1, %o6		! put pc, npc in an unobtrusive place
	mov	%l2, %o7
	clr	%l0			! clean the rest
	clr	%l1
	clr	%l2
	clr	%l3
	clr	%l4
	clr	%l5
	clr	%l6
	clr	%l7
	clr	%o0
	clr	%o1
	clr	%o2
	clr	%o3
	clr	%o4
	clr	%o5
	jmp	%o6			! reexecute save
	rett	%o7
1:
	jmp	%l1			! reexecute save
	rett	%l2
	SET_SIZE(_window_overflow)

#endif	/* lint */
