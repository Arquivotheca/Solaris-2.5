/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)boot_elf.s	1.17	95/03/03 SMI"

#include	"machdep.h"
#include	"profile.h"
#if	defined(lint)
#include	<sys/types.h>
#include	"_rtld.h"
#include	"_elf.h"
#else
#include	<sys/stack.h>

	.file	"boot_elf.s"
	.seg	".text"
#endif

/*
 * We got here because the initial call to a function resolved to a procedure
 * linkage table entry.  That entry did a branch to the first PLT entry, which
 * in turn did a call to elf_rtbndr (refer elf_plt_init()).
 *
 * the code sequence that got us here was:
 *
 * PLT entry for foo():
 *	sethi	(.-PLT0), %g1			! not changed by rtld
 *	ba,a	.PLT0				! patched atomically 2nd
 *	nop					! patched first
 *
 * Therefore on entry, %i7 has the address of the call, which will be added
 * to the offset to the plt entry in %g1 to calculate the plt entry address
 * we must also subtract 4 because the address of PLT0 points to the
 * save instruction before the call.
 *
 * the plt entry is rewritten:
 *
 * PLT entry for foo():
 *	sethi	(.-PLT0), %g1
 *	sethi	%hi(entry_pt), %g1
 *	jmpl	%g1 + %lo(entry_pt), %g0
 */

#if	defined(lint)

extern unsigned long	elf_bndr(caddr_t, unsigned long, Rt_map *, caddr_t);

static void
elf_rtbndr(caddr_t pc, unsigned long reloc, Rt_map * lmp, caddr_t from)
{
	(void) elf_bndr(pc, reloc, lmp, from);
}


#else
	.weak	_elf_rtbndr		! keep dbx happy as it likes to
	_elf_rtbndr = elf_rtbndr	! rummage around for our symbols

	.global	elf_rtbndr
	.type   elf_rtbndr, #function
	.align	4

elf_rtbndr:
	mov	%i7, %o0		! Save callers address(profiling)
	save	%sp, -SA(MINFRAME), %sp	! Make a frame
	mov	%i0, %o3		! Callers address is arg 4
	add	%i7, -0x4, %o0		! %o0 now has address of PLT0
	srl	%g1, 10, %g1		! shift offset set by sethi
	add	%o0, %g1, %o0		! %o0 has the address of jump slot
	mov	%g1, %o1		! %o1 has offset from jump slot
					! to PLT0 which will be used to
					! calculate plt relocation entry
					! by elf_bndr
	call	elf_bndr		! returns function address in %o0
	ld	[%i7 + 8], %o2		! %o2 has ptr to lm
	mov	%o0, %g1		! save address of routine binded
	restore				! how many restores needed ? 2
	jmp	%g1			! jump to it
	restore
	.size 	elf_rtbndr, . - elf_rtbndr

#endif


/*
 * Initialize the first plt entry so that function calls go to elf_rtbndr
 *
 * The first plt entry (PLT0) is:
 *
 *	save	%sp, -64, %sp
 *	call	elf_rtbndr
 *	nop
 *	address of lm
 */

#if	defined(lint)

void
elf_plt_init(unsigned long * plt, Rt_map * lmp)
{
	*(plt + 0) = (unsigned long) M_SAVESP64;
	*(plt + 4) = M_CALL | (((unsigned long)elf_rtbndr -
			((unsigned long)plt)) >> 2);
	*(plt + 8) = M_NOP;
	*(plt + 12) = (unsigned long) lmp;
}

#else
	.global	elf_plt_init
	.type	elf_plt_init, #function
	.align	4

elf_plt_init:
	save	%sp, -SA(MINFRAME), %sp	! Make a frame
1:
	call	2f
	sethi	%hi((_GLOBAL_OFFSET_TABLE_ - (1b - .))), %l7
2:
	sethi	%hi(M_SAVESP64), %o0	! Get save instruction
	or	%o0, %lo(M_SAVESP64), %o0
	or	%l7, %lo((_GLOBAL_OFFSET_TABLE_ - (1b - .))), %l7
	st	%o0, [%i0]		! Store in plt[0]
	iflush	%i0
	add	%l7, %o7, %l7
	ld	[%l7 + elf_rtbndr], %l7
	inc	4, %i0			! Bump plt to point to plt[1]
	sub	%l7, %i0, %o0		! Determine -pc so as to produce
					! offset from plt[1]
	srl	%o0, 2, %o0		! Express offset as number of words
	sethi	%hi(M_CALL), %o4	! Get sethi instruction
	or	%o4, %o0, %o4		! Add elf_rtbndr address
	st	%o4, [%i0]		! Store instruction in plt
	iflush	%i0
	sethi	%hi(M_NOP), %o0		! Generate nop instruction
	st	%o0, [%i0 + 4]		! Store instruction in plt[2]
	iflush	%i0 + 4
	st	%i1, [%i0 + 8]		! Store instruction in plt[3]
	iflush	%i0 + 8
	ret
	restore
	.size	elf_plt_init, . - elf_plt_init
