#pragma ident	"@(#)wbuf.s	1.21	95/08/08 SMI"

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#include <sys/asm_linkage.h>
#include <sys/machthread.h>
#include <sys/privregs.h>
#include <sys/spitasi.h>
#include <sys/trap.h>
#include <sys/mmu.h>
#include <sys/machparam.h>
#include <sys/machtrap.h>
#include <sys/traptrace.h>


#if !defined(lint)
#include "assym.s"


	/*
	 * Spill fault handlers
	 *   sn0 - spill normal tl 0
	 *   sn1 - spill normal tl >0
	 *   so0 - spill other tl 0
	 *   so1 - spill other tl >0
	 */

	ENTRY_NP(fault_32bit_sn0)
	!
	FAULT_32bit_TRACE(%g1, %g2, %g3, TT_F32_SN0)
	!
	! Spill normal tl0 fault.
	! This happens when a user tries to spill to an unmapped stack.
	! We handle it by simulating a pagefault at the trap pc.
	!
	! spill the window into wbuf slot 0
	! (we know wbuf is empty since we came from user mode)
	!
	! g5 = mmu trap type, g6 = tag access reg
	!
	CPU_ADDR(%g4, %g1)
	ld	[%g4 + CPU_MPCB], %g1
	st	%sp, [%g1 + MPCB_SPBUF]
	SAVE_V8WINDOW(%g1 + MPCB_WBUF)
	mov	1, %g2
	st	%g2, [%g1 + MPCB_WBCNT]
	saved
	!
	! setup user_trap args
	!
	set	sfmmu_tsb_miss, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	mov	%g5, %g3			! arg3 = traptype
	sub	%g0, 1, %g4
	!
	! spill traps increment %cwp by 2,
	! but user_trap wants the trap %cwp
	! 
	rdpr	%tstate, %g5
	and	%g5, TSTATE_CWP, %g5
	ba,pt	%xcc, user_trap
	wrpr	%g0, %g5, %cwp	
	SET_SIZE(fault_32bit_sn0)

	ENTRY_NP(fault_32bit_sn1)
	!
	FAULT_32bit_TRACE(%g5, %g6, %g7, TT_F32_SN1)
	!
	! Spill normal tl1 fault.
	! This happens when sys_trap's save spills to an unmapped stack.
	! We handle it by spilling the window to the wbuf and trying
	! sys_trap again.
	!
	CPU_ADDR(%g5, %g6)
	!
	! spill the window into wbuf slot 0
	! (we know wbuf is empty since we came from user mode)
	!
	ld	[%g5 + CPU_MPCB], %g6
	st	%sp, [%g6 + MPCB_SPBUF]
	SAVE_V8WINDOW(%g6 + MPCB_WBUF)
	mov	1, %g5
	st	%g5, [%g6 + MPCB_WBCNT]
	saved
	set	sys_trap, %g5
	wrpr	%g5, %tnpc
	done
	SET_SIZE(fault_32bit_sn1)
	
	ENTRY_NP(fault_32bit_so0)
	!
	FAULT_32bit_TRACE(%g5, %g6, %g1, TT_F32_SO0)
	!
	! Spill other tl0 fault.
	! This happens when the kernel spills a user window and that
	! user's stack has been unmapped.
	! We handle it by spilling the window into the user's wbuf.
	!
	! find lwp & increment wbcnt
	!
	CPU_ADDR(%g5, %g6)
	ld	[%g5 + CPU_MPCB], %g1
	ld	[%g1 + MPCB_WBCNT], %g2
	add	%g2, 1, %g3
	st	%g3, [%g1 + MPCB_WBCNT]
	!
	! use previous wbcnt to spill new spbuf & wbuf
	!
	sll	%g2, 2, %g4			! spbuf size is 4
	add	%g1, MPCB_SPBUF, %g3
	st	%sp, [%g3 + %g4]
	sll	%g2, 6, %g4			! wbuf size is 64
	add	%g1, %g4, %g3
	SAVE_V8WINDOW(%g3 + MPCB_WBUF)
	saved
	retry
	SET_SIZE(fault_32bit_so0)

	ENTRY_NP(fault_32bit_so1)
	!
	FAULT_32bit_TRACE(%g5, %g6, %g7, TT_F32_SO1)
	!
	! Spill other tl1 fault.
	! This happens when priv_trap spills a user window and that
	! user's stack has been unmapped.
	! We handle it by spilling the window to the wbuf and retrying
	! the save.
	!
	CPU_ADDR(%g5, %g6)
	!
	! find lwp & increment wbcnt
	!
	ld	[%g5 + CPU_MPCB], %g6
	ld	[%g6 + MPCB_WBCNT], %g5
	add	%g5, 1, %g7
	st	%g7, [%g6 + MPCB_WBCNT]
	!
	! use previous wbcnt to spill new spbuf & wbuf
	!
	sll	%g5, 2, %g7			! spbuf size is 4
	add	%g6, %g7, %g7
	st	%sp, [%g7 + MPCB_SPBUF]
	sll	%g5, 6, %g7			! wbuf size is 64
	add	%g6, %g7, %g7
	SAVE_V8WINDOW(%g7 + MPCB_WBUF)
	saved
	set	sys_trap, %g5
	wrpr	%g5, %tnpc
	done
	SET_SIZE(fault_32bit_so1)

	ENTRY_NP(fault_64bit_sn0)
	ta	72
	SET_SIZE(fault_64bit_sn0)

	ENTRY_NP(fault_64bit_sn1)
	ta	72
	SET_SIZE(fault_64bit_sn1)

	ENTRY_NP(fault_64bit_so0)
	ta	72
	SET_SIZE(fault_64bit_so0)

	ENTRY_NP(fault_64bit_so1)
	ta	72
	SET_SIZE(fault_64bit_so1)

	/*
	 * Fill fault handlers
	 *   fn0 - fill normal tl 0
	 *   fn1 - fill normal tl 1
	 */

	ENTRY_NP(fault_32bit_fn0)
	!
	FAULT_32bit_TRACE(%g1, %g2, %g3, TT_F32_FN0)
	!
	! Fill normal tl0 fault.
	! This happens when a user tries to fill from an unmapped stack.
	! We handle it by simulating a pagefault at the trap pc.
	!
	! setup user_trap args
	!
	! g5 = mmu trap type, g6 = tag access reg
	!
	set	sfmmu_tsb_miss, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	mov	%g5, %g3			! arg3 = traptype
	sub	%g0, 1, %g4
	!
	! sys_trap wants %cwp to be the same as when the trap occured,
	! so set it from %tstate
	!
	rdpr	%tstate, %g5
	and	%g5, TSTATE_CWP, %g5
	ba,pt	%xcc, user_trap
	wrpr	%g0, %g5, %cwp
	SET_SIZE(fault_32bit_fn0)

	ENTRY_NP(fault_64bit_fn0)
	ta	72
	SET_SIZE(fault_64bit_fn0)

	ENTRY_NP(fault_32bit_fn1)
	!
	FAULT_32bit_TRACE(%g1, %g2, %g3, TT_F32_FN1)
	!
	! Fill normal tl1 fault.
	! This happens when user_rtt's restore fills from an unmapped
	! stack.  We handle it by simulating a pagefault in the kernel
	! at user_rtt.
	!
	! save fault addr & fix %cwp
	!
	srl	%sp, 0, %g7
	rdpr	%tstate, %g1
	and	%g1, TSTATE_CWP, %g1
	wrpr	%g0, %g1, %cwp
	!
	! fake tl1 traps regs so that after pagefault runs, we
	! re-execute at user_rtt.
	!
	wrpr	%g0, 1, %tl
	set	TSTATE_KERN | TSTATE_IE, %g1
	wrpr	%g0, %g1, %tstate
	set	user_rtt, %g1
	wrpr	%g0, %g1, %tpc
	add	%g1, 4, %g1
	wrpr	%g0, %g1, %tnpc
	!
	! setup sys_trap args
	!
	! g5 = mmu trap type, g6 = tag access reg
	!
	set	sfmmu_tsb_miss, %g1
	mov	%g6, %g2			! arg2 = tagaccess
	set	T_USER | T_WIN_UNDERFLOW, %g3	! arg3 = traptype
	sub	%g0, 1, %g4
	!
	! setup to run kernel again by setting THREAD_REG, %wstate
	! and the mmu to their kernel values.
	!
	rdpr	%pstate, %l1
	wrpr	%l1, PSTATE_AG, %pstate
	mov	%l6, THREAD_REG			! %l6 is user_rtt's thread
	wrpr	%g0, %l1, %pstate
	wrpr	%g0, WSTATE_KERN, %wstate
	mov	KCONTEXT, %g5
	mov	MMU_PCONTEXT, %g6
	stxa	%g5, [%g6]ASI_DMMU
	ba,pt	%xcc, priv_trap
	membar	#Sync
	SET_SIZE(fault_32bit_fn1)

	ENTRY_NP(fault_64bit_fn1)
	ta	72
	SET_SIZE(fault_64bit_fn1)

	/*
	 * Kernel fault handlers
	 */
	ENTRY_NP(fault_32bit_not)
	ENTRY_NP(fault_64bit_not)
	ta	72
	SET_SIZE(fault_32bit_not)
	SET_SIZE(fault_64bit_not)
#endif !lint
