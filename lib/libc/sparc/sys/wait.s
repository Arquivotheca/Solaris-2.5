/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)wait.s	1.7	92/07/14 SMI"	/* SVr4.0 1.9	*/

/* C library -- wait						*/
/* pid_t wait (int *stat_loc);					*/
/* pid_t wait ((int *)0);					*/

	.file	"wait.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(wait,function)

#include "SYS.h"

	SYSCALL_RESTART(wait)
	ld	[%sp+68], %o2
	tst	%o2		! load interlock, but nothing can move here.
	bnz,a	1f
	st	%o1, [%o2]
1:
	RET

	SET_SIZE(wait)
