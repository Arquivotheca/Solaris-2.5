/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)locore.s	1.178	95/10/30 SMI"

#if defined(lint)
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/privregs.h>
#endif	/* lint */

#include <sys/param.h>
#include <sys/vmparam.h>
#include <sys/errno.h>
#include <sys/asm_linkage.h>
#include <sys/buserr.h>
#include <sys/clock.h>
#include <sys/intreg.h>
#include <sys/async.h>
#include <sys/memerr.h>
#include <sys/eeprom.h>
#include <sys/debug/debug.h>
#include <sys/mmu.h>
#include <sys/pcb.h>
#include <sys/psr.h>
#include <sys/pte.h>
#include <sys/machpcb.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/scb.h>
#include <sys/auxio.h>
#include <sys/devaddr.h>
#include <sys/machthread.h>
#include <sys/machlock.h>
#include <sys/x_call.h>
#include <sys/physaddr.h>
#include <sys/msacct.h>
#include <sys/mutex_impl.h>
#include <sys/gprof.h>

#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

#if defined(lint)

#include <sys/thread.h>
#include <sys/time.h>

struct pte Sysmap[1];
char Sysbase[1];
char Syslimit[1];
char E_Sysbase[1];
char E_Syslimit[1];
char DVMA[1];

#else	/* lint */

#include "assym.s"

#include <sys/iommu.h>

/*
 * Lab workaround for ROSS625 A0/A1 rev level %psr bug
 */

#define ROSS625_PSR_NOP()	nop

#ifdef IOC
#include <sys/iocache.h>
#endif IOC

	.seg	".data"

	.global	utimersp
utimersp:
	.word	0

	.global sync_cpus
	.word   0

!
! MINFRAME and the register offsets in struct regs must add up to allow
! double word loading of struct regs. MPCB_WBUF must also be aligned.
!
#if (MINFRAME & 7) == 0 || (G2 & 1) == 0 || (O0 & 1) == 0
ERROR - struct regs not aligned
#endif
#if (MPCB_WBUF & 7)
ERROR - pcb_wbuf not aligned
#endif

/*
 * Absolute external symbols.
 * On the sun4m we put the message buffer in the third and fourth pages.
 * We set things up so that the first 2 pages of KERNELBASE is illegal
 * to act as a redzone during copyin/copyout type operations. One of
 * the reasons the message buffer is allocated in low memory to
 * prevent being overwritten during booting operations (besides
 * the fact that it is small enough to share pages with others).
 */

	.global	DVMA, msgbuf, t0stack

PROM	= 0xFFE00000			! address of prom virtual area
DVMA	= DVMABASE			! address of DVMA area
msgbuf	= V_MSGBUF_ADDR			! address of printf message buffer

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

	.global t0
	.align	PTR24_ALIGN		! alignment for mutex.
	.type	t0, #object
t0:
	.skip	THREAD_SIZE		! thread 0
	.size	t0, THREAD_SIZE
	.align	8

/*
 * Trap tracing. If TRAPTRACE is defined, every trap records info
 * in a circular buffer. Define TRAPTRACE in Makefile.sun4m
 */
#ifdef	TRAPTRACE

#define	TRAP_TSIZE	(TRAP_ENT_SIZE*256)

	.global	trap_trace_ctl
	.global	trap_tr0, trap_tr1, trap_tr2, trap_tr3

_trap_last_intr:
	.word	0
	.word	0
	.word	0
	.word	0

_trap_last_intr2:
	.word	0
	.word	0
	.word	0
	.word	0
	.align	16

trap_trace_ctl:
	.word	trap_tr0		! next	CPU 0
	.word	trap_tr0		! first
	.word	trap_tr0 + TRAP_TSIZE	! limit
	.word	0			! junk for alignment of prom dump

	.word	trap_tr1		! next	CPU 1
	.word	trap_tr1		! first
	.word	trap_tr1 + TRAP_TSIZE	! limit
	.word	0			! junk for alignment of prom dump

	.word	trap_tr2		! next	CPU 2
	.word	trap_tr2		! first
	.word	trap_tr2 + TRAP_TSIZE	! limit
	.word	0			! junk for alignment of prom dump

	.word	trap_tr3		! next	CPU 3
	.word	trap_tr3		! first
	.word	trap_tr3 + TRAP_TSIZE	! limit
	.word	0			! junk for alignment of prom dump
	.align	16

trap_tr0:
	.skip	TRAP_TSIZE

trap_tr1:
	.skip	TRAP_TSIZE

trap_tr2:
	.skip	TRAP_TSIZE

trap_tr3:
	.skip	TRAP_TSIZE

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

/*
 * System software page tables
 */
#define vaddr(x)	((((x)-Sysmap)/4)*NBPG + SYSBASE)
#define SYSMAP(mname, vname, npte)	\
	.global	mname;			\
	.type	mname,#object;		\
	.size	mname,(4*npte);		\
mname:	.skip	(4*npte);		\
	.global	vname;			\
	.type	vname,#object;		\
	.size	vname,0;		\
vname = vaddr(mname);
	SYSMAP(Sysmap	 ,Sysbase	,SYSPTSIZE	)
	SYSMAP(ESysmap	 ,Syslimit	,0		) ! must be last

	.global	Syssize
	.type	Syssize, #object
	.size	Syssize, 0
Syssize = (ESysmap-Sysmap)/4

	.global	E_Sysbase
E_Sysbase = Sysbase
	.global	E_Syslimit
E_Syslimit = Syslimit

#ifdef	NOPROM
	.global availmem
availmem:
	.word	0
#endif	NOPROM

/*
 * pointer to kernel symbol table info which is used for debugging and
 * for kvm_nlist so that it can read symbols in modules.  We put it here
 * so that it doesn't move from kernel to kernel since this is the first
 * module linked and data before this point doesn't change that often.
 */

	DGDEF(kobj_symbol_info)
	.word	0

/*
 * Opcodes for instructions in PATCH macros
 */
#define MOVPSRL0	0xa1480000
#define MOVL4		0xa8102000
#define BA		0x10800000
#define	NO_OP		0x01000000
#define	SETHI		0x27000000
#define	JMP		0x81c4e000

/*
 * Trap vector macros.
 *
 * A single kernel that supports machines with differing
 * numbers of windows has to write the last byte of every
 * trap vector with NW-1, the number of windows minus 1.
 * It does this at boot time after it has read the implementation
 * type from the psr.
 *
 * NOTE: All trap vectors are generated by the following macros.
 * The macros must maintain that a write to the last byte to every
 * trap vector with the number of windows minus one is safe.
 */
#define TRAP(H) \
    sethi %hi(H),%l3; jmp %l3+%lo(H); mov %psr,%l0; nop;

/* the following trap uses only the trap window, you must be prepared */
#define WIN_TRAP(H) \
    mov %psr,%l0; sethi %hi(H),%l6; jmp %l6+%lo(H); mov %wim,%l3;

#define WIN_INDIRECT_TRAP(PH) \
    sethi %hi(PH),%l3; ld [%l3+%lo(PH)], %l3; jmp %l3; mov %wim,%l3;

#define SYS_TRAP(T) \
    mov %psr,%l0; sethi %hi(_sys_trap),%l3; jmp %l3+%lo(_sys_trap); mov (T),%l4;

#define TRAP_MON(T) \
    mov %psr,%l0; sethi %hi(trap_mon),%l3; jmp %l3+%lo(trap_mon); mov (T),%l4;

#define GETPSR_TRAP() \
	mov %psr, %i0; jmp %l2; rett %l2+4; nop;

#define FAST_TRAP(T) \
	sethi %hi(T), %l3; jmp %l3 + %lo(T); nop; nop;

#define BAD_TRAP	SYS_TRAP((. - scb) >> 4);

/*
 * Tracing traps.  For speed we don't save the psr;  all this means is that
 * the condition codes come back different.  This is OK because these traps
 * are only generated by the trace_[0-5]() wrapper functions, and the
 * condition codes are volatile across procedure calls anyway.
 * If you modify any of this, be careful.
 */
#ifdef	TRACE
#define	TRACE_TRAP(H) \
	sethi %hi(H), %l3; jmp %l3 + %lo(H); nop; nop;
#else	/* TRACE */
#define	TRACE_TRAP(H) \
	BAD_TRAP;
#endif	/* TRACE */

/*
 * GETSIPR: read system interrupt pending register
 */
#define	GETSIPR(r) \
	sethi	%hi(v_sipr_addr), r			;\
	ld	[r + %lo(v_sipr_addr)], r		;\
	ld	[r], r


#define PATCH_ST(T, V) \
	set	scb, %g1; \
	set	MOVPSRL0, %g2; \
	st	%g2, [%g1 + ((V)*16+0*4)]; \
	sethi	%hi(_sys_trap), %g2; \
	srl	%g2, %l0, %g2; \
	set     SETHI, %g3; \
	or      %g2, %g3, %g2; \
	st      %g2, [%g1 + ((V)*16+1*4)]; \
	set     JMP, %g2; \
	or      %g2, %lo(_sys_trap), %g2; \
	st      %g2, [%g1 + ((V)*16+2*4)]; \
	set	MOVL4 + (T), %g2; \
	st	%g2, [%g1 + ((V)*16+3*4)];

#endif	/* lint */

/*
 * Trap vector table.
 * This must be the first text in the boot image.
 *
 * When a trap is taken, we vector to KERNELBASE+(TT*16) and we have
 * the following state:
 *	2) traps are disabled
 *	3) the previous state of PSR_S is in PSR_PS
 *	4) the CWP has been incremented into the trap window
 *	5) the previous pc and npc is in %l1 and %l2 respectively.
 *
 * Registers:
 *	%l0 - %psr immediately after trap
 *	%l1 - trapped pc
 *	%l2 - trapped npc
 *	%l3 - wim (sys_trap only)
 *	%l4 - system trap number (sys_trap only)
 *	%l6 - number of windows - 1
 *	%l7 - stack pointer (interrupts and system calls)
 *
 * Note: UNIX receives control at vector 0 (trap)
 */

#if defined(lint)

void
_start(void)
{}

#else /* lint */

	ENTRY_NP2(_start, scb)

	TRAP(.entry);				! 00 - reset
	SYS_TRAP(T_FAULT | T_TEXT_FAULT);	! 01 - instruction access
	WIN_TRAP(ill_instr_trap);		! 02 - illegal instruction
	SYS_TRAP(T_PRIV_INSTR);			! 03 - privileged instruction
	SYS_TRAP(T_FP_DISABLED);		! 04 - floating point disabled
#ifdef TRAPTRACE
	WIN_TRAP(window_overflow_trace);	! 05 - register window overflow
	WIN_TRAP(window_underflow_trace);	! 06 - register window underflow
#else /* TRAPTRACE */
	WIN_INDIRECT_TRAP(v_window_overflow);	! 05 - register window overflow
	WIN_INDIRECT_TRAP(v_window_underflow);	! 06 - register window underflow
#endif /* TRAPTRACE */
	SYS_TRAP(T_ALIGNMENT);			! 07 - alignment fault
	SYS_TRAP(T_FP_EXCEPTION);		! 08 - floating point exc.
	SYS_TRAP(T_FAULT | T_DATA_FAULT);	! 09 - data fault
	SYS_TRAP(T_TAG_OVERFLOW);		! 0A - tag_overflow
	BAD_TRAP;				! 0B
	BAD_TRAP;				! 0C
	BAD_TRAP;				! 0D
	BAD_TRAP;				! 0E
	BAD_TRAP;				! 0F
	BAD_TRAP;				! 10
	SYS_TRAP(T_INTERRUPT | 1);		! 11
	SYS_TRAP(T_INTERRUPT | 2);		! 12
	SYS_TRAP(T_INTERRUPT | 3);		! 13
	SYS_TRAP(T_INTERRUPT | 4);		! 14
	SYS_TRAP(T_INTERRUPT | 5);		! 15
	SYS_TRAP(T_INTERRUPT | 6);		! 16
	SYS_TRAP(T_INTERRUPT | 7);		! 17
	SYS_TRAP(T_INTERRUPT | 8);		! 18
	SYS_TRAP(T_INTERRUPT | 9);		! 19
	SYS_TRAP(T_INTERRUPT | 10);		! 1A
	SYS_TRAP(T_INTERRUPT | 11);		! 1B
	SYS_TRAP(T_INTERRUPT | 12);		! 1C
	TRAP(level13_trap);			! 1D
	TRAP(level14_trap);			! 1E
	SYS_TRAP(T_INTERRUPT | 15);		! 1F

/*
 * The rest of the traps in the table up to 0x80 should 'never'
 * be generated by hardware.
 */
#if defined(MPSAS) && defined(SAS)
	TRAP(entry0);				! 20: simulator entry for cpu 0
	TRAP(entry1);				! 21: simulator entry for cpu 1
	TRAP(entry2);				! 22: simulator entry for cpu 2
	TRAP(entry3);				! 23: simulator entry for cpu 3
#else
	BAD_TRAP;				! 20
        SYS_TRAP(T_FAULT | T_TEXT_FAULT);       ! 21 - instruction access error
	BAD_TRAP;				! 22
	BAD_TRAP;				! 23
#endif defined(MPSAS) && defined(SAS)
	BAD_TRAP;				! 24 - coprocessor disabled
/* 
 * IFLUSH
 * the following trap is taken for a FLUSH instructions, but only
 * if such traps are enabled (actually NOT disabled). This is the
 * situation, at least for the Ross 625 hyperSparc. If this trap 
 * happens for a module which doesn't handle this trap, then a
 * BAD_TRAP's effect will happen
 */
	SYS_TRAP(T_UNIMP_FLUSH);		! 25 - IFLUSH - unimp. flush
	BAD_TRAP;				! 26
	BAD_TRAP;				! 27
	BAD_TRAP;				! 28 - coprocessor exception
        SYS_TRAP(T_FAULT | T_DATA_FAULT);       ! 29 - data access error
	SYS_TRAP(T_IDIV0);			! 2A - integer divide by 0
	SYS_TRAP(T_FAULT | T_DATA_STORE);	! 2B - data store exception
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 2C - 2F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 30 - 33
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 34 - 37
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 38 - 3B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 3C - 3F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 40 - 43
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 44 - 47
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 48 - 4B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 4C - 4F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 50 - 53
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 54 - 57
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 58 - 5B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 5C - 5F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 60 - 63
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 64 - 67
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 68 - 6B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 6C - 6F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 70 - 73
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 74 - 77
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 78 - 7B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 7C - 7F

/*
 * User generated traps
 */
	TRAP(syscall_trap_4x)			! 80 - SunOS4.x system call
	SYS_TRAP(T_BREAKPOINT);			! 81 - user breakpoint
	SYS_TRAP(T_DIV0);			! 82 - divide by zero
	WIN_TRAP(fast_window_flush);		! 83 - flush windows
	TRAP(.clean_windows);			! 84 - clean windows
        BAD_TRAP;				! 85 - range check
	TRAP(.fix_alignment)			! 86 - do unaligned references
	BAD_TRAP;				! 87
	TRAP(syscall_trap);			! 88 - system call
	TRAP(.set_trap0_addr);			! 89 - set trap0 address
	BAD_TRAP; BAD_TRAP; 			! 8A - 8B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 8C - 8F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 90 - 93
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 94 - 97
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 98 - 9B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! 9C - 9F
	TRAP(.getcc);				! A0 - get condition codes
	TRAP(.setcc);				! A1 - set condition codes
	GETPSR_TRAP()				! A2 - get psr
	TRAP(.setpsr)				! A3 - set psr (some fields)
	FAST_TRAP(get_timestamp)		! A4 - get timestamp
	FAST_TRAP(get_virtime)			! A5 - get lwp virtual time
	BAD_TRAP;				! A6
	FAST_TRAP(get_hrestime)			! A7 - get hrestime
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! A8 - AB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! AC - AF
	TRACE_TRAP(trace_trap_0)		! B0 - trace, no data
	TRACE_TRAP(trace_trap_1)		! B1 - trace, 1 data word
	TRACE_TRAP(trace_trap_2)		! B2 - trace, 2 data words
	TRACE_TRAP(trace_trap_3)		! B3 - trace, 3 data words
	TRACE_TRAP(trace_trap_4)		! B4 - trace, 4 data words
	TRACE_TRAP(trace_trap_5)		! B5 - trace, 5 data words
	BAD_TRAP;				! B6 - trace, reserved
	TRACE_TRAP(trace_trap_write_buffer)	! B7 - trace, atomic buf write
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP;	! B9 - BB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! BC - BF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! C0 - C3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! C4 - C7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! C8 - CB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! CC - CF
	BAD_TRAP;				! D0
	BAD_TRAP;				! D1
	BAD_TRAP;				! D2 - mpsas magic trap "SYSCALL"
	BAD_TRAP;				! D3 - mpsas magic trap "IDLE"
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! D4 - D7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! D8 - DB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! DC - DF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! E0 - E3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! E4 - E7
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! E8 - EB
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! EC - EF
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! F0 - F3
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! F4 - F7
	BAD_TRAP;				! F8
	BAD_TRAP;				! F9
	BAD_TRAP;				! FA - prom re-entry?
	BAD_TRAP;				! FB
	BAD_TRAP;				! FC - mpsas magic trap "INC"
	BAD_TRAP;				! FD - mpsas magic trap "OUTC"
	BAD_TRAP;				! FE
	TRAP_MON(0xff)				! FF - prom re-entry?


/*
 * The number of windows, set once on entry.  Note that it is
 * in the text segment so that it is write protected in startup().
 */
	.global nwindows
nwindows:
	.word   8

/*
 * The number of windows - 1, set once on entry.  Note that it is
 * in the text segment so that it is write protected in startup().
 */
	.global nwin_minus_one
nwin_minus_one:
	.word   7

/*
 * The window mask, set once on entry.  Note that it is
 * in the text segment so that it is write protected in startup().
 */
	.global winmask
winmask:
	.word	8

	.global	afsrbuf
afsrbuf:
	.word	0,0,0,0

#if defined(MPSAS) && defined(SAS)
/************************************************************************
 *
 *	Simulator processor startup code goes here to set things up so
 *	when the kernel gets control at "entry" things are all as it
 *	expects. one entry point per possible processor, so we can
 *	sidestep how to figure out which processor we are.
 *
 *	Thanks to mce@east, most of the work that entry0 was going to
 *	do is now done by loading the "fakeprom" memory images and
 *	setting things up in the simulator initilization script.
 *
 *	Alternate processors need to be held off from entering vmunix code
 *	outside this code area until the kernel has finished
 *	initializing the world and asks for a cpu by name.
 *==========
 * NOTE: until we have real Open Boot PROMs that support starting
 * alternate processors, we need this stuff here to build PSMP
 * kernels for real systems. Revisit when switching to OBP.
 */

entry0:
	b	.entry
	nop

entry1:
	b	romloop
	add	%g0, 0, %o0

entry2:
	b	romloop
	add	%g0, 8, %o0

entry3:
	b	romloop
	add	%g0, 16, %o0


/*************************************************************************
 *	Services normally acquired from the boot prom that would not
 *	otherwise be available from the kernel are picked up here, and
 *	we do whatever is necessary to service them.
 */

do_enter:
	ta	0xc8

do_exit:
	ta	0xc8

do_startcpu:				! (cpuid, pc, ctx)
	set	startboxes-KERNELBASE, %g1
	sub	%o0, 1, %o0		! first is #1
	sll	%o0, 3, %o0		! eight bytes per cpu
	add	%g1, %o0, %g1		! where to put req pc
	add	%g1, 4, %g4		! where to put req ctpr
	sta	%o2, [%g4]ASI_MEM	! set ctpr
	retl
	sta	%o1, [%g1]ASI_MEM	! set pc

/************************************************************************
 *	Code executed by processors that have not yet been told
 *	to go execute real kernel code: just wait around until
 *	the destination address is set, modify the context table
 *	pointer and head off to the wild blue yonder.
 */

romloop:		! (cpuid)
	set	startboxes-KERNELBASE, %g1
	add	%g1, %o0, %g1		! ptr to starting address
	add	%g1, 4, %g4		! ptr to ctx tbl ptr
1:
        ld      [%g1], %g3              ! get start address
        tst     %g3                     ! if still zero,
        beq     1b                      ! loop back
        nop

	ld	[%g4], %g4		! get context table address
	srl	%g4, 4, %g4		! shift into place for CTPR

/*
 *	Currently, this code works only for simulations that look like
 *	a single SRMMU. If no 1-1 mappings exist in the new context,
 *	then we better take a TLB hit when fetching the "nop" below.
 */
	set	RMMU_CTP_REG, %g5
	sta	%g4, [%g5]ASI_MOD
 
        jmp     %g3                     ! vector out to the start address
        nop

/**********************************************************************************
 *	support data for the code above
 */
	.seg	".data"
startboxes:
	.word 0, 0, 0, 0, 0, 0
#define	ROMSTUB(name)	\
	.global	op_/**/name	;	\
op_/**/name:			;	\
	.word	do_/**/name

ROMSTUB(enter)
ROMSTUB(exit)

ROMSTUB(startcpu)

	.seg	".text"
#endif defined(MPSAS) && defined(SAS)

