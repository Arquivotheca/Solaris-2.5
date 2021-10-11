/*
 * Copyright (c) 1991, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)locore.s	1.77	95/08/18 SMI"

#include <sys/param.h>
#include <sys/vmparam.h>
#include <sys/errno.h>
#include <sys/asm_linkage.h>
#include <sys/buserr.h>
#include <sys/clock.h>
#include <sys/intreg.h>
#include <sys/memerr.h>
#include <sys/eeprom.h>
#include <sys/debug/debug.h>
#include <sys/mmu.h>
#include <sys/pcb.h>
#include <sys/psr.h>
#include <sys/pte.h>
#include <sys/machpcb.h>
#include <sys/privregs.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/scb.h>
#include <sys/auxio.h>
#include <sys/machthread.h>
#include <sys/machlock.h>
#include <sys/msacct.h>
#include <sys/mutex_impl.h>
#include <sys/traptrace.h>

#if defined(lint)

#include <sys/thread.h>
#include <sys/time.h>

struct pte Sysmap[1];
struct pte ESysmap[1];
char Sysbase[1];
char Syslimit[1];
char DVMA[1];

#else	/* lint */

#include "assym.s"

	.seg	".data"

#define	PROT		((PG_V | PG_W) >> PG_S_BIT)
#define	VALID		((PG_V) >> PG_S_BIT)
#define	PG_VS_MASK	((PG_V | PG_S) >> PG_S_BIT)

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
 * On the sun4c we put the message buffer in the third and fourth pages.
 * We set things up so that the first 2 pages of KERNELBASE is illegal
 * to act as a redzone during copyin/copyout type operations. One of
 * the reasons the message buffer is allocated in low memory to
 * prevent being overwritten during booting operations (besides
 * the fact that it is small enough to share pages with others).
 *
 * The DVMA area is not like other sun architectures. On sun4c
 * I/O goes directly through whatever is mapped in context 0
 * (kernel context), and not through any cache. We reserve an
 * area of kernel virtual addresses though in order to have an
 * area to map stuff for DVMA. For historical reasons, this
 * symbol is declared and set here.
 */

	.global	DVMA, msgbuf, t0stack

DVMA	= DVMABASE			! address of DVMA area
msgbuf	= KERNELBASE + 0x2000		! address of printf message buffer

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

	.global	t0
	.align	PTR24_ALIGN		! thread must be aligned for mutex impl.
	.type	t0, #object
t0:
	.skip	THREAD_SIZE		! thread 0
	.size	t0, THREAD_SIZE
	.align	8

/*
 * Trap tracing.  If TRAPTRACE is defined, every trap records the
 * %tbr, %psr, %pc, %sp, %g7 (THREAD_REG) and possibly more into a circular
 * trace buffer.
 *
 * TRAPTRACE can be defined in Makefile.sun4c.
 */

#ifdef	TRAPTRACE
#define	TRAP_TSIZE	(TRAP_ENT_SIZE*256)

	.global	trap_tr, trap_trace_ctl
_trap_last_intr:
	.word	0
_trap_last_intr2:
	.word	0
	.align	16
trap_trace_ctl:
	.word	trap_tr			! next
	.word	trap_tr			! first
	.word	trap_tr + TRAP_TSIZE	! limit
	.word	0			! junk for alignment of prom dump
	.align	16
trap_tr:
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
 * Software page tables for ekernelmap which
 * manage virtual memory addressable by the ethernet.
 */
#define	E_vaddr(x)	((((x)-E_Sysmap)/4)*NBPG + E_SYSBASE)
#define	E_SYSMAP(mname, vname, npte)	\
	.global	mname;			\
	.type	mname,#object;		\
	.size	mname,(4*npte);		\
mname:	.skip	(4*npte);		\
	.global	vname;			\
	.type	vname,#object;		\
	.size	vname,0;		\
vname = E_vaddr(mname);
	E_SYSMAP(E_Sysmap  ,E_Sysbase	,E_SYSPTSIZE	)
	E_SYSMAP(E_ESysmap ,E_Syslimit	,0		) ! must be last

	.global	E_Syssize
	.type	E_Syssize, #object
	.size	E_Syssize, 0
E_Syssize = (E_ESysmap-E_Sysmap)/4

/*
 * System software page tables
 */
#define	vaddr(x)	((((x)-Sysmap)/4)*NBPG + SYSBASE)
#define	SYSMAP(mname, vname, npte)	\
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


#if defined(SAS) || defined(MPSAS)
	DGDEF(availmem)
	.word	0
#endif

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
#define	MOVPSRL0	0xa1480000
#define	MOVL4		0xa8102000
#define	BA		0x10800000
#define	NO_OP		0x01000000

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
	nop; mov %psr,%l0; b (H); mov 7, %l6;

/* the following trap uses only the trap window, you must be prepared */
#define	WIN_TRAP(H) \
	mov %psr,%l0; mov %wim,%l3; b (H); mov 7,%l6;

#define	SYS_TRAP(T) \
	mov %psr,%l0; mov (T),%l4; b _sys_trap; mov 7,%l6;

#define	TRAP_MON(T) \
	mov %psr,%l0; b trap_mon; mov (T),%l4; nop;

#define	GETPSR_TRAP() \
	mov %psr, %i0; jmp %l2; rett %l2+4; nop;

#define	FAST_TRAP(T) \
	b (T); nop; nop; nop;
/*
 * This vector is installed after the loop has walked through the
 * scb adjusting the "nwindows-1" values for each vector, we also
 * "know" that the code this calls does not need %l6 set up
 */

#define BUG1156505_TRAP(T) \
	sethi %hi(bug1156505_enter),%l6; mov T, %l4; \
	jmp %l6+%lo(bug1156505_enter); mov %psr,%l0;


#ifdef	SIMUL
/*
 * For hardware simulator, want to double trap on any unknown trap
 * (which is usually a call into SAS)
*/
#define	BAD_TRAP \
	mov %psr, %l0; mov %tbr, %l4; t 0; nop;
#else
#define	BAD_TRAP	SYS_TRAP((. - scb) >> 4);
#endif

/*
 * Tracing traps.  For speed we don't save the psr;  all this means is that
 * the condition codes come back different.  This is OK because these traps
 * are only generated by the trace_[0-5]() wrapper functions, and the
 * condition codes are volatile across procedure calls anyway.
 * If you modify any of this, be careful.
 */
#ifdef	TRACE
#define	TRACE_TRAP(H) \
	b (H); nop; nop; nop;
#else	/* TRACE */
#define	TRACE_TRAP(H) \
	BAD_TRAP;
#endif	/* TRACE */

#define	PATCH_ST(T, V) \
	set	scb, %g1; \
	set	MOVPSRL0, %g2; \
	st	%g2, [%g1 + ((V)*16+0*4)]; \
	set	_sys_trap, %g2; \
	sub	%g2, %g1, %g2; \
	sub	%g2, ((V)*16 + 8), %g2; \
	srl	%g2, 2, %g2; \
	set	BA, %g3; \
	or	%g2, %g3, %g2; \
	st	%g2, [%g1 + ((V)*16+2*4)]; \
	set	MOVL4 + (T), %g2; \
	st	%g2, [%g1 + ((V)*16+1*4)];

#define	PATCH_T(H, V) \
	set	scb, %g1; \
	set	(H), %g2; \
	sub	%g2, %g1, %g2; \
	sub	%g2, (V)*16, %g2; \
	srl	%g2, 2, %g2; \
	set	BA, %g3; \
	or	%g2, %g3, %g2; \
	st	%g2, [%g1 + ((V)*16+0*4)]; \
	set	MOVPSRL0, %g2; \
	st	%g2, [%g1 + ((V)*16+1*4)];

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

struct scb scb;
int nwindows;
trapvec trap_kadb_tcode;
trapvec bug1156505_vec;

#else	/* lint */

	ENTRY_NP2(_start, scb)

	TRAP(.entry);				! 00
	SYS_TRAP(T_FAULT | T_TEXT_FAULT);	! 01
