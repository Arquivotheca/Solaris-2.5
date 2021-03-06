/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)ladd.s	1.9	92/12/01 SMI"	/* SVr4.0 1.1	*/

/*
 * Double long add routine.  Ported from pdp 11/70 version
 * with considerable effort.  All supplied comments were ported.
 *
 * Ported from m32 version to sparc. No comments about difficulty.
 *
 *	dl_t
 *	ladd (lop, rop)
 *		dl_t	lop;
 *		dl_t	rop;
 */

	.file	"ladd.s"

#include	<sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(ladd,function)

#include	"synonyms.h"

	ENTRY(ladd)

	ld	[%o7+8],%o4		! Instruction at ret-addr should be a
	cmp     %o4,8			! 'unimp 8' indicating a valid call.
	be	1f			! if OK, go forward.
	nop				! delay instruction.
	jmp	%o7+8			! return
	nop				! delay instruction.

1:
	ldd	[%o0],%o2		! fetch lop
	ldd	[%o1],%o4		! fetch rop
	addcc	%o3,%o5,%o3		! lop.dl_lop + rop.dl_lop (set carry)
	addxcc	%o2,%o4,%o2		! lop.dl_hop + rop.dl_hop + <carry>
	ld	[%sp+(16*4)],%o0	! address to store result into
	jmp	%o7+12			! return
	std	%o2,[%o0]		! store result

	SET_SIZE(ladd)