#endif

#ifdef	PROF

/*
 * If the function to which a plt has been bound is within a shared library
 * that is being profiled, then the plt will have jumped here (refer to
 * elf_plt_cg_write).
 *
 * By referencing the plt we've come from we can extract the symbol index of
 * the function we are calling.  This index and the address of the original
 * caller are passed to _plt_cg_interp which creates a call graph entry for
 * this call.  On return the real function is called.
 *
 * Note, it is possible that the calling plt only had the first and second
 * patches applied before another thread accessed this plt.  If the plt[0]
 * entry isn't what we expect then return back to the plt.
 */

#if	defined(lint)

void
elf_plt_cg(int ndx, caddr_t from)
{
	(void) plt_cg_interp(ndx, from, (caddr_t)0);
}

#else
	.global	elf_plt_cg
	.type   elf_plt_cg, #function
	.align	4

elf_plt_cg:
	save	%sp, -SA(MINFRAME), %sp	! Make a frame
	mov	%i7, %l0		! Get address of plt we've come from
	ld	[%l0 + 4], %l1		! Get symbol index from plt[1]
	set	0x3fffff, %o2		! Set up a S_MASK(22) and get symbol
	and	%l1, %o2, %o0		!	index as first argument
	mov	%g1, %o1		! `from' address is second argument
	mov	%g1, %i7		! Set `from' address for return
	call	plt_cg_interp		! Call interpreter
	mov	%g0, %o2		! `to' address is unused (debugging)
	jmpl	%o0, %g0		! On return, jump to target
	restore
	.size	elf_plt_cg, . - elf_plt_cg

#endif
#endif

/*
 * After the first call to a plt, elf_bndr() will have determined the true
 * address of the function being bound.  The plt is now rewritten so that
 * any subsequent calls go directly to the bound function.  If the library
 * to which the function belongs is being profiled refer to _plt_cg_write.
 *
 * the new plt entry is:
 *
 *	sethi	(.-PLT0), %g1			! constant
 *	sethi	%hi(function address), %g1	! patched second
 *	jmpl	%g1 + %lo(function address, %g0	! patched first
 */

#if	defined(lint)

void
elf_plt_write(unsigned long * pc, unsigned long * symval)
{
	*(pc + 8) = (M_JMPL | ((unsigned long)symval & S_MASK(10)));
	*(pc + 4) = (M_SETHIG1 | ((unsigned long)symval >> (32 - 22)));
}

#else
	.global	elf_plt_write
	.type	elf_plt_write, #function
	.align	4

elf_plt_write:
	sethi	%hi(M_JMPL), %o3	! Get jmpl instruction
	and	%o1, 0x3ff, %o2		! Lower part of function address
	or	%o3, %o2, %o3		!	is or'ed into instruction
	st	%o3, [%o0 + 8]		! Store instruction in plt[2]
	iflush	%o0 + 8
	stbar
	srl	%o1, 10, %o1		! Get high part of function address
	sethi	%hi(M_SETHIG1), %o3	! Get sethi instruction
	or	%o3, %o1, %o3		! Add sethi and function address
	st	%o3, [%o0 + 4]		! Store instruction in plt[1]
	retl
	iflush	%o0 + 4
	.size 	elf_plt_write, . - elf_plt_write

#endif

/*
 * performs the 'iflush' instruction on a range
 * of memory.
 */
#if	defined(lint)
void
iflush_range(caddr_t addr, int len)
{
	caddr_t	i;
	for (i = addr; i <= addr + len; i+=4)
		/* iflush i */;
}
#else
	.global	iflush_range
	.type	iflush_range, #function
	.align	4

iflush_range:
	save	%sp, -SA(MINFRAME), %sp	! Make a frame
	mov	%i0, %l0
	add	%i0, %i1, %l1
1:
	cmp	%l0, %l1
	bg	2f
	nop
	iflush	%l0
	ba	1b
	add	%l0, 0x4, %l0
2:
	ret
	restore
	.size	iflush_range, . - iflush_range
#endif