#ifdef VAC
	! XXX A bug in the SS2 cache controller will corrupt the first
	! XXX word of the vector in the cache under certain conditions
	! XXX (see bug #1050558). The solution is to invalidate the
	! XXX cache line containing the trap vector, so we never have
	! XXX a cache-hit.
	! XXX we handle WIN_TRAPs indirectly, others via sys_trap().
	WIN_TRAP(go_multiply_check);		! 02
#else /* VAC */
	WIN_TRAP(multiply_check);		! 02
#endif /* VAC */
	SYS_TRAP(T_PRIV_INSTR);			! 03
	SYS_TRAP(T_FP_DISABLED);		! 04
#ifdef	TRAPTRACE
#ifdef VAC
	! XXX bug in the SS2 cache controller (see above)
	WIN_TRAP(go_window_overflow_trace);	! 05
	WIN_TRAP(go_window_underflow_trace);	! 06
#else /* VAC */
	WIN_TRAP(window_overflow_trace);	! 05
	WIN_TRAP(window_underflow_trace);	! 06
#endif /* VAC */
#else	/* TRAPTRACE */
#ifdef VAC
	! XXX bug in the SS2 cache controller (see above)
	WIN_TRAP(go_window_overflow);		! 05
	WIN_TRAP(go_window_underflow);		! 06
#else /* VAC */
	WIN_TRAP(_window_overflow);		! 05
	WIN_TRAP(_window_underflow);		! 06
#endif /* VAC */
#endif	/* TRAPTRACE */
	SYS_TRAP(T_ALIGNMENT);			! 07
	SYS_TRAP(T_FP_EXCEPTION);		! 08
	SYS_TRAP(T_FAULT | T_DATA_FAULT);	! 09
	SYS_TRAP(T_TAG_OVERFLOW)		! 0A
	BAD_TRAP; BAD_TRAP;			! 0B - 0C
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 0D - 10
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
	SYS_TRAP(T_INTERRUPT | 13);		! 1D
	SYS_TRAP(T_INTERRUPT | 14);		! 1E
	SYS_TRAP(T_INTERRUPT | 15);		! 1F

/*
 * The rest of the traps in the table up to 0x80 should 'never'
 * be generated by hardware.
 */
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 20 - 23
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 24 - 27
	BAD_TRAP; BAD_TRAP; 			! 28 - 29
	SYS_TRAP(T_IDIV0);			! 2A
	BAD_TRAP;				! 2B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 2C - 2F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 30 - 33
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 34 - 37
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 38 - 33
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
	TRAP(syscall_trap_4x);			! 80 - SunOS4.x system call
	SYS_TRAP(T_BREAKPOINT);			! 81 - user breakpoint
	SYS_TRAP(T_DIV0);			! 82 - divide by zero
	WIN_TRAP(fast_window_flush);		! 83 - flush windows
	TRAP(.clean_windows);			! 84 - clean windows
	BAD_TRAP;				! 85 - range check
	TRAP(.fix_alignment)			! 86 - do unaligned references
	BAD_TRAP;				! 87
	TRAP(syscall_trap);			! 88 - syscall
	TRAP(set_trap0_addr);			! 89 - set trap0 address
	BAD_TRAP; BAD_TRAP; 			! 8A - 8B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 8C - 8F
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 90 - 93
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 94 - 97
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 98 - 9B
	BAD_TRAP; BAD_TRAP; BAD_TRAP; BAD_TRAP; ! 9C - 9F
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
	BAD_TRAP; BAD_TRAP; BAD_TRAP;		! FC - FE
	TRAP_MON(0xff)				! FF


	SET_SIZE(_start)
	SET_SIZE(scb)
/*
 *  This is the workaround for (yet another) bug #1156505.
 *  It is different from 1050558.
 *  The overall strategy is:  We need to flush the line in the cache
 *  coresponding to the target of the write fault, since it is _definitely_
 *  corrupted.  
 *  We perform this stuff while in a non-cached page.  It was made that
 *  way in startup().
 *
 * Register state:
 * %l0 = psr
 * %l1 = pc
 * %l2 = npc
 * %l4 = trap type
 * %l6 = nwindows - 1
 * 
 * Note that the contents of %l6 (nwindows-1) will be trashed so
 * we can use it for scratch.  We'll reload it soon.
 *
 * Finally, note that we should not do any reads from cacheable data space
 * until we've executed the 3rd 'sta' line below, or we are at risk of reading
 * corrupted data.  Writes are ok.
 */
#define PAGESIZE	0x1000
	.align	PAGESIZE	! paranoia
	
	ENTRY_NP(bug1156505_enter)

	set	SYNC_ERROR_REG, %l7	
	lda	[%l7]ASI_CTL, %l3		! get cause of fault
	sta	%l3, [%l7]ASI_CTL		! remember cause of fault
/*
 *  We only have to do the following nonsense if 
 *  this is a write fault.  
 */
	sethi	%hi(SE_RW), %l5			! cheat, no low bits
	btst	%l5, %l3			! see if it's a WRITE fault
	bz	sys_trap			! if not, boogie
	.empty					! don't warn about set
	set	SYNC_VA_REG, %l7		! get fault addr to flush
	lda	[%l7]ASI_CTL, %l6
	sta	%l6, [%l7]ASI_CTL		! restore fault addr

	/*
	 *  We could read vac_size and vac_linesize here, but we risk
	 *  getting corrupted data.  Just hard-code it at this late stage.
	 *  Startup.c ASSERTS the value assumed here.
	 */
	set	0xffe0, %l5		! prepare mask of :
					! (vac_size - 1) & ~(vac_linesz - 1)
	and	%l6, %l5, %l7		! get offset of fault addr within cache
	sethi	%hi(CACHE_TAGS), %l3	! cheat, no low bits
	ba	sys_trap
	sta	%g0, [%l3 + %l7]ASI_CTL	! invalidate that cache line!

/*
 * Note that we lump the following code in with the workaround above so
 * that we won't waste as much space when we mark the page non-cachable.
 * By then the following code will have been (mostly) already executed.
 * (the code is marked non-cacheable in startup, which is called from main)
 */
/*
 * System initialization
 *
 * We make the following assumptions about our environment
 * as set up by the monitor:
 *
 *	- traps are disabled (XXX suppose monitor is stepping us)
 *	- we have enough memory mapped for the entire kernel + some more
 * KNH: (Not true, since the ONLY thing mapped is the T + D + BSS)
 *	- all pages are writable
 *	- the last pmeg [SEGINV] has no valid pme's
 *	- the next to the last pmeg has no valid pme's
 *
 *	- when the monitor's romp->v_memorybitmap points to a zero
 *	    - each low segment i is mapped to use pmeg i
 *	    - each page map entry i maps physical page i
 * KNH:	The above statement has no meaning for OBP's
 *
 *	- upon entry, the romvec pointer (romp) is the first
 *	  argument; i.e., %o0.
 *
 *	- the debug vector (dvec) pointer is the second argument (%o1)
 *
 *	- the bootops vector is in the third argument (%o2)
 *
 *	NOTE: Take care not to munge these values until they can be
 *	safely stored. We stash them in %g7 and %g6 until relocated
 *	and bss cleared.
 *
 * We will set the protection properly in startup().
 */
.entry:
	mov	%o0, %g7		! save arg (romp) until bss is clear
	mov	%o1, %g6		! save dvec
	mov	%o2, %g5		! save bootops
	set	PSR_S|PSR_PIL|PSR_ET, %g1	! setup psr, leave traps
						!  enabled for monitor XXX
	mov	%g1, %psr
	mov	0x02, %wim		! setup wim
	nop				! psr delay
	nop
	set	CONTEXT_REG, %g1	! setup kernel context (0)
	stba	%g0, [%g1]ASI_CTL

/*
 * KNH:	It is NO LONGER necessary to remap the kernel.  The boot program is
 *	now charged with this responsibility.  The kernel is loaded en masse
 *	into wherever it is linked to run from.  For most kernels known to
 *	man, this place is KERNBASE and is given in the Mapfile.
 */
	!
	! Patch vector 0 trap to "zero" in case it happens again.
	!
	PATCH_ST(T_ZERO, 0)

	!
	! Find the the number of implemented register windows.
	! The last byte of every trap vector must be equal to
	! the number of windows in the implementation minus one.
	! The trap vector macros (above) depend on it!
	! The vectors assume nwindows is 8, only fix them up if not true.
	!
	mov	%g0, %wim		! note psr has cwp = 0
	save				! decrement cwp, wraparound to NW-1
	mov	%psr, %g1
	and	%g1, PSR_CWP, %g1	! we now have nwindows-1
	restore				! get back to orignal window
	mov	2, %wim			! reset initial wim

	cmp	%g1, 7
	be	1f
	sethi	%hi(nwindows), %g4	! initialize pointer to nwindows

	set	scb, %g2
	mov	256, %g3		! number of trap vectors
0:
	stb	%g1, [%g2 + 15]		! write last byte of trap vectors
	subcc	%g3, 1, %g3		! with nwindows-1
	bnz	0b
	add 	%g2, 16, %g2

1:
	inc	%g1			! initialize the nwindows variable
	st	%g1, [%g4 + %lo(nwindows)]

	sethi   %hi(bootops), %g4
	st	%g5, [%g4 + %lo(bootops)]	! save bootops vector

	!
	! The code that flushes windows may have an extra save/restore
	! as current sparc implementations have 7 and 8 windows.
	! On those implementations with 7 windows write nops over the
	! unneeded save/restore.
	!
	cmp	%g1, 8
	be	1f
	.empty				! hush assembler warnings
	set	fixnwindows, %o3
	set	NO_OP, %o2
	st	%o2, [%o3]
	st	%o2, [%o3 + 4]
1:
	!
	! Now calculate winmask.  0's set for each window.
	!
	dec	%g1
	mov	-2, %g2
	sll	%g2, %g1, %g2
	sethi	%hi(winmask), %g4
	st	%g2, [%g4 + %lo(winmask)]

#if defined(SAS) || defined(MPSAS)
	!
	! If we are in the simulator we now size memory by counting the
	! valid segment maps.
	!
	clr	%l0
	set	PMGRPSIZE, %g2
2:
	lduba	[%l0]ASI_SM, %g1
	cmp	%g1, 127		! Campus has 7 bits for PMEG
	blu,a	2b
	add	%l0, %g2, %l0

	sethi	%hi(_availmem), %g1
	st	%l0, [%g1 + %lo(_availmem)]
#endif	/* SAS */

	!
	! Setup trap base and make a kernel stack.
	!
	mov	%tbr, %l4		! save monitor's tbr
	bclr	0xfff, %l4		! remove tt

	!
	! Save monitor's level14 clock interrupt vector code.
	!
	or	%l4, TT(T_INT_LEVEL_14), %o0
	set	mon_clock14_vec, %o1
	ldd	[%o0], %g2
	std	%g2, [%o1]
	ldd	[%o0 + 8], %g2
	std	%g2, [%o1 + 8]

	!
	! Save monitor's breakpoint vector code.
	!
	or	%l4, TT(ST_MON_BREAKPOINT + T_SOFTWARE_TRAP), %o0
	set	mon_breakpoint_vec, %o1
	ldd	[%o0], %g2
	std	%g2, [%o1]
	ldd	[%o0 + 8], %g2
	std	%g2, [%o1 + 8]
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

	set	T0STKSZ, %g2
	add	%g1, %g2, %sp
	sub	%sp, SA(MPCBSIZE), %sp
	mov	0, %fp

	!
	! Dummy up fake user registers on the stack.
	!
	set	USRSTACK-WINDOWSIZE, %g1
	st	%g1, [%sp + MINFRAME + SP*4] ! user stack pointer
	set	PSL_USER, %l0
	st	%l0, [%sp + MINFRAME + PSR*4] ! psr
	set	USRTEXT, %g1
	st	%g1, [%sp + MINFRAME + PC*4] ! pc
	add	%g1, 4, %g1
	st	%g1, [%sp + MINFRAME + nPC*4] ! npc

	mov	%psr, %g1
	or	%g1, PSR_ET, %g1	! (traps may already be enabled)
	mov	%g1, %psr		! enable traps

	!
	! Now save the romp and dvec we've been holding onto all this time.
	!
#if !defined(SAS) && !defined(MPSAS)
	sethi   %hi(romp), %o0
	st	%g7, [%o0 + %lo(romp)]
	sethi   %hi(dvec), %o0
	st	%g6, [%o0 + %lo(dvec)]
	sethi	%hi(bootops), %o0
	st	%g5, [%o0 + %lo(bootops)]
#endif	/* SAS */

	!
	! Initialize global thread register.
	!
	set	t0, THREAD_REG
	mov	1, FLAG_REG		! Flag register (%g6) must be non-zero

	!
	! Call mlsetup with address of prototype user registers, romp.
	! set up thread register before call to mlsetup for profiling
	!
        set     cpu0, %o0
        st      %o0, [THREAD_REG + T_CPU]
	st	THREAD_REG, [%o0 + CPU_THREAD]
	sethi	%hi(romp), %o1
	ld	[%o1 + %lo(romp)], %o1
	call	mlsetup
	add	%sp, REGOFF, %o0

	!
	! Now call main. We will return as process 1 (init).
	!
	call	main
	nop

	!
	! Proceed as if this was a normal user trap.
	!
	b,a	_sys_rtt			! fake return from trap

/*
 * a few more things that can be moved into this (soon to be) non-cached
 * page because they are only used once or are used before startup runs
 */

	.align	8
	.global bug1156505_vec

/*
 * If we are running on a SS2 with the cache controller bug, this
 * entry gets copied over the trap 09 entry
 */
bug1156505_vec:
	BUG1156505_TRAP(T_FAULT | T_DATA_FAULT)

	SET_SIZE(bug1156505_enter)

	.global trap_kadb_tcode
/*
 * tcode to replace trap vectors if kadb steals them away
 * this is used (as data) only by mlsetup
 */
trap_kadb_tcode:
	mov	%psr, %l0
	sethi	%hi(trap_kadb), %l4
	jmp	%l4 + %lo(trap_kadb)
       	mov     %tbr, %l4
	SET_SIZE(trap_kadb_tcode)

	/* This .align will fill the rest of the page with zeros */
	.align	PAGESIZE

	!
	! here we have a cache-aligned string of non_writeable
	! zeros used mainly by bcopy hardware
	! and by fpu_probe
	!
	.global zeros
zeros:
	.word 0,0,0,0,0,0,0,0

/*
 * The number of windows, set once on entry.  Note that it is
 * in the text segment so that it is write protected in startup().
 */
	.global nwindows
nwindows:
	.word   8

/*
 * The window mask, set once on entry.  Note that it is
 * in the text segment so that it is write protected in startup().
 */
	.global winmask
winmask:
	.word	8

#ifdef	TRAPTRACE
go_window_overflow_trace:
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
	b	go_window_overflow
	mov	%wim, %l3

go_window_underflow_trace:
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
	b	go_window_underflow
	mov	%wim, %l3

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

#ifdef VAC
	! XXX A bug in the SS2 cache controller will corrupt the first
	! XXX word of the vector in the cache under certain conditions
	! XXX (see bug #1050558). The solution is to invalidate the
	! XXX cache line containing the trap vector, so we never have
	! XXX a cache-hit.
	!
	! XXX These 4 traps are seperate entry points.
	! XXX They are no-oped in machdep.c for machines without the bug.
	!
	.global go_multiply_check
go_multiply_check:
	sethi   %hi(CACHE_TAGS+0x4020), %l5
	or      %l5, %lo(CACHE_TAGS+0x4020), %l5
	sta     %g0, [%l5]ASI_CTL
	b,a     multiply_check
 
	.global go_window_overflow
go_window_overflow:
	sethi   %hi(CACHE_TAGS+0x4050), %l5
	or      %l5, %lo(CACHE_TAGS+0x4050), %l5
	sta     %g0, [%l5]ASI_CTL
	b,a     _window_overflow
 
	.global go_window_underflow
go_window_underflow:
	sethi   %hi(CACHE_TAGS+0x4060), %l5
	or      %l5, %lo(CACHE_TAGS+0x4060), %l5
	sta     %g0, [%l5]ASI_CTL
	b,a     _window_underflow
#endif VAC

#endif	/* lint */


/*
 * Generic system trap handler.
 *
 * XXX - why two names? (A vestige from the compiler change?) _sys_trap
 *	 is only called from within this module, overflow*.s, underflow*.s
 *	 and crt.s.
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
#ifdef VAC
	! XXX A bug in the SS2 cache controller will corrupt the first
	! XXX word of the vector in the cache under certain conditions
	! XXX (see bug #1050558). The solution is to invalidate the
	! XXX cache line containing the trap vector, so we never have
	! XXX a cache-hit.
	! XXX we handle WIN_TRAPs indirectly, others via sys_trap().
	!
	! XXX For machines that don't have this bug, the following
	! XXX instructions will be no-op'ed in machdep.c during startup().
	!
	mov     %tbr, %l5               ! get tbr
	sethi   %hi(vac_size), %l3     ! get size of cache
	ld      [%l3 + %lo(vac_size)], %l3
	sub     %l3, 1, %l3             ! turn cache size into bitmask
	and     %l3, %l5, %l5           ! get offset of vector within cache
	                                ! hardware folks say lsb's don't matter
	sethi   %hi(CACHE_TAGS), %l3    ! cheat, no low bits
	sta     %g0, [%l3 + %l5]ASI_CTL ! invalidate that cache line!
#endif VAC

#ifdef	TRAPTRACE
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

#ifdef TRACE
	!
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
#ifdef	TRAPWINDOW
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
	btst	%l5, %l3		! retest
#endif	/* TRAPWINDOW */

	st	%fp, [%l7 + MINFRAME + SP*4] ! stack pointer
	st	%l0, [%l7 + MINFRAME + PSR*4] ! psr
	st	%l1, [%l7 + MINFRAME + PC*4] ! pc

	!
	! If we are in last trap window, all windows are occupied and
	! we must do window overflow stuff in order to do further calls
	!
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
	!
	! We do this now, rather than at the very start of sys_trap,
	! because the _SAVE_GLOBALS above gives us free scratch registers.
	!
	TRACE_SYS_TRAP_START(%l4, %g1, %g2, %g3, %g4, %g5)
	btst	%l5, %l3		! retest: compare WIM and CWM
#endif	/* TRACE */

	SAVE_OUTS(%l7 + MINFRAME)
	mov	%l7, %g5
	ld	[%l6 + CPU_THREAD], THREAD_REG	!  ! load thread pointer
	mov	1, FLAG_REG		! set flag reg (%g6) nz for mutex_enter
	sethi	%hi(scb + 0x1f), %l6		! XXX - temp hack
	ldub	[%l6 + %lo(scb + 0x1f)], %l6	! load NW-1

	st	%l0, [%l7 + MINFRAME + PSR*4] ! psr
	st	%l1, [%l7 + MINFRAME + PC*4] ! pc
	st	%l2, [%l7 + MINFRAME + nPC*4] ! npc

	!
	! If we are in last trap window, all windows are occupied and
	! we must do window overflow stuff in order to do further calls
	!
	bz	1f			! if ((CWM&WIM)==0) no overflow
	clr	[%g5 + MPCB_WBCNT]	! delay slot, save buffer ptr = 0
	not	%l5, %g2		! UWM = ~CWM
	mov	-2, %g3			! gen window mask from NW-1 in %l6
	sll	%g3, %l6, %g3

	andn	%g2, %g3, %g2
	b	.st_user_ovf		! overflow
	srl	%l3, 1, %g1		! delay slot,WIM = %g1 = ror(WIM, 1, NW)

	!
	! Compute the user window mask (lwp_pcb.pcb_uwm), which is a mask of
	! window which contain user data. It is all the windows "between"
	! CWM and WIM.
	!
1:
	subcc	%l3, %l5, %g1		! if (WIM >= CWM)
	bneg,a	2f			!    lwp_pcb.pcb_uwm = (WIM-CWM)&~CWM
	sub	%g1, 1, %g1		! else
2:					!    lwp_pcb.pcb_uwm = (WIM-CWM-1)&~CWM
	bclr	%l5, %g1
	mov	-2, %g3			! gen window mask from NW-1 in %l6
	sll	%g3, %l6, %g3
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

	!
	! Flush windows trap.
	!
	nop				! psr delay
	call	flush_user_windows	! psr delay, flush user windows
	nop				! psr delay
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
	mov	%l4, %o0		! psr delay - trap(t, rp);
	clr	%o2			! psr delay - addr = 0
	clr	%o3			! psr delay - be = 0
	mov	S_OTHER, %o4		!	      rw = S_OTHER
	call	trap			! C trap handler
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt			! return from trap

/*
 * Sys_trap overflow handling.
 * Psuedo subroutine returns to .st_have_window.
 */
.st_sys_ovf:
	!
	! Overflow from system.
	! Determine whether the next window is a user window.
	! If lwp->mpcb_uwm has any bits set, then it is a user
	! which must be saved.
	!
#ifdef	PERFMETER
	sethi	%hi(overflowcnt), %g5
	ld	[%g5 + %lo(overflowcnt)], %g2
	inc	%g2
	st	%g2, [%g5 + %lo(overflowcnt)]
#endif	/* PERFMETER */

	ld	[%l6 + CPU_MPCB], %g5
	sethi	%hi(scb + 0x1f), %l6		! XXX - temp hack
	ldub	[%l6 + %lo(scb + 0x1f)], %l6	! reload load NW-1
	tst	%g5			! lwp == 0 for kernel threads
	bz	1f			! skip uwm checking when lwp == 0
	srl	%l3, 1, %g1		! delay, WIM = %g1 = ror(WIM, 1, NW)

	ld	[%g5 + MPCB_UWM], %g2	! if (lwp_pcb.pcb_uwm)
	tst	%g2			!	user window
	bnz	.st_user_ovf
	nop
1:	
	!
	! Save supervisor window. Compute the new WIM and change current window
	! to the window to be saved.
	!
	sll	%l3, %l6, %l3		! %l6 == NW-1
	or	%l3, %g1, %g1
	save				! get into window to be saved
	mov	%g1, %wim		! install new WIM
	mov	%sp, %g4		! address for SAVE_WINDOW()
	!
	! Save window on the stack.
	!
.st_stack_res:
	SAVE_WINDOW(%g4)		! %g4 has user's %sp in shared case
	b	.st_have_window		! finished overflow processing
	restore				! delay slot, back to original window

.st_user_ovf:
	!
	! Overflow. Window to be saved is a user window.
	! Compute the new WIM and change the current window to the
	! window to be saved.
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
	! Save kernel copy of the shared window.
	!
	clr	[%g5 + MPCB_SWM]		! clear the shared window mas
	SAVE_WINDOW(%sp)		! save copy of shared window
	! use saved stack pointer for user copy	
	ld	[%g5 + MPCB_REGS + SP*4], %g4	
	
	!
	! We must check whether the user stack is resident where the window
	! will be saved, which is pointed to by the window's sp.
	! We must also check that the sp is aligned to a word boundary.
	! Normally, we would check the alignment, and then probe the top
	! and bottom of the save area on the stack. However we optimize
	! this by checking that both ends of the save area are within a
	! 4k unit (the biggest mask we can generate in one cycle), and
	! the alignment in one shot. This allows us to do one probe to
	! the page map. NOTE: this assumes a page size of at least 4k.
	!
1:
	and	%g4, 0xfff, %g1
#ifdef	VA_HOLE
	! check if the sp (in %g4) points into the hole in the address space
	sethi	%hi(hole_shift), %g2	! hole shift address
	ld	[%g2 + %lo(hole_shift)], %g3
	add	%g1, (14*4), %g1	! interlock, top of save area
	sra	%g4, %g3, %g2
	inc	%g2
	andncc	%g2, 1, %g2
	bz	1f
	andncc	%g1, 0xff8, %g0
	b,a	.st_stack_not_res	! sp points into the hole
1:
#else
	add	%g1, (14*4), %g1
	andncc	%g1, 0xff8, %g0
#endif	/* VA_HOLE */
	bz,a	.st_sp_bot
	lda	[%g4]ASI_PM, %g1	! check for stack page resident
	!
	! Stack is either misaligned or crosses a 4k boundary.
	!
	btst	0x7, %g4		! test sp alignment
	bz	.st_sp_top
	add	%g4, (14*4), %g1	! delay slot, check top of save area
	b,a	.st_stack_not_res	! stack misaligned, catch it later

.st_sp_top:
#ifdef	VA_HOLE
	! check if the sp points into the hole in the address space
	sra	%g1, %g3, %g2
	inc	%g2
	andncc	%g2, 1, %g2
	bz,a	1f
	lda	[%g1]ASI_PM, %g1	! get pme for this address
	b,a	.st_stack_not_res	! stack page can never be resident
1:
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	sra	%g4, %g3, %g2
	inc	%g2
	andncc	%g2, 1, %g2
	bz	1f
	cmp	%g1, PROT		! look for valid, writeable, user
	b,a	.st_stack_not_res	! stack not resident, catch it later
1:
#else
	lda	[%g1]ASI_PM, %g1	! get pme for this address
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	cmp	%g1, PROT		! look for valid, writeable, user
#endif	/* VA_HOLE */

	be,a	.st_sp_bot
	lda	[%g4]ASI_PM, %g1	! delay slot, check bottom of save area

	b,a	.st_stack_not_res	! stack not resident, catch it later

.st_sp_bot:
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	cmp	%g1, PROT		! look for valid, writeable, user
	be	.st_stack_res
	nop				! extra nop in rare case

.st_stack_not_res:
	!
	! User stack is not resident, save in PCB for processing in _sys_rtt.
	!
	ld	[%g5 + MPCB_WBCNT], %g1
	sll	%g1, 2, %g1		! convert to spbuf offset

	add	%g1, %g5, %g2
	st	%g4, [%g2 + MPCB_SPBUF] ! save sp in %g1 + curthread + MPCB_SPBUF
	sll	%g1, 4, %g1		! convert wbcnt to pcb_wbuf offset

	add	%g1, %g5, %g2
	set	MPCB_WBUF, %g4
	add	%g4, %g2, %g2
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
	ld	[%sp + MINFRAME + PSR*4], %l0 ! get saved psr
	btst	PSR_PS, %l0		! test pS for return to supervisor
	bnz	.sr_sup
	mov	%psr, %g1
	!
	! Return to user. Turn off traps using the current CWP (because
	! we are returning to user).
	! Test for AST for resched. or prof.
	!
	and	%g1, PSR_CWP, %g1	! insert current CWP in old psr
	andn	%l0, PSR_CWP, %l0
	or	%l0, %g1, %l0
	mov	%l0, %psr		! install old psr, disable traps
	nop; nop; nop;			! psr delay
	!
	! check for an AST that's posted to the lwp
	!
	ldub	[THREAD_REG + T_ASTFLAG], %g1
	mov	%sp, %g5		! user stack base is mpcb ptr
	tst	%g1			! test signal or AST pending
	bz	1f
	ld	[%g5 + MPCB_WBCNT], %g3	! delay - user regs been saved?

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
	! User regs have been saved into the LWP PCB.
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
	! Try to get one from his stack.
	!
#ifdef	PERFMETER
	sethi	%hi(underflowcnt), %l6
	ld	[%l6 + %lo(underflowcnt)], %l3
	inc	%l3
	st	%l3, [%l6 + %lo(underflowcnt)]
#endif	/* PERFMETER */
	set	scb, %l6		! get NW-1 for rol calculation
	ldub	[%l6+(5*16)+15], %l6	! last byte of overflow trap vector
	mov	%wim, %l3		! get wim
	sll	%l3, 1, %l4		! next WIM = rol(WIM, 1, NW)
	srl	%l3, %l6, %l5		! %l6 == NW-1
	or	%l5, %l4, %l5
	mov	%l5, %wim		! install it
	!
	! Normally, we would check the alignment, and then probe the top
	! and bottom of the save area on the stack. However we optimize
	! this by checking that both ends of the save area are within a
	! 4k unit (the biggest mask we can generate in one cycle), and
	! the alignment in one shot. This allows us to do one probe to
	! the page map. NOTE: this assumes a page size of at least 4k.
	!
	! notation: fp = bottom of save area; fp+14*4 = top of save area.
	!
	and	%fp, 0xfff, %g1
#ifdef	VA_HOLE
	! check if the fp points into the hole in the address space
	sethi	%hi(hole_shift), %g2	! hole shift address
	ld	[%g2 + %lo(hole_shift)], %g3
	add	%g1, (14*4), %g1	! interlock, bottom of save area
	sra	%fp, %g3, %g2		! test for bottom of area in hole
	inc	%g2
	andncc	%g2, 1, %g2
	bz	1f			! bottom of area is not in the hole
	andncc	%g1, 0xff8, %g0		! delay, test alignment and page-cross
	b	.sr_stack_not_res	! stack page can never be resident
	mov	%fp, %g1		! delay, save fault address
1:
#else
	add	%g1, (14*4), %g1
	andncc	%g1, 0xff8, %g0
#endif	/* VA_HOLE */
	bz,a	.sr_sp_bot		! if stack is aligned and on one page
	lda	[%fp]ASI_PM, %g2	! delay, check for stack page resident
	!
	! Stack is either misaligned or crosses a 4k boundary.
	!
	btst	0x7, %fp		! test fp alignment
	bz	.sr_sp_top		! not misaligned
	add	%fp, (14*4), %g1	! delay slot, check top of save area

	!
	! A user underflow with a misaligned sp.
	! Fake a memory alignment trap.
	!
	mov	%l3, %wim		! restore old wim

.sr_align_trap:
	or	%l0, PSR_PIL, %o1	! spl8 first to protect CPU base pri
	mov	%o1, %psr		! set priority before enabling (IU bug)
	wr	%o1, PSR_ET, %psr	! turn on traps
	nop				! psr delay
	call	splx			! psr delay - call splx()
	mov	%l0, %o0		! psr delay - pass old %psr to splx()
	mov	T_ALIGNMENT, %o0
	call	trap			! trap(T_ALIGNMENT, rp)
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt

	!
	! The stack crosses a 4K boundary.  Check top (%fp+14*4)
	!
.sr_sp_top:
#ifdef	VA_HOLE
	sra	%g1, %g3, %g2		! see if top of stack is in hole
	inc	%g2
	andncc	%g2, 1, %g2
	bz,a	1f			! branch if top of area not in hole
	lda	[%g1]ASI_PM, %g2	! delay - get pme for this address
	b,a	.sr_stack_not_res	! stack page can never be resident
1:
#else
	lda	[%g1]ASI_PM, %g2	! get pme for this address
#endif	/* VA_HOLE */
	srl	%g2, PG_S_BIT, %g2	! get vws bits
	and	%g2, PG_VS_MASK, %g3	! check just the valid and system bits
	cmp	%g3, VALID		! look for valid bit and not system
	be,a	.sr_sp_bot		! top is valid
	lda	[%fp]ASI_PM, %g2	! delay slot, check bottom of save area

	b,a	.sr_stack_not_res	! stack page not resident

.sr_sp_bot:
	srl	%g2, PG_S_BIT, %l4	! get vws bits
	and	%l4, PG_VS_MASK, %g3	! check just the valid and system bits
	cmp	%g3, VALID		! look for valid bit and not system
	be,a	.sr_stack_res		! stack bottom is valid
	restore				! get into window to be restored

	mov	%fp, %g1		! save fault address
.sr_stack_not_res:
	!
	! Restore area on user stack is not resident.
	! We punt and fake a page fault so that trap can bring the page in.
	!
	mov	%l3, %wim		! restore old wim
	or	%l0, PSR_PIL, %o1	! spl8 first to protect CPU base pri
	mov	%o1, %psr		! set priority before enabling (IU bug)
	wr	%o1, PSR_ET, %psr	! turn on traps
					! expect fault address in %g1
 	mov	%g1, %l2		! psr delay, save fault address
					! hat_fault_trap changes %g1
	call	splx			! psr delay splx() (preserves %g1, %g2)
	mov	%l0, %o0		! psr delay - pass old %psr to splx()

	btst	VALID, %l4
	bne	1f			! page was valid
	mov	SE_PROTERR, %o3		! delay - set protection error
	ld	[THREAD_REG + T_PROCP], %o4
	ld	[%o4 + P_AS], %o4
	tst	%o4
	bz,a	1f			! no address space - kernel fault?
	mov	SE_INVALID, %o3		! delay - setup be type for trap
	mov	%l2, %o1
	call	hat_fault_trap		! hat_fault_trap(hat, addr);
	ld	[%o4 + A_HAT], %o0
	tst	%o0
	bz	_sys_rtt		! hat layer resolved the fault
	nop

	btst	VALID, %l4
	bnz,a	1f
	mov	SE_PROTERR, %o3
	mov	SE_INVALID, %o3		! was stack protected or invalid?

1:
	mov	T_DATA_FAULT, %o0
	add	%sp, MINFRAME, %o1
       	mov	%l2, %o2
	call	trap			! trap(T_DATA_FAULT,
	mov	S_READ, %o4		!	rp, addr, be, S_READ)

	b,a	_sys_rtt

.sr_stack_res:
	!
	! Resident user window. Restore window from stack
	!
	RESTORE_WINDOW(%sp)
	save				! get back to original window

.sr_user_regs:
	!
	! Fix saved %psr so the PIL will be CPU->cpu_base_pri
	!
	ld	[THREAD_REG + T_CPU], %o1	! get CPU_BASE_SPL
	ld	[%o1 + CPU_BASE_SPL], %o1
	andn	%l0, PSR_PIL, %l0
	or	%o1, %l0, %l0			! fixed %psr
	!
	! User has at least one window in the register file.
	!
	!
	ld	[%g5 + MPCB_FLAGS], %l3
	ld	[%sp + MINFRAME + nPC*4], %l2	! user npc
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
	mov	%l0, %psr		! delay - install old PSR_CC

	!
	! Maintain clean windows.
	!
	mov	%wim, %g2		! psr delay - put wim in global
	mov	0, %wim			! psr delay - zero wim to allow saving
	mov	%l0, %g3		! psr delay - put original psr in global
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
	mov	%g2, %wim		! psr delay - restore wim
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
	jmp	%o6			! return
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

	RESTORE_GLOBALS(%sp + MINFRAME)	! psr delay - restore user globals
	jmp	%l1			! return
	rett	%l2

	!
	! Return to supervisor
	! %l0 == old psr.
	! %g1 == current psr
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
	xor	%g1, %l0, %g2		! test for CWP change
	wr	%g1, PSR_ET, %psr	! disable traps
	nop; nop; nop			! psr delay
	btst	PSR_CWP, %g2		! test window change
	bz	.sr_samecwp		! branch if same window
	mov	%l0, %g3		! delay, save old psr
	!
	! The CWP will be changed. We must save sp and the ins
	! and recompute WIM. We know we need to restore the next
	! window in this case.
	!
	mov	%sp, %g4		! save sp, ins for new window
	std	%i0, [%sp +(8*4)]	! normal stack save area
	std	%i2, [%sp +(10*4)]
	std	%i4, [%sp +(12*4)]
	std	%i6, [%sp +(14*4)]
	mov	%g3, %psr		! old psr, disable traps, CWP, PSR_CC
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

	RESTORE_GLOBALS(%sp + MINFRAME)	! psr delay - restore system globals
	ld	[%sp + MINFRAME + PC*4], %l1 ! restore sys pc
	ld	[%sp + MINFRAME + nPC*4], %l2 ! sys npc
	jmp	%l1			! return to trapped instruction
	rett	%l2
/* end sys_rtt */


/*
 * Fault handler.
 */
	.type   fault, #function
fault:
/*
 * We support both types of machines; must choose proper one by cpu
 * type (TODO and patch the branch as we do so).
 */
	sethi	%hi(cpu_buserr_type), %g1
	ld	[%g1 + %lo(cpu_buserr_type)], %g1
	! TODO setup patch to .fixfault for campus-1
	tst	%g1
	bz,a	.fault_60
	nop	! TODO store patch into .fixfault
	! TODO setup patch to .fixfault for calvin
	b	.fault_70
	nop	! TODO store patch into .fixfault

/* Campus-1 type support */
.fault_60:
	set	SYNC_VA_REG, %g1
	lda	[%g1]ASI_CTL, %o2	! get error address for later use
	set	SYNC_ERROR_REG, %g1
	lda	[%g1]ASI_CTL, %o3	! get sync error reg (clears reg)
	mov	S_EXEC, %o4		! assume execute fault
	btst	SE_MEMERR, %o3		! test memory error bit
	bz,a	generic_fault		! branch if not a memory error
	wr	%l0, PSR_ET, %psr	! enable traps (no priority change)

	!
	! Synchronous memory error.
	!
	! XXX we must read the ASER and ASEVAR to unlatch them, XXX
	! XXX as they are latched whenever SE_MEMERR is on. XXX
	set	ASYNC_VA_REG, %g1	! XXXX
	lda	[%g1]ASI_CTL, %g2	! XXXX(in g2 for double store)
	set	ASYNC_ERROR_REG, %g1	! XXXX
	lda	[%g1]ASI_CTL, %g3	! XXXX(in g3 for double store)
	!
	! XXX If I have two stores, and the first produces an async
	! XXX error and the second produces a synchronous error, I can
	! XXX take the synchronous trap first but have the async trap
	! XXX immediately pending.  I have to handle this carefully.
	! XXX If this is just synchronous memory error, then the SEVAR
	! XXX and the ASEVAR will contain the same value.
	! XXX If this is a "simultaneous" error (back-to-back stores),
	! XXX then they will have different values and we want to treat
	! XXX this like a memory_err and ignore the sync error.
	! XXX There will also be a level 15 interrupt pending, which we
	! XXX must clear if we haven't already (not a P1.5).
	! XXX (And even if we have, asyncerr expects interrupts off.)
	cmp	%o2, %g2		! XXX does SEVAR == ASEVAR?
	be,a	generic_fault		! XXX yes, a true sync error
	wr	%l0, PSR_ET, %psr	! enable traps (no priority change)
	!
	! XXX We need to process the asynchronous error first.  If we
	! XXX return, we will eventually re-execute the store that
	! XXX causes the synchronous error.
	! XXX First we turn off interrupts.
	! XXX (this will also clear the pending level 15)
	set	INTREG_ADDR, %g4	! interrupt register address
	ldub	[%g4], %g1		! read interrupt register
	bclr	IR_ENA_INT, %g1		! off
	stb	%g1, [%g4]		! turn off all interrupts
	! XXX Now we can enable traps
	wr	%l0, PSR_ET, %psr	! enable traps (no priority change)
	! XXX we want o0=ser, o1=sevar, o2=aser, o3=asevar
	! XXX so we take advantage of the psr delay
	mov	%o3, %o0		! psr delay - SER
	mov	%o2, %o1		! psr delay - SEVAR
	mov	%g3, %o2		! psr delay - ASER
	call	asyncerr		! asyncerr(ser, sevar, aser, asevar)
	mov	%g2, %o3		! ASEVAR

	mov	IR_ENA_INT, %o0		! reenable interrupts
	call	set_intreg
	mov	1, %o1
	b,a	_sys_rtt			! and return from trap

.fault_70:
/* Calvin type support */
	set	SYNC_VA_REG, %g1
	lda	[%g1]ASI_CTL, %o2	! get error address for later use
	set	SYNC_ERROR_REG, %g1
	lda	[%g1]ASI_CTL, %o3	! get sync error reg (clears reg)
	wr	%l0, PSR_ET, %psr	! enable traps (no priority change)
	mov	S_EXEC, %o4		! psr delay - assume execute fault
generic_fault:				! psr written in delay of branch to here
	nop; nop; nop			! psr delay
	cmp	%l4, T_TEXT_FAULT	! text fault?
	be,a	2f
	mov	%l1, %o2		! pc is text fault address

	mov	S_READ, %o4		! assume read fault
	set	SE_RW, %g1		! test r/w bit
	btst	%g1, %o3
	bclr	%g1, %o3		! clear r/w bit (for trap())
	bnz,a	2f
	mov	S_WRITE, %o4		! delay slot, it's a write fault
2:
	cmp	%o3, SE_INVALID
	bne	3f
	mov	%l4, %o0

	mov	%o2, %l2		! save %o2 - %o4 in locals
	mov	%o3, %l3
	mov	%o4, %l5

	ld	[THREAD_REG + T_PROCP], %o3
	ld	[%o3 + P_AS], %o3	! no as yet, must take long path
	tst	%o3
	bz	3f
	mov	%o2, %o1
	call	hat_fault_trap		! hat_fault_trap(hat, addr)
	ld	[%o3 + A_HAT], %o0

	tst	%o0
	be	_sys_rtt		! hat layer resolved the fault

	!
	! hat_fault_trap didn't resolve the fault.
	! Restore saved %o2 - %o4 and call trap.
	!
	mov	%l2, %o2
	mov	%l3, %o3
	mov	%l5, %o4
	mov	%l4, %o0		! trap(t, rp, addr, be, rw)
3:
	!
	! Call C trap handler
	!
	call	trap			! C trap handler
	add	%sp, MINFRAME, %o1
	b,a	_sys_rtt		! return from trap
/* end fault */

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
	! the IE register's soft interrupt levels (level 4 and
	! 6 in this implementation).
	!
	.global _int_vector
_int_vector:
	.word	spurious	! level 0, should not happen
	.word	_level1		! level 1, IE register 1 | sbus level 1
	.word	.level2		! level 2, sbus level 2
	.word	.level3		! level 3, sbus level 3
	.word	.level4		! level 4, IE register 2
	.word	.level5		! level 5, sbus level 4
	.word	.level6		! level 6, IE register 3
	.word	.level7		! level 7, sbus level 5
	.word	.level8		! level 8, sbus level 6
	.word	.level9		! level 9, sbus level 7
	.word	_level10	! level 10, normal clock
	.word	.level11	! level 11, floppy disk
#if defined(SAS) || defined(MPSAS)
	.word	simcintr	! sas console interrupt
#else
	.word	.level12	! level 12, scc - serial i/o
#endif	/* SAS */
	.word	.level13	! level 13, audio
	.word	.level14	! level 14, kprof / monitor clock
fixmemory_err:
	.word	_memory_err	! level 15, memory error
	SET_SIZE(_int_vector)

#endif	/* lint */

/*
 * Generic interrupt handler.
 *	Entry:  traps disabled.
 *		%l4 = T_INTERRUPT ORed with level (1-15)
 *		%l0, %l1, %l2 = saved %psr, %pc, %npc.
 *		%l7 = saved %sp
 *	Uses:
 *		%l3 = scratch register
 *		%l4 = interrupt level (1-15).
 *		%l5 = old PSR with PIL cleared (for vme_interrupt)
 *		%l6 = new thread pointer
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
	or	%l5, %g1, %l0		! new PSR with proper PIL

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
	tst	%l6			! test if panicking
	bnz,a	1f			! if NULL, test for lock level
	or	%l0, PSR_PIL, %l0	! delay - mask everything if panicing
1:
	cmp	%l4, LOCK_LEVEL		! compare to highest thread level
	bl	intr_thread		! process as a separate thread
	ld	[THREAD_REG + T_CPU], %l3	! delay - get CPU pointer

#if CLOCK_LEVEL != LOCK_LEVEL		/* If compare not already done */
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
	tst	%sp			! non-zero %sp?
	bz,a	1f			! no- reload origianl stack pointer
	mov	%l7, %sp		! ...in this delay slot...
1:
	!
	! If we just took a memory error we don't want to turn interrupts
	! on just yet in case there is another memory error waiting in the
	! wings. So disable interrupts if the PIL is 15.
	!
	cmp	%g1, PSR_PIL
	bne	2f
	sll	%l4, 2, %l3		! convert level to word offset
	mov	IR_ENA_INT, %o0
	call	set_intreg
	mov	0, %o1
2:
	!
	! Get handler address for level.
	!
	set	_int_vector, %g1
	ld	[%g1 + %l3], %l3	! grab vector
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
	nop
	!
	! these handlers cannot jump to int_rtt (as they used to), they
	! must not return as normal functions or jump to .intr_ret.
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
	nop
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
 *		%l6 = new thread pointer
 */
intr_thread:
	! Get set to run interrupt thread.
	! There should always be an interrupt thread since we allocate one
	! for each level on the CPU, and if we release an interrupt, a new
	! thread gets created.
	!
	ld	[%l3 + CPU_INTR_THREAD], %l6	! interrupt thread pool
#ifdef	DEBUG
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
#endif	/* DEBUG */

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
	! resume() may use that stack between threads.
	!
	st	%l7, [THREAD_REG + T_SP]	! mark stack for resume
	st	THREAD_REG, [%l6 + T_INTR]	! push old thread
	st	%l6, [%l3 + CPU_THREAD]		! set new thread
	mov	%l6, THREAD_REG			! set global curthread register
	ld	[%l6 + T_STACK], %sp		! interrupt stack pointer
	!
	! Initialize thread priority level from intr_pri
	!
	set	intr_pri, %g1
	ldsh	[%g1], %l3		! grab base interrupt priority
	add	%l4, %l3, %l3		! convert level to dispatch priority
	sth	%l3, [THREAD_REG + T_PRI]
	!
	! Get handler address for level.
	!
	sll	%l4, 2, %l3		! convert level to word offset
	set	_int_vector, %g1
	ld	[%g1 + %l3], %l3	! grab vector
#ifdef	TRAPTRACE
	!
	! make trace entry - helps in debugging watchdogs
	!
	TRACE_PTR(%g1, %g2)		! get trace pointer
	set	_trap_last_intr, %g2
	st	%g1, [%g2]		! save last interrupt trace record
	mov	-1, %g2
	st	%g2, [%g1 + TRAP_ENT_TBR]
	st	%l0, [%g1 + TRAP_ENT_PSR]
	st	%l1, [%g1 + TRAP_ENT_PC]
	st	%sp, [%g1 + TRAP_ENT_SP]
	st	%g7, [%g1 + TRAP_ENT_G7]
	ld	[%l7 + MINFRAME + PSR*4], %o1 ! get saved psr
	st	%o1, [%g1 + 0x14]	! put saved psr in trace
	clr	[%g1 + 0x18]
	st	%l6, [%g1 + 0x1c]	! interrupt thread
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
	nop				! psr delay
	TRACE_ASM_1 (%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);
	call	%l3			! psr delay - call interrupt handler
	nop				! psr delay
	!
	! return from interrupt - this is return from call above or
	! just jumpped to.
	! %l0-%l4 and %l7 should be the same as they were before the call.
	! Note that intr_passivate(), below, relies on this.
	! XXX: The above statement is a lie. %l4 and %l7 are the only ones
	! XXX: needed at present.
	!
.int_rtt:
#ifdef	TRAPTRACE
	!
	! make trace entry - helps in debugging watchdogs
	!
	mov	%psr, %g1
	wr	%g1, PSR_ET, %psr	! disable traps
	TRACE_PTR(%l5, %g2)		! get trace pointer
	set	_trap_last_intr2, %g2
	st	%l5, [%g2]		! save last interrupt trace record
	mov	-2, %g2			! interrupt return
	st	%g2, [%l5 + TRAP_ENT_TBR]
	st	%g1, [%l5 + TRAP_ENT_PSR]
	clr	[%l5 + TRAP_ENT_PC]
	st	%fp, [%l5 + TRAP_ENT_SP]
	st	%g7, [%l5 + TRAP_ENT_G7]
	ld	[THREAD_REG + T_CPU], %g2
	ld	[%g2 + CPU_INTR_ACTV], %g2
	st	%g2, [%l5 + 0x14]	! interrupts active
	clr	[%l5 + 0x18]
	ld	[THREAD_REG + T_INTR], %g2
	st	%g2, [%l5 + 0x1c]	! thread underneath
	TRACE_NEXT(%l5, %o1, %g2)	! set new trace pointer
	wr	%g1, %psr		! enable traps
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
	mov	%l5, %psr		! mask all interrupts
	nop; nop			! psr delay
	!
	! If there is still an interrupted thread underneath this one,
	! then the interrupt was never blocked or released and the
	! return is fairly simple.  Otherwise jump to intr_thread_exit.
	!
	ld	[THREAD_REG + T_INTR], %g2	! psr delay
	tst	%g2
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

int
intr_passivate(void)
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

/*
 * Spurious trap... 'should not happen'
 * %l4 - processor interrupt level
 * %l3 - interrupt handler address
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
	b,a	.int_rtt
	.empty
	.seg	".data"
1:	.asciz	"spurious interrupt at processor level %d\n"
	.seg	".text"
	SET_SIZE(spurious)

/*
 * Macro for autovectored interrupts.
 * %l4 or %l7 must be preserved in it.
 */
#define	IOINTR(LEVEL) \
	set	level/**/LEVEL,%l5 /* get vector ptr */;\
1:									;\
	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */	;\
	tst	%l3							;\
	bz	4f			/* end of list */		;\
	ld	[%l5 + AV_MUTEX], %l2	/* mutex guard (if any) */;\
	tst	%l2		;\
	bz	2f		/* no guard needed */;\
	nop								;\
	call	mutex_enter	/* get driver mutex */	;\
	mov	%l2, %o0	;\
	call	%l3		/* go there */;\
	ld	[%l5 + AV_INTARG], %o0	/* delay - pass the argument */;\
	mov	%o0, %l3	/* save return value */;\
	call	mutex_exit	/* release driver mutex */ ;\
	mov	%l2, %o0	;\
	b	3f		;\
	nop			;\
2:\
	call	%l3		/* go there */;\
	ld	[%l5 + AV_INTARG], %o0	/* delay - pass the argument */;\
	mov	%o0, %l3	/* save return value */;\
3:\
	tst	%l3		/* success? */;\
	bz,a	1b		/* no, try next one */;\
	add	%l5, AUTOVECSIZE, %l5	/* delay slot, next one to try */;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %g1			;\
	b	.int_rtt	/* done */				;\
	/* delay slot, non-spurious interrupt, clear count */		;\
	clr	[%g1 + %lo(level/**/LEVEL/**/_spurious)]		;\
4:									;\
	set	level/**/LEVEL/**/_spurious, %o0			;\
	set	busname_vec, %o2					;\
	call	not_serviced						;\
	mov	LEVEL, %o1	/* delay */				;\
	b	.int_rtt	/* done */				;\
	nop

#define	IOINTR_ABOVE_LOCK(LEVEL) \
	set	level/**/LEVEL, %l5 /* get vector ptr */;\
1:	ld	[%l5 + AV_VECTOR], %l3 /* get routine address */;\
	tst	%l3							;\
	bz,a	2f		/* end of list */			;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %o0	/* delay */	;\
	call	%l3		/* go there */;\
	ld	[%l5 + AV_INTARG], %o0	/* pass the argument */;\
	tst	%o0		/* success? */;\
	bz,a	1b		/* no, try next one */;\
	add	%l5, AUTOVECSIZE, %l5	/* delay slot, next one to try */;\
	/* delay slot, non-spurious interrupt, clear count */;\
	sethi	%hi(level/**/LEVEL/**/_spurious), %g1;\
	b	.intr_ret	/* done */;\
	clr	[%g1 + %lo(level/**/LEVEL/**/_spurious)]		;\
2:									;\
	or	%o0, %lo(level/**/LEVEL/**/_spurious), %o0		;\
	set	busname_vec, %o2					;\
	call	not_serviced						;\
	mov	LEVEL, %o1						;\
	b	.intr_ret	/* done */				;\
	nop

#endif	/* lint */

/*
 * Level 1 interrupts, from Sbus level 1, or from software interrupt
 */

#if defined(lint)

void
_level1(void)
{}

#else	/* lint */

	ENTRY_NP(_level1)
	mov	IR_SOFT_INT1, %l6
	set	level1_spurious, %l3
	set	xlvl1, %l1
	sethi	%hi(level1), %l5	! level one autovec pointer
	b	.soft_and_io_intr
	or	%l5, %lo(level1), %l5
	SET_SIZE(_level1)

#endif	/* lint */


#if !defined(lint)

/*
 * Level 2 interrupts, from Sbus level 2.
 */
	.type	.level2,#function
.level2:
	IOINTR(2)
	SET_SIZE(.level2)

/*
 * Level 3 interrupts, from Sbus level 3 (SCSI/DMA).
 */
	.type	.level3,#function
.level3:
	IOINTR(3)
	SET_SIZE(.level3)

/*
 * Level 4 interrupts, IE register 2
 */
	.type	.level4,#function
.level4:
	mov	IR_SOFT_INT4, %l6
	set	level4_spurious, %l3
	set	xlvl4, %l1
	sethi	%hi(level4), %l5	! level four autovec pointer
	b	.soft_and_io_intr
	or	%l5, %lo(level4), %l5
	SET_SIZE(.level4)

/*
 * Level 5 interrupts, from Sbus level 4 (ethernet).
 */
	.type	.level5,#function
.level5:
	IOINTR(5)
	SET_SIZE(.level5)

/*
 * Level 6 interrupts, IE register 3
 * Used by drivers for second level interrupts
 */
	.type	.level6,#function
.level6:
	mov	IR_SOFT_INT6, %l6
	set	level6_spurious, %l3
	set	xlvl6, %l1
	sethi	%hi(level6), %l5	! level 6 autovec pointer
	b	.soft_and_io_intr
	or	%l5, %lo(level6), %l5
	SET_SIZE(.level6)

/*
 * Level 7 interrupts, from Sbus level 5 (video).
 */
	.type	.level7,#function
.level7:
	IOINTR(7)
	SET_SIZE(.level7)

/*
 * Level 8 interrupts, from Sbus level 6.
 */
	.type	.level8,#function
.level8:
	IOINTR(8)
	SET_SIZE(.level8)

/*
 * Level 9 interrupts, from Sbus level 7.
 */
	.type	.level9,#function
.level9:
	IOINTR(9)
	SET_SIZE(.level9)

#ifdef	sun4
	!
	! The following 3 autovectors are not used on sun4
	!
#endif
/*
 * Level 11 interrupts, from floppy disk.
 */
	.type	.level11,#function
.level11:
	IOINTR_ABOVE_LOCK(11)
	SET_SIZE(.level11)

/*
 * Level 12 interrupts, from zs.
 */
	.type	.level12,#function
.level12:
	IOINTR_ABOVE_LOCK(12)
	SET_SIZE(.level12)

/*
 * Level 13 interrupts, from audio chip.
 */
	.type	.level13,#function
.level13:
	IOINTR_ABOVE_LOCK(13)
	SET_SIZE(.level13)

/*
 * Level 14 interrupts, from profiling counter/timer.  We must first
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
	IOINTR_ABOVE_LOCK(14)
	SET_SIZE(.level14)

/*
 * In this implementation we have both soft and regular autovectored
 * interrupts on the same level. We have to check to make sure that
 * the system interrupt register is actually asserting an interrupt
 * before waltzing off to service 'soft' interrupts.
 *
 * We enter with %l5 pointing to the hard int list, %l1 pointing to the
 * soft int list, %l3 pointing to the levelN_spurious counter, and %l6 holding
 * our favorite interrupt register bit, and %l4 holding our interrupt level
 * (for printing a message).
 *
 * We use %l2 to hold a pointer to a possible mutex
 * we might need to grab.
 *
 * We use %l0, %l1, %l5, %g1 and %g2.
 * We must preserve %l4 and %l7.
 *
 * We assume that there are no 'fast' interrupts at soft interrupt levels.
 *
 */

.soft_and_io_intr:
1:
	ld	[%l5 + AV_VECTOR], %g1		! get entry point
	tst	%g1				! end of list?
	bz	3f				! yes
	nop
	ld	[%l5 + AV_MUTEX], %l2		! get a mutex (if any)
	tst	%l2				! do we have one?
	be,a	2f				! nope
	nop					! ...
	call	mutex_enter			! get the mutex
	mov	%l2, %o0			! fix up arg
2:	ld	[%l5 + AV_VECTOR], %g1 		! get routine address
	call	%g1				! go there
	ld	[%l5 + AV_INTARG], %o0		! pass the argument
	mov	%o0, %l0			! save result
	tst	%l2				! need to release a mutex?
	be,a	2f				! nope...
	nop
	call	mutex_exit			! release the mutex
	mov	%l2, %o0			! fix up arg
2:	tst	%l0				! success?
	bz,a	1b				! no, try next one
	add	%l5, AUTOVECSIZE, %l5		! delay slot, next one to try

	b	.int_rtt			! done
	/*
	 * delay slot, non-spurious interrupt; and clear spurious counter
	 */
	clr	[%l3]				! clr spurious cnt

3:	set	INTREG_ADDR, %o0		! was our favorite bit set?
	ldub	[%o0], %o0
	btst	%l6, %o0			! Is our bit set?
	bz,a	6f				! No. Spurious hard interrupt.
	mov	%g0, %g2

	mov	%l6, %o0			! tell set_intreg what to clear
	call	set_intreg			! go clear int reg
	mov	%g0, %o1			! 'yup' in delay slot
	mov	%l1, %l5 			! load software vector ptr


	!
	! Okay- we know that we had a soft int set for our level.
	! Traverse the av softint list, trying all soft handlers
	!
	! At this point, registers are:
	!
	!	%l7 - (no touchie)
	!	%l6 - our intreg bit
	!	%l5 - softint av list pointer
	!	%l4 - (no touchie - interrupt level)
	!	%l3 - spurious interrupt counter address
	!	%l2 - possible driver mutex
	!	%l1 - av list pointer (no longer used)
	!

4:	ld	[%l5 + AV_VECTOR], %g1		! fetch possible entry point
	tst	%g1				! end of list?
	bz	.int_rtt			! yes
	ld	[%l5 + AV_MUTEX], %l2		! load possible mutex (delay)
	tst	%l2				! do we have one?
	be,a	3f				! nope..
	nop
	call	mutex_enter			! get the driver mutex
	mov	%l2, %o0			! fix arg
3:	call	%g1				! go there...
	ld	[%l5 + AV_INTARG], %o0		! pass the argument
	tst	%l2				! mutex to release?
	be,a	2f				! nope..
	nop					!
	call	mutex_exit			! release mutex
	mov	%l2, %o0			! fix arg
2:	add	%l5, AUTOVECSIZE, %l5		! increment
	b	4b
	nop					! (delay)
6:	set	busname_vec, %o2		! name of our bus
	mov	%l3, %o0			! ptr to counter
	call	not_serviced			! call somebody to complain
	mov	%l4, %o1			! our level
	b,a	.int_rtt			! done
	nop

#ifdef	FANCYLED
/*
 * LED data for front-panel light.
 * NOTE: pattern is sampled at 100hz.
 *
 * I know, you can't figure out the LED pattern so you are
 * looking at the source to find out what it is.
 * This is cheating.  But if you must cheat, please don't tell your
 * friends what the pattern is; that spoils the fun.
 * I'm waiting for the Sun-Spots messages: "Has anyone figured
 * out the flashing LED on the 4/60?  It looks random to me."
 * You can say "Well, I looked at the source, and there is a pattern,
 * but the comments ask us not to tell you what the pattern is because
 * the developer wants to see if you can figure it out for yourself."
 * (Besides, divulging the algorithm may be a violation of your source
 * license agreement!)
 *
 * Thanks.
 * --The nameless developer
 */
	.seg	".data"
	.type	.led, #object
led:
	! LEDDUTY
	.byte	10, 90
	.byte	20, 80
	.byte	30, 70
	.byte	40, 60
	.byte	50, 50
	.byte	60, 40
	.byte	70, 30
	.byte	80, 20
	.byte	90, 10
	.byte	80, 20
	.byte	70, 30
	.byte	60, 40
	.byte	50, 50
	.byte	40, 60
	.byte	30, 70
	.byte	20, 80
led_end:
	! end of LEDDUTY
	.byte	0		! LEDCNT current count of ticks
	.byte	0		! LEDPTR offset in pattern

LEDDUTYCNT =	(led_end - led)
LEDCNT =	LEDDUTYCNT + 1
LEDPTR =	LEDCNT + 1
#else	/* FANCYLED */
	.align	4
	.global _led_on
_led_on:
	.long	1
#endif	/* FANCYLED */

	.seg	".text"
/*
 * This code assumes that the real time clock interrupts 100 times
 * per second, for SUN4C we call clock at that rate.
 *
 * Clock is called in a special thread called the clock_thread.
 * It sort-of looks like and acts like an interrupt thread, but doesn't
 * hold the spl while it's active.  If a clock interrupt occurs and
 * the clock_thread is already active, the clock_pend flag is set to
 * cause clock_thread to repeat for another tick.
 *
#ifdef	FANCYLED
 * Update the LEDs with new values before calling hardclock so
 * at least the user can tell that something is still running.
#else	FANCYLED
 * Since we may have started the monitor's clock at any time,
 * which may have turned off the led, turn it back on every clock tick
 * unless led_on is 0 (used to communicate with the future /dev/led)
#endif	FANCYLED
 */

	.common clk_intr, 4, 4
!
        .seg    ".data"
	.global hrtime_base, vtrace_time_base
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

!	Level 10 clock interrupt - entered with traps disabled
!	May switch to new stack.
!
!	Entry:
!	%l3 = CPU pointer
!	%l4 = interrupt level throughout.  intr_passivate() relies on this.
!	%l7 = saved stack pointer throughout.
!
!	Register usage
!	%g1 - %g3 = scratch
!	%l0 = new %psr
!	%l1, %l2 = saved %pc, %npc
!	%l5 = scratch
!	%l6 = clock thread pointer

	ALTENTRY(_level10)

	mov	%l4, %l6			! we need another register pair

	sethi	%hi(hrtime_base), %l4
	ldd	[%l4 + %lo(hrtime_base)], %g2
	sethi   %hi(nsec_per_tick), %g1
	ld      [%g1 + %lo(nsec_per_tick)], %g1	! %g1 = nsec_per_tick (npt)
	addcc   %g3, %g1, %g3			! add 1 tick to hrtime_base
	addx    %g2, %g0, %g2			! if any carry, add it in
	std	%g2, [%l4 + %lo(hrtime_base)]	! store new hrtime_base

#ifdef TRACE
	ld      [%l4 + %lo(vtrace_time_base)], %g2
	sethi   %hi(usec_per_tick), %l5;
	ld      [%l5 + %lo(usec_per_tick)], %l5;
	add     %g2, %l5, %g2                   ! add tick to vtrace_time_base
	st	%g2, [%l4 + %lo(vtrace_time_base)]
#endif	/* TRACE */

	!
	! load current timer value
	!
	sethi	%hi(COUNTER_ADDR + CTR_COUNT10), %g2	! timer address
	ld	[%g2 + %lo(COUNTER_ADDR + CTR_COUNT10)], %g2	! timer value
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

	! Load the limit 10 register to clear the clock interrupt.
	! Note: SAS now simulates the counter; we take clock ticks!
	!
	sethi	%hi(COUNTER_ADDR + CTR_LIMIT10), %l5
	ld	[%l5 + %lo(COUNTER_ADDR + CTR_LIMIT10)], %g0
#ifdef	FANCYLED
	!
	! Cycle the LED, whether or not we are doing anything
	! We need to keep interrupts disabled as we do this
	!
#if !defined(SAS) || !defined(MPSAS)
	set	led, %l5		! countdown to next update of LEDs
	ldub	[%l5 + LEDCNT], %g1
	subcc	%g1, 1, %g1
	bge,a	3f			! not zero, just call clock
	stb	%g1, [%l5 + LEDCNT]	! delay, write out new ledcnt

	ldub	[%l5 + LEDPTR], %g1
	sethi	%hi(AUXIO_REG), %o3
	ldub	[%o3 + %lo(AUXIO_REG)], %g4
	ldub	[%l5 + %g1], %g2	! get next LED count
	subcc	%g1, 1, %g1		! point to next one
	stb	%g2, [%l5 + LEDCNT]	! store the new count
	bneg,a	2f
	mov	LEDDUTYCNT-1, %g1
2:
	stb	%g1, [%l5 + LEDPTR]	! update duty count pointer
	xor	%g4, AUX_LED, %g4	! toggle the LED
	or	%g4, AUX_MBO, %g4	! Must Be Ones!
	stb	%g4, [%o3 + %lo(AUXIO_REG)]	! store it back
3:
#endif	/* SAS */
#else	/* FANCYLED */
	sethi	%hi(_led_on), %l5
	ld	[%lo(_led_on) + %l5], %g1
	sethi	%hi(AUXIO_REG), %o3
	tst	%g1
	bz	1f
	ldub	[%o3 + %lo(AUXIO_REG)], %g4
	or	%g4, AUX_LED | AUX_MBO, %g4
	stb	%g4, [%o3 + %lo(AUXIO_REG)]
1:
#endif	/* FANCYLED */

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
	mov	%l0, %psr		! set level (IU bug)
	wr	%l0, PSR_ET, %psr	! enable traps

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
#endif	/* FREE_THREAD */

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

	TRACE_ASM_1 (%o2, TR_FAC_INTR, TR_INTR_START, TR_intr_start, %l4);

	!
	! On a Uniprocessor like the sun4c, we're protected by SPL level
	! and don't need clock_lock.
	!
#ifdef	MP
	set	clock_lock, %l6		! psr delay
	call	lock_set
	mov	%l6, %o0
#endif	/* MP */
	sethi	%hi(clock_pend), %g1	! psr delay (depending on ifdefs)
	ld	[%g1 + %lo(clock_pend)], %o0
	inc	%o0			!  increment clock_pend
	st	%o0, [%g1 + %lo(clock_pend)]
#ifdef	MP
	clrb	[%l6]			! clear the clock_lock
#endif	/* MP */
	TRACE_ASM_0 (%o1, TR_FAC_INTR, TR_INTR_END, TR_intr_end);
	b	_sys_rtt
	nop				! psr delay
	SET_SIZE(_level10)

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
 */

#if defined(FAST_KPROF)

/*
 * Level 14 hardware interrupts can be caused by the clock when
 * kernel profiling is enabled.  They are handled immediately
 * in the trap window.
 */

#if defined(lint)

void
fast_profile_intr(void)
{}

#else	/* lint */

	ENTRY_NP(fast_profile_intr)

	!
	! Read the limit 14 register to clear the clock interrupt.
	!
	sethi	%hi(COUNTER_ADDR + CTR_LIMIT14), %l3
	ld	[%l3 + %lo(COUNTER_ADDR + CTR_LIMIT14)], %g0

	CPU_ADDR(%l4, %l3)
	ld	[%l4 + CPU_PROFILING], %l3
	tst	%l3			! no profile struct, spurious?
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
 * Level 15 interrupts can only be caused by asynchronous errors.
 * On a parity machine, these errors are always fatal. However, on an ECC
 * machine, the error may have been corrected, and memerr may return after
 * logging the error.
 * XXX On Async errors, the 4/60 cache chip sometimes doesn't set the
 * XXX right bits on in the ASER, so give all the registers to
 * XXX asyncerr() to figure out what to do; it will call memerr().
 */

	ENTRY_NP(_memory_err)
/*
 * We support both types of machines; must choose proper one by cpu
 * type (TODO and patch the vector as we do so).
 * (or use an indirect branch).
 */
	sethi	%hi(cpu_buserr_type), %g1
	ld	[%g1 + %lo(cpu_buserr_type)], %g1
	! TODO setup patch to fixmemory_err for campus-1
	tst	%g1
	bz,a	.memory_err_60
	nop	! TODO store patch into .fixfault
	! TODO setup patch to .fixfault for calvin
	b	.memory_err_70
	nop	! TODO store patch into .fixfault

/* Campus-1 type support */
.memory_err_60:
	set	ASYNC_VA_REG, %g1	! get error address for later use
	lda	[%g1]ASI_CTL, %o3
	set	ASYNC_ERROR_REG, %g1	! get async error reg (clears reg)
	lda	[%g1]ASI_CTL, %o2

	! we want o0=ser, o1=sevar, o2=aser, o3=asevar
	! So far, we have
	! %o2 = aser
	! %o3 = asevar
	set	SYNC_VA_REG, %g1
	lda	[%g1]ASI_CTL, %o1	! sevar
	! The sync error register seems to be set on async errors; clear
	! it.
	set	SYNC_ERROR_REG, %g1	! XXX get sync error reg (clears reg)
	call	asyncerr		! asyncerr(ser, sevar, aser, asevar)
	lda	[%g1]ASI_CTL, %o0	! XXX
3:
	mov	IR_ENA_INT, %o0		! reenable interrupts
	call	set_intreg
	mov	1, %o1
	b,a	.intr_ret

/* Calvin type support */
.memory_err_70:
	set	ASYNC_VA_REG, %g1	! get error address for later use
	lda	[%g1]ASI_CTL, %o2
	set	ASYNC_ERROR_REG, %g1	! get async error reg (clears reg)
	lda	[%g1]ASI_CTL, %o1
	set	ASYNC_DATA1_REG, %g1	! get async data1 reg (clears reg)
	lda	[%g1]ASI_CTL, %o3
	set	ASYNC_DATA2_REG, %g1	! get async data2 reg (clears reg)
	lda	[%g1]ASI_CTL, %o4
	btst	ASE_ERRMASK_70, %o1	! valid memory error?

	bz	1f
	nop
	call	memerr_70		! memerr_70(type, reg, addr, d1, d2)
					!     sometimes returns
	mov	MERR_ASYNC, %o0		! delay slot, async flag to memerr
	b,a	3f
1:
/* XXX - this is possible due to hardware bug, should we not print msg?? */
	sethi	%hi(2f), %o0		! print stray interrupt message
	call	printf			! print a message to the console
	or	%o0, %lo(2f), %o0
3:
	mov	IR_ENA_INT, %o0		! reenable interrupts
	call	set_intreg
	mov	1, %o1
	b,a	.intr_ret

	.seg	".data"
2:	.asciz	"stray level 15 interrupt\n"
	.seg	".text"

	SET_SIZE(_memory_err)

#endif	/* lint */

/*
 * Turn on or off bits in the auxiliary i/o register.
 * We must lock out interrupts, since we don't have an atomic or/and to mem.
 * set_auxioreg(bit, flag)
 *	int bit;		bit mask in aux i/o reg
 *	int flag;		0 = off, otherwise on
 */

#if defined(lint)

/* ARGSUSED */
int
set_auxioreg(int bit, int flag)
{ return(0); }

#else	/* lint */

	ENTRY_NP(set_auxioreg)
	mov	%psr, %g2
	or	%g2, PSR_PIL, %g1	! spl hi to protect aux i/o reg update
	mov	%g1, %psr
	nop				! psr delay
	set	AUXIO_REG, %o2		! psr delay; aux i/o register address
	ldub	[%o2], %g1		! psr delay; read aux i/o register
	tst	%o1
	bnz,a	1f
	bset	%o0, %g1		! on
	bclr	%o0, %g1		! off
1:
	or	%g1, AUX_MBO, %g1	! Must Be Ones
	stb	%g1, [%o2]		! write aux i/o register
	b	splx			! splx and return
	mov	%g2, %o0
	SET_SIZE(set_auxioreg)
/* end set_auxioreg */

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

	.global	fixnwindows
fixnwindows:
	save	%sp, -WINDOWSIZE, %sp	! could be no-ops if machine
	restore				! has only 7 register windows

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
 */

#if defined(lint)

void
flush_user_windows(void)
{}

#else	/* lint */

	ENTRY_NP(flush_user_windows)
	ld	[THREAD_REG + T_CPU], %g5
	ld	[%g5 + CPU_MPCB], %g5
	tst	%g5			! lwp == 0 for kernel threads
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
	clr	[%g5 + MPCB_UWM]		! throw user windows away
	set	scb, %g5
	b	2f
	ldub	[%g5 + 31], %g5		! %g5 == NW-1

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
	clr	[%g5 + MPCB_SWM]		! clear shared window mask
	b	splx			! splx and return
	mov	%g4, %o0
3:
	retl
	clr     [%g5 + MPCB_WBCNT]       ! zero window buffer cnt
	SET_SIZE(trash_user_windows)

#endif	/* lint */

/*
 * A faster version of flush_user_windows() that will inline saving
 * register windows to memory when stack pointer is resident. Otherwise
 * take the slow path and call flush_user_windows().
 *
 * On entry:
 *	%l0, %l1, %l2 = %psr, %pc, %npc
 *	%l3 = %wim
 *	%l6 = nwindows - 1
 *	%g7 = user level TLS
 *	%g1-%g6 = scratch
 * Register usage:
 *	%g2 = CWM
 *	%g4 = window bit mask
 *	%g7 = NW -1
 *	%g6 = page address of stack page
 *	%g5 = PSR
 *
 */

#if !defined(lint)

	.type fast_window_flush, #function
	.globl fast_window_flush

fast_window_flush:
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
	! get ready to save user windows by first saving
	! globals to the current window's locals (the trap
	! window).
	!
	mov	%g6, %l6		! save %g6 in %l6
	mov	%g0, %g6		! clear %g6
	mov	%g5, %l5		! save %g5 in %l5
	mov	%l0, %g5		! save PSR in %g5
	mov	%g2, %l0		! save %g2 in %l0
	mov	%l4, %g2		! save CWM in %g2
	mov	%g3, %l3		! save %g3 in %l3
	mov	%g1, %l4		! save %g1 in %l4

	restore				! skip trap window, advance to
					! calling window
