/*
 * Copyright (c) 1990-1995 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)locore.s	1.74	95/09/25 SMI"

/* obselete macros moved here from elsewhere */
#define	DVMABASE	(0-(1024*1024))

#if defined(lint)
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/promif.h>
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
#include <sys/privregs.h>
#include <sys/machpcb.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/scb.h>
#include <sys/machparam.h>
#include <sys/machthread.h>
#include <sys/machlock.h>
#include <sys/x_call.h>
#include <sys/msacct.h>
#include <sys/mutex_impl.h>
#include <sys/gprof.h>
#include <sys/fdreg.h>
#include <sys/spitregs.h>
#include <sys/traptrace.h>

#if defined(lint)

#include <sys/thread.h>
#include <sys/time.h>

char DVMA[1];

#else	/* lint */

#include "assym.s"

	.seg	".data"
	.global Sysbase, Syslimit, E_Sysbase, E_Syslimit

Sysbase = SYSBASE
Syslimit = SYSEND
E_Sysbase = SYSBASE
E_Syslimit = SYSEND


!
! REGOFF must add up to allow double word access to r_tstate.
! PCB_WBUF must also be aligned.
!
#if (REGOFF & 7) != 0
ERROR - struct regs not aligned
#endif
#if (MPCB_WBUF & 7)
ERROR - pcb_wbuf not aligned
#endif

/*
 * Absolute external symbols.
 * On the sun4u we put the message buffer in the third and fourth pages.
 * We set things up so that the first 2 pages of KERNELBASE is illegal
 * to act as a redzone during copyin/copyout type operations. One of
 * the reasons the message buffer is allocated in low memory to
 * prevent being overwritten during booting operations (besides
 * the fact that it is small enough to share pages with others).
 */

	.global	msgbuf, t0stack

PROM	= 0xFFE00000			! address of prom virtual area
msgbuf	= MSGBUF_BASE			! address of printf message buffer

#if MSGBUFSIZE > 0x2000
ERROR - msgbuf too large
#endif

/*
 * Define some variables used by post-mortem debuggers
 * to help them work on kernels with changing structures.
 */
	.global KERNELBASE_DEBUG, VADDR_MASK_DEBUG
	.global PGSHIFT_DEBUG, SLOAD_DEBUG

KERNELBASE_DEBUG	= KERNELBASE
VADDR_MASK_DEBUG	= 0xffffffff
PGSHIFT_DEBUG		= PGSHIFT
SLOAD_DEBUG		= SLOAD

/*
 * The thread 0 stack. This must be the first thing in the data
 * segment (other than an sccs string) so that we don't stomp
 * on anything important if the stack overflows. We get a
 * red zone below this stack for free when the kernel text is
 * write protected.
 */

	.align	8
t0stack:
	.skip	T0STKSZ			! thread 0 stack
t0stacktop:

	.global t0
	.align	PTR24_ALIGN		! alignment for mutex.
	.type	t0, #object
t0:
	.skip	THREAD_SIZE		! thread 0
	.size	t0, THREAD_SIZE

#ifdef	TRAPTRACE
	.global	trap_trace_ctl
	.global	trap_tr0
	.global trap_trace_bufsize
	.global trap_trace_msg
	.global	trap_freeze

	.align	4
trap_trace_bufsize:
	.word	TRAP_TSIZE		! default trap buffer size
trap_freeze:
	.word	0

	.align	64
trap_trace_ctl:
	.skip	NCPU * TRAPTR_SIZE	! NCPU control headers

	.align	16
trap_tr0:
	.skip	TRAP_TSIZE		! one buffer for the boot cpu

	.align	4
trap_trace_msg:
	.asciz "bad traptrace environment (pstate or ctl struct)"

	.align	4
#endif	/* TRAPTRACE */

#ifdef	TRACE

TR_intr_start:
	.asciz "interrupt_start:level %d";
	.align 4;
TR_intr_end:
	.asciz "interrupt_end";
	.align 4;
TR_intr_exit:
	.asciz "intr_thread_exit";
	.align 4;

#endif	/* TRACE */

#ifdef	NOPROM
	.global availmem
availmem:
	.word	0
#endif	NOPROM

_local_p1275cis:
	.word	0
/*
 * pointer to kernel symbol table info which is used for debugging and
 * for kvm_nlist so that it can read symbols in modules.  We put it here
 * so that it doesn't move from kernel to kernel since this is the first
 * module linked and data before this point doesn't change that often.
 */

	DGDEF(kobj_symbol_info)
	.word	0

/*
 * SYS_TRAP: old stuff. get rid of it ASAP.
 */
#define	SYS_TRAP(T)	\
    nop; sethi %hi(_sys_trap),%l3; jmp %l3+%lo(_sys_trap); mov (T),%l4;


#endif	/* lint */

#if defined(lint)

void
_start(void)
{}

#else /* lint */

	.seg	".data"

	.global nwindows, nwin_minus_one, winmask
nwindows:
	.word   8
nwin_minus_one:
	.word   7
winmask:
	.word	8

	.global	afsrbuf
afsrbuf:
	.word	0,0,0,0

