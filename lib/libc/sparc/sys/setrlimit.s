/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)setrlimit.s	1.6	92/07/14 SMI"	/* SVr4.0 1.1	*/

/* C library -- setrlimit					*/
/* int setrlimit(int resource, const struct rlimit *rlp)	*/

	.file	"setrlimit.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(setrlimit,function)

#include "SYS.h"

	SYSCALL(setrlimit)
	RET

	SET_SIZE(setrlimit)