/*
 * System initialization
 *
 * Our contract with the boot prom specifies that the MMU is on
 * and the first 16 meg of memory is mapped with a level-1 pte.
 * We are called with romp in %o0 and dvec in %o1; we start
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
.entry:
	!
	! Stash away our arguments.
	!
	mov	%o0, %g7		! save arg (romp) until bss is clear
	mov	%o1, %g6		! save dvec
	mov	%o2, %g5		! save bootops

	mov	0x02, %wim		! setup wim
	set	PSR_S|PSR_PIL|PSR_ET, %g1
	mov	%g1, %psr		! initialize psr: supervisor, splmax
	nop				! and leave traps enabled for monitor
	nop				! psr delay
	nop				! psr delay

	sethi	%hi(romp), %g1
	st	%g7, [%g1 + %lo(romp)]

	sethi	%hi(dvec), %g1
	st	%g6, [%g1 + %lo(dvec)]

	sethi	%hi(bootops), %g1
	st	%g5, [%g1 + %lo(bootops)]

	!
	! Patch vector 0 trap to "zero" if it happens again.
	!
	PATCH_ST(T_ZERO, 0)

	!
	! Find the the number of implemented register windows.
	! Using %g4 here is OK, as it doesn't interfere with fault info
	! stored in %g4, since this is only executed during startup.
	!
	! The last byte of every trap vector must be equal to
	! the number of windows in the implementation minus one.
	! The trap vector macros (above) depend on it!
	!
	mov	%g0, %wim		! note psr has cwp = 0

	sethi	%hi(nwin_minus_one), %g4 ! initialize pointer to nwindows - 1

	save				! decrement cwp, wraparound to NW-1
	mov	%psr, %g1
	and	%g1, PSR_CWP, %g1       ! we now have nwindows-1
	restore				! get back to orignal window
	mov	2, %wim			! reset initial wim

	st	%g1, [%g4 + %lo(nwin_minus_one)] ! initialize nwin_minus_one

	inc	%g1			! initialize the nwindows variable
	sethi	%hi(nwindows), %g4	! initialzie pointer to nwindows
	st	%g1, [%g4 + %lo(nwindows)] ! initialize nwin_minus_one

	!
	! Now calculate winmask.  0's set for each window.
	!
	dec	%g1
	mov	-2, %g2
	sll	%g2, %g1, %g2
	sethi	%hi(winmask), %g4
	st	%g2, [%g4 + %lo(winmask)]

#ifdef	SAS
	!
	! If we are in the simulator ask it for the memory size.
	! probably should shift to having the right stuff in romp.
	!
	lda [%g0]ASI_SAS, %l0		! Load from 0 in ASI_SAS (0x30) will
					! return the available amount of memory
	sethi	%hi(availmem), %g1
	st	%l0, [%g1 + %lo(availmem)]
#endif	SAS

	mov	%tbr, %l4		! save monitor's tbr
	bclr	0xfff, %l4		! remove tt

	!
	! Save monitor's level14 clock interrupt vector code.
	! and duplicate it to our area.
	!
	or	%l4, TT(T_INT_LEVEL_14), %o0
	set	mon_clock14_vec, %o1
	ldd	[%o0], %o2
	ldd	[%o0 + 8], %o4
	std	%o2, [%o1]
	std	%o4, [%o1 + 8]

	!
	! Save monitor's breakpoint vector code. 
	!
	or	%l4, TT(ST_MON_BREAKPOINT + T_SOFTWARE_TRAP), %o0
	set	mon_breakpoint_vec, %o1
	ldd	[%o0], %o2
	ldd	[%o0 + 8], %o4
	std	%o2, [%o1]
	std	%o4, [%o1 + 8]

	!
	! Switch to our trap base register
	!
	set	scb, %g1		! setup trap handler
	mov	%g1, %tbr

	!
	! Zero thread 0's stack.
	!
	set	t0stack, %g1		! setup kernel stack pointer
	set	T0STKSZ, %l1
0:	subcc	%l1, 4, %l1
	bnz	0b
	clr	[%g1 + %l1]

	!
	! Setup thread 0's stack.
	!
	set	T0STKSZ, %g2
	add	%g1, %g2, %sp
	sub	%sp, SA(MPCBSIZE), %sp
	mov	0, %fp

	!
	! Dummy up fake user registers on the stack,
	! so we can dive into user code as if we were
	! returning from a trap.
	!
	set	USRSTACK-WINDOWSIZE, %g1
	st	%g1, [%sp + MINFRAME + SP*4] ! user stack pointer
	set	PSL_USER, %g1
	st	%g1, [%sp + MINFRAME + PSR*4] ! psr
	set	USRTEXT, %g1
	st	%g1, [%sp + MINFRAME + PC*4] ! pc
	add	%g1, 4, %g1
	st	%g1, [%sp + MINFRAME + nPC*4] ! npc

	!
	! Initialize global thread register.
	!
	set	t0, THREAD_REG
	mov	1, FLAG_REG

	!
	! Fill in enough of the cpu structure so that over/underflow handlers
	! work!
	!
	set	cpu, %o0			! &cpu[0]
	ld	[%o0], %o0			! cpu[0]
	st	%o0, [THREAD_REG + T_CPU]	! threadp()->t_cpu = cpu[0]
	st	THREAD_REG, [%o0 + CPU_THREAD]	! cpu[0]->cpu_thread = threadp()

	!
	! It's now safe to call other routines.  Can't do so earlier
	! because %tbr need to be set up if profiling is to work.
	!
	call    module_setup		! setup correct module routines
	lda     [%g0]ASI_MOD, %o0	! find module type

	!
	! Clear status of any registers that might be latching errors.
	! Note: mmu_getsyncflt assumes that it can clobber %g1 and %g4.
	!
	call	mmu_getsyncflt
	nop
	sethi	%hi(afsrbuf), %o0
	call	mmu_getasyncflt		! clear module AFSR
	or	%o0, %lo(afsrbuf), %o0

	!
	! Call mlsetup with address of prototype user registers
	! and romp.
	sethi	%hi(romp), %o1
	ld	[%o1 + %lo(romp)], %o1
	call	mlsetup
	add	%sp, REGOFF, %o0

	!
	! Enable interrupts in the SIPR by writing all-ones to
	! the sipr mask "clear" location.
	!
	sethi	%hi(v_sipr_addr), %g1
	ld	[%g1 + %lo(v_sipr_addr)], %g1
	sub	%g0, 1, %g2		! clear all mask bits by writing ones
	st	%g2, [%g1 + IR_CLEAR_OFFSET] ! to the CLEAR location for itmr
	!
	! Now call main.  We will return as process 1 (init).
	!
	call	main
	nop

	!
	! Proceed as if this was a normal user trap.
	!
	b,a	_sys_rtt			! fake return from trap
	nop
	SET_SIZE(_start)
	SET_SIZE(scb)

#ifdef	TRAPTRACE
window_overflow_trace:
	!
	! make trace entry - helps in debugging watchdogs
	!
	TRACE_PTR(%l5, %l3)		! get trace pointer
	mov	%tbr, %l3
	st	%l3, [%l5 + TRAP_ENT_TBR]
	st	%l0, [%l5 + TRAP_ENT_PSR]
	st	%l1, [%l5 + TRAP_ENT_PC]
	st	%fp, [%l5 + TRAP_ENT_SP]
	st	%g7, [%l5 + TRAP_ENT_G7]
	add	%l5, TRAP_ENT_TR, %l4	! pointer to trace word
	st	%g0, [%l4]
	TRACE_NEXT(%l5, %l3, %l7)	! set new trace pointer
	b	_window_overflow
	mov	%wim, %l3

window_underflow_trace:
	!
	! make trace entry - helps in debugging watchdogs
	!
	TRACE_PTR(%l5, %l3)		! get trace pointer
	mov	%tbr, %l3
	st	%l3, [%l5 + TRAP_ENT_TBR]
	st	%l0, [%l5 + TRAP_ENT_PSR]
	st	%l1, [%l5 + TRAP_ENT_PC]
	st	%fp, [%l5 + TRAP_ENT_SP]
	st	%g7, [%l5 + TRAP_ENT_G7]
	add	%l5, TRAP_ENT_TR, %l4	! pointer to trace word
	st	%g0, [%l4]
	TRACE_NEXT(%l5, %l3, %l7)	! set new trace pointer
	b	_window_underflow
	mov	%wim, %l3

#endif	/* TRAPTRACE */

	.type	.set_trap0_addr, #function
.set_trap0_addr:
	CPU_ADDR(%l5, %l7)		! load CPU struct addr to %l5 using %l7
	ld	[%l5 + CPU_THREAD], %l5	! load thread pointer
	ld	[%l5 + T_LWP], %l5	! load klwp pointer
	andn	%g1, 3, %g1		! force alignment
	st	%g1, [%l5 + PCB_TRAP0]	! lwp->lwp_pcb.pcb_trap0addr
	jmp	%l2			! return
	rett	%l2 + 4
	SET_SIZE(.set_trap0_addr)

#endif	/* lint */


/*
 * Some (shady) SPARC implementations that don't implement 'iflush'
 * give an illegal instruction trap (sigh) so we convert it
 * to a nop so that it doesn't gratuitously break userland.
 */

#if defined(lint)

void
ill_instr_trap(void)
{}

#else	/* lint */

	ENTRY_NP(ill_instr_trap)
/*
 *	the following lda will not cause a mapping fault because
 *	the translation still lives in the TLB.  X-calls to flush
 * 	the TLB are blocked on this CPU since traps were disabled
 *	at the time of the illegal instruction fault.
 */
	
	btst	PSR_PS, %l0		! User or Supervisor Illegal Instruction
	bne,a	1f
	lda	[%l1]ASI_SI, %l4	! get Supervisor illegal instruction

	! Loading the user instruction that caused the trap.  We need
	! to do this with the no-fault bit set because the TLB entry
	! for the instruction may have been replaced by the trap code.
	set	RMMU_FSR_REG, %l5	! clear any old faults out
	lda	[%l5]ASI_MOD, %g0	! of the SFSR.

	lda	[%g0]ASI_MOD, %l6	! turn on no-fault bit in
	or	%l6, MMCREG_NF, %l6	! mmu control register to
	sta	%l6, [%g0]ASI_MOD	! prevent taking a fault.
	
	lda	[%l1]ASI_UI, %l4	! get User illegal instruction

	andn	%l6, MMCREG_NF, %l6	! turn off no-fault bit
	sta	%l6, [%g0]ASI_MOD

	set	RMMU_FAV_REG, %l6
	lda	[%l6]ASI_MOD, %l6	! read SFAR
	lda	[%l5]ASI_MOD, %l5	! read SFSR

	btst    SFSREG_FAV, %l5         ! did a fault occur?
	bnz,a	_sys_trap		! yes, try again with traps enabled
	mov	T_UNIMP_INSTR, %l4
1:
	set	0xc1f80000, %l6		! mask for the iflush instruction
	and	%l4, %l6, %l4
	set	0x81d80000, %l6		! and check the opcode
	cmp	%l4, %l6		! iflush -> nop
	bne,a	1f
	mov	T_UNIMP_INSTR, %l4
	mov	%l0, %psr		! install old PSR_CC
	nop; nop
	jmp	%l2			! skip illegal iflush instruction
	rett	%l2 + 4
1:
	b	_sys_trap
	nop
	SET_SIZE(ill_instr_trap)

#endif	/* lint */

/*
 * Generic system trap handler.
 *
 * XXX - why two names? (A vestige from the compiler change?) _sys_trap
 *	 is only called from within this module.
 *
 * Register Usage:
 *	%l0 = trap %psr
 *	%l1 = trap %pc
 *	%l2 = trap %npc
 *	%l3 = %wim
 *	%l4 = trap type
 *	%l5 = CWP
 *	%l6 = nwindows - 1
 *	%l7 = stack pointer
 *
 *	%g4 = scratch
 *	%g5 = lwp pointer
 *	%g6 = FLAG_REG
 *	%g7 = thread pointer
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
	sethi   %hi(nwin_minus_one), %l6
	ld      [%l6 + %lo(nwin_minus_one)], %l6
#ifdef TRAPTRACE
	!
	! make trace entry - helps in debugging watchdogs
	!
	TRACE_PTR(%l5, %l3)		! get trace pointer
	mov	%tbr, %l3
	st	%l3, [%l5 + TRAP_ENT_TBR]
	st	%l0, [%l5 + TRAP_ENT_PSR]
	st	%l1, [%l5 + TRAP_ENT_PC]
	st	%fp, [%l5 + TRAP_ENT_SP]
	st	%g7, [%l5 + TRAP_ENT_G7]
	mov	%psr, %l3
	st	%l3, [%l5 + 0x14]
	TRACE_NEXT(%l5, %l3, %l7)	! set new trace pointer
#endif	/* TRAPTRACE */
	!
	! Prepare to go to C (batten down the hatches).
	!
	mov	0x01, %l5		! CWM = 0x01 << CWP
	sll	%l5, %l0, %l5
	mov	%wim, %l3		! get WIM
	btst	PSR_PS, %l0		! test pS
	bz	.st_user		! branch if user trap
	btst	%l5, %l3		! delay slot, compare WIM and CWM

	!
	! Trap from supervisor.
	! We can be either on the system stack or interrupt stack.
	!
	sub	%fp, MINFRAME+REGSIZE, %l7 ! save sys globals on stack
	SAVE_GLOBALS(%l7 + MINFRAME)
	SAVE_OUTS(%l7 + MINFRAME)

#ifdef TRACE
	! We do this now, rather than at the very start of sys_trap,
	! because the _SAVE_GLOBALS above gives us free scratch registers.
	!
	TRACE_SYS_TRAP_START(%l4, %g1, %g2, %g3, %g4, %g5)
	btst	%l5, %l3		! retest: compare WIM and CWM
#endif	/* TRACE */

	!
	! restore %g7 (THREAD_REG) in case we came in from the PROM or kadb
	!
	CPU_ADDR(%l6, THREAD_REG)	! load CPU struct addr to %l6 using %g7
	ld	[%l6 + CPU_THREAD], THREAD_REG	! load thread pointer
	!
	! Call mmu_getsyncflt to get the synchronous fault status
	! register and the synchronous fault address register stored in
	! the per-CPU structure.
	! 
	! The reason for reading these synchronous fault registers now is
	! that the window overflow processing that we are about to do may
	! also cause another fault.
	!
	! Note that mmu_getsyncflt assumes that it can clobber %g1 and %g4.
	!
	btst	T_FAULT, %l4
	bz,a	2f
	clr	[%l6 + CPU_SYNCFLT_STATUS]	! delay

	mov	%o7, %g3		! save %o7 in %g3
	call	mmu_getsyncflt
	nop
	mov	%g3, %o7		! restore %o7.
2:
#ifdef TRAPWINDOW
	!
	! store the window at the time of the trap into a static area.
	!
	set	trap_window, %g1
	mov	%wim, %g2
	st	%g2, [%g1+96]
	mov	%psr, %g2
	restore
	st %o0, [%g1]; st %o1, [%g1+4]; st %o2, [%g1+8]; st %o3, [%g1+12]
	st %o4, [%g1+16]; st %o5, [%g1+20]; st %o6, [%g1+24]; st %o7, [%g1+28]
	st %l0, [%g1+32]; st %l1, [%g1+36]; st %l2, [%g1+40]; st %l3, [%g1+44]
	st %l4, [%g1+48]; st %l5, [%g1+52]; st %l6, [%g1+56]; st %l7, [%g1+60]
	st %i0, [%g1+64]; st %i1, [%g1+68]; st %i2, [%g1+72]; st %i3, [%g1+76]
	st %i4, [%g1+80]; st %i5, [%g1+84]; st %i6, [%g1+88]; st %i7, [%g1+92]
	mov	%g2, %psr
	nop; nop; nop;
#endif TRAPWINDOW
	st	%fp, [%l7 + MINFRAME + SP*4] ! stack pointer
	st	%l0, [%l7 + MINFRAME + PSR*4] ! psr
	st	%l1, [%l7 + MINFRAME + PC*4] ! pc
	!
	! If we are in last trap window, all windows are occupied and
	! we must do window overflow stuff in order to do further calls
	!
	btst	%l5, %l3		! retest
	bz	.st_have_window		! if ((CWM&WIM)==0) no overflow
	st	%l2, [%l7 + MINFRAME + nPC*4] ! npc
	b,a	.st_sys_ovf

.st_user:
	!
	! Trap from user. Save user globals and prepare system stack.
	! Test whether the current window is the last available window
	! in the register file (CWM == WIM).
	!
	CPU_ADDR(%l6, %l7)		! load CPU struct addr to %l6 using %l7
	ld	[%l6 + CPU_THREAD], %l7	! load thread pointer
	ld	[%l7 + T_STACK], %l7	! %l7 is lwp's kernel stack
	SAVE_GLOBALS(%l7 + MINFRAME)

#ifdef TRACE
	! We do this now, rather than at the very start of sys_trap,
	! because the _SAVE_GLOBALS above gives us free scratch registers.
	!
	TRACE_SYS_TRAP_START(%l4, %g1, %g2, %g3, %g4, %g5)
	btst	%l5, %l3		! retest: compare WIM and CWM
#endif	/* TRACE */

	SAVE_OUTS(%l7 + MINFRAME)
	mov	%l7, %g5		! stack base is also mpcb ptr

	!
	! Call mmu_getsyncflt to get the synchronous fault status
	! register and the synchronous fault address register stored in
	! the per-CPU structure.
	!
	! The reason for reading these synchronous fault registers now is
	! that the window overflow processing that we are about to do may
	! also cause another fault.
	!
	! Note that mmu_getsyncflt assumes that it can clobber %g1 and %g4.
	!

	btst	T_FAULT, %l4
	bz,a	1f
	clr	[%l6 + CPU_SYNCFLT_STATUS]	! delay

	mov	%o7, %g3		! save %o7 in %g3
	call	mmu_getsyncflt
	nop
	mov	%g3, %o7		! restore %o7
1:
	ld	[%l6 + CPU_THREAD], THREAD_REG	! load thread pointer
	mov	1, FLAG_REG
	sethi	%hi(nwin_minus_one), %l6 ! re-load NW - 1
	ld	[%l6 + %lo(nwin_minus_one)], %l6
	st	%l0, [%l7 + MINFRAME + PSR*4] ! psr
	st	%l1, [%l7 + MINFRAME + PC*4] ! pc
	st	%l2, [%l7 + MINFRAME + nPC*4] ! npc
	!
	! If we are in last trap window, all windows are occupied and
	! we must do window overflow stuff in order to do further calls
	!
	btst	%l5, %l3		! retest
	bz	1f			! if ((CWM&WIM)==0) no overflow
	clr	[%g5 + MPCB_WBCNT]	! delay slot, save buffer ptr = 0
	not	%l5, %g2		! UWM = ~CWM
	sethi	%hi(winmask), %g3	! load the window mask
	ld	[%g3 + %lo(winmask)], %g3
	andn	%g2, %g3, %g2
	b	.st_user_ovf		! overflow
	srl	%l3, 1, %g1		! delay slot,WIM = %g1 = ror(WIM, 1, NW)

	!
	! Compute the user window mask (mpcb_uwm), which is a mask of
	! window which contain user data. It is all the windows "between"
	! CWM and WIM.
	!
1:
	subcc	%l3, %l5, %g1		! if (WIM >= CWM)
	bneg,a	2f			!    mpcb_uwm = (WIM-CWM)&~CWM
	sub	%g1, 1, %g1		! else
2:					!    mpcb_uwm = (WIM-CWM-1)&~CWM
	sethi	%hi(winmask), %g3	! load the window mask.
	ld	[%g3 + %lo(winmask)], %g3
	bclr	%l5, %g1
	andn	%g1, %g3, %g1
	st	%g1, [%g5 + MPCB_UWM]

.st_have_window:
	!
	! The next window is open.
	!
	mov	%l7, %sp		! setup previously computed stack
	!
	! Process trap according to type
	!
	btst	T_INTERRUPT, %l4	! interrupt
	bnz	_interrupt
	btst	T_FAULT, %l4		! fault

.fixfault:
	bnz,a	fault
	bclr	T_FAULT, %l4
	cmp	%l4, T_FP_EXCEPTION	! floating point exception
	be	_fp_exception
	cmp	%l4, T_FLUSH_WINDOWS	! flush user windows to stack
	bne	1f
	wr	%l0, PSR_ET, %psr	! enable traps
	nop				! psr delay

	!
	! Flush windows trap.
	!
	call	flush_user_windows	! flush user windows
	nop
	!
	! Don't redo trap instruction.
	!
	ld	[%sp + MINFRAME + nPC*4], %g1
	st	%g1, [%sp + MINFRAME + PC*4]  ! pc = npc
	add	%g1, 4, %g1
	b	_sys_rtt
	st	%g1, [%sp + MINFRAME + nPC*4] ! npc = npc + 4

1:
	!
	! All other traps. Call C trap handler.
	!
	mov	%l4, %o0		! trap(t, rp)
	clr	%o2			!  addr = 0
	clr	%o3			!  be = 0
	mov	S_OTHER, %o4		!  rw = S_OTHER
	call	trap			! C trap handler
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt		! return from trap

/*
 * Sys_trap overflow handling.
 * Psuedo subroutine returns to .st_have_window.
 */
.st_sys_ovf:
	!
	! Overflow from system.
	! Determine whether the next window is a user window.
	! If mpcb->mpcb_uwm has any bits set, then it is a user
	! which must be saved.
	!

	ld	[%l6 + CPU_MPCB], %g5
	sethi	%hi(nwin_minus_one), %l6	! re-load NW - 1
	ld	[%l6 + %lo(nwin_minus_one)], %l6
	tst	%g5			! mpcb == 0 for kernel threads
	bz	1f			! skip uwm checking when lwp == 0
	srl	%l3, 1, %g1		! delay, WIM = %g1 = ror(WIM, 1, NW)
	ld	[%g5 + MPCB_UWM], %g2	! if (mpcb.mpcb_uwm)
	tst	%g2			!	user window
	bnz	.st_user_ovf
	nop
1:
	!
	! Save supervisor window.  Compute the new WIM and change current window
	! to the window to be saved.
	!
	sll	%l3, %l6, %l3		! %l6 == NW-1
	or	%l3, %g1, %g1
	save				! get into window to be saved
	mov	%g1, %wim		! install new WIM
	!
	! Save window on the stack.
	!
.st_stack_res:
	SAVE_WINDOW(%sp)
	b	.st_have_window		! finished overflow processing
	restore				! delay slot, back to original window

.st_user_ovf:
	!
	! Overflow. Window to be saved is a user window.
	! Compute the new WIM and change the current window to the
	! window to be saved.
	!
	! On entry:
	!
	!	%l6 = nwindows - 1
	!	%g1 = ror(WIM, 1)
	!	%g2 = UWM
	!	%g5 = mpcb pointer
	!
	sll	%l3, %l6, %l3		! %l6 == NW-1
	or	%l3, %g1, %g1
	bclr	%g1, %g2		! turn off uwm bit for window
	st	%g2, [%g5 + MPCB_UWM]	! we are about to save
	save				! get into window to be saved
	mov	%g1, %wim		! install new WIM

	ld	[%g5 + MPCB_SWM], %g4	! test shared window mask
	btst	%g1, %g4		! if saving shared window
	bz,a	1f			! not shared window
	mov	%sp, %g4		! delay - stack pointer to be used

	!
	! Save kernel copy of shared window
	!
	clr	[%g5 + MPCB_SWM]	! clear shared window mask
	SAVE_WINDOW(%sp)		! save kernel copy of shared window
	! use saved stack pointer for user copy
	ld	[%g5 + MPCB_REGS + SP*4], %g4
	!
	! In order to save the window onto the stack, the stack
	! must be aligned on a word boundary, and the part of the
	! stack where the save will be done must be present.
	! We first check for alignment.
	!