/*
 * System initialization
 *
 * Our contract with the boot prom specifies that the MMU is on
 * and the first 16 meg of memory is mapped with a level-1 pte.
 * We are called with p1275cis ptr in %o0 and dvec in %o1; we start
 * execution directly from physical memory, so we need to get
 * up into our proper addresses quickly: all code before we
 * do this must be position independent.
 *
 * XXX: Above is not true for boot/stick kernel, the only thing mapped 
 *      is the text+data+bss. The kernel is loaded directly into 
 *      KERNELBASE.
 *      - upon entry, the romvec pointer (romp) is the first
 *        argument; i.e., %o0.
 *
 *      - the debug vector (dvec) pointer is the second argument (%o1)
 *
 *      - the bootops vector is in the third argument (%o2)
 *
 * Our tasks are:
 *	save parameters
 *	construct mappings for KERNELBASE (not needed for boot/stick kernel)
 *	hop up into high memory           (not needed for boot/stick kernel)
 *	initialize stack pointer
 *	initialize trap base register
 *	initialize window invalid mask
 *	initialize psr (with traps enabled)
 *	figure out all the module type stuff
 *	tear down the 1-1 mappings
 *	dive into main()
 *
 */
	ENTRY_NP(_start)
	!
	! Stash away our arguments in memory.
	!
	sethi	%hi(_local_p1275cis), %g1
	st	%o0, [%g1 + %lo(_local_p1275cis)]
	sethi	%hi(dvec), %g1
	st	%o1, [%g1 + %lo(dvec)]
	sethi	%hi(bootops), %g1
	st	%o2, [%g1 + %lo(bootops)]

	!
	! Initialize CPU state register
	!
	wrpr	%g0, PSTATE_KERN, %pstate
	wr	%g0, %g0, %fprs

	!
	! Get maxwin from %ver
	!
	rdpr	%ver, %g1
	and	%g1, VER_MAXWIN, %g1

	!
	! Stuff some memory cells related to numbers of windows.
	!
	sethi	%hi(nwin_minus_one), %g2
	st	%g1, [%g2 + %lo(nwin_minus_one)]
	inc	%g1
	sethi	%hi(nwindows), %g2
	st	%g1, [%g2 + %lo(nwindows)]
	dec	%g1
	mov	-2, %g2
	sll	%g2, %g1, %g2
	sethi	%hi(winmask), %g4
	st	%g2, [%g4 + %lo(winmask)]

	! If dvec is non-zero, we are running under the debugger.
	set	dvec, %g1
	ld	[%g1], %g1
	tst	%g1
	bz	1f
	nop

	! Copy obp's 0x7e trap entry to kadb_bpt. This is used for
	! kadb's enter (L1-A) and breakpoint traps.
	rdpr	%tba, %g1
	set	T_SOFTWARE_TRAP | ST_KADB_TRAP, %g2
	sll	%g2, 5, %g2
	or	%g1, %g2, %g1
	set	kadb_bpt, %g2
	ldx	[%g1], %g3
	stx	%g3, [%g2]
	flush	%g2
	ldx	[%g1 + 8], %g3
	stx	%g3, [%g2 + 8]
	flush	%g2 + 8
	ldx	[%g1 + 16], %g3
	stx	%g3, [%g2 + 16]
	flush	%g2 + 16
	ldx	[%g1 + 24], %g3
	stx	%g3, [%g2 + 24]
	flush	%g2 + 24
1:

	!
	! copy obp's breakpoint trap entry to obp_bpt
	!
	rdpr	%tba, %g1
	set	T_SOFTWARE_TRAP | ST_MON_BREAKPOINT, %g2
	sll	%g2, 5, %g2
	or	%g1, %g2, %g1
	set	obp_bpt, %g2
	ldx	[%g1], %g3
	stx	%g3, [%g2]
	flush	%g2
	ldx	[%g1 + 8], %g3
	stx	%g3, [%g2 + 8]
	flush	%g2 + 8
	ldx	[%g1 + 16], %g3
	stx	%g3, [%g2 + 16]
	flush	%g2 + 16
	ldx	[%g1 + 24], %g3
	stx	%g3, [%g2 + 24]
	flush	%g2 + 24

	!
	! Initialize thread 0's stack.
	! XXX4U: do we need to clear it?
	!
	set	t0stacktop, %g1		! setup kernel stack pointer
	sub	%g1, SA(V9FPUSIZE+GSR_SIZE), %g2
	and	%g2, 0x3F, %g3
	sub	%g2, %g3, %o2
	sub	%o2, SA(MPCBSIZE), %sp

	!
	! Initialize global thread register.
	!
	set	t0, THREAD_REG

	!
	! Fill in enough of the cpu structure so that
	! the wbuf management code works. Make sure the
	! boot cpu is inserted in cpu[] based on cpuid.
	!
	CPU_INDEX(%g2)
	sll	%g2, 2, %g2			! convert cpuid to cpu[] offset

	set	cpu0, %o0			! &cpu0
	set	cpu, %g1			! &cpu[]
	st	%o0, [%g1 + %g2]	! cpu[cpuid] = &cpu0

	st	%o0, [THREAD_REG + T_CPU]	! threadp()->t_cpu = cpu[cpuid]
	st	THREAD_REG, [%o0 + CPU_THREAD]	! cpu[cpuid]->cpu_thread = threadp()

	!
	! It's now safe to call other routines.  Can't do so earlier
	! because %tbr need to be set up if profiling is to work.
	!
	call    module_setup		! setup correct module routines
	nop

	!  We do NOT need to bzero our BSS...boot has already done it for us.
	!  Just need to reference edata so that we don't break /dev/ksyms
	set	edata, %g0

	!
	! Call mlsetup with address of prototype user registers.
	! and p1275cis pointers.

	sethi	%hi(_local_p1275cis), %o1
	ld	[%o1 + %lo(_local_p1275cis)], %o1
	call	mlsetup
	add	%sp, REGOFF, %o0

	!
	! Now call main.  We will return as process 1 (init).
	!
	call	main
	nop

	!
	! Main should never return.
	!
	set	.mainretmsg, %o0
	call	panic
	nop
	SET_SIZE(_start)

.mainretmsg:
	.asciz	"main returned"
	.align	4

#endif	/* lint */


