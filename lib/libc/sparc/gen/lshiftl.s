/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)lshiftl.s	1.9	92/12/01 SMI"	/* SVr4.0 1.1	*/

/*
 * Shift a double long value. Ported from m32 version to sparc.
 *
 *	dl_t
 *	lshiftl (op, cnt)
 *		dl_t	op;
 *		int	cnt;
 */

	.file	"lshiftl.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(lshiftl,function)

#include "synonyms.h"


	ENTRY(lshiftl)

	ld	[%o7+8],%o4		! Instruction at ret-addr should be a
	cmp     %o4,8			! 'unimp 8' indicating a valid call.
	be	1f			! if OK, go forward.
	nop				! delay instruction.
	jmp	%o7+8			! return
	nop				! delay instruction.

1:
	ldd	[%o0],%o2		! fetch op
	subcc	%g0,%o1,%o4		! test cnt < 0 and save reciprocol
	bpos	.right			!
	ld	[%sp+(16*4)],%o0	! address to store result into

					! Positive (or null) shift (left)
	and	%o1,0x3f,%o1		! Reduce range to 0..63
	subcc	%o1,32,%o5		! cnt - 32 (also test cnt >= 32)
	bneg,a	.leftsmall		!
	add	%o4,32,%o4		! 32 - cnt (actually ((-cnt) + 32)
	sll	%o3,%o5,%o2		! R.h = R.l << (cnt - 32)
	ba	.done			!
	or	%g0,%g0,%o3		! R.l = 0

.leftsmall:
	srl	%o3,%o4,%o5		! temp = R.l >> (31 - cnt)
	sll	%o3,%o1,%o3		! R.l = R.l << cnt
	sll	%o2,%o1,%o2		! R.h = R.h << cnt
	ba	.done			!
	or	%o2,%o5,%o2		! R.h = R.h | temp

.right:					! Negative shift (right)
	and	%o4,0x3f,%o4		! Reduce range to 0..63
	subcc	%o4,32,%o5		! cnt - 32 (also test cnt >= 32)
	bneg,a	.rightsmall		!
	add	%o1,32,%o1		! 32 - cnt (actually ((-cnt) + 32)
	srl	%o2,%o5,%o3		! R.l = R.h >> (cnt - 32)
	ba	.done			!
	or	%g0,%g0,%o2		! R.h = 0

.rightsmall:
	sll	%o2,%o1,%o5		! temp = R.h << (31 - cnt)
	srl	%o3,%o4,%o3		! R.l = R.l >> cnt
	srl	%o2,%o4,%o2		! R.h = R.h >> cnt
	ba	.done			!
	or	%o3,%o5,%o3		! R.l = R.l | temp

.done:
	jmp	%o7+12			! return
	std	%o2,[%o0]		! store result

	SET_SIZE(lshiftl)