2:	
	!
	! flush user windows to the stack. this is an inlined version
	! of the window overflow code.
	!
	!
	! The window to be saved is a user window.
	! We must check whether the user stack is resident where the window
	! will be saved, which is pointed to by the window's sp.
	! We must also check that the sp is aligned to a word boundary.
	!
	! Normally, we would check the alignment, and then probe the top
	! and bottom of the save area on the stack. However we optimize
	! this by checking that both ends of the save area are within a
	! 4k unit (the biggest mask we can generate in one cycle), and
	! the alignment in one shot. This allows us to do one probe to
	! the page map. NOTE: this assumes a page size of at least 4k.
	!

	!
	! first check if stack frame is within the same page.
	! %g6 contains the previous page address. increment
	! frame by MINFRAME. mask out the middle bits. if this
	! mask equals the previous page address then the new frame
	! is mapped and is still within the same page.
	!
	tst	%g6			! quick path, check for same stack page
	bz,a	4f			! skip if zero
	and	%sp, 0xfff, %g1		! delay, get bytes within page
	add	%sp, WINDOWSIZE, %g3	! verify that save area is in same page
	andn	%g3, 0xff8, %g1		! end page
	andn	%sp, 0xfff, %g3		! start page
	xor	%g6, %g1, %g1		! compare end page with previous page
	xor	%g6, %g3, %g3		! compare first page with previous
	orcc	%g1, %g3, %g0		! are end and first the same page?
	bz	.ff_ustack_res		! branch when zero
	andcc	%g1, 7, %g0		! check if save area is aligned
	bnz	.ff_failed
	and	%sp, 0xfff, %g1