/*
 * Generic system trap handler.
 *
 * XXX - why two names? (A vestige from the compiler change?) _sys_trap
 *	 is only called from within this module.
 *
 * Some kernel trap handlers save themselves from buying a window by
 * borrowing some of sys_trap's unused locals. %l0 thru %l3 may be used
 * for this purpose, as user_rtt and priv_rtt do not depend on them.
 * %l4 thru %l7 should NOT be used this way.
 *
 * Entry Conditions:
 *	%pstate		am:0 priv:1 ie:0
 *			globals are either ag or ig (not mg!)
 *
 * Register Inputs:
 *	%g1		pc of trap handler
 *	%g2, %g3	args for handler
 *	%g4		desired %pil (-1 means current %pil)
 *	%g5, %g6	destroyed
 *	%g7		saved
 *
 * Register Usage:
 *	%l0, %l1	temps
 *	%l3		saved %g1
 *	%l6		curthread for user traps, %pil for priv traps
 *	%l7		regs
 */

#if defined(lint)

void
sys_trap(void)
{}

void
_sys_trap(void)
{}

#else	/* lint */

	ENTRY_NP2(_sys_trap, sys_trap)
	!
	! force tl 1, update %cwp, branch to correct handler
	!
	wrpr	%g0, 1, %tl
	SYSTRAP_TRACE(%g5, %g6, %g7)
	rdpr	%tstate, %g5
	btst	TSTATE_PRIV, %g5
	and	%g5, TSTATE_CWP, %g6
	bnz,pn	%xcc, priv_trap
	wrpr	%g0, %g6, %cwp
	.global	user_trap
user_trap:
	!
	! user trap
	!
	! make all windows clean for kernel
	! buy a window using the current thread's stack
	!
	sethi	%hi(nwin_minus_one), %g5
	ld	[%g5 + %lo(nwin_minus_one)], %g5
	wrpr	%g0, %g5, %cleanwin
	CPU_ADDR(%g5, %g6)
	ld	[%g5 + CPU_THREAD], %g5
	ld	[%g5 + T_STACK], %g6
	save	%g6, 0, %sp
	!
	! set window registers so that current windows are "other" windows
	!
	rdpr	%canrestore, %l0
	wrpr	%g0, 0, %canrestore
	wrpr	%g0, WSTATE_KERN, %wstate
	wrpr	%g0, %l0, %otherwin
	!
	! set pcontext to run kernel
	!
	mov	KCONTEXT, %l0
	mov	MMU_PCONTEXT, %l1
	sethi	%hi(FLUSH_ADDR), %l2
	stxa	%l0, [%l1]ASI_DMMU
	flush	%l2			! flush required by immu
	set	utl0, %g6		! bounce to utl0

have_win:
	!
	! at this point we have a new window we can play in,
	! and %g6 is the label we want done to bounce to
	!
	! save needed current globals
	!
	mov	%g1, %l3	! pc
	mov	%g2, %o1	! arg #1
	mov	%g3, %o2	! arg #2
	srlx	%g2, 32, %o3	! arg #3
	srlx	%g3, 32, %o4	! arg #4
	mov	%g5, %l6	! curthread if user trap, %pil if priv trap
	!
	! save trap state on stack
	!
	add	%sp, REGOFF, %l7
	rdpr	%tpc, %l0
	rdpr	%tnpc, %l1
	rdpr	%tstate, %l2
	st	%l0, [%l7 + PC*4]
	st	%l1, [%l7 + nPC*4]
	stx	%l2, [%l7 + TSTATE*4]
	!
	! setup pil
	!
	brlz,pt		%g4, 1f
	nop
#ifdef DEBUG
	!
	! ASSERT(%g4 >= %pil).
	!
	rdpr	%pil, %l0
	cmp	%g4, %l0
	bge,pt	%xcc, 0f
	nop				! yes, nop; to avoid anull
	set	bad_g4_called, %l3
	mov	1, %o1
	st	%o1, [%l3]
	set	bad_g4, %l3		! pc
	set	sys_trap_wrong_pil, %o1	! arg #1
	mov	%g4, %o2		! arg #2
	ba	1f			! stay at the current %pil
	mov	%l0, %o3		! arg #3
0:
#endif DEBUG
	wrpr		%g0, %g4, %pil
1:
	!
	! set trap regs to execute in kernel at %g6
	! done resumes execution there
	!
	wrpr	%g0, %g6, %tnpc
	rdpr	%cwp, %l0
	set	TSTATE_KERN, %l1
	wrpr	%l1, %l0, %tstate
	done
	/* NOTREACHED */

	.global	prom_trap
prom_trap:
	!
	! prom trap switches the stack to 32-bit
	! if we took a trap from a 64-bit window
	! Then buys a window on the current stack.
	!
	save %sp, -SA64(REGOFF + REGSIZE), %sp	/*32 bit frame,64 bit sized */
	add	%sp, V9BIAS64, %sp		/* back to a 32-bit frame */
	set	ptl0, %g6
	ba,pt	%xcc, have_win
	nop

	.global	priv_trap
priv_trap:
	!
	! kernel trap
	! buy a window on the current stack
	!
	btst	1, %sp
	bne	prom_trap
	rdpr	%pil, %g5
	set	ktl0, %g6
	ba,pt	%xcc, have_win
	save	%sp, -SA(REGOFF + REGSIZE), %sp

	.global	utl0
utl0:
	SAVE_GLOBALS(%l7)
	SAVE_OUTS(%l7)
	mov	%l6, THREAD_REG
	wrpr	%g0, PSTATE_KERN, %pstate	! enable ints
	set	user_rtt - 8, %o7
	jmp	%l3
	mov	%l7, %o0

	.global	ktl0
ktl0:
	SAVE_GLOBALS(%l7)
	SAVE_OUTS(%l7)				! for the call bug workaround
	wrpr	%g0, PSTATE_KERN, %pstate	! enable ints
	set	priv_rtt - 8, %o7
	jmp	%l3
	mov	%l7, %o0

	.global	ptl0
ptl0:
	SAVE_GLOBALS(%l7)
	SAVE_OUTS(%l7)
	CPU_ADDR(%g5, %g6)
	ld	[%g5 + CPU_THREAD], THREAD_REG
	wrpr	%g0, PSTATE_KERN, %pstate	! enable ints
	set	prom_rtt - 8, %o7
	jmp	%l3
	mov	%l7, %o0
	SET_SIZE(_sys_trap)
	SET_SIZE(sys_trap)