1:
	btst	0x7, %g4		! test sp alignment - actual %sp in %g4
	set     KERNELBASE, %g1		! Check to see if we
	bnz	.st_stack_not_res	! stack misaligned, catch it later
	cmp     %g1, %g4		! are touching non-user space
	bleu    .st_stack_not_res
	nop

	!
	! Most of the time the stack is resident in main memory,
	! so we don't verify its presence before attempting to save
	! the window onto the stack.  Rather, we simply set the
	! no-fault bit of the SRMMU's control register, so that
	! doing the save won't cause another trap in the case
	! where the stack is not present.  By checking the
	! synchronous fault status register, we can determine
	! whether the save actually worked (ie. stack was present),
	! or whether we first need to fault in the stack.
	!
	! Other sun4/sun4c trap handlers first probe for the stack, and
	! then, if the stack is present, they store to the stack.
	! This approach CANNOT be used with a multiprocessor system
	! because of a race condition between the time that the
	! stack is probed, and the store to the stack is done, the
	! stack could be stolen by the page daemon.
	!
	! Uses %g1 and %g2
	!
.st_assume_stack_res:
	set	RMMU_FSR_REG, %g1	! clear any old faults out
	lda	[%g1]ASI_MOD, %g0	! of the SFSR.

	lda	[%g0]ASI_MOD, %g2	! turn on no-fault bit in
	or	%g2, MMCREG_NF, %g2	! mmu control register to
	sta	%g2, [%g0]ASI_MOD	! prevent taking a fault.

	SAVE_WINDOW(%g4)		! try to save reg window

	andn	%g2, MMCREG_NF, %g2	! turn off no-fault bit
	sta	%g2, [%g0]ASI_MOD

	set	RMMU_FAV_REG, %g2
	lda	[%g2]ASI_MOD, %g2	! read SFAR
	lda	[%g1]ASI_MOD, %g1	! read SFSR

	btst    SFSREG_FAV, %g1         ! did a fault occurr?
	bz,a	.st_have_window
	restore				! delay slot, back to original window

	!	
	! A fault occurred, so the stack is not resident.
	!
.st_stack_not_res:
	!
	! User stack is not resident, save in PCB for processing in _sys_rtt.
	!
	ld	[%g5 + MPCB_WBCNT], %g1
	sll	%g1, 2, %g1		! convert to spbuf offset

	add	%g1, %g5, %g2		! 
	st	%g4, [%g2 + MPCB_SPBUF] ! save sp in %g1+curthread+MPCB_SPBUF
	sll	%g1, 4, %g1		! convert wbcnt to mpcb_wbuf offset

	add	%g1, %g5, %g2
#if (MPCB_WBUF > ((1<<12)-1))
ERROR - pcb_wbuf offset to large
#endif
	add	%g2, MPCB_WBUF, %g2
	SAVE_WINDOW(%g2)
	srl	%g1, 6, %g1		! increment mpcb->pcb_wbcnt
	add	%g1, 1, %g1
	st	%g1, [%g5 + MPCB_WBCNT]
	b	.st_have_window		! finished overflow processing
	restore				! delay slot, back to original window
	SET_SIZE(_sys_trap)
	SET_SIZE(sys_trap)

#endif	/* lint */

/*
 * Return from _sys_trap routine.
 */

#if defined(lint)

void
_sys_rtt(void)
{}

#else	/* lint */

	ENTRY_NP(_sys_rtt)
#ifdef TRAPTRACE
	mov	%psr, %g1
	andn	%g1, PSR_ET, %g2	! disable traps
	mov	%g2, %psr
	nop; nop; nop;
	TRACE_PTR(%g3, %o1)		! get trace pointer
	set	TT_SYS_RTT, %o1
	st	%o1, [%g3 + TRAP_ENT_TBR]
	st	%l0, [%g3 + TRAP_ENT_PSR]
	st	%l1, [%g3 + TRAP_ENT_PC]
	st	%fp, [%g3 + TRAP_ENT_SP]
	st	%g7, [%g3 + TRAP_ENT_G7]
	st	%l4, [%g3 + 0x14]
	ld	[%g7 + T_CPU], %o1
	ld	[%o1 + CPU_BASE_SPL], %o1
	st	%o1, [%g3 + 0x18]
	TRACE_NEXT(%g3, %o0, %o1)	! set new trace pointer
	mov	%g1, %psr
	nop; nop; nop;
#endif
	ld	[%sp + MINFRAME + PSR*4], %l0 ! get saved psr
	btst	PSR_PS, %l0		! test pS for return to supervisor
	bnz	.sr_sup
	mov	%psr, %g1
	!
	! Return to User.
	! Turn off traps using the current CWP (because
	! we are returning to user).
	! Test for AST for rescheduling or profiling.
	!
	and	%g1, PSR_CWP, %g1	! insert current CWP in old psr
	andn	%l0, PSR_CWP, %l0
	or	%l0, %g1, %l0
	mov	%l0, %psr		! install old psr, disable traps
	nop; nop; nop;			! psr delay

#ifdef TRAPTRACE
	mov	%psr, %g1
	andn	%g1, PSR_ET, %g2	! disable traps
	mov	%g2, %psr
	nop; nop; nop;
	TRACE_PTR(%g3, %o1)		! get trace pointer
	set	TT_SYS_RTTU, %o1
	st	%o1, [%g3 + TRAP_ENT_TBR]
	st	%l0, [%g3 + TRAP_ENT_PSR]
	st	%l1, [%g3 + TRAP_ENT_PC]
	st	%fp, [%g3 + TRAP_ENT_SP]
	st	%g7, [%g3 + TRAP_ENT_G7]
	st	%l4, [%g3 + 0x14]
	ld	[%g7 + T_CPU], %o1
	ld	[%o1 + CPU_BASE_SPL], %o1
	st	%o1, [%g3 + 0x18]
	TRACE_NEXT(%g3, %o0, %o1)	! set new trace pointer
	mov	%g1, %psr
	nop; nop; nop;
#endif
	!
	! check for an AST that's posted to the lwp
	!
	ldub	[THREAD_REG + T_ASTFLAG], %g1
	mov	%sp, %g5		! user stack base is mpcb ptr
	tst	%g1			! test signal or AST pending
	bz	1f
	ld	[%g5 + MPCB_WBCNT], %g3	! user regs been saved?

	!
	! Let trap handle the AST.
	!
	or	%l0, PSR_PIL, %o1	! spl8 first to protect CPU base pri
	mov	%o1, %psr		! in case of changed priority (IU bug)
	wr	%o1, PSR_ET, %psr	! turn on traps
	nop				! psr delay
	call	splx			! psr delay - call splx()
	mov	%l0, %o0		! psr delay - pass old %psr to splx()
	mov	T_AST, %o0
	call	trap			! trap(T_AST, rp)
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt

1:
	!
	! If user regs have been saved to the window buffer we must clean it.
	!
	tst	%g3
	bz,a	2f
	ld	[%g5 + MPCB_UWM], %l4	! delay slot, user windows in reg file?

	!
	! User regs have been saved into PCB area.
	! Let trap handle putting them on the stack.
	!
	or	%l0, PSR_PIL, %o1	! spl8 first to protect CPU base pri
	mov	%o1, %psr		! set priority before enabling (IU bug)
	wr	%o1, PSR_ET, %psr	! turn on traps
	nop				! psr delay
	call	splx			! psr delay - call splx()
	mov	%l0, %o0		! psr delay - pass old %psr to splx()
	mov	T_WIN_OVERFLOW, %o0
	call	trap			! trap(T_WIN_OVERFLOW, rp)
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt
2:
	!
	! We must insure that the rett will not take a window underflow trap.
	!
	RESTORE_OUTS(%sp + MINFRAME)	! restore user outs
	tst	%l4
	bnz	.sr_user_regs
	ld	[%sp + MINFRAME + PC*4], %l1 ! restore user pc

	!
	! The user has no windows in the register file.
	! Try to get one from the stack.
	!
	sethi	%hi(nwin_minus_one), %l6	! NW-1 for rol calculation
	ld	[%l6 + %lo(nwin_minus_one)], %l6
	mov	%wim, %l3		! get wim
	sll	%l3, 1, %l4		! next WIM = rol(WIM, 1, NW)
	srl	%l3, %l6, %l5		! %l6 == NW-1
	or	%l5, %l4, %l5
	mov	%l5, %wim		! install it
	!
	! In order to restore the window from the stack, the stack
	! must be aligned on a word boundary, and the part of the
	! stack where the save will be done must be present.
	! We first check for alignment.
	!
	btst	0x7, %fp		! test fp alignment
	bz	.sr_assume_stack_res
	nop

	!
	! A user underflow with a misaligned sp.
	! Fake a memory alignment trap.
	!
	mov	%l3, %wim		! restore old wim

.sr_align_trap:
	or	%l0, PSR_PIL, %o1	! spl8 first to protect CPU base pri
	mov	%o1, %psr		! set priority before enabling (IU bug)
	wr	%o1, PSR_ET, %psr	! turn on traps
	nop
	call	splx			! splx()
	mov	%l0, %o0		! delay - pass old %psr to splx()
	mov	T_ALIGNMENT, %o0
	call	trap			! trap(T_ALIGNMENT, rp)
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt

	!
	! Branch to code which is dependent upon the particular
	! type of SRMMU module.  this code will attempt to restore
	! the underflowed window from the stack, without first
	! verifying the presence of the stack.
	!
	! Most of the time the stack is resident in main memory,
	! so we don't verify its presence before attempting to restore
	! the window from the stack.  Rather, we simply set the
	! no-fault bit of the SRMMU's control register, so that
	! doing the restore won't cause another trap in the case
	! where the stack is not present.  By checking the
	! synchronous fault status register, we can determine
	! whether the restore actually worked (ie. stack was present),
	! or whether we first need to fault in the stack.
	!
	! Other sun4 trap handlers first probe for the stack, and
	! then, if the stack is present, they restore from the stack.
	! This approach CANNOT be used with a multiprocessor system
	! because of a race condition: between the time that the
	! stack is probed, and the restore from the stack is done, the
	! stack could be stolen by the page daemon.
	!
.sr_assume_stack_res:
	set	RMMU_FSR_REG, %g1	! clear any old faults out
	lda	[%g1]ASI_MOD, %g0	! of the SFSR.

	lda	[%g0]ASI_MOD, %g2	! turn on no-fault bit in
	or	%g2, MMCREG_NF, %g2	! mmu control register to
	sta	%g2, [%g0]ASI_MOD	! prevent taking a fault

	restore
	RESTORE_WINDOW(%sp)		! try to restore reg window
	save

	andn	%g2, MMCREG_NF, %g2	! turn off no-fault bit
	sta	%g2, [%g0]ASI_MOD

	set	RMMU_FAV_REG, %g2
	lda	[%g2]ASI_MOD, %g2	! read SFAR
	lda	[%g1]ASI_MOD, %g1	! read SFSR

	btst    SFSREG_FAV, %g1         ! did a fault occurr?
	bz	.sr_user_regs
	nop

.sr_stack_not_res:
	!
	! Restore area on user stack is not resident.
	! We punt and fake a page fault so that trap can bring the page in.
	!
	mov	%l3, %wim		! restore old wim
	or	%l0, PSR_PIL, %o1	! spl8 first to protect CPU base pri
	mov	%o1, %psr		! set priority before enabling (IU bug)
	wr	%o1, PSR_ET, %psr	! turn on traps
	nop
	call	splx			! splx() (preserves %g1, %g2)
	mov	%l0, %o0		! delay - pass old %psr to splx()
	mov	T_DATA_FAULT, %o0
	add	%sp, MINFRAME, %o1
	mov	%g2, %o2		! save fault address
	mov	%g1, %o3		! save fault status
	call	trap			! trap(T_DATA_FAULT,
	mov	S_READ, %o4		!	rp, addr, be, S_READ)

	b,a	_sys_rtt

.sr_user_regs:
	!
	! Fix saved %psr so the PIL will be CPU->cpu_base_pri
	!
	ld	[THREAD_REG + T_CPU], %o1	! get CPU_BASE_SPL
	ld	[%o1 + CPU_BASE_SPL], %o1
	andn	%l0, PSR_PIL, %l0
	or	%o1, %l0, %l0		! fixed %psr
