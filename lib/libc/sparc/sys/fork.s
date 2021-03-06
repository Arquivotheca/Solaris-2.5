/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)fork.s	1.9	92/07/14 SMI"	/* SVr4.0 1.7	*/

/* C library -- fork						*/
/* pid_t fork(void)						*/

/* From the syscall:
 * %o1 == 0 in parent process, %o1 == 1 in child process.
 * %o0 == pid of child in parent, %o0 == pid of parent in child.
 */

	.file	"fork.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(fork,function)

#include "SYS.h"

	SYSCALL(fork)
	tst	%o1		! test for child
	bnz,a	1f
	clr	%o0		! child, return (0)
1:
	RET			! parent, return (%o0 = child pid)

	SET_SIZE(fork)