4:
	andn	%sp, 0xfff, %g6		! save page bits in %g6
#ifdef VA_HOLE
        ! check if the sp points into the hole in the address space
        sethi   %hi(hole_shift), %g3    ! hole shift address
        ld      [%g3 + %lo(hole_shift)], %g3
        add     %g1, (14*4), %g1        ! interlock, bottom of save area
        sra     %sp, %g3, %g3
        inc     %g3
        andncc  %g3, 1, %g3
        bz      1f
        andncc  %g1, 0xff8, %g0
        b,a     .ff_failed       	! stack page is in the hole
1:
#else
	add	%g1, (14*4), %g1
	andncc	%g1, 0xff8, %g0
#endif VA_HOLE
	bz,a	.ff_sp_bot
	lda	[%sp]ASI_PM, %g1	! check for stack page resident
	!
	! Stack is either misaligned or crosses a 4k boundary.
	!
	btst	0x7, %sp		! test sp alignment
	bz	.ff_sp_top
	add	%sp, (14*4), %g1	! delay slot, check top of save area

	!
	! Misaligned sp. If this is a userland trap fake a memory alignment
	! trap. Otherwise, put the window in the window save buffer so that
	! we can catch it again later.
	!
	b	.ff_failed		! sup trap, save window in uarea buf
	.empty