#endif	/* lint */

#ifndef lint

#ifdef DEBUG
	.seg	".data"
	.align	4

	.global bad_g4_called
bad_g4_called:
	.word	0

sys_trap_wrong_pil:
	.asciz	"sys_trap: %g4(%d) is lower than %pil(%d)"
	.align	4
	.seg	".text"

	ENTRY_NP(bad_g4)
	mov	%o1, %o0
	mov	%o2, %o1
	call	panic
	mov	%o3, %o2
	SET_SIZE(bad_g4)
#endif DEBUG
#endif /* lint */

/*
 * sys_tl1_panic can be called by traps at tl1 which
 * really want to panic, but need the rearrangement of
 * the args as provided by this wrapper routine.
 */
#if defined(lint)

void
sys_tl1_panic(void)
{}

#else	/* lint */
	ENTRY_NP(sys_tl1_panic)
	mov	%o1, %o0
	mov	%o2, %o1
	call	panic
	mov	%o3, %o2
	SET_SIZE(sys_tl1_panic)
#endif /* lint */

/*
 * Return from _sys_trap routines.
 *
 * user_rtt returns from traps taken in user mode
 * priv_rtt return from traps taken in privileged mode
 */

#if defined(lint)

void
_sys_rtt(void)
{}

#else	/* lint */

	.global	user_rtt
user_rtt:
	!
	! Register inputs
	!	%l7 - regs
	!
	! disable interrupts and check for ASTs and wbuf restores
	! keep cpu_base_spl in %l4 and THREAD_REG in %l6 (needed
	! in wbuf.s when globals have already been restored).
	!
	wrpr	%g0, PIL_MAX, %pil
	ld	[THREAD_REG + T_CPU], %l0
	ld	[%l0 + CPU_BASE_SPL], %l4

	ldub	[THREAD_REG + T_ASTFLAG], %l2
	brz,pt	%l2, 1f
	ld	[%sp + MPCB_WBCNT], %l3
	!
	! call trap to do ast processing
	!
	wrpr	%g0, %l4, %pil			! pil = cpu_base_spl
	mov	%l7, %o0
	call	trap
	mov	T_AST, %o1
	ba,a,pt	%xcc, user_rtt
1:
	brz,pt	%l3, 2f
	mov	THREAD_REG, %l6
	!
	! call restore_wbuf to push wbuf windows to stack
	!
	wrpr	%g0, %l4, %pil			! pil = cpu_base_spl
	mov	%l7, %o0
	call	trap
	mov	T_WIN_OVERFLOW, %o1
	ba,a,pt	%xcc, user_rtt
2:
#ifdef TRAPTRACE
	TRACE_RTT(TT_SYS_RTT_USER, %l0, %l1, %l2, %l3)
#endif /* TRAPTRACE */
	!
	! restore user globals and outs
	! switch to alternate globals, saving THREAD_REG in %l6
	!
	rdpr	%pstate, %l1
	wrpr	%l1, PSTATE_IE, %pstate
	RESTORE_GLOBALS(%l7)
	wrpr	%l1, PSTATE_IE | PSTATE_AG, %pstate
	RESTORE_OUTS(%l7)
	!
	! set %pil from cpu_base_spl
	!
	wrpr	%g0, %l4, %pil
	!
	! raise tl (now using nucleus context)
	! set pcontext to scontext for user execution
	!
	wrpr	%g0, 1, %tl
	mov	MMU_SCONTEXT, %g1
	ldxa	[%g1]ASI_DMMU, %g2
	mov	MMU_PCONTEXT, %g1
	sethi	%hi(FLUSH_ADDR), %g3
	stxa	%g2, [%g1]ASI_DMMU
	flush	%g3				! flush required by immu
	!
	! setup trap regs
	!
	ld	[%l7 + PC*4], %g1
	ld	[%l7 + nPC*4], %g2
	ldx	[%l7 + TSTATE*4], %l0
	andn	%l0, TSTATE_CWP, %g7
	wrpr	%g1, %tpc
	wrpr	%g2, %tnpc
	!
	! switch "other" windows back to "normal" windows and
	! restore to window we originally trapped in
	!
	rdpr	%otherwin, %g1
	wrpr	%g0, 0, %otherwin
	wrpr	%g0, WSTATE_USER, %wstate
	wrpr	%g0, %g1, %canrestore
	restore
	!
	! set %cleanwin to %canrestore
	! set %tstate to the correct %cwp
	! retry resumes user execution
	!
	rdpr	%canrestore, %g1
	wrpr	%g0, %g1, %cleanwin
	rdpr	%cwp, %g1
	wrpr	%g1, %g7, %tstate
	retry
	/* NOTREACHED */

	.global	prom_rtt
prom_rtt:
#ifdef TRAPTRACE
	TRACE_RTT(TT_SYS_RTT_PROM, %l0, %l1, %l2, %l3)
#endif /* TRAPTRACE */
	ba,pt	%xcc, common_rtt
	mov	THREAD_REG, %l0

	.global	priv_rtt
priv_rtt:
#ifdef TRAPTRACE
	TRACE_RTT(TT_SYS_RTT_PRIV, %l0, %l1, %l2, %l3)
#endif /* TRAPTRACE */
	!
	! Register inputs
	!	%l7 - regs
	!	%l6 - trap %pil
	!
	! Check for a kernel preemption request
	!
	ld	[THREAD_REG + T_CPU], %l0
	ldub	[%l0 + CPU_KPRUNRUN], %l0
	brz,pt	%l0, 1f
	nop
	!
	! Attempt to preempt
	!
	call	kpreempt
	mov	%l6, %o0		! pass original interrupt level

	rdpr	%pil, %o0		! compare old pil level
	cmp	%l6, %o0		!   with current pil level
	movg	%xcc, %o0, %l6		! if current is lower, drop old pil
