/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)sigsuspend.s	1.5	92/07/14 SMI"	/* SVr4.0 1.3	*/

/* C library -- sigsuspend					*/
/* int sigsuspend (sigset_t *set);				*/

	.file	"sigsuspend.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(sigsuspend,function)

#include "SYS.h"

	SYSCALL(sigsuspend)
	RET

	SET_SIZE(sigsuspend)