.ff_sp_top:
#ifdef VA_HOLE
        sethi   %hi(hole_shift), %g3    ! hole shift address
        ld      [%g3 + %lo(hole_shift)], %g3
        sra     %g1, %g3, %g3
        inc     %g3
        andncc  %g3, 1, %g3
        bz,a    1f
        lda     [%g1]ASI_PM, %g1        ! get pme for this address
        ba      .ff_failed       	! stack page can never be resident
	.empty
1:
        sethi   %hi(hole_shift), %g3    ! hole shift address
        ld      [%g3 + %lo(hole_shift)], %g3
        srl     %g1, PG_S_BIT, %g1      ! get vws bits
        sra     %sp, %g3, %g3
        inc     %g3
        andncc  %g3, 1, %g3
        bz,a    1f
        cmp     %g1, PROT               ! look for valid, writeable, user
        b,a    	.ff_failed 	      	! stack page can never be resident
	.empty
1:
#else
	lda	[%g1]ASI_PM, %g1	! get pme for this address
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	cmp	%g1, PROT		! look for valid, writeable, user
#endif VA_HOLE
	be,a	.ff_sp_bot
	lda	[%sp]ASI_PM, %g1	! delay slot, check bottom of save area

	b,a	.ff_failed		! stack page not resident
	.empty