1:
	mov	THREAD_REG, %l0
common_rtt:
	!
	! switch to alternate globals
	! restore globals and outs
	!
	rdpr	%pstate, %l1
	wrpr	%l1, PSTATE_IE, %pstate
	RESTORE_GLOBALS(%l7)
	wrpr	%l1, PSTATE_IE | PSTATE_AG, %pstate
	RESTORE_OUTS(%l7)
	!
	! set %pil from max(old pil, cpu_base_spl)
	!
	ld	[%l0 + T_CPU], %l0
	ld	[%l0 + CPU_BASE_SPL], %l0
	cmp	%l6, %l0
	movg	%xcc, %l6, %l0
	wrpr	%g0, %l0, %pil
	!
	! raise tl
	! setup trap regs
	! restore to window we originally trapped in
	!
	wrpr	%g0, 1, %tl
	ld	[%l7 + PC*4], %g1
	ld	[%l7 + nPC*4], %g2
	ldx	[%l7 + TSTATE*4], %l0
	andn	%l0, TSTATE_CWP, %g7
	wrpr	%g1, %tpc
	wrpr	%g2, %tnpc
	restore
	!
	! set %tstate to the correct %cwp
	! retry resumes prom execution
	!
	rdpr	%cwp, %g1
	wrpr	%g1, %g7, %tstate
	retry
	/* NOTREACHED */


#endif /* lint */

#if defined(lint)
void
_deadman(void)
{}

#else	/* lint */
	ENTRY_NP(_deadman)
	save	%sp, -SA(MINFRAME), %sp	! get a new window 
	call	deadman
	nop
	ret
	restore
	SET_SIZE(_deadman)
#endif	/* lint */

#if defined(lint)

void
level10(void)
{}

#else	/* lint */
/*
 * This code assumes that the real time clock interrupts 100 times
 * per second, for SUN5F we call hardclock at that rate.
 */
        .seg    ".data"

	.common clk_intr, 4, 4

	.global	hrestime, hrestime_adj, hres_last_tick, hres_lock, timedelta
	.global v_clk_regs_addr, v_clk_clr_regs_addr
	/*
	 * WARNING WARNING WARNING
	 *
	 * All of these variables MUST be together on a 64-byte boundary.
	 * In addition to the primary performance motivation (having them
	 * all on the same cache line(s)), code here and in the GET*TIME()
	 * macros assumes that they all have the same high 22 address bits
	 * (so there's only one sethi).
	 */

	.align  64
hrestime:
	.word	0, 0
hrestime_adj:
	.word	0, 0
hres_last_tick:
	.word	0, 0
timedelta:
	.word	0, 0
	.align	8
hres_lock:
	.word	0
v_clk_regs_addr:
	.word	0
v_clk_clr_regs_addr:
	.word	0
	.skip   4

	.seg	".text"

/*
 *	Level 10 clock interrupt - entered with traps enabled
 *	May swtch to new stack.
 *
 *	Entry:  traps enabled.
 *		%g7 = thread pointer
 *
 *	Register usage	
 *		%l1 = saved %pstate
 *		%l3 = CPU pointer
 *		%l4 = high 22 bit address for above variables
 *		%l6 = clock thread pointer
 */

	ENTRY(level10)
	save	%sp, -SA(MINFRAME), %sp	! get a new window
	rdpr	%pstate, %l1		! disable ints
	wrpr	%l1, PSTATE_IE, %pstate

	sethi	%hi(hres_lock + HRES_LOCK_OFFSET), %l5	! %l5 not set on entry
#ifndef	ATOMICS_WORK
	membar	#Sync
#endif
	ldstub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4	! try locking
7:	tst	%l4
	bz,a,pn	%xcc, 8f			! if lock held, try again
	sethi	%hi(hrestime), %l4
	ldub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4
9:	tst	%l4
#ifndef	ATOMICS_WORK
	membar	#Sync
#endif
	bz,a,pn	%xcc, 7b
	ldstub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4
	ba,pt	%xcc, 9b
	ldub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4
	sethi	%hi(hrestime), %l4
8:
	membar	#StoreLoad

	!
	! load current %tick value
	!
	rd	%asr4, %g1			! %15 = %tick count
	sllx	%g1, 1, %g1			! clear NPT bit
	srlx	%g1, 1, %g1			! %g1 = count

	!
	! calculate nsec (based on nsec_scale)
	! nsec = count << NSEC_SHIFT * nsec_scale
	!
	sethi	%hi(nsec_scale), %i2
	ld	[%i2 + %lo(nsec_scale)], %i2	! %i2 = nsec_scale
	srlx	%g1, NSEC_SHIFT1, %g5
	mulx	%g5, %i2, %g5			! %g5 = nsec
	srlx	%g5, NSEC_SHIFT2, %g5

	!
	! update hres_last_tick
	!
	ldx	[%l4 + %lo(hres_last_tick)], %g1! previous timer value
	sub	%g5, %g1, %i1 			! %i1 = current-previous = nslt
	stx	%g5, [%l4 + %lo(hres_last_tick)]! prev = current

	!
	! apply adjustment, if any
	!
	ldx	[%l4 + %lo(hrestime_adj)], %g5	! %g5 = hrestime_adj
	brz	%g5, 2f
						! hrestime_adj == 0 ?
						! yes, skip adjustments
	clr	%l5				! delay: set adj to zero
	tst	%g5				! is hrestime_adj >= 0 ?
	bge,pt	%xcc, 1f			! yes, go handle positive case
	srl	%i1, ADJ_SHIFT, %l5		! delay: %l5 = adj

	addcc	%g5, %l5, %g0			! hrestime_adj < -adj ?
	bl,pt	%xcc, 2f			! yes, use current adj
	neg	%l5				! delay: %l5 = -adj
	ba,pt	%xcc, 2f
	mov	%g5, %l5			! no, so set adj = hrestime_adj
