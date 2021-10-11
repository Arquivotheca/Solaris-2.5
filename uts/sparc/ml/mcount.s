/*
 * Copyright (c) 1986-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mcount.s 1.11	93/02/05 SMI"

#if defined(lint)
void
_mcount()
{}
#else
#include <sys/param.h>
#include <sys/asm_linkage.h>
#include <sys/machthread.h>
#include <sys/gprof.h>
#include "assym.s"

/* don't mess with FROMPC and SELFPC, or you won't be able to return */
#define	FROMPC		%i7
#define	SELFPC		%o7

/*
 *  register usage:  we're in the same window as the calling routine, so we
 *	only use the first 6 output registers
 */

#define PROFILE_STRUCT	%o5

#define LINK_ADDR	%o4

#define TOS_P		%o3

#define TOP		%o2

#define SCRATCH1	%o1

#define	SCRATCH0	%o0
#define	CPU_STRUCT	SCRATCH0

! _mcount is being used by tracing as a mechanism to trace each procedure's
! entry to analyze path lengths.  The compiler inserts the calls to _mcount
! via the -xpg option and the mcount code is conditionally compiled to
! call a trace point if TRACE is defined.
! The mechanism uses the profiling struct hung off each cpu to enable the
! system to boot far enough to get tracing initialized.  If there is no
! profiling struct the routine just returns, just as it does for profiling.
! Tracing and profiling are mutually exclusive.
! Using the profiling structure as temporary storage and the
! profile mutex also solve the MP case!

	.section	".text"
	.proc	0
	.global _mcount
	.type _mcount,#function
_mcount:

#if (defined(GPROF) || defined(TRACE))

! Make sure there is a kern_profiling structure assigned to this cpu

	CPU_ADDR(CPU_STRUCT, PROFILE_STRUCT)
	ld	[CPU_STRUCT + CPU_PROFILING], PROFILE_STRUCT 
	tst	PROFILE_STRUCT			! no profile struct, just return
	bz	out			
	nop

#if defined(GPROF)

!  Make sure that we're profiling
!	- if we are tracing we will never be profiling
	
	ld	[PROFILE_STRUCT + PROFILING], SCRATCH0
	cmp	SCRATCH0, PROFILE_ON		! find out if profiling is on
	bne	out
	nop

#endif /* defined(GPROF) */

! grab the mt lock, if we can't get it return

	ldstub	[PROFILE_STRUCT + PROF_LOCK], SCRATCH0
	cmp	SCRATCH0, %g0 			! see if we are recursing
	bne	out				! if profiling is on return
	nop

#if defined(TRACE)

	TRACE_ASM_2 (%o3, TR_FAC_MCOUNT, TR_MCOUNT_ENTER, 0, %i7, %o7);
	ld	[THREAD_REG + T_CPU], CPU_STRUCT
	ld	[CPU_STRUCT + CPU_PROFILING], PROFILE_STRUCT
	tst	PROFILE_STRUCT			! reload the profiling struct 
	bz	out				! pointer if its null return
	nop
	retl
	stb	%g0, [PROFILE_STRUCT + PROF_LOCK]	! clear profiling lock

#elif defined(GPROF)
	
! Hash frompc to some entry in froms.  Note, low order two bits of frompc will
! be 0, and fromssize is some power of 2 times sizeof (kp_call_t *).
!
! link_addr = &froms[(frompc >> 2) & ((fromssize/sizeof(kp_call_t *)) - 1)]

	ld	[PROFILE_STRUCT + PROF_FROMSSIZE], SCRATCH1
	ld	[PROFILE_STRUCT + PROF_FROMS], SCRATCH0
	dec	SCRATCH1			! convert to bit mask
	and	FROMPC, SCRATCH1, SCRATCH1	! mask frompc
	add	SCRATCH0, SCRATCH1, LINK_ADDR	! offset into froms

testlink:
	ld	[LINK_ADDR], TOP		! top = *link_addr
	tst	TOP
	bne,a	oldarc
	nop

! This is the first time we've seen a call from here; get new kp_call struct

	ld	[PROFILE_STRUCT + PROF_TOSNEXT], TOP	!address of free struct
	ld	[PROFILE_STRUCT + PROF_TOS], TOS_P
	ld 	[PROFILE_STRUCT + PROF_TOSSIZE], SCRATCH0
	add	TOP, KPCSIZE, SCRATCH1		!advance to next free struct
	st	SCRATCH1, [PROFILE_STRUCT + PROF_TOSNEXT]
	add	TOS_P, SCRATCH0, SCRATCH0	!end of tos array
	cmp	TOP, SCRATCH0			!too many?
	bge,a	overflow
	nop

!  Initialize arc

	st	TOP, [LINK_ADDR]		!*link_addr = top
	st	FROMPC, [TOP+KPC_FROM]		!top->frompc = frompc (%o7)
	st	SELFPC, [TOP+KPC_TO]		!top->topc = topc (%i7)
	mov	1, SCRATCH0
	st	SCRATCH0, [TOP+KPC_COUNT]	!top->count = 1
	b	done
	st	%g0, [TOP+KPC_LINK]		!delay slot; top->link = 0

!  Check if current arc is the correct one - usually it will be

oldarc:
	ld	[TOP+KPC_TO], SCRATCH0		!top->topc
	ld	[TOP+KPC_FROM], SCRATCH1	!top->frompc
	cmp	SCRATCH0, SELFPC		!does topc match
	bne	chainloop
	ld	[TOP+KPC_COUNT], SCRATCH0	!top->count, for later
	cmp	SCRATCH1, FROMPC
	inc	SCRATCH0
	be,a	done				!does frompc match
	st	SCRATCH0, [TOP+KPC_COUNT]	!if so increment count

!  Check next arc

chainloop:
	b	testlink
	add	TOP, KPC_LINK, LINK_ADDR

done:
	retl
	stb	%g0, [PROFILE_STRUCT + PROF_LOCK]	! clear profiling lock

overflow:
	save	%sp, -SA(MINFRAME), %sp
	sethi	%hi(OVERMSG), %o0
	call	printf, 1			!printf(OVERMSG)
	or	%o0, %lo(OVERMSG), %o0
	ret
	restore
	
#endif

#endif /* (defined(GPROF) || defined(TRACE)) */

out:
	retl
	nop

#if defined(GPROF)

	.section	".data"			

	.global kernel_profiling
kernel_profiling:
	.word	1

OVERMSG:
	.asciz	"mcount: tos overflow\n"

#endif /* defined(GPROF) */

#endif /* lint */