.ff_sp_bot:
	srl	%g1, PG_S_BIT, %g1	! get vws bits
	cmp	%g1, PROT		! look for valid, writeable, user
	be	.ff_ustack_res
	nop				! extra nop

.ff_failed:
	!
	! The user's stack is not accessable. reset the WIM, and PSR to
	! the trap window. call sys_trap(T_FLUSH_WINDOWS) to do the dirty
	! work.
	!

	mov	%g5, %psr		! restore PSR back to trap window
	nop;nop;nop
	mov	%l7, %g7		! restore %g7
	mov	%l6, %g6		! restore %g6
	mov	%l5, %g5		! restore %g5
	mov	%l3, %g3		! restore %g3
	mov	%l0, %g2		! restore %g2
	mov	%l4, %g1		! restore %g1
	SYS_TRAP(T_FLUSH_WINDOWS);
	!
	! The user's save area is resident. Save the window.
	!
.ff_ustack_res:
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

	mov	%wim, %g1		! %g1 now has WIM
	srl     %g2, %g7, %g3           ! shift CWM right by NW-1
	tst	%g3
	bz,a	.+8
	sll	%g2, 1, %g3		! shift CWM left by 1
	mov	%g3, %g2
	andcc	%g2, %g1, %g0

	bz,a	2b			! continue loop as long as CWM != WIM
	restore				! delay, advance window