1:
	subcc	%g5, %l5, %g0			! hrestime_adj < adj ?
	bl,a,pt	%xcc, 2f			! yes, set adj = hrestime_adj
	mov	%g5, %l5			! delay: adj = hrestime_adj
2:
	ldx	[%l4 + %lo(timedelta)], %g5	! %g5 = timedelta
	sub	%g5, %l5, %g5			! timedelta -= adj

	stx	%g5, [%l4 + %lo(timedelta)]	! store new timedelta
	stx	%g5, [%l4 + %lo(hrestime_adj)]	! hrestime_adj = timedelta

	or	%l4, %lo(hrestime), %l2
	ld	[%l2], %i2			! %i2:%i3 = hrestime sec:nsec
	ld	[%l2 + 4], %i3
	add	%i3, %l5, %i3			! hrestime.nsec += adj
	add	%i3, %i1, %i3			! hrestime.nsec += nslt

	set	NANOSEC, %l5			! %l5 = NANOSEC
	cmp	%i3, %l5
	bl,pt	%xcc, 5f			! if hrestime.tv_nsec < NANOSEC
	sethi	%hi(one_sec), %i1		! delay
	add	%i2, 0x1, %i2			! hrestime.tv_sec++
	sub	%i3, %l5, %i3			! hrestime.tv_nsec - NANOSEC
	mov	0x1, %l5
	st	%l5, [%i1 + %lo(one_sec)]
5:
	st	%i2, [%l2]
	st	%i3, [%l2 + 4]			! store the new hrestime

	ld	[%l4 + %lo(v_clk_clr_regs_addr)], %i1
	stx	%g0, [%i1]	! clear intr source	! clear intr source

	ld	[%l4 + %lo(hres_lock)], %i1
	inc	%i1				! release lock
	st	%i1, [%l4 + %lo(hres_lock)]	! clear hres_lock

	!
	! NOTE: fake_ticks needed for timewarping under SAS 
	!	but otherwise attempting to execute kernel code verbatim
	!
#ifdef MPSAS
	set     fake_tick_requested, %o0
	st      %g0, [%o0]
#endif
	membar	#StoreStore		! tell the world we're done
	!
	! XXX - desktops don't have diagleds; servers might want to use 
	!       a seperate driver to handle them
	!

	wrpr	%l1, %pstate			! enable ints

	mov	1, %i0
	ret				! return to restore previous stack
	restore

	SET_SIZE(level10)

#endif	/* lint */

#if defined(lint)

void
level14(void)
{}

#else	/* lint */

/*
 *	Level 14 clock interrupt
 *	May want different handlers for snooping/profiling
 */

	ALTENTRY(level14)
	retl
	nop
	SET_SIZE(level14)

#endif	/* lint */

/*
 * Various neat things can be done with high speed clock sampling.
 *
 * This code along with kprof.s and the ddi_add_fastintr() call in
 * profile_attach() is here as an example of how to go about doing them.
 *
 * Because normal kernel profiling has a fairly low sampling rate we do
 * not currently use this code.  Instead we use a regular interrupt
 * handler, which is much slower, but has the benefit of not having to
 * be written in a single window of assembly code.
 *
 * This code is intended to be used for illustration purposes only.  It
 * cannot be directly used on the sun4u.  This is because ddi_add_fastintr()
 * is not supported by the sun4u.  settrap() which updates the scb
 * unfortunately may make use of branch instructions to call the target
 * interrupt handler.  These will not work because each processor maps the
 * scb in to a different address, and hence we need jump instructions.
 */

#ifndef lint

	.seg	".data"
	.align	4
fastintr_panic:
	.asciz	"fast_profile_intr: called and not implemented"
	.align	4
	.seg	".text"

#endif /* lint */


#if defined(FAST_KPROF)

/*
 * Level 14 hardware interrupts can be caused by the clock when
 * kernel profiling is enabled.  They are handled immediately
 * in the trap window.
 *
 * Level 14 software interrupts are caused by poke_cpu.  They are
 * handled by _sys_trap.
 *
 * If both are pending we handle the hardware interrupt and are
 * then bounced back to handle the software interrupt.
 */

#if defined(lint)

void
fast_profile_intr(void)
{}

#else	/* lint */

	ENTRY_NP(fast_profile_intr)
	sethi	%hi(fastintr_panic), %o0
	call	panic
	  or	%o0, %lo(fastintr_panic), %o0
	retl
	nop
	SET_SIZE(fast_profile_intr)

	.seg	".data"
	.align	4

	.global	have_fast_profile_intr
have_fast_profile_intr:
	.word	1

	.seg	".text"
	.align	4

#endif	/* lint */

#else	/* defined(FAST_KPROF) */

#if defined(lint)

void
fast_profile_intr(void)
{}

#else	/* lint */
	ENTRY_NP(fast_profile_intr)
	sethi	%hi(fastintr_panic), %o0
	call	panic
	  or	%o0, %lo(fastintr_panic), %o0
	retl
	nop
	SET_SIZE(fast_profile_intr)

	.seg	".data"
	.align	4

	.global	have_fast_profile_intr
have_fast_profile_intr:
	.word	1

	.seg	".text"
	.align	4

#endif	/* lint */

#endif	/* defined(FAST_KPROF) */

/*
 * Turn on or off bits in the auxiliary i/o register.
 *
 * set_auxioreg(bit, flag)
 *	int bit;		bit mask in aux i/o reg
 *	int flag;		0 = off, otherwise on
 *
 * This is intrinsicly ugly but is used by the floppy driver.  It is also
 * used to turn on/off the led.
 */

#if defined(lint)

/* ARGSUSED */
void
set_auxioreg(int bit, int flag)
{}

#else	/* lint */

	.seg	".data"
	.align	4
auxio_panic:
	.asciz	"set_auxioreg: interrupts already disabled on entry"
	.align	4
	.seg	".text"

	ENTRY_NP(set_auxioreg)
	/*
	 * o0 = bit mask
	 * o1 = flag: 0 = off, otherwise on
	 *
	 * disable interrupts while updating auxioreg
	 */
	rdpr	%pstate, %o2