! XXX - add test for clock level here 
	!
	! User has at least one window in the register file.
	!
	!
	ld	[%g5 + MPCB_FLAGS], %l3
	ld	[%sp + MINFRAME + nPC*4], %l2 ! user npc
	!
	! check user pc alignment.  This can get messed up either using
	! ptrace, or by using the '-T' flag of ld to place the text
	! section at a strange location (bug id #1015631)
	!
	or	%l1, %l2, %g2
	btst	0x3, %g2
	bz,a	1f
	btst	CLEAN_WINDOWS, %l3
	b,a	.sr_align_trap

1:
	bz,a	3f
	mov	%l0, %psr		! install old PSR_CC

	!
	! Maintain clean windows.
	!
	mov	%wim, %g2		! put wim in global
	mov	0, %wim			! zero wim to allow saving
	mov	%l0, %g3		! put original psr in global
	b	2f			! test next window for invalid
	save
	!
	! Loop through windows past the trap window
	! clearing them until we hit the invlaid window.
	!
1:
	clr	%l1			! clear the window
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
	clr	%o6
	clr	%o7
	save
2:
	mov	%psr, %g1		! get CWP
	srl	%g2, %g1, %g1		! test WIM bit
	btst	1, %g1
	bz,a	1b			! not invalid window yet
	clr	%l0			! clear the window

	!
	! Clean up trap window.
	!
	mov	%g3, %psr		! back to trap window, restore PSR_CC
	mov	%g2, %wim		! restore wim
	nop; nop;			! psr delay

#ifdef TRACE
	!
	! We do this right before the _RESTORE_GLOBALS, so we can safely
	! use the globals as scratch registers.
	! On entry, %g3 = %psr.
	!
	TRACE_SYS_TRAP_END(%g1, %g2, %g6, %g4, %g5)
	mov	%g3, %psr
#endif	/* TRACE */

	RESTORE_GLOBALS(%sp + MINFRAME)	! restore user globals
	mov	%l1, %o6		! put pc, npc in unobtrusive place
	mov	%l2, %o7
	clr	%l0			! clear the rest of the window
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
	jmp	%o6
	rett	%o7

3:

#ifdef TRACE
	!
	! We do this right before the _RESTORE_GLOBALS, so we can safely
	! use the globals as scratch registers.
	! On entry, %l0 = %psr.
	!
	TRACE_SYS_TRAP_END(%g1, %g2, %g3, %g4, %g5)
	mov	%l0, %psr
#endif	/* TRACE */

	RESTORE_GLOBALS(%sp + MINFRAME)	! restore user globals
#ifdef VIKING_BUG_16
	rd	%y, %g0			! viking bug fix- VIKING_BUG_16 elsewhere
#endif
	jmp	%l1			! return
	rett	%l2

	!
	! Return to supervisor
	! %l0 = old %psr.
	! %g1 = current %psr
	!
.sr_sup:
	!
	! Check for a kernel preemption request
	!
	ld	[THREAD_REG+T_CPU], %g5	! get CPU
	ldub	[%g5 + CPU_KPRUNRUN], %g5 ! get CPU->cpu_kprunrun
	tst	%g5
	bz	2f
	nop				! delay
	!
	! Attempt to preempt
	!
	call	kpreempt
	mov	%l0, %o0		! pass original interrupt level

	mov	%psr, %g1		! reload %g1
	and	%l0, PSR_PIL, %o0	! compare old pil level
	and	%g1, PSR_PIL, %g5	!   with current pil level
	subcc	%o0, %g5, %o0
	bgu,a	2f			! if current is lower,
	sub	%l0, %o0, %l0		!   drop old pil to current level
2:
	!
	! We will restore the trap psr. This has the effect of disabling
	! traps and changing the CWP back to the original trap CWP. This
	! completely restores the PSR (except possibly for the PIL).
	! We only do this for supervisor return since users can't manipulate
	! the psr.  Kernel code modifying the PSR must mask interrupts and
	! then compare the proposed new PIL to the one in CPU->cpu_base_spl.
	!
	sethi	%hi(nwindows), %g5
	ld	[%g5 + %lo(nwindows)], %g5 ! number of windows on this machine
	ld	[%sp + MINFRAME + SP*4], %fp ! get sys sp
	wr	%g1, PSR_ET, %psr	! delay - disable traps
	xor	%g1, %l0, %g2		! test for CWP change
	nop
	btst	PSR_CWP, %g2
	bz	.sr_samecwp
	mov	%l0, %g3		! delay - save old psr
	!
	! The CWP will be changed. We must save sp and the ins
	! and recompute WIM. We know we need to restore the next
	! window in this case.
	!
	! Using %g4 here doesn't interfere with fault info. stored in %g4,
	! since we are currently returning from the trap.
	!
	mov	%sp, %g4		! save sp, ins for new window
	std	%i0, [%sp +(8*4)]	! normal stack save area
	std	%i2, [%sp +(10*4)]
	std	%i4, [%sp +(12*4)]
	std	%i6, [%sp +(14*4)]
	mov	%g3, %psr		! old psr, disable traps, CWP, PSR_CC
	ROSS625_PSR_NOP()
	ROSS625_PSR_NOP()
	mov	0x4, %g1		! psr delay, compute mask for CWP + 2
	sll	%g1, %g3, %g1		! psr delay, won't work for NW == 32
	srl	%g1, %g5, %g2		! psr delay
	or	%g1, %g2, %g1
	mov	%g1, %wim		! install new wim
	mov	%g4, %sp		! reestablish sp
	ldd	[%sp + (8*4)], %i0	! reestablish ins
	ldd	[%sp + (10*4)], %i2
	ldd	[%sp + (12*4)], %i4
	ldd	[%sp + (14*4)], %i6
	restore				! restore return window
	RESTORE_WINDOW(%sp)
	b	.sr_out
	save

.sr_samecwp:
	!
	! There is no CWP change.
	! We must make sure that there is a window to return to.
	!
	mov	0x2, %g1		! compute mask for CWP + 1
	sll	%g1, %l0, %g1		! XXX won't work for NW == 32
	srl	%g1, %g5, %g2		! %g5 == NW, from above
	or	%g1, %g2, %g1
	mov	%wim, %g2		! cmp with wim to check for underflow
	btst	%g1, %g2
	bz	.sr_out
	nop
	!
	! No window to return to. Restore it.
	!
	sll	%g2, 1, %g1		! compute new WIM = rol(WIM, 1, NW)
	dec	%g5			! %g5 == NW-1
	srl	%g2, %g5, %g2
	or	%g1, %g2, %g1
	mov	%g1, %wim		! install it
	nop; nop; nop;			! wim delay
	restore				! get into window to be restored
	RESTORE_WINDOW(%sp)
	save				! get back to original window
	!
	! Check the PIL in the saved PSR.  Don't go below CPU->cpu_base_spl
	!
.sr_out:
	ld	[THREAD_REG + T_CPU], %g4
	and	%g3, PSR_PIL, %g1	! check saved PIL
	ld	[%g4 + CPU_BASE_SPL], %g4
	subcc	%g4, %g1, %g4		! form base - saved PIL
	bg,a	1f			! base PIL is greater so 
	add	%g3, %g4, %g3		! fix PSR by adding (base - saved) PIL
1:
	mov	%g3, %psr		! restore PSR with correct PIL, CC

#ifdef TRACE
	!
	! We do this right before the _RESTORE_GLOBALS, so we can safely
	! use the globals as scratch registers.
	! On entry, %g3 = %psr.
	!
	TRACE_SYS_TRAP_END(%g1, %g2, %g6, %g4, %g5)
	mov	%g3, %psr
#endif	/* TRACE */

	RESTORE_GLOBALS(%sp + MINFRAME)	! restore system globals
	ld	[%sp + MINFRAME + PC*4], %l1 ! delay slot, restore sys pc
	ld	[%sp + MINFRAME + nPC*4], %l2 ! sys npc
#ifdef VIKING_BUG_16
	rd	%y, %g0			! viking bug fix- VIKING_BUG_16 elsewhere
#endif
	jmp	%l1			! return to trapped instruction
	rett	%l2
	SET_SIZE(_sys_rtt);
/* end sys_rtt*/

/*
 * Fault handler.
 */
	.type   fault, #function
fault:

#ifdef	FIXME
	!
	! FIXME: need to make sure MEMERR_ADDR (or memory error register)
	!	 is the same as coded here.
	!
	! XXX - Enable this code when MEMERR_ADDR fix is in.
	!
	mov	S_EXEC, %o4		! assume execute fault
	cmp	%l4, T_TEXT_FAULT	! text fault?
	be,a	2f
	mov	%l1, %o2		! pc is text fault address
#endif	FIXME
#ifdef	FIXME
	set	MEMERR_ADDR, %g1
	ld	[%g1 + ME_VADDR], %o2	! get data fault address in mem err reg
	ld	[%g1], %g2		! is this a memory error or a fault?
	!
	! FIXME: ER_INTR is defined for sun4 but not sun4c. How about us?
	!
	btst	ER_INTR, %g2
	bz,a	1f			! memory error
	clr	[%g1 + ME_VADDR]	! clear fault address
	!
	! Memory error.  Enable traps to let interrupt happen.
	!
	wr	%l0, PSR_ET, %psr
	nop				! psr delay
	b	_sys_rtt		! return from trap
	nop
#endif	FIXME

1:
	!
	! The synchronous fault status register (SFSR) and
	! synchronous fault address register (SFAR) were obtained
	! above by calling mmu_getsyncflt, and have been stored
	! in the per-CPU area.
	!
	! For the call to trap below, move the status reg into
	! %o3, and the address reg into %o2
	!
	CPU_ADDR(%g2, %g4)
	ld	[%g2 + CPU_SYNCFLT_STATUS], %o3
	cmp     %l4, T_TEXT_FAULT       ! text fault?
	be,a    4f
	mov     %l1, %o2                ! use saved pc as the fault address
	ld	[%g2 + CPU_SYNCFLT_ADDR], %o2

4:	srl	%o3, FS_ATSHIFT, %g2	! Get the access type bits
	and	%g2, FS_ATMASK, %g2
	mov	S_READ, %o4		! assume read fault
	cmp	%g2, AT_UREAD
	be	3f
	nop
	cmp	%g2, AT_SREAD
	be	3f
	nop
	mov	S_WRITE, %o4		! else it's a write fault
	cmp	%g2, AT_UWRITE
	be	3f
	nop
	cmp	%g2, AT_SWRITE
	be	3f
	nop

	!
	! Must be an execute cycle
	!
	mov	S_EXEC, %o4		! it's an execution fault
3:
6:	wr	%l0, PSR_ET, %psr	! enable traps (no priority change)
	nop; nop;

	!
	! %o0, %o1 is setup by sys_trap, %o2 from SFAR, %o3 from SFSR, and
	! %o4 from looking at SFSR.
	!
	mov	%l4, %o0		! trap(t, rp, addr, fsr, rw)
	call	trap			! C Trap handler
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt		! return from trap
	SET_SIZE(fault)

/*
 * Interrupt vector tables
 */
	.seg	".data"
	.align	4
	!
	! Most interrupts are vectored via the following table
	! We usually can't vector directly from the scb because
	! we haven't done window setup. Some drivers elect to
	! manage their own window setup, and thus come directly
	! from the scb (they do this by setting their interrupt
	! via ddi_add_fastintr()). Typically, they also have
	! second stage interrupt established by use of one of
	! the IE register's soft interrupt levels.
	!
	! First sixteen entries are for the sixteen
	! levels of hardware interrupt.  The next sixteen entries
	! are for the sixteen levels of software interrupt.
	!
	.global _int_vector
_int_vector:
	!
	! First 16 words: hard interrupt service.
	!
	.word	spurious	! level 0, should not happen
	.word	_level1		! level 1
	.word	.level2		! level 2, sbus/vme level 1
	.word	.level3		! level 3, sbus/vme level 2
	.word	.level4		! level 4, onboard scsi
	.word	.level5		! level 5, sbus/vme level 3
	.word	.level6		! level 6, onboard ether
	.word	.level7		! level 7, sbus/vme level 4
	.word	.level8		! level 8, onboard video
	.word	.level9		! level 9, sbus/vme level 5, module interrupt
	.word	_level10	! level 10, normal clock
	.word	.level11	! level 11, sbus/vme level 6, floppy
#if defined(SAS) || defined(MPSAS)
	.word   simcintr        ! sas console interrupt
#else
	.word	.level12	! level 12, serial i/o
#endif SAS
	.word	.level13	! level 13, sbus/vme/vme level 7, audio
#if !defined(SNOOPING) || defined(GPROF) || defined(TRACE)/* code for */
							  /* deadman timers */
	.word	.level14	! level 14, high level clock
#else
	.word	_deadman	! check for deadlocks
#endif
	.word	.level15	! level 15, asynchronous error
	!
	! Second 16 words: soft interrupt service.
	!
	.word	spurious	! level 0, should not happen.
	.word	.softlvl1	! level 1, softclock interrupts
	.word	.softlvl2	! level 2
	.word	.softlvl3	! level 3
	.word	.softlvl4	! level 4, floppy and audio soft ints
	.word	.softlvl5	! level 5
	.word	.softlvl6	! level 6, serial driver soft interrupt
	.word	.softlvl7	! level 7
	.word	.softlvl8	! level 8
	.word	.softlvl9	! level 9
	.word	.softlvl10	! level 10
	.word	.softlvl11	! level 11
	.word	.softlvl12	! level 12
	.word	.softlvl13	! level 13
	.word	.softlvl14	! level 14
	.word	.softlvl15	! level 15
	SET_SIZE(_int_vector)

	!
	! 256 vectors for vme interrupt service.
	! Each vector consists of an interrupt service
	! routine, an argument for that routine, and
	! (possibly) a mutex to grab before calling it.
	!
	.global vme_vector
	.type	vme_vector, #object
vme_vector:
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,

	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,

	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,

	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	.word	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,	0,0,0,
	
	SET_SIZE(vme_vector)

	!
	! Mask for all interrupts 0 - 9 used to mask out all interrupts
	! at a particular level before re-assigning the interrupt target
	! register (ITR) to another CPU.
	!
	.global intr_level_mask
intr_level_mask:
	.word	0
	.word	SIR_L1
	.word	SIR_L2
	.word	SIR_L3
	.word	SIR_L4
	.word	SIR_L5
	.word	SIR_L6
	.word	SIR_L7
	.word	SIR_L8
	.word	SIR_L9

	.seg	".text"

#endif /* lint */

/*
 * Generic interrupt handler.
 *
 * On entry:  traps disabled.
 *
 *	%l4 = T_INTERRUPT ORed with level (1-15)
 *	%l0, %l1, %l2 = saved %psr, %pc, %npc.
 *	%l7 = saved %sp
 *	%g7 = thread pointer
 *
 * Register Usage:	
 *
 *	%l4 = interrupt level (1-15).
 *	%l5 = old PSR with PIL cleared (for vme_interrupt)
 *	%l6 = new thread pointer (interrupt)
 */

#if defined(lint)

void
_interrupt(void)
{}

#else	/* lint */

	ENTRY_NP(_interrupt)
	andn	%l0, PSR_PIL, %l5	! compute new psr with proper PIL
	and	%l4, T_INT_LEVEL, %l4
	sll	%l4, PSR_PIL_BIT, %g1
	or	%l5, %g1, %l0

	!
	! Hook for mutex_enter().  If mutex_enter() was interrupted after the
	! lock was set, but before the owner was, then we set the owner here.
	! %i0 contains the mutex address.  %i1 contains the ldstub result.
	! We determine whether we were in the critical region of mutex_enter()
	! or mutex_adaptive_tryenter() by checking the trapped pc.
	! We test %i1 first because if it's non-zero, we can skip all
	! other checks regardless -- even if we *were* in a critical region,
	! we didn't get the lock so there's nothing to do here.
	!
	! See sparc/ml/lock_prim.s for further details.  Guilty knowledge
	! abounds here.
	!
	tst	%i1			! any chance %i0 is an unowned lock?
	bnz	0f			! no - skip all other checks
	sethi	%hi(panicstr), %l6	! delay - begin test for panic

	set	MUTEX_CRITICAL_UNION_START, %l3	! union = region + hole + region
	sub	%l1, %l3, %l3		! %l3 = offset into critical union
	cmp	%l3, MUTEX_CRITICAL_UNION_SIZE
	bgeu	0f			! not in critical union
	sub	%l3, MUTEX_CRITICAL_REGION_SIZE, %l3 ! delay - offset into hole
	cmp	%l3, MUTEX_CRITICAL_HOLE_SIZE
	blu	0f			! in hole, not in either critical region
	sra	THREAD_REG, PTR24_LSB, %l3 ! delay - compute lock+owner
	st	%l3, [%i0 + M_OWNER]	! set lock+owner field
0:
	ld	[%l6 + %lo(panicstr)], %l6
	tst	%l6
	bnz,a	1f			! if NULL, test for lock level
	or	%l0, PSR_PIL, %l0	! delay - mask everthing if panicing
1:
	cmp	%l4, LOCK_LEVEL		! compare to highest thread level
	bl	intr_thread		! process as a separate thread
	ld	[THREAD_REG + T_CPU], %l3	! delay - get CPU pointer

#if CLOCK_LEVEL != LOCK_LEVEL
	cmp	%l4, CLOCK_LEVEL
#endif
	be	_level10		! go direct for clock interrupt
	ld	[%l3 + CPU_ON_INTR], %l6	! delay - load cpu_on_intr

	!
	! Handle high_priority nested interrupt on separate interrupt stack
	!
	tst	%l6
	inc	%l6
	bnz	1f			! already on the stack
	st	%l6, [%l3 + CPU_ON_INTR]
	ld	[%l3 + CPU_INTR_STACK], %sp	! get on interrupt stack
	tst	%sp			! if INTR_STACK not initialized,
	bz,a	1f			! .. stay on current stack
	mov	%l7, %sp		! (can happen; eg, early level 15)
1:
	!
	! If we just took a memory error we don't want to turn interrupts
	! on just yet in case there is another memory error waiting in the
	! wings. So disable interrupts if the PIL is 15.
	!
	cmp	%g1, PSR_PIL
	bne	2f
	sll	%l4, 2, %l3		! convert level to word offset
	set	IR_ENA_INT, %o0
	call	set_intmask		! disable interrupts
!
!!! XXX - This is a bug, but before we fix it
!!! %%% we need to figure out when to turn
!!! %%% interrupt enable back on.
!
	mov	0, %o1
!
!!!	mov	1, %o1			! by turning on the ENA bit.
!
2:
	!
	!
        ! check hard/soft interrupt info.
        ! if soft pending, add 16 to
        ! the interrupt level to put us in
        ! the range for soft interrupts,
        ! else if hard pending, just go on.
        ! else if neither pending, then
        ! someone took the interrupt away;
        ! this happens (but rarely).
        !
	CPU_INDEX(%o0)			! get cpu id number 0..3
	sll	%o0, 2, %o0		! convert to word offset
	set	v_interrupt_addr, %g1
	add	%g1, %o0, %g1
	ld	[%g1], %g1
	ld	[%g1], %o0		! get module int pending reg
	set	0x10000, %o1
	sll	%o1, %l4, %o1		! calculate softint bit
	andcc	%o0, %o1, %g0
	bne,a	0f			! if (softint is set)
	add	%l3, 64, %l3		!   use the softint vector table
	srl	%o1, 16, %o1		! else { calc hardint bit 
	andcc	%o0, %o1, %g0		!   if (hardint is not set) {
	bne	0f			!      ?Enable traps 
	nop
	mov	%l0, %psr		!      set level (IU bug)
	wr	%l0, PSR_ET, %psr	!      enable traps
	nop; nop			!      psr delay
	b	.intr_ret		!      call .intr_rtt
	nop 				!   }
0:					! }
	!
	! All softlevel interrupts and the hard level 15 broadcast interrupt
	! need to be ack'd by writing to the module clear-pending pseudo
	! register.  0xFFFF8000 is the mask used to determine if the
	! interrupt falls into the appropriate category.
	!
	sethi	%hi(0xFFFF8000), %o0	! ints we need to ack
	andcc	%o1, %o0, %o1		! do we ack this int?
	bnz,a	0f
	st	%o1, [%g1 + 4]		! ack the int, if latched
0:
	bnz,a	0f
	ld	[%g1], %g0		! wait for change to propagate through
0:

	!
	! Get handler address for level.
	!
	set	_int_vector, %g1
	ld	[%g1 + %l3], %l3	! grab vector
!
! XXX - add handling for VME level 6 and 7 interrupts later
!
	!
	! On board interrupt.
	! Due to a bug in the IU, we cannot increase the PIL and
	! enable traps at the same time. In effect, ET changes
	! slightly before the new PIL becomes effective.
	! So we write the psr twice, once to set the PIL and
	! once to set ET.
	!
	mov	%l0, %psr		! set level (IU bug)
	wr	%l0, PSR_ET, %psr	! enable traps
	nop; nop			! psr delay
	TRACE_ASM_1 (%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);

	call	%l3			! interrupt handler
	nop				! note this location being called
					! does not do a save!
.interrupt_ret:
	!
	! Decide here whether we're returning as an interrupt thread or not.
	! All interrupts below LOCK_LEVEL are handled by interrupt threads.
	!
	cmp	%l4, LOCK_LEVEL
	bl	.int_rtt
	nop

	!
	! On interrupt stack.  These handlers cannot jump to int_rtt
	! (as they used to), they must now return as normal functions
	! or jump to .intr_ret.
	!
.intr_ret:
	TRACE_ASM_0 (%o1, TR_FAC_INTR, TR_INTR_END, TR_intr_end);
	ld	[THREAD_REG + T_CPU], %l3 	! reload CPU pointer
	ld	[%l3 + CPU_SYSINFO_INTR], %g2	! cpu_sysinfo.intr++
	inc	%g2
	st	%g2, [%l3 + CPU_SYSINFO_INTR]

	!
	! Disable interrupts while changing %sp and cpu_on_intr
	! so any subsequent intrs get a precise state.	
	!
	mov	%psr, %g2
	wr	%g2, PSR_ET, %psr		! disable traps
	ROSS625_PSR_NOP()
	ROSS625_PSR_NOP()
	ld	[%l3 + CPU_ON_INTR], %l6	! decrement on_intr
	dec	%l6				! psr delay 3
	st	%l6, [%l3 + CPU_ON_INTR]	! store new on_intr
	mov	%l7, %sp			! reset stack pointer
	mov	%g2, %psr			! enable traps
	nop
	b	_sys_rtt			! return from trap
	nop					! psr delay 3

/*
 * Handle an interrupt in a new thread.
 *	Entry:  traps disabled.
 *		%l3 = CPU pointer
 *		%l4 = interrupt level
 *		%l0 = old psr with new PIL
 *		%l1, %l2 = saved %psr, %pc, %npc.
 *		%l7 = saved %sp
 *	Uses:	
 *		%l4 = interrupt level (1-15).
 *		%l5 = old PSR with PIL cleared (for vme_interrupt)
 *		%l6 = new thread pointer, indicates if interrupt level
 *		      needs to be unmasked after invoking the trap handler.
 */
intr_thread:
	! Get set to run interrupt thread.
	! There should always be an interrupt thread since we allocate one
	! for each level on the CPU, and if we release an interrupt, a new
	! thread gets created.
	!
	ld	[%l3 + CPU_INTR_THREAD], %l6	! interrupt thread pool
#ifdef DEBUG
	tst	%l6
	bnz	2f
	nop
	set	1f, %o0
	mov	%l0, %psr		! set level (IU bug)
	wr	%l0, PSR_ET, %psr	! enable traps
	nop				! psr delay
	call	panic			! psr delay
	nop				! psr delay
1:	.asciz	"no interrupt thread"
	.align	4
2:
#endif /* DEBUG */

	ld	[%l6 + T_LINK], %o2		! unlink thread from CPU's list
	st	%o2, [%l3 + CPU_INTR_THREAD]
	!
	! Consider the new thread part of the same LWP so that
	! window overflow code can find the PCB.
	!
	ld	[THREAD_REG + T_LWP], %o2
	st	%o2, [%l6 + T_LWP]
	!
	! Threads on the interrupt thread free list could have state already
	! set to TS_ONPROC, but it helps in debugging if they're TS_FREE
	! Could eliminate the next two instructions with a little work.
	!
	mov	ONPROC_THREAD, %o3
	st	%o3, [%l6 + T_STATE]
	!
	! Push interrupted thread onto list from new thread.
	! Set the new thread as the current one.
	! Set interrupted thread's T_SP because if it is the idle thread,
	! resume may use that stack between threads.
	!
	st	%l7, [THREAD_REG + T_SP]	! mark stack for resume
	st	THREAD_REG, [%l6 + T_INTR]	! push old thread
	st	%l6, [%l3 + CPU_THREAD]		! set new thread
	mov	%l6, THREAD_REG			! set global curthread register
	ld	[%l6 + T_STACK], %sp		! interrupt stack pointer
	!
	! Initialize thread priority level from intr_pri
	!
	sethi	%hi(intr_pri), %g1
	ldsh	[%g1 + %lo(intr_pri)], %l3	! grab base interrupt priority
	add	%l4, %l3, %l3		! convert level to dispatch priority
	sth	%l3, [THREAD_REG + T_PRI]
	clr	%l6				! clear %l6
	!
	! Check hard/soft interrupt info.
	! if hard pending, just go on.
	! else if soft pending, add 16 to
	! the interrupt level to put us in
	! the range for soft interrupts,
	! and turn off our soft interrupt bit.
	! else if neither pending, then
	! someone took the interrupt away;
	! this happens (but rarely).
	!
	CPU_INDEX(%o2)			! get cpu id number 0..3
	sll	%o2, 2, %o0		! convert to word offset
	set	v_interrupt_addr, %g1
	add	%g1, %o0, %g1
	ld	[%g1], %g1		! will need this base addr later, too.
	ld	[%g1], %o0		! get module int pending reg
	sll	%l4, 2, %l3		! convert level to word offset
	set	1, %o1
	sll	%o1, %l4, %o1		! calculate hardint bit
	andcc	%o0, %o1, %g0
	bne	0f
	nop				! if (hardint is not set) {
	sll	%o1, 16, %o1		!   calculate softint bit
	andcc	%o0, %o1, %g0		!   if (softint is not set) {
	!
	! Enable traps before calling .int_rtt
	!
	bne,a	1f
	add	%l3, 64, %l3		!   use the softint vector table
	mov	%l0, %psr		! set level (IU bug)
	wr	%l0, PSR_ET, %psr	! enable traps
	nop; nop; nop			! psr delay
	TRACE_ASM_1 (%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);
	b	.int_rtt
	mov	%l7, %sp
0:					! }
	!
	! Mask the appropriate hard interrupt below level 9 in the
	! system interrupt target mask register (ITRM).  The mask is
	! obtained from an array (intr_level_mask) containing masks for
	! levels 0 - 9.  The array is index using the interrupt level
	! converted to a word offset (%l3 contains the converted word offset).
	!
	set	intr_level_mask, %o0
	ld	[%o0 + %l3], %o0
	sethi	%hi(v_sipr_addr), %o4
	ld	[%o4 + %lo(v_sipr_addr)], %o4
	st	%o0, [%o4 + IR_SET_OFFSET]
	mov	0x1, %l6		! indicate interrupt level is masked off
1:
	!
	! All softlevel interrupts and the hard level 15 broadcast interrupt
	! need to be ack'd by writing to the module clear-pending pseudo
	! register.  0xFFFF8000 is the mask used to determine if the
	! interrupt falls into the appropriate category.
	!
	sethi	%hi(0xFFFF8000), %o0	! ints we need to ack
	andcc	%o1, %o0, %o1		! do we ack this int?
	bnz,a	0f
	st	%o1, [%g1 + 4]		! ack the int, if latched
0:
	bnz,a	0f
	ld	[%g1], %g0		! wait for change to propagate through
0:
	!
	! Re-assign interrupt target register (ITR) to the next available
	! CPU.  This allows multiple interrupts at different levels to be
	! handled simultaneously on different CPU's and needs to be done
	! with all traps disabled.
	!
	! set_itr() assumes %o2 contains CPU id and it is trashed
	! on return.
	!
	call	set_itr
	nop

	!
	! Get handler address for level.
	!
	set	_int_vector, %g1
	ld	[%g1 + %l3], %l3	! grab vector

#ifdef TRAPTRACE
	!
	! make trace entry - helps in debugging watchdogs
	!
	TRACE_PTR(%g1, %g2)		! get trace pointer
	CPU_INDEX(%o1)
	sll	%o1, 2, %o1		! get index to trap_last_intr
	set	_trap_last_intr, %g2
	st	%g1, [%g2 + %o1]	! save last interrupt trace record
	mov	TT_INTR_ENT, %g2
	st	%g2, [%g1 + TRAP_ENT_TBR]
	st	%l0, [%g1 + TRAP_ENT_PSR]
	st	%l1, [%g1 + TRAP_ENT_PC]
	st	%sp, [%g1 + TRAP_ENT_SP]
	st	%g7, [%g1 + TRAP_ENT_G7]
	mov	%psr, %g2
	st	%g2, [%g1 + 0x14]
	mov	%tbr, %g2
	st	%g2, [%g1 + 0x18]
	GETSIPR(%g2)
	st	%g2, [%g1 + 0x1c]
	TRACE_NEXT(%g1, %o1, %g2)	! set new trace pointer
#endif	/* TRAPTRACE */
	!
	! On board interrupt.
	! Due to a bug in the IU, we cannot increase the PIL and
	! enable traps at the same time. In effect, ET changes
	! slightly before the new PIL becomes effective.
	! So we write the psr twice, once to set the PIL and
	! once to set ET.
	!
	mov	%l0, %psr		! set level (IU bug)
	wr	%l0, PSR_ET, %psr	! enable traps
	nop
	TRACE_ASM_1 (%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);
	call	%l3			! interrupt handler
	nop				! note the location being called here
					! does not do a save!
	!
	! Return from interrupt - this is return from call above or
	! just jumpped to.
	!
	! %l4, %l6 and %l7 must remain unchanged since they contain
	! information required after returning from the trap handler.
	!
	! Note that intr_passivate(), below, relies on values in %l4 and %l7.
	!
.int_rtt:
	!
	! If %l6 == 1, then the hard interrupt had been masked off so
	! unmask by writing to the system interrupt target mask register.
	!
	cmp	%l6, 0x1		! is the interrupt masked off?
	bne	1f
	sethi	%hi(intr_level_mask), %o0
	or	%o0, %lo(intr_level_mask), %o0
	sll	%l4, 2, %l3
	ld	[%o0 + %l3], %o0
	sethi	%hi(v_sipr_addr), %o4
	ld	[%o4 + %lo(v_sipr_addr)], %o4
	st	%o0, [%o4 + IR_CLEAR_OFFSET]
1:

#ifdef TRAPTRACE
	!
	! make trace entry - helps in debugging watchdogs
	!
	mov	%psr, %g1
	wr	%g1, PSR_ET, %psr	! disable traps
	ROSS625_PSR_NOP(); ROSS625_PSR_NOP(); ROSS625_PSR_NOP()
	TRACE_PTR(%l5, %g2)		! get trace pointer
	CPU_INDEX(%o1)
	sll	%o1, 2, %o1		! get index to trap_last_intr2
	set	_trap_last_intr2, %g2
	st	%l5, [%g2 + %o1]	! save last interrupt trace record
	mov	TT_INTR_RET, %g2	! interrupt return
	st	%g2, [%l5 + TRAP_ENT_TBR]
	st	%g1, [%l5 + TRAP_ENT_PSR]
	clr	[%l5 + TRAP_ENT_PC]
	st	%sp, [%l5 + TRAP_ENT_SP]
	st	%g7, [%l5 + TRAP_ENT_G7]
	st	%l7, [%l5 + 0x14]	! saved %sp
	GETSIPR(%g2)			! save sipr instead of nothing
	st	%g2, [%l5 + 0x18]
	ld	[THREAD_REG + T_INTR], %g2
	st	%g2, [%l5 + 0x1c]	! thread underneath
	TRACE_NEXT(%l5, %o1, %g2)	! set new trace pointer
	wr	%g1, %psr		! enable traps
	ROSS625_PSR_NOP(); ROSS625_PSR_NOP(); ROSS625_PSR_NOP()
#endif	/* TRAPTRACE */

	TRACE_ASM_0 (%o1, TR_FAC_INTR, TR_INTR_END, TR_intr_end);

	ld	[THREAD_REG + T_CPU], %g1	! get CPU pointer
	ld	[%g1 + CPU_SYSINFO_INTR], %g2	! cpu_sysinfo.intr++
	inc	%g2
	st	%g2, [%g1 + CPU_SYSINFO_INTR]
	ld	[%g1 + CPU_SYSINFO_INTRTHREAD], %g2	! cpu_sysinfo.intrthread++
	inc	%g2
	st	%g2, [%g1 + CPU_SYSINFO_INTRTHREAD]

	!
	! block interrupts to protect the interrupt thread pool.
	! XXX - May be able to avoid this by assigning a thread per level.
	!
	mov	%psr, %l5
	andn	%l5, PSR_PIL, %l5	! mask out old PIL
	or	%l5, LOCK_LEVEL << PSR_PIL_BIT, %l5
	mov	%l5, %psr	 	! mask all interrupts
	nop; nop			! psr delay
	!
	! If there is still an interrupted thread underneath this one,
	! then the interrupt was never blocked or released and the
	! return is fairly simple.  Otherwise jump to intr_thread_exit.
	! 
	ld	[THREAD_REG + T_INTR], %g2	! psr delay
	nop					! psr delay (tst sets cc's)
	tst	%g2				! psr delay
	bz	intr_thread_exit		! no interrupted thread
	ld	[THREAD_REG + T_CPU], %g1	! delay - load CPU pointer

	!
	! link the thread back onto the interrupt thread pool
	!
	ld	[%g1 + CPU_INTR_THREAD], %l6
	st	%l6, [THREAD_REG + T_LINK]
	st	THREAD_REG, [%g1 + CPU_INTR_THREAD]
	!	
	!	Set the thread state to free so kadb doesn't see it
	!
	mov	FREE_THREAD, %l6
	st	%l6, [THREAD_REG + T_STATE]
	!
	! Switch back to the interrupted thread
	!
	st	%g2, [%g1 + CPU_THREAD]
	mov	%g2, THREAD_REG

	b	_sys_rtt		! restore previous stack pointer
	mov	%l7, %sp		! restore %sp

	!
	! An interrupt returned on what was once (and still might be)
	! an interrupt thread stack, but the interrupted process is no longer
	! there.  This means the interrupt must've blocked or called
	! release_interrupt().  
	!
	! There is no longer a thread under this one, so put this thread back 
	! on the CPU's free list and resume the idle thread which will dispatch
	! the next thread to run.
	!
	! All interrupts are disabled here (except machine checks), but traps
	! are enabled.
	!
	! Entry:
	!	%g1 = CPU pointer.
	!	%l4 = interrupt level (1-15)
	!
intr_thread_exit:
	TRACE_ASM_0 (%o1, TR_FAC_INTR, TR_INTR_EXIT, TR_intr_exit);
        ld      [%g1 + CPU_SYSINFO_INTRBLK], %g2   ! cpu_sysinfo.intrblk++
        inc     %g2
        st      %g2, [%g1 + CPU_SYSINFO_INTRBLK]
#ifdef TRAPTRACE
	mov	%psr, %l5
	andn	%l5, PSR_ET, %o4	! disable traps
	mov	%o4, %psr
	nop; nop; nop;
	TRACE_PTR(%o4, %o5)		! get trace pointer
	set	TT_INTR_EXIT, %o5
	st	%o5, [%o4 + TRAP_ENT_TBR]
	st	%l0, [%o4 + TRAP_ENT_PSR]
	st	%l1, [%o4 + TRAP_ENT_PC]
	st	%sp, [%o4 + TRAP_ENT_SP]
	st	%g7, [%o4 + TRAP_ENT_G7]
	st	%l4, [%o4 + 0x14]
	ld	[%g7 + T_CPU], %o5
	ld	[%o5 + CPU_BASE_SPL], %o5
	st	%o5, [%o4 + 0x18]
	TRACE_NEXT(%o4, %l6, %o5)	! set new trace pointer
	mov	%l5, %psr
	nop; nop; nop;
#endif
	!
	! Put thread back on the either the interrupt thread list if it is
	! still an interrupt thread, or the CPU's free thread list, if it did a
	! release interrupt.
	!
	lduh	[THREAD_REG + T_FLAGS], %l5
	btst	T_INTR_THREAD, %l5		! still an interrupt thread?
	bz	1f				! No, so put back on free list
	mov	1, %o4				! delay

	!
	! This was an interrupt thread, so clear the pending interrupt flag
	! for this level.
	!
	ld	[%g1 + CPU_INTR_ACTV], %o5	! get mask of interrupts active
	sll	%o4, %l4, %o4			! form mask for level
	andn	%o5, %o4, %o5			! clear interrupt flag
	call	.intr_set_spl			! set CPU's base SPL level
	st	%o5, [%g1 + CPU_INTR_ACTV]	! delay - store active mask

	!
	! Set the thread state to free so kadb doesn't see it
	!
	mov	FREE_THREAD, %l6
	st	%l6, [THREAD_REG + T_STATE]
	!
	! Put thread on either the interrupt pool or the free pool and
	! call swtch() to resume another thread.
	!
	ld	[%g1 + CPU_INTR_THREAD], %l4	! get list pointer
	st	%l4, [THREAD_REG + T_LINK]
	call	swtch				! switch to best thread
	st	THREAD_REG, [%g1 + CPU_INTR_THREAD] ! delay - put thread on list

	b,a	.				! swtch() shouldn't return
1:
	mov	TS_ZOMB, %l6			! set zombie so swtch will free
	call	swtch				! run next process - free thread
	st	%l6, [THREAD_REG + T_STATE]	! delay - set state to zombie

	SET_SIZE(_interrupt)
/* end interrupt */

#endif	/* lint */

/*
 * level13_trap - check for a cpu-dependent level13 cross-call
 * fastpath.  If one exists, use it.
 */
#if defined(lint)
void (*level13_fasttrap_handler)(void);

void
level13_trap(void)
{ }
#else /* lint */
        .seg    ".data"
	.global level13_fasttrap_handler
level13_fasttrap_handler:
	.word	0
        .seg    ".text"

	ENTRY_NP(level13_trap)

	sethi	%hi(level13_fasttrap_handler), %l4
	ld	[%l4 + %lo(level13_fasttrap_handler)], %l4
	tst	%l4
	beq,a	_sys_trap		! No, let sys_trap service it
	mov	(T_INTERRUPT | 13), %l4	! pass the intr # in %l4

	jmp	%l4
	nop
	SET_SIZE(level13_trap)
#endif /* lint */
	

#if defined(lint)
void
level14_trap(void)
{ }

#else /* lint */

	/* 
	 * level14_trap. This routine has been defined as part of the fix
	 * for bug# 1168398.
	 *
	 * Come here for each level14 interrupt. This bit of code figures
	 * out if the PROM should service a hardware level 14 interrupt or not. 
	 * This is needed during booting for the PROM to keep track of time.
	 * The use of the PROM vs. kernel to handle the trap is controlled
	 * by the mon_clock_on flag set and reset by start_mon_clock and
	 * stop_mon_clock in machdep.c. The PROM, however, only handles the
	 * hardware interrupt and not the soft interrupt used for a "poke"
	 * between processors. Therefore it is necessary to make sure that
	 * soft level 14 interrupts are not passed off to the PROM to service.
 	 *
	 * Registers:
	 * %l0 = %psr
	 * %l1 = old pc
	 * %l2 = old npc
	 * %l3 = scratch
	 * %l4 = scratch, but must be == T_INTERRUPT | 14 if we go to _sys_trap
	 */

	ENTRY_NP(level14_trap)

	CPU_INDEX(%l3)			! get CPU number in l3

	sll	%l3, 2, %l3		! make word offset
	set	v_interrupt_addr, %l4	! base of #cpu ptrs to interrupt regs
	add	%l4, %l3, %l4		! ptr to our ptr
	ld	[%l4], %l4		! %l4 = &cpu's intr pending reg
	tst	%l4			! 0 means it's not set yet and
	bz	mon_level14		! the monitor gets everything
	nop

	ld	[%l4], %l4		! %l4 = interrupts pending
	set	IR_SOFT_INT(14), %l3	! Soft interrupts are in bits <17-31>
	andcc	%l3, %l4, %g0		! Is soft level 14 interrupt pending?
	bnz,a	_sys_trap		! Yes, let sys_trap service it
	mov	(T_INTERRUPT | 14), %l4	! pass the intr # in %l4

	set	(0x01 << 14), %l3	! Mask for hardware level 14 intr
	andcc	%l3, %l4, %g0		! Level 14 hardware intr pending?
	bz	_sys_trap		! no - this shouldn't happen but we
	mov	(T_INTERRUPT | 14), %l4	! let sys trap handle this.

	sethi	%hi(mon_clock_on), %l3
	ldub	[%l3 + %lo(mon_clock_on)], %l3	! l3 = mon_clock_on
	tst	%l3
					! 0 = use kernel trap handler
	bz	_sys_trap		! sys_trap would have been the place we
	mov	(T_INTERRUPT | 14), %l4	! went to normally

mon_level14:
	mov	%l0, %psr		! Restore PSR
	nop;nop;nop;			! PSR delay
	ba	mon_clock14_vec		! yes - go to copied monitor vector
	nop				! which will take us to the monitor
					! who will do an rtt when it's done
	SET_SIZE(level14_trap)

#endif	/* lint */

/*
 * Set CPU's base SPL level, based on which interrupt levels are active.
 * 	Called at spl7 or above.
 */

#if defined(lint)

void
set_base_spl(void)
{}

#else	/* lint */

	ENTRY_NP(set_base_spl)
	save	%sp, -SA(MINFRAME), %sp		! get a new window
	ld	[THREAD_REG + T_CPU], %g1	! load CPU pointer
	call	.intr_set_spl			! real work done there
	ld	[%g1 + CPU_INTR_ACTV], %o5	! load active interrupts mask
	ret
	restore

/*
 * WARNING: non-standard callinq sequence; do not call from C
 *	%g1 = pointer to CPU
 *	%o5 = updated CPU_INTR_ACTV
 *	uses %l6, %l3
 */
.intr_set_spl:					! intr_thread_exit enters here
	!
	! Determine highest interrupt level active.  Several could be blocked
	! at higher levels than this one, so must convert flags to a PIL
	! Normally nothing will be blocked, so test this first.
	!
	tst	%o5
	bz	2f				! nothing active
	sra	%o5, 11, %l6			! delay - set %l6 to bits 15-11
	set	_intr_flag_table, %l3
	tst	%l6				! see if any of the bits set
	ldub	[%l3 + %l6], %l6		! load bit number
	bnz,a	1f				! yes, add 10 and we're done
	add	%l6, 11-1, %l6			! delay - add bit number - 1

	sra	%o5, 6, %l6			! test bits 10-6
	tst	%l6
	ldub	[%l3 + %l6], %l6
	bnz,a	1f
	add	%l6, 6-1, %l6

	sra	%o5, 1, %l6			! test bits 5-1
	ldub	[%l3 + %l6], %l6

	!
	! highest interrupt level number active is in %l6
	!
1:
	cmp	%l6, CLOCK_LEVEL		! don't block clock interrupts,
	bz,a	3f
	sub	%l6, 1, %l6			!   instead drop PIL one level
3:
	sll	%l6, PSR_PIL_BIT, %o5		! move PIL into position
2:
	retl
	st	%o5, [%g1 + CPU_BASE_SPL]	! delay - store base priority
	SET_SIZE(set_base_spl)

/*
 * Table that finds the most significant bit set in a five bit field.
 * Each entry is the high-order bit number + 1 of it's index in the table.
 * This read-only data is in the text segment.
 */
_intr_flag_table:
	.byte	0, 1, 2, 2,	3, 3, 3, 3,	4, 4, 4, 4,	4, 4, 4, 4
	.byte	5, 5, 5, 5,	5, 5, 5, 5,	5, 5, 5, 5,	5, 5, 5, 5
	.align	4

#endif	/* lint */

/*
 * int
 * intr_passivate(from, to)
 *	kthread_id_t	from;		interrupt thread
 *	kthread_id_t	to;		interrupted thread
 *
 * Gather state out of partially passivated interrupt thread.
 * Caller has done a flush_windows();
 *
 * RELIES ON REGISTER USAGE IN interrupt(), above.
 * In interrupt(), %l7 contained the pointer to the save area of the
 * interrupted thread.  Now the bottom of the interrupt thread should
 * contain the save area for that register window. 
 *
 * Gets saved state from interrupt thread which belongs on the
 * stack of the interrupted thread.  Also returns interrupt level of
 * the thread.
 */

#if defined(lint)

/* ARGSUSED */
int
intr_passivate(kthread_id_t from, kthread_id_t to)
{ return (0); }

#else	/* lint */

	ENTRY_NP(intr_passivate)
	save	%sp, -SA(MINFRAME), %sp	! get a new window 
	call	flush_windows		! force register windows to stack
	!
	! restore registers from the base of the stack of the interrupt thread.
	!
	ld	[%i0 + T_STACK], %i2	! get stack save area pointer
	ldd	[%i2 + (0*4)], %l0	! load locals
	ldd	[%i2 + (2*4)], %l2
	ldd	[%i2 + (4*4)], %l4
	ldd	[%i2 + (6*4)], %l6
	ldd	[%i2 + (8*4)], %o0	! put ins from stack in outs
	ldd	[%i2 + (10*4)], %o2
	ldd	[%i2 + (12*4)], %o4
	ldd	[%i2 + (14*4)], %i4	! copy stack/pointer without using %sp
	!
	! put registers into the save area at the top of the interrupted
	! thread's stack, pointed to by %l7 in the save area just loaded.
	!
	std	%l0, [%l7 + (0*4)]	! save locals
	std	%l2, [%l7 + (2*4)]
	std	%l4, [%l7 + (4*4)]
	std	%l6, [%l7 + (6*4)]
	std	%o0, [%l7 + (8*4)]	! save ins using outs
	std	%o2, [%l7 + (10*4)]
	std	%o4, [%l7 + (12*4)]
	std	%i4, [%l7 + (14*4)]	! fp, %i7 copied using %i4

	set	_sys_rtt-8, %o3		! routine will continue in _sys_rtt.
	clr	[%i2 + ((8+6)*4)]	! clear frame pointer in save area
	st	%o3, [%i1 + T_PC]	! setup thread to be resumed.
	st	%l7, [%i1 + T_SP]
	ret
	restore	%l4, 0, %o0		! return interrupt level from %l4
	SET_SIZE(intr_passivate)

#endif	/* lint */

/*
 * Return a thread's interrupt level.
 * Since this isn't saved anywhere but in %l4 on interrupt entry, we
 * must dig it out of the save area.
 *
 * Caller 'swears' that this really is an interrupt thread.
 *
 * int
 * intr_level(t)
 *	kthread_id_t	t;
 */

#if defined(lint)

/* ARGSUSED */
int
intr_level(kthread_id_t t)
{ return (0); }

#else	/* lint */

	ENTRY_NP(intr_level)
	save	%sp, -SA(MINFRAME), %sp	! get a new window 
	call	flush_windows		! force register windows into stack
	ld	[%i0 + T_STACK], %o2	! get stack save area pointer
	ld	[%o2 + 4*4], %i0	! get interrupt level from %l4 on stack
	ret				! return
	restore
	SET_SIZE(intr_level)

#endif	/* lint */

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


/*
 * Spurious trap... 'should not happen'
 * %l4 - processor interrupt level
 */
#if defined(lint)

void
spurious(void)
{}

#else	/* lint */

	ENTRY_NP(spurious)
	set	1f, %o0
	call	printf
	mov	%l4, %o1
	b,a	.interrupt_ret
	.seg	".data"
1:	.asciz	"spurious interrupt at processor level %d\n"
	.seg	".text"
	SET_SIZE(spurious)

/*
 * Macro for autovectored interrupts.
 */
#if 1
/*
 * It is the responsibility of any driver that does dma to call
 * ddi_dma_sync(..., DDI_DMA_SYNC_FORCPU) when it needs write buffers to be
 * flushed.
 */
#define SYNCWB
#else
/*
 * SYNCWB: synchronize write buffers
 *
 * Galaxy requires that we synchronize write buffers
 * before dispatching to device drivers, so they
 * do not need to know to do this. Reading the
 * M-to-S Async Fault Status Register three times
 * is sufficient.
 *
 * %%% this needs to be fixed so that it does
 * nothing on small4m, preferably by mapping
 * a well-known virtual address which reading
 * causes nothing to happen.
 */
#define	SYNCWB								\
	call	flush_writebuffers					;\
	nop

#define	OLDSYNCWB							\
	set	0xE0001000, %l5						;\
	lda	[%l5]0x2f, %g0						;\
	lda	[%l5]0x2f, %g0						;\
	lda	[%l5]0x2f, %g0	
#endif

/*
 * IOINTR: Generic hardware interrupt.
 *	branch to interrupt_ret after successful processing
 *	if nobody in list claims interrupt,
 *	report spurious and branch to interrupt_ret.
 */
#define IOINTR(LEVEL)							\
	set	level/**/LEVEL, %l5 /* get vector ptr */		;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	ld	[%l5 + AV_MUTEX], %l2					;\
	tst	%l2							;\
	bz	2f		/* no guard needed */			;\
	nop								;\
	call	mutex_enter						;\
	mov	%l2, %o0						;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	mov	%o0, %l3	/* save return value */			;\
	call	mutex_exit						;\
	mov	%l2, %o0						;\
	b	3f							;\
	nop								;\
2:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	mov	%o0, %l3	/* save return value */			;\
3:									;\
	tst	%l3		/* success? */				;\
	bz,a	1b		/* no, try next one */			;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %l3			;\
	b	.interrupt_ret	/* done */				;\
	clr	[%l3 + %lo(level/**/LEVEL/**/_spurious)]		;\
4:									;\
	b,a	.interrupt_ret						;\
	nop

#define IOINTR_ABOVE_LOCK(LEVEL)					\
	set	level/**/LEVEL, %l5 /* get vector ptr */		;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	mov	%o0, %l3	/* save return value */			;\
3:									;\
	tst	%l3		/* success? */				;\
	bz,a	1b		/* no, try next one */			;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %l3			;\
	b	.interrupt_ret	/* done */				;\
	clr	[%l3 + %lo(level/**/LEVEL/**/_spurious)]		;\
4:									;\
	b,a	.interrupt_ret						;\
	nop

/*
 * OBINTR: onboard interrupt
 *	branch to interrupt_ret after successful processing
 *	drop out bottom if nobody in list claims interrupt
 */
#define	OBINTR(LEVEL,MASK)						\
	GETSIPR(%g1)							;\
	set	MASK,%l5						;\
	andcc	%g1, %l5, %g0		/* see if masked bit(s) set */	;\
	bz	5f			/* if not, skip around. */	;\
	clr	%l2							;\
	set	olvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	ld	[%l5 + AV_MUTEX], %o0					;\
	tst	%o0							;\
	bz	2f		/* no guard needed */			;\
	nop								;\
	call	mutex_enter						;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	add	%o0, %l2, %l2	/* add in return value */		;\
	call	mutex_exit						;\
	ld	[%l5 + AV_MUTEX], %o0					;\
	b	3f							;\
	nop								;\
2:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	add	%o0, %l2, %l2	/* add in return value */		;\
3:									;\
	GETSIPR(%g1)							;\
	set	MASK,%l3						;\
	andcc	%g1, %l3, %g0		/* see if masked bit(s) set */	;\
	bnz,a	1b			/* if yes, keen going. */	;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, step to next*/	;\
4: 	/* spurious? */							;\
	sethi	%hi(olvl/**/LEVEL/**/_spurious), %l3			;\
	tst	%l2		/* success? */				;\
	bnz,a	5f		/* all done! */				;\
	clr	[%l3 + %lo(olvl/**/LEVEL/**/_spurious)]			;\
	set	busname_ovec, %o2					;\
	set	LEVEL, %o1						;\
	call	not_serviced						;\
	or	%l3, %lo(olvl/**/LEVEL/**/_spurious), %o0		;\
5:

#define	OBINTR_ABOVE_LOCK(LEVEL,MASK)					\
	GETSIPR(%g1)							;\
	set	MASK,%l5						;\
	andcc	%g1, %l5, %g0		/* see if masked bit(s) set */	;\
	bz	5f			/* if not, skip around. */	;\
	clr	%l2							;\
	set	olvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	add	%o0, %l2, %l2	/* add in return value */		;\
3:									;\
	GETSIPR(%g1)							;\
	set	MASK,%l3						;\
	andcc	%g1, %l3, %g0		/* see if masked bit(s) set */	;\
	bnz,a	1b			/* if yes, keen going. */	;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, step to next*/	;\
	sethi	%hi(olvl/**/LEVEL/**/_spurious), %l3			;\
	tst	%l2		/* success? */				;\
	bnz,a	5f		/* Yes, leave */			;\
	clr	[%l3 + %lo(olvl/**/LEVEL/**/_spurious)]			;\
4: 	/* spurious */							;\
	set	busname_ovec, %o2					;\
	set	LEVEL, %o1						;\
	sethi	%hi(olvl/**/LEVEL/**/_spurious), %l3			;\
	call	not_serviced						;\
	or	%l3, %lo(olvl/**/LEVEL/**/_spurious), %o0		;\
5:

/*
 * OBINTR_FOUND_ABOVE_LOCK allows us to handle level 14 (profiling)
 * interrupts despite their non-appearance in the system interrupt pending
 * register.
 */
#define OBINTR_FOUND_ABOVE_LOCK(LEVEL)					\
	set	olvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	mov	%o0, %l3	/* save return value */			;\
3:									;\
	tst	%l3		/* success? */				;\
	bz,a	1b		/* no, try next one */			;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, step to next*/	;\
	sethi	%hi(olvl/**/LEVEL/**/_spurious), %l3			;\
	b	.interrupt_ret	/* all done! */				;\
	clr	[%l3 + %lo(olvl/**/LEVEL/**/_spurious)]			;\
4: 	/* spurious */							;\
	set	olvl/**/LEVEL/**/_spurious, %o0				;\
	set	busname_ovec, %o2					;\
	set	LEVEL, %o1						;\
	call	not_serviced						;\
	nop								;\
	b,a	.interrupt_ret						;\
	nop								;\
5:

/*
 * SBINTR: SBus interrupt
 *	if no sbus int pending, drop out bottom
 *	go around until all the handlers are called if interrupt pending
 *	"spurious sbus level LEVEL" if no one claims it
 */
#define	SBINTR(LEVEL)							\
	GETSIPR(%g1)							;\
	srl	%g1, 7, %g1						;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if sbus int pending */	;\
	bz	5f			/* if not, skip around. */	;\
	clr	%l2							;\
	set	slvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	ld	[%l5 + AV_MUTEX], %o0					;\
	tst	%o0							;\
	bz	2f		/* no guard needed */			;\
	nop								;\
	call	mutex_enter						;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	add	%o0, %l2, %l2	/* add in return value */		;\
	call	mutex_exit						;\
	ld	[%l5 + AV_MUTEX], %o0					;\
	b	3f							;\
	nop								;\
2:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	add	%o0, %l2, %l2	/* add in return value */		;\
3:									;\
	GETSIPR(%g1)							;\
	srl	%g1, 7, %g1						;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if sbus int pending */	;\
	bnz,a	1b		/* no, try next one */			;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
4: 	/* spurious */							;\
	sethi	%hi(slvl/**/LEVEL/**/_spurious), %l3			;\
	tst	%l2		/* success? */				;\
	bnz,a	5f		/* all done! */				;\
	clr	[%l3 + %lo(slvl/**/LEVEL/**/_spurious)]			;\
	set	busname_svec, %o2					;\
	set	LEVEL, %o1						;\
	call	not_serviced						;\
	or	%l3, %lo(slvl/**/LEVEL/**/_spurious), %o0		;\
5:

#define	SBINTR_ABOVE_LOCK(LEVEL)					 \
	GETSIPR(%g1)							;\
	srl	%g1, 7, %g1						;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if sbus int pending */	;\
	bz	5f			/* if not, skip around. */	;\
	clr	%l2							;\
	set	slvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f							;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	add	%o0, %l2, %l2	/* add in return value */		;\
3:									;\
	GETSIPR(%g1)							;\
	srl	%g1, 7, %g1						;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if sbus int pending */	;\
	bnz	1b			/* if yes, keep going */	;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
	sethi	%hi(slvl/**/LEVEL/**/_spurious), %l3			;\
	tst	%l2		/* success? */				;\
	bnz,a	5f		/* yes, drop through */			;\
	clr	[%l3 + %lo(slvl/**/LEVEL/**/_spurious)]			;\
4: 	/* spurious */							;\
	set	busname_svec, %o2					;\
	set	LEVEL, %o1						;\
	sethi	%hi(slvl/**/LEVEL/**/_spurious), %l3			;\
	call	not_serviced						;\
	or	%l3, %lo(slvl/**/LEVEL/**/_spurious), %o0		;\
5:

/*
 * VBINTR: VME interrupt
 *	if no vme int pending, drop out bottom
 *	flush write buffers and check again
 *	get vme vector
 *	if someone interested in this vector, call him;
 *	   if he handled it, branch to interrupt_ret
 *	if anyone on vme list claims it, branch to interrupt_ret
 *
 *	caveat:
 *	due to signal propagation after flushing the system buffers,
 *	we can get into the situation where we trigger another
 *	VMEbus interrupt acknowledge cycle after the interrupt source
 *	has gone away.  This results in a VMEbus timeout which results
 *	in not modifing the destination register on the load from
 *	the VMEbus interrupt ack/vector register.  We then fall through
 *	the interrupt dispatch code and we ignore what has turned
 *	out to be a null interrupt.
 *	(see mach_4m.c:sun4m_handle_ebe for timeout debugging code)
 */
#define	VBINTR(LEVEL)							\
	GETSIPR(%g1)							;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if vme int pending */	;\
	bz	9f			/* if not, skip around. */	;\
	nop								;\
	call	flush_vme_writebuffers					;\
	sethi	%hi(v_vmectl_addr), %l5	/* get v_vmectl_addr[1] */	;\
	GETSIPR(%g1)							;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if int still pending */	;\
	bz	9f			/* if not, skip around. */	;\
	or	%l5, %lo(v_vmectl_addr), %l5				;\
	ld	[%l5 + 4], %l5						;\
	add	%l5, ((LEVEL<<1)+1), %l5				;\
	ldub	[%l5], %l5		/* VMEbus int acknowledge */	;\
	cmp	%l5, VEC_MAX	/* if vector number too high, */	;\
	bg	3f		/* ... not a vectored interrupt. */	;\
	subcc	%l5, VEC_MIN, %g3	/* tables start at VEC_MIN */	;\
	bl	3f		/* vector too low, ignore. */		;\
	sll	%g3, 2, %g2	/* %g2 is 4 * %g3 */			;\
	sll	%g3, 3, %g3	/* %g3 is 8 * old %g3 */		;\
	add	%g3, %g2, %g3	/* now %g3 has 12 * old %g3! */		;\
	set	vme_vector, %g2 /* table of interrupt vectors */	;\
	ld	[%g2 + %g3], %l3	/* get handler address */	;\
	tst	%l3			/* make sure it is non-zero */	;\
        be      3f							;\
	add	%g2, 4, %g2	/* generate address of arg ptr */	;\
	ld	[%g2 + %g3], %l1 /* get arg ptr into safe place */	;\
	add	%g2, 4, %g2	/* generate address of mutex arg */	;\
	ld	[%g2 + %g3], %l2	/* and get possible mutex */	;\
	tst	%l2			/* mutex to grab? */		;\
	be	1f                      /* nope.. */			;\
        nop								;\
        call    mutex_enter             /* get it */			;\
        mov     %l2, %o0                /* yep. */			;\
1:      call    %l3                     /* call routine */		;\
        mov     %l1, %o0                /* pass argument */		;\
        tst     %l2							;\
	be      2f							;\
        mov     %o0, %l1    /* save return value from int handler */	;\
        call    mutex_exit	/* release mutex */			;\
        mov     %l2, %o0 						;\
2:      tst     %l1							;\
        be      8f		/* driver didn't claim the interrupt */	;\
        nop								;\
	b	7f		/* all done, ready to exit .. */	;\
	nop								;\
3:									;\
	/* this is for autovector ... */				;\
	set	vlvl/**/LEVEL, %l5 /* get vector ptr */			;\
4:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3		/* end of vector? */			;\
	bz	8f		/* if it is, */				;\
	ld	[%l5 + AV_MUTEX], %l2					;\
	tst	%l2							;\
	bz	5f		/* no guard needed */			;\
	nop								;\
	call	mutex_enter						;\
	mov	%l2, %o0						;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	mov	%o0, %l3	/* save return value */			;\
	call	mutex_exit						;\
	mov	%l2, %o0						;\
	b	6f							;\
	nop								;\
5:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	mov	%o0, %l3	/* save return value */			;\
6:									;\
	tst	%l3							;\
	bz,a	4b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
7:	/* this is the exit pt for VME interrupts ... */		;\
	call    flush_vme_writebuffers					;\
	sethi	%hi(vlvl/**/LEVEL/**/_spurious), %l3			;\
	clr	[%l3 + %lo(vlvl/**/LEVEL/**/_spurious)]			;\
	b,a	.interrupt_ret		/* really all done! */		;\
	.empty								;\
8: 	/* spurious */							;\
	set	vlvl/**/LEVEL/**/_spurious, %o0				;\
	set	busname_vvec, %o2					;\
	set	LEVEL, %o1						;\
	call	not_serviced						;\
	nop								;\
	b,a	.interrupt_ret						;\
9:

#define	VBINTR_ABOVE_LOCK(LEVEL)					\
	GETSIPR(%g1)							;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if vme int pending */	;\
	bz	9f			/* if not, skip around. */	;\
	nop								;\
	call	flush_vme_writebuffers					;\
	sethi	%hi(v_vmectl_addr), %l5	/* get v_vmectl_addr[1] */	;\
	GETSIPR(%g1)							;\
	andcc	%g1, 1<<(LEVEL-1), %g0	/* see if int still pending */	;\
	bz	9f			/* if not, skip around. */	;\
	or	%l5, %lo(v_vmectl_addr), %l5				;\
	ld	[%l5 + 4], %l5						;\
	add	%l5, ((LEVEL<<1)+1), %l5				;\
	ldub	[%l5], %l5						;\
	cmp	%l5, VEC_MAX	/* if vector number too high, */	;\
	bg	3f		/* ... not a vectored interrupt. */	;\
	subcc	%l5, VEC_MIN, %g3	/* tables start at VEC_MIN */	;\
	bl	3f		/* vector too low, ignore. */		;\
	sll	%g3, 2, %g2	/* %g2 is 4 * %g3 */			;\
	sll	%g3, 3, %g3	/* %g3 is 8 * old %g3 */		;\
	add	%g3, %g2, %g3	/* now %g3 has 12 * old %g3! */		;\
	set	vme_vector, %g2 /* table of interrupt vectors */	;\
	ld	[%g2 + %g3], %l6	/* get handler address */	;\
	tst	%l6			/* make sure it is non-zero */	;\
        be      3f							;\
	add	%g2, 4, %g2	/* generate address of arg ptr */	;\
	ld	[%g2 + %g3], %l1 /* get arg ptr into safe place */	;\
1:      call    %l6                     /* call routine */		;\
        mov     %l1, %o0                /* pass argument */		;\
2:      tst     %o0		/* tst return value */			;\
        be      8f		/* driver didn't claim the interrupt */	;\
        nop								;\
	b	7f		/* all done, ready to exit .. */	;\
	nop								;\
3:									;\
	/* this is for autovector ... */				;\
	set	vlvl/**/LEVEL, %l5 /* get vector ptr */			;\
4:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3		/* end of vector? */			;\
	bz	8f		/* if it is, */				;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
6:									;\
	tst	%o0							;\
	bz,a	4b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
7:	/* this exit pt for VME interrupts ... */			;\
	call    flush_vme_writebuffers					;\
	sethi	%hi(vlvl/**/LEVEL/**/_spurious), %l3			;\
	clr	[%l3 + %lo(vlvl/**/LEVEL/**/_spurious)]			;\
	b,a	.interrupt_ret		/* really all done! */		;\
	.empty								;\
8: 	/* spurious */							;\
	set	vlvl/**/LEVEL/**/_spurious, %o0				;\
	set	busname_vvec, %o2					;\
	set	LEVEL, %o1						;\
	call	not_serviced						;\
	nop								;\
	b	.interrupt_ret						;\
	nop								;\
9:

/*
 * SOINTR: software interrupt
 * call ALL attached routines
 * branch to interrupt_ret when done.
 * NOTE: scans the "xlvl" list AND the "level" list.
 * NOTE: silently ignore spurious soft ints.
 */
#define	SOINTR(LEVEL)							\
	set	xlvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	beq	3f			/* try level list */		;\
	nop								;\
	ld	[%l5 + AV_MUTEX], %l2					;\
	tst	%l2							;\
	bz	2f		/* no guard needed */			;\
	nop								;\
	call	mutex_enter						;\
	mov	%l2, %o0						;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	call	mutex_exit						;\
	mov	%l2, %o0						;\
	b	1b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
2:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	b	1b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
3:									;\
	set	level/**/LEVEL, %l5 /* get vector ptr */		;\
4:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	beq	6f			/* done */			;\
	nop								;\
	ld	[%l5 + AV_MUTEX], %l2					;\
	tst	%l2							;\
	bz	5f		/* no guard needed */			;\
	nop								;\
	call	mutex_enter						;\
	mov	%l2, %o0						;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	call	mutex_exit						;\
	mov	%l2, %o0						;\
	b	4b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
5:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	b	4b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
6:									;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %g1			;\
	clr	[%g1 + %lo(level/**/LEVEL/**/_spurious)]		;\
	b,a	.interrupt_ret						;\
	nop

#define	SOINTR_ABOVE_LOCK(LEVEL)					\
	set	xlvl/**/LEVEL, %l5 /* get vector ptr */			;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	beq	3f			/* try level list */		;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	b	1b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
3:									;\
	set	level/**/LEVEL, %l5 /* get vector ptr */		;\
4:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	beq	6f			/* done */			;\
	nop								;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	b	4b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
5:									;\
	call	%l3		/* go there */				;\
	ld	[%l5 + AV_INTARG], %o0					;\
	b	4b							;\
	add     %l5, AUTOVECSIZE, %l5   /* delay slot, find next */	;\
6:									;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %g1			;\
	clr	[%g1 + %lo(level/**/LEVEL/**/_spurious)]		;\
	b,a	.interrupt_ret						;\
	nop

.softlvl1:
#ifdef MP
	call	xc_serv
	mov	X_CALL_LOPRI, %o0
#endif MP
	SOINTR(1)
.softlvl2:
	SOINTR(2)
.softlvl3:
	SOINTR(3)
.softlvl4:
	SOINTR(4)
.softlvl5:
	SOINTR(5)
.softlvl6:
	SOINTR(6)
.softlvl7:
	SOINTR(7)
.softlvl8:
	SOINTR(8)
.softlvl9:
	SOINTR(9)
.softlvl10:
	SOINTR(10)
.softlvl11:
	SOINTR_ABOVE_LOCK(11)
.softlvl12:
	SOINTR_ABOVE_LOCK(12)
.softlvl13:
/*
 * Hook for cross-call service.
 */
#ifdef MP
        call	xc_serv
        mov     X_CALL_MEDPRI, %o0
#endif MP
	SOINTR_ABOVE_LOCK(13)
.softlvl14:
	SOINTR_ABOVE_LOCK(14)
.softlvl15:
/*
 * Hook for cross-call service.
 */
#ifdef MP
#define	PROM_MAILBOX
#ifdef	PROM_MAILBOX

	!
	! Given a prom mailbox message, branch to the
	! proper prom service (via the prom library).
	! prom services will return to the above label.
	!
	set	prom_mailbox, %o0
	CPU_INDEX(%o1)			! get cpu id number 0..3
	sll	%o1, 2, %o2

	ld	[%o0 + %o2], %o0	! get to the appropriate mailbox
	tst	%o0			! and determine if it is mapped
	bz	1f
	nop

	!
	!	ldub	[%o0 + %o1], %o0	! get mailbox message
	!
	ldub	[%o0], %o0		! get mailbox message

	cmp	%o0, 0xFB		! 0xFB: someone did an op_exit
	bne	2f
	nop

	call	flush_windows		! flush register windows to stack
	nop

	sethi	%hi(panicstr), %o0	! test for panic in progress
	ld	[%o0 + %lo(panicstr)], %o0
	tst	%o0
	bne	0f			! setup_panic() has been called
	nop

	call	prom_stopcpu		! prom wants us to "stop"
	mov	%g0, %o0
	b	1f
	nop

	!
	! Panic in progress.
	!
	! Simulate an inline vac_flushall(). Flush only the local CPU.
	!
0:
	! Inline vac_ctxflush(0, FL_LOCALCPU)
	mov	FL_LOCALCPU, %o1
	call	vac_ctxflush			! supv and ctx 0
	mov	%g0, %o0

	! Inline vac_usrflush(FL_LOCALCPU)
	call	vac_usrflush
	mov	FL_LOCALCPU, %o0

	!
	! For debugging support, record the %pc and %sp at the time
	! of the interrupt in "curthread".
	!
0:
	ld	[THREAD_REG + T_PC], %l3
	ld	[THREAD_REG + T_SP], %l5
	st	%l1, [THREAD_REG + T_PC]	! overwrite pc
	st	%fp, [THREAD_REG + T_SP]	! overwrite sp

	call	prom_stopcpu		! prom wants us to "stop"
	mov	%g0, %o0

	st	%l3, [THREAD_REG + T_PC]	! save pc
	st	%l5, [THREAD_REG + T_SP]	! save sp
	b	1f
	nop
2:
	cmp	%o0, 0xFC		! 0xFC: someone did an op_enter
	bne	2f			! (e.g., some cpu entered kadb)
	nop

	!
	! Save current %pc and %sp in non-volatile registers
	!
	ld	[THREAD_REG + T_PC], %l3
	ld	[THREAD_REG + T_SP], %l5
#ifdef KDBX
	mov	%tbr, %g3
#endif /* KDBX */

	call	flush_windows		! flush register windows to stack
	nop

	!
	! Set %pc and %sp at the time of the interrupt in "curthread"
	! (debugging support).
	!	
	st	%l1, [THREAD_REG + T_PC]	! overwrite pc
	st	%fp, [THREAD_REG + T_SP]	! overwrite sp

#ifdef KDBX
	set	kdbx_useme, %g1		! check for patched value
	ld	[%g1], %g1
	cmp	%g1, 0x0
	be	3f
	nop
	set	kdbx_stopcpus, %g1	! did kdbx request this softlvl15?
	ld	[%g1], %g1
	cmp	%g1, 0x0
	be	3f
	nop

	CPU_INDEX(%o0)			! get cpu id number 0..3
	add	%l7, MINFRAME, %o1	! pass the regs structure from the stack
	call	kdbx_softtrap		! kdbx wants our mind and soul
	mov	%g3, %o2		! pass in the tbr
	b	4f
	nop
3:
#endif /* KDBX */
	call	prom_idlecpu		! prom wants us to "idle"
	mov	%g0, %o0

	!
	! Restore %pc and %sp
	!
4:	st	%l3, [THREAD_REG + T_PC]	! save pc
	st	%l5, [THREAD_REG + T_SP]	! save sp
	b	1f
	nop

2:
	cmp	%o0, 0xFD		! 0xFD: someone hit a breakpoint
	bne	2f
	nop
	call	prom_idlecpu		! prom wants us to "idle"
	mov	%g0, %o0
	b	1f
	nop

2:
	cmp	%o0, 0xFE		! 0xFE: someone watchdogged
	bne	2f
	nop

	call	flush_windows		! flush register windows to stack
	nop
	call	prom_stopcpu		! prom wants us to "stop"
	mov	%g0, %o0
	b	1f
	nop
2:

1:				! come here if no prom mailbox request pending.
#endif	PROM_MAILBOX

	CPU_INDEX(%g1)			! get cpu id number 0..3
	sll	%g1, 2, %g1		! offset to cpu array.
	set	cpu, %g2		! cpu array
	add	%g2, %g1, %g1
	ld	[%g1], %g1		! %g1 now point to this
					! cpu's cpu struct.
	ld	[%g1 + XC_PEND_HIPRI], %g2
	
	tst	%g2
	be	2f
	nop
	call	xc_serv
	mov	X_CALL_HIPRI, %o0
2:
#endif MP
	SOINTR_ABOVE_LOCK(15)


/*
 * Level 1 interrupts, from out of the blue.
 */
	ENTRY_NP(_level1)
	SYNCWB
	IOINTR(1)
	SET_SIZE(_level1)

/*
 * Level 2 interrupts, from Sbus level 1, or VMEbus level 1.
 */
	.type	.level2, #function
.level2:
	SYNCWB
	SBINTR(1)
	VBINTR(1)
	IOINTR(2)
	SET_SIZE(.level2)

/*
 * Level 3 interrupts, from Sbus level 2, or VMEbus level 2
 */
	.type	.level3, #function
.level3:
	SYNCWB
	SBINTR(2)
	VBINTR(2)
	IOINTR(3)
	SET_SIZE(.level3)

/*
 * Level 4 interrupts, from onboard SCSI
 */
	.type	.level4, #function
.level4:
	SYNCWB
	OBINTR(4, SIR_SCSI)
	IOINTR(4)
	SET_SIZE(.level4)

/*
 * Level 5 interrupts, from Sbus/VME level 3
 */
	.type	.level5,#function
.level5:
	SYNCWB
	SBINTR(3)
	VBINTR(3)
	IOINTR(5)
	SET_SIZE(.level5)

/*
 * Level 6 interrupts, from onboard ethernet
 */
	.type	.level6,#function
.level6:
	SYNCWB
	OBINTR(6, SIR_ETHERNET)
	IOINTR(6)
	SET_SIZE(.level6)

/*
 * Level 7 interrupts, from SBus/VME level 4
 */
	.type	.level7,#function
.level7:
	SYNCWB
	SBINTR(4)
	VBINTR(4)
	IOINTR(7)
	SET_SIZE(.level7)

/*
 * Level 8 interrupts, from onboard video
 */
	.type	.level8,#function
.level8:
	SYNCWB
	OBINTR(8, SIR_VIDEO)
	IOINTR(8)
	SET_SIZE(.level8)

/*
 * Level 9 interrupts, from SBus/VME level 5 and module interrupt
 */
	.type	.level9,#function
.level9:
	SYNCWB
	OBINTR(9, SIR_MODULEINT)
	SBINTR(5)
	VBINTR(5)
	IOINTR(9)
	SET_SIZE(.level9)

/*
 * Level 10 interrupts, from onboard clock
 *
 * REORGANIZATION NOTE:
 *	level 10 hardint should be handled
 *	by the "real time clock driver", which
 *	should be linked into the level-10
 *	onboard vector. In turn, it should
 *	trigger the LED update either by directly
 *	calling the "led display driver" or by
 *	pending a level-10 software interrupt.
 *	The softint route is more general.
 *	Making these separate device modules
 *	will make it easier to work with possible
 *	alternate implementations (like different
 *	numbers of LEDs, or different clock chips).
 *
 *	Not Yet, tho.
 */
#if	0
_level10:
	OBINTR(10, SIR_REALTIME)
	IOINTR(10)
#endif


/*
 * Level 11 interrupts, from SBus/VME level 6 or floppy
 * NOTE: FLOPPY DRIVER SHOULD BE MOVED FROM TRAP LEVEL, IF POSSIBLE
 */
	.type	.level11,#function
.level11:
	SYNCWB
	OBINTR_ABOVE_LOCK(11, SIR_FLOPPY)
	SBINTR_ABOVE_LOCK(6)
	VBINTR_ABOVE_LOCK(6)
	IOINTR_ABOVE_LOCK(11)
	SET_SIZE(.level11)

/*
 * Level 12 interrupts, from onboard serial ports
 */
	.type	.level12,#function
.level12:
	SYNCWB
	OBINTR_ABOVE_LOCK(12, SIR_KBDMS|SIR_SERIAL)
	IOINTR_ABOVE_LOCK(12)

/*
 * Level 13 interrupts, from SBus/VME level 7, or onboard audio
 * NOTE: AUDIO DRIVER SHOULD BE MOVED FROM TRAP LEVEL, IF POSSIBLE
 */
	.type	.level13,#function
.level13:
	SYNCWB
	OBINTR_ABOVE_LOCK(13, SIR_AUDIO)
	SBINTR_ABOVE_LOCK(7)
	VBINTR_ABOVE_LOCK(7)
	IOINTR_ABOVE_LOCK(13)
	SET_SIZE(.level13)

/*
 * Level 14 interrupts, from per processor counter/timer.  We must first
 * save the address of the stack frame on which the interrupted context
 * info was pushed, since it is otherwise not possible for profiling to
 * discover the interrupted pc value.
 *
 * Assumes, %l7 = saved %sp.
 */
	.type	.level14,#function
.level14:
	CPU_ADDR(%o0, %o1)		! load CPU struct addr to %o0 using %o1
	ld	[%o0 + CPU_PROFILING], %o0	! profiling struct addr in %o0
	add	%l7, MINFRAME, %o1		! addr. of saved regs. in %o1
	tst	%o0
	bne,a	1f
	st	%o1, [%o0 + PROF_RP]
1:
	OBINTR_FOUND_ABOVE_LOCK(14)
	IOINTR_ABOVE_LOCK(14)
	SET_SIZE(.level14)

	.type	.level15,#function
.level15:
	call	l15_async_fault
	nop
	b,a	.interrupt_ret
	/*IOINTR_ABOVE_LOCK(15)*/

/*
 * LED data for idle pattern of diagnostic register.
 */
	.seg	".data"
	.type	.led, #object
.led:
	/*
 	 * Twelve bit wide helical chase pattern
 	 * sampled at 100hz
 	 */
	.byte	0x00, 0x41	! .....#.....#
	.byte	0x00, 0x22	! ......#...#.
	.byte	0x00, 0x14	! .......#.#..
	.byte	0x00, 0x08	! ........#...
	.byte	0x00, 0x14	! .......#.#..
	.byte	0x00, 0x22	! ......#...#.
	.byte	0x00, 0x41	! .....#.....#
	.byte	0x00, 0x82	! ....#.....#.
	.byte	0x01, 0x04	! ...#.....#..
	.byte	0x02, 0x08	! ..#.....#...
	.byte	0x04, 0x10	! .#.....#....
	.byte	0x08, 0x20	! #.....#.....
	.byte	0x04, 0x40	! .#...#......
	.byte	0x02, 0x80	! ..#.#.......
	.byte	0x01, 0x00	! ...#........
	.byte	0x02, 0x80	! ..#.#.......
	.byte	0x04, 0x40	! .#...#......
	.byte	0x08, 0x20	! #.....#.....
	.byte	0x04, 0x10	! .#.....#....
	.byte	0x02, 0x08	! ..#.....#...
	.byte	0x01, 0x04	! ...#.....#..
	.byte	0x00, 0x82	! ....#.....#.

LEDTICKS = 33
LEDPAT = 0
LEDPATCNT = 88
LEDCNT = 44
LEDPTR = 45
	! end of LEDPAT
	.byte	0		! LEDCNT current count of ticks
	.byte	0		! LEDPTR offset in pattern

	.seg	".text"
/*
 * This code assumes that the real time clock interrupts 100 times
 * per second, for SUN4M we call hardclock at that rate.
 */
	.common clk_intr, 4, 4

        .seg    ".data"
	.global hrtime_base, vtrace_time_base, v_level10clk_addr
	.global	hres_lock, hres_last_tick, hrestime, hrestime_adj, timedelta
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
hrtime_base:
	.word   0, 0
vtrace_time_base:
	.word   0
v_level10clk_addr:
	.word	0
hres_lock:
	.word	0
hres_last_tick:
	.word	0
	.align	8
hrestime:
	.word	0, 0
hrestime_adj:
	.word	0, 0
timedelta:
	.word	0, 0

	.skip   4

	.seg	".text"

/*
 *	Level 10 clock interrupt - entered with traps disabled
 *	May swtch to new stack.
 *
 *	Entry:  traps disabled.
 *		%l3 = CPU pointer
 *		%l4 = interrupt level throughout.  intr_passivate()
 *		      relies on this.
 *		%l7 = saved %sp
 *		%g7 = thread pointer
 *
 *	Register usage	
 *		%g1 - %g3 = scratch
 *		%l0 = new %psr
 *		%l1, %l2 = %pc, %npc
 *		%l5 = scratch
 *		%l6 = clock thread pointer
 */

	ALTENTRY(_level10)

	mov	%l4, %l6			! we need another register pair

	sethi	%hi(hres_lock + HRES_LOCK_OFFSET), %l5	! %l5 not set on entry
	ldstub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4	! try locking
7:	tst	%l4
	bz,a	8f				! if lock held, try again
	sethi	%hi(hrtime_base), %l4
	ldub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4
9:	tst	%l4
	bz,a	7b
	ldstub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4
	b	9b
	ldub	[%l5 + %lo(hres_lock + HRES_LOCK_OFFSET)], %l4
8:
	sethi	%hi(hrtime_base), %l4
	ldd	[%l4 + %lo(hrtime_base)], %g2
	sethi   %hi(nsec_per_tick), %g1
	ld      [%g1 + %lo(nsec_per_tick)], %g1	! %g1 = nsec_per_tick (npt)
	addcc   %g3, %g1, %g3			! add 1 tick to hrtime_base
	addx    %g2, %g0, %g2			! if any carry, add it in
	neg	%g2
	std	%g2, [%l4 + %lo(hrtime_base)]	! invalidate hrtime_base

#ifdef TRACE
	ld      [%l4 + %lo(vtrace_time_base)], %g2
	sethi   %hi(usec_per_tick), %l5;
	ld      [%l5 + %lo(usec_per_tick)], %l5;
	add     %g2, %l5, %g2                   ! add tick to vtrace_time_base
	or	%g2, 1, %g2			! invalidate vtrace_time_base
	st	%g2, [%l4 + %lo(vtrace_time_base)]
#endif	/* TRACE */

	stbar					! broadcast timer invalidation

	!
	! load current timer value
	!
	ld	[%l4 + %lo(v_level10clk_addr)], %g2	! %g2 = timer address
	ld	[%g2 + CTR_COUNT10], %g2	! %g2 = timer, limit bit set
	sll	%g2, 1, %g2			! clear out limit bit 31
	srl	%g2, 7, %g3			! 2048u / 128 = 16u
	sub	%g2, %g3, %g2			! 2048u - 16u = 2032u
	sub	%g2, %g3, %g2			! 2032u - 16u = 2016u
	sub	%g2, %g3, %g2			! 2016u - 16u = 2000u
	srl	%g2, 1, %g2			! 2000u / 2 = nsec timer value
	ld	[%l4 + %lo(hres_last_tick)], %g3	! previous timer value
	st	%g2, [%l4 + %lo(hres_last_tick)]	! prev = current
	add	%g1, %g2, %g1			! %g1 = nsec_per_tick + current
	sub	%g1, %g3, %g1			! %g1 =- prev == nslt

	!
	! apply adjustment, if any
	!
	ldd	[%l4 + %lo(hrestime_adj)], %g2	! %g2:%g3 = hrestime_adj
	orcc	%g2, %g3, %g0			! hrestime_adj == 0 ?
	bz	2f				! yes, skip adjustments
	clr	%l5				! delay: set adj to zero
	tst	%g2				! is hrestime_adj >= 0 ?
	bge	1f				! yes, go handle positive case
	srl	%g1, ADJ_SHIFT, %l5		! delay: %l5 = adj

	addcc	%g3, %l5, %g0			!
	addxcc	%g2, %g0, %g0			! hrestime_adj < -adj ?
	bl	2f				! yes, use current adj
	neg	%l5				! delay: %l5 = -adj
	b	2f
	mov	%g3, %l5			! no, so set adj = hrestime_adj
1:
	subcc	%g3, %l5, %g0			!
	subxcc	%g2, %g0, %g0			! hrestime_adj < adj ?
	bl,a	2f				! yes, set adj = hrestime_adj
	mov	%g3, %l5			! delay: adj = hrestime_adj
2:
	ldd	[%l4 + %lo(timedelta)], %g2	! %g2:%g3 = timedelta
	sra	%l5, 31, %l4			! sign extend %l5 into %l4
	subcc	%g3, %l5, %g3			! timedelta -= adj
	subx	%g2, %l4, %g2			! carry
	sethi	%hi(hrtime_base), %l4		! %l4 = common hi 22 bits
	std	%g2, [%l4 + %lo(timedelta)]	! store new timedelta
	std	%g2, [%l4 + %lo(hrestime_adj)]	! hrestime_adj = timedelta
	ldd	[%l4 + %lo(hrestime)], %g2	! %g2:%g3 = hrestime sec:nsec
	add	%g3, %l5, %g3			! hrestime.nsec += adj
	add	%g3, %g1, %g3			! hrestime.nsec += nslt

	set	NANOSEC, %l5			! %l5 = NANOSEC
	cmp	%g3, %l5
	bl	5f				! if hrestime.tv_nsec < NANOSEC
	sethi	%hi(one_sec), %g1		! delay
	add	%g2, 0x1, %g2			! hrestime.tv_sec++
	sub	%g3, %l5, %g3			! hrestime.tv_nsec - NANOSEC
	mov	0x1, %l5
	st	%l5, [%g1 + %lo(one_sec)]
5:
	std	%g2, [%l4 + %lo(hrestime)]	! store the new hrestime
	mov	%l6, %l4			! restore %l4

	sethi	%hi(v_level10clk_addr), %l5
	ld	[%l5 + %lo(v_level10clk_addr)], %l5	! load counter10 addr
	ld	[%l5 + CTR_LIMIT10], %g1	! read limit10 reg to clear intr

	sethi	%hi(hrtime_base), %l6
	ldd	[%l6 + %lo(hrtime_base)], %g2
	neg	%g2				! validate hrtime_base
	std	%g2, [%l6 + %lo(hrtime_base)]

#ifdef TRACE
	ld	[%l6 + %lo(vtrace_time_base)], %g2
	andn	%g2, 1, %g2			! validate vtrace_time_base
	st	%g2, [%l6 + %lo(vtrace_time_base)]
#endif	/* TRACE */

	sethi	%hi(hres_lock), %l6
	ld	[%l6 + %lo(hres_lock)], %g1
	inc	%g1				! release lock
	st	%g1, [%l6 + %lo(hres_lock)]	! clear hres_lock

	stbar					! tell the world we're done

	mov	%psr, %g3		! XXX - Try and use %l0 instead of
					! reading %psr which takes longer

	!
	! NOTE: SAS now simulates the counter; we take clock ticks!
	!
#ifdef SAS
	set     fake_tick_requested, %o0
	st      %g0, [%o0]
#else
	! see if we have any diagleds to update
	sethi	%hi(diagleds), %l5
	ld	[%l5 + %lo(diagleds)], %l5
	tst	%l5
	bz	3f
	nop
	!
	! Check if executing in the idle loop.
	!
	set	.led, %l5		! countdown to next update of LEDs
	btst	PSR_PS, %g3		! test for user trap
	bz	4f			! from user, THREAD_REG is not valid
	nop				! delay slot
	ld	[THREAD_REG + T_CPU], %g1	! CPU running
	ld	[%g1 + CPU_IDLE_THREAD], %g1
	cmp	THREAD_REG, %g1
	beq	1f			! idle, update LEDs
	nop
4:
	ldub	[%l5 + LEDCNT], %g1
	subcc	%g1, 1, %g1
	bge,a	3f			! not zero, just call clock
	stb	%g1, [%l5 + LEDCNT]	! delay, write out new ledcnt
1:
	mov	LEDTICKS, %g1
	stb	%g1, [%l5 + LEDCNT]

	ldub	[%l5 + LEDPTR], %g1
	srl	%g1, 2, %g2		! four ticks per sample
	sll	%g2, 1, %g2		! two bytes per sample
	lduh	[%l5 + %g2], %g2	! get LED pattern
	subcc	%g1, 1, %g1		! point to next one
	bneg,a	2f
	mov	LEDPATCNT-1, %g1
2:
	stb	%g1, [%l5 + LEDPTR]	! update pattern pointer
	call	set_diagled
	mov	%g2, %o0
#endif SAS
3:
	sethi	%hi(clk_intr), %g2	! count clock interrupt
	ld	[%lo(clk_intr) + %g2], %g3
	inc	%g3
	st	%g3, [%lo(clk_intr) + %g2]

	ld	[THREAD_REG + T_CPU], %g1	! get CPU pointer
	ld	[%g1 + CPU_SYSINFO_INTR], %g2	! cpu_sysinfo.intr++
	inc	%g2
	st	%g2, [%g1 + CPU_SYSINFO_INTR]

	!
	! Try to activate the clock interrupt thread.  Set the t_lock first.
	!
	sethi	%hi(clock_thread), %g2
	ld	[%g2 + %lo(clock_thread)], %l6	! clock thread pointer
	ldstub	[%l6 + T_LOCK], %o0	! try to set clock_thread->t_lock
	tst	%o0
	bnz	9f			! clock already running
	ld	[%l6 + T_STATE], %o0	! delay - load state

	!
	! Check state.  If it isn't TS_FREE (FREE_THREAD), it must be blocked
	! on a mutex or something.
	!
	cmp	%o0, FREE_THREAD
	bne,a	9f			! clock_thread not idle
	clrb	[%l6 + T_LOCK]		! delay - release the lock

	!
	! consider the clock thread part of the same LWP so that window
	! overflow code can find the PCB.
	!
	ld	[THREAD_REG + T_LWP], %o0
	mov	ONPROC_THREAD, %o1	! set running state
	st	%o0, [%l6 + T_LWP]
	st	%o1, [%l6 + T_STATE]
	
	!
	! Push the interrupted thread onto list from the clock thread.
	! Set the clock thread as the current one.
	!
	st	%l7, [THREAD_REG + T_SP]	! mark stack
	st	THREAD_REG, [%l6 + T_INTR]	! point clock at old thread
	st	%l3, [%l6 + T_CPU]		! set new thread's CPU pointer
	st	%l3, [%l6 + T_BOUND_CPU]	! set cpu binding for thread
	st	%l6, [%l3 + CPU_THREAD]		! point CPU at new thread
	add	%l3, CPU_THREAD_LOCK, %g1	! pointer to onproc thread lock
	st	%g1, [%l6 + T_LOCKP]		! set thread's disp lock ptr
	mov	%l6, THREAD_REG			! set curthread register
	ld	[%l6 + T_STACK], %sp		! set new stack pointer

	!
	! Now that we're on the new stack, enable traps
	!
	! The three nop's after the write to the psr register workaround
	! a HW chip bug in Ross620 HyperSPARC Pinnacle modules and A.0, A.1
	! silicon of the Colorado modules. There is no plan to ship systems
	! with these modules, but the workaround is necessary to use 
	! development systems (in house) configured with  these modules.
	! The HW bug happens in the case of writes to the processor state
	! register (PSR, Y etc). If these writes to the state register are not
	! followed by three nops and one of the instructions filling in the
	! delay cycles is the target of an interrupt, then the instruction
	! which is the target of the interrupt gets executed twice. The
	! bug here is that in this kind of scenario PC and NPC contents are
	! the same, so after the interrupt is serviced the instruction in the
	! delay cycle gets executed twice. This bug seems to manifest only
	! when the CWP in the psr register is affected.

	mov	%l0, %psr		! set level (IU bug)
	nop; nop; nop
	wr	%l0, PSR_ET, %psr	! enable traps
	ROSS625_PSR_NOP(); ROSS625_PSR_NOP(); ROSS625_PSR_NOP()
	!
	! Initialize clock thread priority based on intr_pri and call clock
	!
	sethi	%hi(intr_pri), %l5		! psr delay
	ldsh	[%l5 + %lo(intr_pri)], %l5	! psr delay - get base priority
	add	%l5, LOCK_LEVEL, %l5		! psr delay
	TRACE_ASM_1(%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);

	call	clock
	sth	%l5, [%l6 + T_PRI]		! delay slot - set priority
	TRACE_ASM_0(%o1, TR_FAC_INTR, TR_INTR_END, TR_intr_end);

	!
	! On return, we must determine whether the interrupted thread is
	! still pinned or not.  If not, just call swtch().
	! Note %l6 = THREAD_REG = clock_intr
	! 
	ld	[%l6 + T_INTR], %l5	! is there a pinned thread?
	tst	%l5
#if FREE_THREAD != 0	/* Save an instruction since FREE_THREAD is 0 */
	mov	FREE_THREAD, %o0	! use reg since FREE not 0
	bz	1f			! nothing pinned - swtch
	st	%o0, [%l6 + T_STATE]	! delay - set thread free
#else
	bz	1f			! nothing pinned - swtch
	clr	[%l6 + T_STATE]		! delay - set thread free
#endif /* FREE_THREAD */

	st	%l5, [%l3 + CPU_THREAD]	! set CPU thread back to pinned one
	mov	%l5, THREAD_REG		! set curthread register
	clrb	[%l6 + T_LOCK]		! unlock clock_thread->t_lock
	b	_sys_rtt		! return to restore previous stack
	mov	%l7, %sp		! delay - restore %sp

1:
	!
	! No pinned (interrupted) thread to return to,
	! so clear the pending interrupt flag for this level and call swtch
	!
	ld	[%l3 + CPU_INTR_ACTV], %o5	! get mask of interrupts active
	andn	%o5, (1 << CLOCK_LEVEL), %o5	! clear clock interrupt flag
	call	set_base_spl			! set CPU's base SPL level
	st	%o5, [%l3 + CPU_INTR_ACTV]	! delay - store active mask

	ld      [%l3 + CPU_SYSINFO_INTRBLK], %g2   ! cpu_sysinfo.intrblk++
	inc     %g2
	call	swtch			! swtch() - give up CPU - won't be back
	st      %g2, [%l3 + CPU_SYSINFO_INTRBLK]

	!
	! returning from _level10 without calling clock().
	! Increment clock_pend so clock() will rerun tick processing.
	! Must enable traps before returning to allow sys_rtt to call C.
	!
9:
	mov	%l0, %psr		! set level (IU bug)
	wr	%l0, PSR_ET, %psr	! enable traps
	nop; nop			! psr delay
	TRACE_ASM_1 (%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);

	!
	! On a Uniprocessor like the sun4c, we're protected by SPL level
	! and don't need clock_lock.
	!
#ifdef MP
	set	clock_lock, %l6		! psr delay
	call	lock_set
	mov	%l6, %o0
#endif /* MP */
	sethi	%hi(clock_pend), %g1	! psr delay (depending on ifdefs)
	ld	[%g1 + %lo(clock_pend)], %o0
	inc	%o0			!  increment clock_pend
	st	%o0, [%g1 + %lo(clock_pend)]
#ifdef MP
	clrb	[%l6]			! clear the clock_lock
#endif /* MP */
	TRACE_ASM_0 (%o1, TR_FAC_INTR, TR_INTR_END, TR_intr_end);
	b	_sys_rtt
	nop				! psr delay
	SET_SIZE(_level10)

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
 * cannot be directly used on the sun4m.  This is because ddi_add_fastintr()
 * is not supported by the sun4m.  settrap() which updates the scb
 * unfortunately may make use of branch instructions to call the target
 * interrupt handler.  These will not work because each processor maps the
 * scb in to a different address, and hence we need jump instructions.
 */

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
	CPU_INDEX(%l4)			! get cpu id number 0..3
	sll	%l4, 2, %l4		! convert to word offset

	set	v_interrupt_addr, %l3
	ld	[%l3 + %l4], %l3
	ld	[%l3], %l3		! get module int pending reg
	set	IR_HARD_INT(14), %l5
	andcc	%l3, %l5, %g0		! if hardint is not set
	bz	knone			! do normal interrupt processing
	nop

	!
	! Read the limit 14 register to clear the clock interrupt.
	!
	set	v_counter_addr, %l3
	ld	[%l3 + %l4], %l3		! load this CPU's counter addr
	ld	[%l3 + CTR_LIMIT14], %g0	! read limit14 reg to clear intr

	CPU_ADDR(%l4, %l3)
	ld	[%l4 + CPU_PROFILING], %l3
	tst	%l3				! no profile struct, spurious?
	bnz	kprof
	nop

knone:
	b	sys_trap		! do normal interrupt processing
	mov	(T_INTERRUPT | 14), %l4
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

	.global	fast_profile_intr
fast_profile_intr:

	.seg	".data"
	.align	4

	.global	have_fast_profile_intr
have_fast_profile_intr:
	.word	0

	.seg	".text"
	.align	4

#endif	/* lint */

#endif	/* defined(FAST_KPROF) */

/*
 * Turn on or off bits in the auxiliary i/o register.
 * We must lock out interrupts, since we don't have an atomic or/and to mem.
 * set_auxioreg(bit, flag)
 *	int bit;		bit mask in aux i/o reg
 *	int flag;		0 = off, otherwise on
 */

#if defined(lint)

/* ARGSUSED */
void
set_auxioreg(int bit, int flag)
{}

#else	/* lint */

	ENTRY_NP(set_auxioreg)
	mov	%psr, %g2
	or	%g2, PSR_PIL, %g1	! spl hi to protect aux i/o reg update
	mov	%g1, %psr
	ROSS625_PSR_NOP()
	nop					! psr delay
	sethi	%hi(v_auxio_addr), %o2		! psr delay; aux i/o reg address
	ld	[%o2 + %lo(v_auxio_addr)], %o2	! psr delay
	ldub	[%o2], %g1			! read aux i/o register
	tst	%o1
	bnz,a	1f
	bset	%o0, %g1		! on
	bclr	%o0, %g1		! off
1:
	or	%g1, AUX_MBO, %g1	! Must Be Ones
	stb	%g1, [%o2]		! write aux i/o register
	b	splx			! splx and return
	mov     %g2, %o0
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
	save	%sp, -WINDOWSIZE, %sp
	save	%sp, -WINDOWSIZE, %sp
	save	%sp, -WINDOWSIZE, %sp
	save	%sp, -WINDOWSIZE, %sp
	save	%sp, -WINDOWSIZE, %sp
 
/*
 * XXX - FIXME - This code was originally written for Maxiray.
 * If it is ever hacked to support something else which
 * doesn't have 12 windows, more ifdefs will be needed.
 * Also, see where _fixnwindows is referenced at other
 * places in the module for the purpose of changing these
 * extra save and restores to NOPs
 */
	.global	fixnwindows
fixnwindows:	
	save	%sp, -WINDOWSIZE, %sp
	restore
	restore
	restore
	restore
	restore
	ret
	restore
	SET_SIZE(fixnwindows)
	SET_SIZE(flush_windows)

#endif	/* lint */

/*
 * flush user windows to memory.
 * This is a leaf routine that only wipes %g1, %g2, and %g5.
 * Some callers may depend on this behavior.
 */

#if defined(lint)

void
flush_user_windows(void)
{}

#else	/* lint */

	ENTRY_NP(flush_user_windows)
	ld	[THREAD_REG + T_CPU], %g5
	ld	[%g5 + CPU_MPCB], %g5
	tst	%g5			! mlwp == 0 for kernel threads
	bz	3f			! return immediately when true
	nop
	ld	[%g5 + MPCB_UWM], %g1	! get user window mask
	tst	%g1			! do save until mask is zero
	bz	3f
	clr	%g2
1:
	save	%sp, -WINDOWSIZE, %sp
	ld	[%g5 + MPCB_UWM], %g1	! get user window mask
	tst	%g1			! do save until mask is zero
	bnz	1b
	add	%g2, 1, %g2
2:
	subcc	%g2, 1, %g2		! restore back to orig window
	bnz	2b
	restore
3:
	retl
	nop
	SET_SIZE(flush_user_windows)

#endif	/* lint */

#if defined(lint)

void
fast_window_flush(void)
{}

#else	/* lint */


	ENTRY_NP(fast_window_flush)

	sethi   %hi(nwin_minus_one), %l6
	ld      [%l6+%lo(nwin_minus_one)], %l6

	mov	%g7, %l7		! save %g7 in %l7
	mov	%l6, %g7		! save NW-1 in %g7

	mov	1, %l5
	sll	%l5, %l0, %l4		! calculate CWM (1<<CWP)

	!
	! skip over the trap window by rotating the CWM left by 1
	!	
	srl	%l4, %g7, %l5		! shift CWM right by NW-1
	tst	%l5
	bz,a	.+8
	sll	%l4, 1, %l5		! shift CWM left by 1	
	mov	%l5, %l4

	!
	! check if there are any registers windows between the CWM
	! and the WIM that are left to be saved.
	!
	andcc	%l4, %l3, %g0		! WIM & CWM
	bnz,a	.ff_out_ret2
	mov	%l0, %psr		! restore psr to trap window

	!
	! get ready to save user windows by first saving globals
	! to the current window's locals (the trap window).
	!
	mov	%g5, %l5		! save %g5 in %l5
	mov	%l0, %g5		! save PSR in %g5
	mov	%g2, %l0		! save %g2 in %l0
	mov	%l4, %g2		! save CWM in %g2
	mov	%g3, %l3		! save %g3 in %l3
	mov	%g1, %l4		! save %g1 in %l4

	restore				! skip trap window, advance to
					! calling window
	!
	! flush user windows to the stack. this is an inlined version
	! of the window overflow code.
	!
	! Most of the time the stack is resident in main memory,
	! so we don't verify its presence before attempting to save
	! the window onto the stack.  Rather, we simply set the
	! no-fault bit of the SRMMU's control register, so that
	! doing the save won't cause another trap in the case
	! where the stack is not present.  By checking the
	! synchronous fault status register, we can determine
	! whether the save actually worked (ie. stack was present),
	! or whether we first need to fault in the stack.
	! Other sun4 trap handlers first probe for the stack, and
	! then, if the stack is present, they store to the stack.
	! This approach CANNOT be used with a multiprocessor system
	! because of a race condition: between the time that the
	! stack is probed, and the store to the stack is done, the
	! stack could be stolen by the page daemon.
	!

	set	RMMU_FSR_REG, %g1	! clear old faults from SFSR
	lda	[%g1]ASI_MOD, %g0


	set	RMMU_CTL_REG, %g1
	lda	[%g1]ASI_MOD, %g3	! turn on no-fault bit in
	or	%g3, MMCREG_NF, %g3	! mmu control register to
	sta	%g3, [%g1]ASI_MOD	! prevent taking a fault.
.ff_ustack_res:
	! Security Check to make sure that user is not
	! trying to save/restore unauthorized kernel pages
	! Check to see if we are touching non-user pages, if
	! so, fake up SFSR, SFAR to simulate an error
	set     KERNELBASE, %g1
	cmp     %g1, %sp
	bleu,a  .ff_failed
	mov	RMMU_CTL_REG, %g1	! turn off no-fault bit
	!
	! advance to the next window to save. if the CWM is equal to
	! the WIM then there are no more windows left. terminate loop
	! and return back to the user.
	!
	std     %l0, [%sp + (0*4)]
	std     %l2, [%sp + (2*4)]
	std     %l4, [%sp + (4*4)]
	std     %l6, [%sp + (6*4)]
	std     %i0, [%sp + (8*4)]
	std     %i2, [%sp + (10*4)]
	std     %i4, [%sp + (12*4)]
	std     %i6, [%sp + (14*4)]

	set	RMMU_FAV_REG, %g3
	lda	[%g3]ASI_MOD, %g0	! read SFAR
	set	RMMU_FSR_REG, %g1
	lda	[%g1]ASI_MOD, %g1	! read SFSR

	btst	MMU_SFSR_FAV, %g1	! did a fault happen?
	bnz,a	.ff_failed		! terminate loop if fault happened
	mov	RMMU_CTL_REG, %g1	! turn off no-fault bit

	mov	%wim, %g1		! save WIM to %g1
	srl     %g2, %g7, %g3           ! shift CWM right by NW-1
	tst	%g3
	bz,a	.+8
	sll	%g2, 1, %g3		! shift CWM left by 1	
	mov	%g3, %g2
	andcc	%g2, %g1, %g0		! new CWM & WIM

	bz,a	.ff_ustack_res		! continue loop as long as CWM != WIM
	restore				! delay, advance window
.ff_out:

	set	RMMU_CTL_REG, %g1	! turn off no-fault bit
	lda	[%g1]ASI_MOD, %g3
	andn	%g3, MMCREG_NF, %g3
	sta	%g3, [%g1]ASI_MOD

	! restore CWP and set WIM to mod(CWP+2, NW).

	mov	1, %g3			
	sll	%g3, %g5, %g3		! calculate new WIM
	sub	%g7, 1, %g2		! put NW-2 in %g2
	srl	%g3, %g2, %g2		! mod(CWP+2, NW) == WIM	
	sll	%g3, 2, %g3
	wr	%g2, %g3, %wim		! install wim
	mov	%g5, %psr		! restore PSR	
.ff_out_ret:
	nop;nop;nop
	mov	%l7, %g7		! restore %g7
	mov	%l5, %g5		! restore %g5
	mov	%l3, %g3		! restore %g3
	mov	%l0, %g2		! restore %g2
	mov	%l4, %g1		! restore %g1
	jmp	%l2
	rett	%l2+4
.ff_out_ret2:
	nop;nop;nop;
	mov	%l7, %g7		! restore %g7
	jmp	%l2
	rett	%l2+4
.ff_failed:
	!
	! The user's stack is not accessable. reset the WIM, and PSR to
	! the trap window. call sys_trap(T_FLUSH_WINDOWS) to do the dirty
	! work.
	!

	lda	[%g1]ASI_MOD, %g3	! turn off no-fault bit
	andn	%g3, MMCREG_NF, %g3
	sta	%g3, [%g1]ASI_MOD

	mov	%g5, %psr		! restore PSR back to trap window
	nop;nop;nop
	mov	%l7, %g7		! restore %g7
	mov	%l5, %g5		! restore %g5
	mov	%l3, %g3		! restore %g3
	mov	%l0, %g2		! restore %g2
	mov	%l4, %g1		! restore %g1
	SYS_TRAP(T_FLUSH_WINDOWS);
	SET_SIZE(fast_window_flush)

#endif /* lint */

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
	ld	[THREAD_REG + T_CPU], %g5
	ld	[%g5 + CPU_MPCB], %g5
	ld	[%g5 + MPCB_UWM], %g1	! get user window mask
	tst	%g1
	bz	3f			! user windows?
	nop

	!
	! There are old user windows in the register file. We disable traps
	! and increment the WIM so that we don't overflow on these windows.
	! Also, this sets up a nice underflow when first returning to the
	! new user.
	!
	mov	%psr, %g4
	or	%g4, PSR_PIL, %g1	! spl hi to prevent interrupts
	mov	%g1, %psr
	nop; nop; nop			! psr delay
	ld	[%g5 + MPCB_UWM], %g1	! get user window mask
	clr	[%g5 + MPCB_UWM]	! throw user windows away
	sethi	%hi(nwindows), %g5
	ld	[%g5 + %lo(nwindows)], %g5
	b	2f
	dec	%g5			! %g5 == NW-1
1:
	srl	%g2, 1, %g3		! next WIM = ror(WIM, 1, NW)
	sll	%g2, %g5, %g2		! %g5 == NW-1
	or	%g2, %g3, %g2
	mov	%g2, %wim		! install wim
	bclr	%g2, %g1		! clear bit from UWM
2:
	tst	%g1			! more user windows?
	bnz,a	1b
	mov	%wim, %g2		! get wim

	ld	[THREAD_REG + T_LWP], %g5
	ld	[%g5 + CPU_MPCB], %g5
	clr	[%g5 + MPCB_WBCNT]	! zero window buffer cnt
	clr	[%g5 + MPCB_SWM]	! clear newtrap flag
	b	splx			! splx and return
	mov	%g4, %o0
3:
	retl
 	clr     [%g5 + MPCB_WBCNT]       ! zero window buffer cnt
	SET_SIZE(trash_user_windows)

/*
 * Clean out register file.
 * Note: this routine is using the trap window.
 * [can't use globals unless they are preserved for the user]
 */
	.type	.clean_windows,#function

.clean_windows:
	CPU_ADDR(%l5, %l4)		! load CPU struct addr to %l5 using %l4
	ld	[%l5 + CPU_THREAD], %l6	! load thread pointer
	mov	1, %l4
	stb	%l4, [%l6 + T_POST_SYS]	! so syscalls will clean windows
	ld	[%l5 + CPU_MPCB], %l6	! load mpcb pointer
	ld	[%l6 + MPCB_FLAGS], %l4	! set CLEAN_WINDOWS in pcb_flags
	mov	%wim, %l3
	bset	CLEAN_WINDOWS, %l4
	st	%l4, [%l6 + MPCB_FLAGS]
	srl	%l3, %l0, %l3		! test WIM bit
	btst	1, %l3
	bnz,a	.cw_out			! invalid window, just return
	mov	%l0, %psr		! restore PSR_CC

	mov	%g1, %l5		! save some globals
	mov	%g2, %l6
	mov	%g3, %l7
	mov	%wim, %g2		! put wim in global
	mov	0, %wim			! zero wim to allow saving
	mov	%l0, %g3		! put original psr in global
	b	2f			! test next window for invalid
	save

	!
	! Loop through windows past the trap window
	! clearing them until we hit the invalid window.
	!
1:
	clr	%l1			! clear the window
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
	clr	%o6
	clr	%o7
	save
2:
	mov	%psr, %g1		! get CWP
	srl	%g2, %g1, %g1		! test WIM bit
	btst	1, %g1
	bz,a	1b			! not invalid window yet
	clr	%l0			! clear the window

	!
	! Clean up trap window.
	!
	mov	%g3, %psr		! back to trap window, restore PSR_CC
	mov	%g2, %wim		! restore wim
	nop; nop;			! psr delay
	mov	%l5, %g1		! restore globals
	mov	%l6, %g2
	mov	%l7, %g3
	mov	%l2, %o6		! put npc in unobtrusive place
	clr	%l0			! clear the rest of the window
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
	clr	%o7
	jmp	%o6			! return to npc
	rett	%o6 + 4

.cw_out:
	nop
	nop				! psr delay
	jmp	%l2			! return to npc
	rett	%l2 + 4
	SET_SIZE(.clean_windows)

#endif	/* lint */

/*
 * Enter the monitor -- called from console abort
 */

#if defined(lint)

/* ARGSUSED */
void
montrap(void (*func)(void))
{}

#else	/* lint */

	ENTRY_NP(montrap)
	save	%sp, -SA(MINFRAME), %sp	! get a new window
	call	flush_windows		! flush windows to stack
	nop
#if defined(SAS) || defined(MPSAS)
	ta	255			! trap to simulator
	nop
#else
	call	%i0			! go to monitor
	nop
#endif /* SAS */
	ret
	restore
	SET_SIZE(montrap)

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
	mov	%psr, %o1
	or	%o1, PSR_PIL, %o2
	mov	%o2, %psr
	nop
	nop
	mov	%tbr, %o4
	mov	%o0, %tbr
	nop
	nop
	nop
	mov	%o1, %psr 	! restore psr
	nop
	nop
	retl
	mov	%o4, %o0	! return value = old tbr
	SET_SIZE(set_tbr)

#endif	/* lint */

/*
 * void
 * reestablish_curthread(void)
 *    - reestablishes the invariant that THREAD_REG contains
 *      the same value as the cpu struct for this cpu (implicit from
 *      where we're running). This is needed for OBP callback routines.
 *	We need to figure out the cpuid by reading hardware registers.
 *	We can't use the CPU_ADDR macro since that depends on the
 *	contents of the tbr and we don't know necessarily know which
 *	tbr we're running on (it may be the kernel or the prom monitor's
 *	tbr due to possible kadb interference).
 */

#if defined(lint)

void
reestablish_curthread(void)
{}

#else	/* lint */

	ENTRY_NP(reestablish_curthread)

	mov	%g0, %o1			! start with cpuid = 0

	sethi	%hi(viking), %o1
	ld	[%o1 + %lo(viking)], %o1
	tst	%o1
	bz,a	1f
	nop

	sethi	%hi(mxcc), %o1
	ld	[%o1 + %lo(mxcc)], %o1
	tst	%o1
	bz,a	2f
	nop

	sethi	%hi(MXCC_PORT), %o1	! MXCC version
	add	%o1, %lo(MXCC_PORT), %o1
	lda	[%o1]ASI_MXCC, %o1
	sll	%o1, 6, %o1
	srl	%o1, 30, %o1
	b,a	3f
	nop

1:	sethi	%hi(small_4m), %o1	! check if small_4m machine
	ld	[%o1 + %lo(small_4m)], %o1
	tst	%o1
	bz,a	1f
	nop
	mov	%g0, %o1		! if yes then cpuid = 0
	b,a	3f
	nop

1:	lda     [%o1]ASI_MOD, %o1	! ROSS version
	sll     %o1, 15, %o1
	srl     %o1, 30, %o1
	b,a	3f
	nop

2:	
	sethi   %hi(debug_enter_cpuid), %o1     ! Viking without Ecache
	ld      [%o1 + %lo(debug_enter_cpuid)], %o1 ! CPU id is saved

3:	sll	%o1, 2, %o1		! turn into index
	set	cpu, %o2		! point to start of cpu table
	ld	[%o2 + %o1], %o1	! add offset
	retl
	ld	[%o1 + CPU_THREAD], THREAD_REG
	SET_SIZE(reestablish_curthread)

/*
 * return the condition codes in %g1
 * note: this routine is using the trap window.
 */

	.type	.getcc, #function
.getcc:
	sll	%l0, 8, %g1		! right justify condition code
	srl	%g1, 28, %g1
	jmp	%l2			! return, skip trap instruction
	rett	%l2 + 4
	SET_SIZE(.getcc)

/*
 * Set the condtion codes from the value in %g1
 * Note: this routine is using the trap window.
 */
	.type	.setcc, #function
.setcc:
	sll	%g1, 20, %l5		! mov icc bits to their position in psr
	set	PSR_ICC, %l4		! condition code mask
	andn	%l0, %l4, %l0		! zero the current bits in the psr
	or	%l5, %l0, %l0		! or new icc bits
	mov	%l0, %psr		! write new psr
	nop; nop; nop;			! psr delay
	jmp	%l2			! return, skip trap instruction
	rett	%l2 + 4
	SET_SIZE(.setcc)

/*
 * some user has to do unaligned references, yuk!
 * set a flag in the pcb so that when alignment traps happen
 * we fix it up instead of killing the user
 * Note: this routine is using the trap window
 */
	.type	.fix_alignment, #function
.fix_alignment:
	CPU_ADDR(%l5, %l4)		! load CPU struct addr to %l5 using %l4
	ld	[%l5 + CPU_THREAD], %l5	! load thread pointer
	ld	[%l5 + T_LWP], %l5	! load lwp pointer
	ld	[%l5 + PCB_FLAGS], %l4	! get pcb_flags
	bset	FIX_ALIGNMENT, %l4
	st	%l4, [%l5 + PCB_FLAGS]
	jmp	%l2			! return, skip trap instruction
	rett	%l2 + 4
	SET_SIZE(.fix_alignment)

#endif	/* lint */

/*
 * Return the current THREAD pointer.
 * This is also available as an inline function.
 */
#if defined(lint)

kthread_id_t
threadp(void)
{ return ((kthread_id_t)0); }

trapvec mon_clock14_vec;
trapvec kadb_tcode;
trapvec trap_kadb_tcode;
trapvec trap_monbpt_tcode;

#else	/* lint */

	ENTRY_NP(threadp)
	retl
	mov	THREAD_REG, %o0
	SET_SIZE(threadp)

/*
 * The level 14 interrupt vector can be handled by both the
 * kernel and the monitor.  The monitor version is copied here
 * very early before the kernel sets the tbr.  The kernel copies its
 * own version a little later in mlsetup.  They are write proteced
 * later on in kvm_init() when the the kernels text is made read only.
 * The monitor's vector is installed when we call the monitor and
 * early in system booting before the kernel has set up its own
 * interrupts, otherwise the kernel's vector is installed.
 */
	.align  8			! must be double word aligned
	.global mon_clock14_vec
mon_clock14_vec:
	SYS_TRAP(0x1e)			! gets overlaid.
	SET_SIZE(mon_clock14_vec)

/*
 * Glue code for traps that should take us to the monitor/kadb if they
 * occur in kernel mode, but that the kernel should handle if they occur
 * in user mode.
 */
        .global trap_monbpt_tcode, trap_kadb_tcode
	.global	mon_breakpoint_vec
 
 /* tcode to replace trap vecots if kadb steals them away */
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
	btst	PSR_PS, %l0		! test pS
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
	btst	PSR_PS, %l0		! test pS
	bz	_sys_trap		! user-mode, treat as bad trap
	nop
	mov	%l0, %psr		! restore psr
	nop				! psr delay
	b	kadb_tcode
	nop				! psr delay, too!

	.align	8		! MON_BREAKPOINT_VEC MUST BE DOUBLE ALIGNED.
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

#if defined(SAS)
	ENTRY_NP(mpsas_idle_trap)
	retl
	t	211	! stall here until something fun happens.
	SET_SIZE(mpsas_idle_trap)
#endif

#endif	/* lint */

/*
 * SPARC Trap Enable/Disable
 */

#if defined(lint)

u_int
disable_traps(void)
{ return (0); }

/* ARGSUSED */
void
enable_traps(u_int psr_value)
{}

#else	/* lint */

	ENTRY_NP(enable_traps)
	.volatile
	wr	%o0, %psr
	nop; nop; nop			! SPARC V8 requirement
	retl
	nop
	.nonvolatile
	SET_SIZE(enable_traps)

	ENTRY_NP(disable_traps)
	rd	%psr, %o0		! save old value
	andn	%o0, PSR_ET, %o1	! disable traps
	wr	%o1, %psr		! write psr
	nop; nop; nop			! SPARC V8 requirement
	retl
	nop
	SET_SIZE(disable_traps)

#endif	/* lint */

/*
 * Processor Unit
 */
#define	ASI_CC		0x02
#define	CC_BASE		0x01c00000

/*
 * CC Block Copy routines
 */
#define	CC_SDR	0x00000000
#define	CC_SSAR	0x00000100
#define	CC_SDAR	0x00000200

#ifdef	lint

longlong_t
xdb_cc_ssar_get(void)
{ return (0); }

/* ARGSUSED */
void
xdb_cc_ssar_set(longlong_t src)
{}

longlong_t
xdb_cc_sdar_get(void)
{ return (0); }

/* ARGSUSED */
void
xdb_cc_sdar_set(longlong_t src)
{}

#else	/* lint */

	ENTRY(xdb_cc_ssar_get)
	set	CC_BASE+CC_SSAR, %o0
	retl
	ldda	[%o0]ASI_CC, %o0	/* delay slot */
	SET_SIZE(xdb_cc_ssar_get)

	ENTRY(xdb_cc_ssar_set)
	set	CC_BASE+CC_SSAR, %o2
	retl
	stda	%o0, [%o2]ASI_CC	/* delay slot */
	SET_SIZE(xdb_cc_ssar_set)

	ENTRY(xdb_cc_sdar_get)
	set	CC_BASE+CC_SDAR, %o0
	retl
	ldda	[%o0]ASI_CC, %o0	/* delay slot */
	SET_SIZE(xdb_cc_sdar_get)

	ENTRY(xdb_cc_sdar_set)
	set	CC_BASE+CC_SDAR, %o2
	retl
	stda	%o0, [%o2]ASI_CC	/* delay slot */
	SET_SIZE(xdb_cc_sdar_set)

#endif	/* lint */




/*
 * Set the interrupt target register to the next available CPU using
 * a round-robin scheme.
 *
 *	Entry: traps disabled
 *		%o2 = current CPU id.
 */
#if defined(lint)
 
/* ARGSUSED */
void
set_itr(cpuid)
{}

#else	/* lint */
 
	ENTRY_NP(set_itr) 
	set	cpu, %o3			! get cpu array pointer
	mov	%o2, %o5			! save current cpu
1:
	inc	%o2				! increment to get next cpu id
	and	%o2, NCPU-1, %o2		! mask of high order bits
	sll	%o2, 2, %o4			! get index into cpu array
	ld	[%o3 + %o4], %o4		! get cpu struct pointer
	tst	%o4				! If NULL, get next cpu id
	bz	1b
	nop
	lduh	[%o4 + CPU_FLAGS], %o4		! get cpu_flags and determine
	and	%o4, CPU_READY|CPU_ENABLE, %o4 	! if the CPU is running
	cmp	%o4, CPU_READY|CPU_ENABLE	! branch if disabled
	bne	1b
	nop
	sethi	%hi(v_sipr_addr), %o4		! get interrupt target register
	ld	[%o4 + %lo(v_sipr_addr)], %o4
	ld	[%o4 + IR_SET_ITR], %o3
	cmp	%o2, %o5			! see if we got the same cpu
	bz	1f				! skip setting itr if yes
						! this is important for tsunami
	cmp	%o3, %o5			! do we own itr?
	bz	2f				! yes, give it away
	nop
1:
	retl
	nop
2:
	st	%o2, [%o4 + IR_SET_ITR]		! set interrupt target register
	retl
	ld	[%o4 + IR_SET_ITR], %o2		! read to flush store buffer
	SET_SIZE(set_itr)
#endif	/* lint */

/*
 * Set the interrupt target register to the next available CPU using
 * a round-robin scheme.
 *
 *	Entry: from C  - cpu_lock held
 *		%o0 = current CPU id.
 */
#if defined(lint)
 
/* ARGSUSED */
void
update_itr(cpuid)
{}

#else	/* lint */
 
	ENTRY(update_itr) 
	b	set_itr			! set_itr does all the work and returns
	mov	%o0, %o2		! move argument into position
	SET_SIZE(update_itr)
#endif	/* lint */

#if defined(lint)

/* ARGSUSED */
void
set_itr_bycpu(cpuid)
{}

#else	/* lint */
	.global set_itr_bycpu

	ENTRY 	(set_itr_bycpu)
	sethi	%hi(v_sipr_addr), %o1
	ld	[%o1 + %lo(v_sipr_addr)], %o1
	st	%o0, [%o1 + IR_SET_ITR]		! set interrupt target register
	retl
	nop
	SET_SIZE(set_itr_bycpu)
#endif	/* lint */


#if !defined(lint)

/*
 * setpsr(newpsr)
 * %i0 contains the new bits to install in the psr
 * %l0 contains the psr saved by the trap vector
 * allow only user modifiable of fields to change
 */
	.type	.setpsr, #function
.setpsr:
	set	PSL_UBITS, %l1
	andn	%l0, %l1, %l0			! zero current user bits
	and	%i0, %l1, %i0			! clear bits not modifiable
	wr	%i0, %l0, %psr
	nop; nop; nop;
	jmp	%l2
	rett	%l2 + 4
	SET_SIZE(.setpsr)

#endif	/* lint */

#if defined(lint)
/*
 * These need to be defined somewhere to lint and there is no "hicore.s"...
 */
char etext[1], end[1];
#endif	/* lint*/