.ff_out:
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
	mov	%l6, %g6		! restore %g6
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
	SET_SIZE(fast_window_flush)

#endif /* lint */

/*
 * Clean out register file.
 * Note: this routine is using the trap window.
 * [can't use globals unless they are preserved for the user]
 */

#if !defined(lint)

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
	ta	255			! trap to siumlator
	nop
#else
	call	%i0			! go to monitor
	nop
#endif	/* SAS */
	ret
	restore
	SET_SIZE(montrap)

#endif	/* lint */

#if !defined(lint)
/*
 * return the condition codes in %g1
 * Note: this routine is using the trap window
 */
	.type	.getcc, #function
.getcc:
	sll	%l0, 8, %g1		! right justify condition code
	srl	%g1, 28, %g1
	jmp	%l2			! return, skip trap instruction
	rett	%l2+4
	SET_SIZE(.getcc)

/*
 * set the condtion codes from the value in %g1
 * Note: this routine is using the trap window
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
	rett	%l2+4
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
	ld	[%l5 + T_LWP], %l5	! load lwp pointer (pcb is first in lwp)
	ld	[%l5 + PCB_FLAGS], %l4	! get pcb_flags
	bset	FIX_ALIGNMENT, %l4
	st	%l4, [%l5 + PCB_FLAGS]
	jmp	%l2			! return, skip trap instruction
	rett	%l2+4
	SET_SIZE(.fix_alignment)

#endif	/* lint */