#ifdef	DEBUG
	andcc	%o2, PSTATE_IE, %g0	/* if interrupts already */
	bnz,a,pt %icc, 1f		/* disabled, panic */
	  nop
	sethi	%hi(auxio_panic), %o0
	call	panic
	  or	%o0, %lo(auxio_panic), %o0
1:
#endif /* DEBUG */
	wrpr	%o2, PSTATE_IE, %pstate		/* disable interrupts */
	sethi	%hi(v_auxio_addr), %o3
	ld	[%o3 + %lo(v_auxio_addr)], %o4
	ldub	[%o4], %g1			/* read aux i/o register */
	tst	%o1
	bnz,a	2f
	 bset	%o0, %g1		/* on */
	bclr	%o0, %g1		/* off */
2:
	or	%g1, AUX_MBO, %g1	/* Must Be Ones */
	stb	%g1, [%o4]		/* write aux i/o register */
	retl
	 wrpr	%g0, %o2, %pstate	/* enable interrupt */
	SET_SIZE(set_auxioreg)

#endif	/* lint */

/*
 * Flush all windows to memory, except for the one we entered in.
 * We do this by doing NWINDOW-2 saves then the same number of restores.
 * This leaves the WIM immediately before window entered in.
 * This is used for context switching.
 */

#if defined(lint)

void
flush_windows(void)
{}

#else	/* lint */

	ENTRY_NP(flush_windows)
	retl
	flushw
	SET_SIZE(flush_windows)

#endif	/* lint */

/*
 * flush user windows to memory.
 */

#if defined(lint)

void
flush_user_windows(void)
{}

#else	/* lint */

	ENTRY_NP(flush_user_windows)
	rdpr	%otherwin, %g1
	brz	%g1, 3f
	clr	%g2
1:
	save	%sp, -WINDOWSIZE, %sp
	rdpr	%otherwin, %g1
	brnz	%g1, 1b
	add	%g2, 1, %g2
2:
	sub	%g2, 1, %g2		! restore back to orig window
	brnz	%g2, 2b
	restore
3:
	retl
	nop
	SET_SIZE(flush_user_windows)

#endif	/* lint */

/*
 * Throw out any user windows in the register file.
 * Used by setregs (exec) to clean out old user.
 * Used by sigcleanup to remove extraneous windows when returning from a
 * signal.
 */

#if defined(lint)

void
trash_user_windows(void)
{}

#else	/* lint */

	ENTRY_NP(trash_user_windows)
	rdpr	%otherwin, %g1
	brz	%g1, 3f			! no user windows?
	ld	[THREAD_REG + T_STACK], %g5

	!
	! There are old user windows in the register file. We disable ints
	! and increment cansave so that we don't overflow on these windows.
	! Also, this sets up a nice underflow when first returning to the
	! new user.
	!
	rdpr	%pstate, %g2
	wrpr	%g2, PSTATE_IE, %pstate
	rdpr	%cansave, %g3
	add	%g3, %g1, %g3
	wrpr	%g0, 0, %otherwin
	wrpr	%g0, %g3, %cansave
	wrpr	%g0, %g2, %pstate
3:
	retl
 	clr     [%g5 + MPCB_WBCNT]       ! zero window buffer cnt
	SET_SIZE(trash_user_windows)


#endif	/* lint */

/*
 * Setup g7 via the CPU data structure.
 */
#if defined(lint)

void
set_tbr(void)
{}

#else	/* lint */

	ENTRY_NP(set_tbr)
	retl
	ta	72		! no tbr, stop simulation
	SET_SIZE(set_tbr)

#endif	/* lint */

/*
 * void
 * reestablish_curthread(void)
 *    - reestablishes the invariant that THREAD_REG contains
 *      the same value as the cpu struct for this cpu (implicit from
 *      where we're running). This is needed for OBP callback routines.
 *	The CPU_ADDR macro figures out the cpuid by reading hardware registers.
 */

#if defined(lint)

void
reestablish_curthread(void)
{}

#else	/* lint */

	ENTRY_NP(reestablish_curthread)

	CPU_ADDR(%o0, %o1)
	retl
	ld	[%o0 + CPU_THREAD], THREAD_REG
	SET_SIZE(reestablish_curthread)


#endif	/* lint */

/*
 * Return the current THREAD pointer.
 * This is also available as an inline function.
 */
#if defined(lint)

kthread_id_t
threadp(void)
{ return ((kthread_id_t)0); }

trapvec kadb_tcode;
trapvec trap_kadb_tcode;
trapvec trap_monbpt_tcode;

#else	/* lint */

	ENTRY_NP(threadp)
	retl
	mov	THREAD_REG, %o0
	SET_SIZE(threadp)


/*
 * Glue code for traps that should take us to the monitor/kadb if they
 * occur in kernel mode, but that the kernel should handle if they occur
 * in user mode.
 */
        .global trap_monbpt_tcode, trap_kadb_tcode
	.global	mon_breakpoint_vec
 
 /* tcode to replace trap vectors if kadb steals them away */
trap_monbpt_tcode:
	mov	%psr, %l0
	sethi	%hi(trap_mon), %l4
	jmp	%l4 + %lo(trap_mon)
	mov	0xff, %l4
	SET_SIZE(trap_monbpt_tcode)

trap_kadb_tcode:
	mov	%psr, %l0
	sethi	%hi(trap_kadb), %l4
	jmp	%l4 + %lo(trap_kadb)
	mov	0xfe, %l4
	SET_SIZE(trap_kadb_tcode)

/*
 * This code assumes that:
 * 1. the monitor uses trap ff to breakpoint us
 * 2. kadb steals both ff and fe when we call scbsync()
 * 3. kadb uses the same tcode for both ff and fe.
 * Note: the ".align 8" is needed so that the code that copies
 *	the vectors at system boot time can use ldd and std
 * XXX - Stop other CPUs?
 */
	.align	8
