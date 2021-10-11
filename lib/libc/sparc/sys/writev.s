/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)writev.s	1.6	92/07/14 SMI"	/* SVr4.0 1.3	*/

/* C library -- writev 						*/
/* int writev(fd, iovp, iovcnt)					*/

	.file	"writev.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(writev,function)

#include "SYS.h"

	SYSCALL_RESTART(writev)
	RET

	SET_SIZE(writev)
