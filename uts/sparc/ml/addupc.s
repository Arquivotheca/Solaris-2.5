/*
 *	Copyright (c) 1986 by Sun Microsystems, Inc.
 */

#ident	"@(#)addupc.s	1.7	92/07/14 SMI"

#include <sys/asm_linkage.h>

/*
 * Add to user profiling counters.
 *
 * struct uprof {
 * 	short	*pr_base;
 * 	unsigned pr_size;
 * 	unsigned pr_off;
 * 	unsigned pr_scale;
 * } ;
 *
 * addupc( pc, pr, incr)
 *	register void (*pc)();
 *	register struct prof *pr;
 *	int incr;
 * {
 * 	register short *slot;
 * 	short counter;
 *
 *	if (pr->pr_scale == 2)
 *		slot = pr->pr_base;
 *	else
 * 		slot = pr->pr_base + 
 *			(((pc-pr->pr_off) * pr->pr_scale) >> 16)/(sizeof *slot);
 * 	if (slot >= pr->pr_base &&
 * 	    slot < (short *)(pr->pr_size + (int)pr->pr_base)) {
 * 		if ((counter=fusword(slot))<0) {
 * 			pr->pr_scale = 0;
 * 		} else {
 * 			counter += incr;
 * 			susword(slot, counter);
 * 		}
 * 	}
 * }
 */

#if defined(lint)

#include <sys/thread.h>
#include <sys/lwp.h>	/* for definition of struct prof */

/* ARGSUSED */
void
addupc(void (*pc)(), struct prof *pr, int incr)
{}

#else	/* lint */

	.global	.mul

PR_BASE =	0
PR_SIZE	=	4
PR_OFF	=	8
PR_SCALE=	12

	.seg	".text"
	.align	4

	ENTRY(addupc)
	save	%sp, -SA(MINFRAME), %sp

	ld	[%i1 + PR_OFF], %l0	! pr->pr_off
	subcc	%i0, %l0, %o0		! pc - pr->pr_off
	bl	.out
	srl	%o0, 1, %o0		! /2, for sign bit
	ld	[%i1 + PR_SCALE], %o1
	cmp	%o1, 2			! if (pr->pr_scale == 2)
	be	1f			!	slot = pr->pr_base
	clr	%l0			! 
	call	.mul			! ((pc - pr->pr_off) * pr->pr_scale)/4
	srl	%o1, 1, %o1		! /2

	sra	%o1, 14, %g1		! overflow after >>14?
	tst	%g1
	bnz	.out
	srl	%o0, 14, %l0		! >>14 double word
	sll	%o1, 32-14, %g1
	or	%l0, %g1, %l0
	bclr	1, %l0			! make even
1:
	ld	[%i1 + PR_SIZE], %l1	! check length
	cmp 	%l0, %l1
	bgeu	.out
	ld	[%i1 + PR_BASE], %l1	! add base
	add	%l1, %l0, %l0
	call	fusword			! fetch counter from user
	mov	%l0, %o0		! delay slot
	tst	%o0
	bge,a	1f			! fusword succeeded
	add	%o0, %i2, %o1		! counter += incr
	!
	! Fusword failed, turn off profiling.
	!
	clr	[%i1 + PR_SCALE]
	ret
	restore

	!
	! Store new counter
	!
1:
	call	susword			! store counter, checking permissions
	mov	%l0, %o0		! delay slot

.out:
	ret
	restore
	SET_SIZE(addupc)

#endif	/* lint */