trap_mon:
	! btst	PSR_PS, %l0		! test pS
	bz	_sys_trap		! user-mode, treat as bad trap
	nop
	mov	%l0, %psr		! restore psr
	nop				! psr delay
	b	mon_breakpoint_vec
	nop				! psr delay, too!

	.align	8		! MON_BREAKPOINT_VEC MUST BE DOUBLE ALIGNED.
mon_breakpoint_vec:
        SYS_TRAP(0xff)                  ! gets overlaid.

trap_kadb:
	! btst	PSR_PS, %l0		! test pS
	bz	_sys_trap		! user-mode, treat as bad trap
	nop
	mov	%l0, %psr		! restore psr
	nop				! psr delay
	b	kadb_tcode
	nop				! psr delay, too!

	.align	8		! KADB_BREAKPOINT_VEC MUST BE DOUBLE ALIGNED.
	.global kadb_tcode
kadb_tcode:
        SYS_TRAP(0xfe)                  ! gets overlaid.

/*
 * Stub to get into the monitor.
 */
	.global	call_into_monitor_prom
call_into_monitor_prom:
	retl
	t	0x7f
	SET_SIZE(call_into_monitor_prom)

#endif	/* lint */


/*
 * The interface for a 32-bit client program that takes over the TBA
 * calling the 64-bit romvec OBP.
 */

#if defined(lint)

/* ARGSUSED */
int
client_handler(void *cif_handler, void *arg_array)
{ return 0; }

#else	/* lint */

	ENTRY(client_handler)
	save	%sp, -SA64(MINFRAME64), %sp	! 32 bit frame, 64 bit sized
	sethi	%hi(tba_taken_over), %l2
	ld	[%l2+%lo(tba_taken_over)], %l3
	brz	%l3, 1f				! is the tba_taken_over = 1 ?
	rdpr	%wstate, %l5			! save %wstate
	wrpr	%g0, WSTATE_MIXED, %wstate	! Set wstate to mixed-mode
1:	srl	%i0, 0, %i0			! zero extend handler addr.
	srl	%i1, 0, %o0			! zero extend first argument.
	set	1f, %o1				! zero extend pc
	jmp	%o1
	srl	%sp, 0, %sp			! zero extend sp
1:	rdpr	%pstate, %l4			! Get the present pstate value
	wrpr	%l4, PSTATE_AM, %pstate		! Set PSTATE_AM = 0
	jmpl	%i0, %o7			! Call cif handler
	sub	%sp, V9BIAS64, %sp		! delay; Now a 64 bit frame
	add	%sp, V9BIAS64, %sp		! back to a 32-bit frame
	rdpr	%pstate, %l4			! Get the present pstate value
	wrpr	%l4, PSTATE_AM, %pstate		! Set PSTATE_AM = 1
	brz	%l3, 1f				! is the tba_taken_over = 1
	nop
	wrpr	%g0, %l5, %wstate		! restore wstate
1:	ret					! Return result ...
	restore	%o0, %g0, %o0			! delay; result in %o0
	SET_SIZE(client_handler)
#endif	/* lint */

/*
 * The IEEE 1275-1994 callback handler for a 64-bit SPARC V9 PROM
 * calling a 32 bit client program. The PROM calls us with a 64 bit
 * stack and a pointer to a client interface argument array in %o0.
 * The called code returns 0 if the call succeeded (i.e. the service
 * name exists) or -1 if the call failed. NOTE: All addresses are
 * in the range 0..2^^32-1
 *
 * This code is called as trusted subroutine of the firmware, and
 * is called with %tba pointing to the boot firmware's trap table.
 * All of the prom's window handlers are mixed mode handlers.
 */

#if defined(lint)

int
callback_handler(cell_t *arg_array)
{
	extern int vx_handler(cell_t *arg_array);
	return (vx_handler(arg_array));
}

#else	/* lint */

	ENTRY_NP(callback_handler)
	!
	! We assume we are called with a 64 bit stack with PSTATE_AM clear
	!
	save	%sp, -SA64(MINFRAME64), %sp	! 64 bit save
	rdpr	%wstate, %l5			! save %wstate
	wrpr	%g0, WSTATE_MIXED, %wstate	! Set wstate to mixed-mode
	add	%sp, V9BIAS64, %sp		! switch to a 32 bit stack

	rdpr	%pstate, %l0			! save %pstate
	wrpr	%l0, PSTATE_AM, %pstate		! Set PSTATE_AM

	call	vx_handler			! vx_handler(void **arg_array)
	mov	%i0, %o0			! delay; argument array
	sra	%o0, 0, %i0			! sign extend result

	set	1f, %l2				! zero extend %pc
	jmp	%l2
	srl	%sp, 0, %sp			! delay; zero extend %sp
1:	wrpr	%g0, %l0, %pstate		! restore %pstate
	wrpr	%g0, %l5, %wstate		! restore %wstate

	ret					! return result in %o0
	restore					! back to a 64 bit stack
	SET_SIZE(callback_handler)

#endif	/* lint */


#if defined(lint)
/*
 * These need to be defined somewhere to lint and there is no "hicore.s"...
 */
char etext[1], end[1];
#endif	/* lint*/

#ifdef TRAPTRACE

#if defined (lint)
void
win_32bit_trace()
{
}

#else /* lint */
	ENTRY_NP(win_32bit_trace)
	TRACE_32bit_TRAP
	jmp	%l0 + 8
	nop
	SET_SIZE(win_32bit_trace)

#endif /* lint */


#if defined (lint)
void
win_64bit_trace()
{
}

#else /* lint */
	ENTRY_NP(win_64bit_trace)
	TRACE_64bit_TRAP
	jmp	%l0 + 8
	nop
	SET_SIZE(win_64bit_trace)
#endif /* lint */

#endif TRAPTRACE