/*
 *	Setup g7 via the CPU data structure
 *
 */

#if defined(lint)

struct scb *
set_tbr(struct scb *s)
{
	s = s;
	return (&scb);
}

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

#if defined(lint)

/* ARGSUSED */
void
curthread_setup(struct cpu *cpu)
{}

#else	/* lint */

	ENTRY_NP(curthread_setup)
	retl
	ld	[%o0 + CPU_THREAD], THREAD_REG
	SET_SIZE(curthread_setup)

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
trapvec kclock14_vec;
trapvec kadb_tcode;

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
 * interrupts, oterwise the kernel's vector is installed.
 */
	.align	8
	.global mon_clock14_vec, kclock14_vec
mon_clock14_vec:
	SYS_TRAP(0x1e)			! gets overlaid.
kclock14_vec:
	SYS_TRAP(0x1e)			! gets overlaid.

/*
 * Glue code for traps that should take us to the monitor/kadb if they
 * occur in kernel mode, but that the kernel should handle if they occur
 * in user mode.
 */
	.global kadb_tcode
	.global mon_breakpoint_vec


/*
 * This code assumes that:
 * 1. the monitor uses trap ff to breakpoint us
 * 2. kadb steals both fe and fd when we call scbsync()
 * 3. kadb uses the same tcode for both fe and fd.
 * Note: the ".align 8" is needed so that the code that copies
 *	the vectors at system boot time can use ldd and std.
 */
	.align	8
trap_mon:
	btst	PSR_PS, %l0		! test pS
	bnz,a	1f			! branch if kernel trap
	mov	%l0, %psr		! delay slot, restore psr
	b,a	sys_trap		! user-mode, treat as bad trap
1:
	nop;nop;nop;nop			! psr delay, plus alignment
mon_breakpoint_vec:
	SYS_TRAP(0xff)			! gets overlaid.

trap_kadb:
	btst	PSR_PS, %l0		! test pS
	bnz,a	1f			! branch if kernel trap
	mov	%l0, %psr		! delay slot, restore psr
	srl	%l4, 4, %l4		! user-mode; get trap code
	b	sys_trap		! and treat as bad trap
	and	%l4, 0xff, %l4
1:
	nop;nop;nop;nop			! psr delay, plus alignment
kadb_tcode:
	SYS_TRAP(0xfe)			! gets overlaid.


! BEGIN remote debugger interface code
	.common _db_kernel_breakpoint_handler_adr, 4, 4
	.common _db_kernel_breakpoint_handler_new_adr, 4, 4
	.global _db_kernel_breakpoint_handler
_db_kernel_breakpoint_handler:
	set _db_kernel_breakpoint_handler_adr, %l4
	ld	[%l4], %l4
	jmpl	%l4, %g0
	nop

	.global _db_kernel_breakpoint_handler_new
_db_kernel_breakpoint_handler_new:
	set _db_kernel_breakpoint_handler_new_adr, %l4
	ld	[%l4], %l4
	jmpl	%l4, %g0
	nop

	.global call_gdb
call_gdb:
	ta	0x70
	retl
	nop

.use_breakpoint_70.: .word 0
! END remote debugger interface code

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
	rett	%l2+4
	SET_SIZE(.setpsr)

#endif	/* lint */

#if defined(lint)
/*
 * These need to be defined somewhere to lint and there is no "hicore.s"...
 */
caddr_t e_text;
#endif	/* lint */
