/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)read.s	1.5	92/07/14 SMI"	/* SVr4.0 1.9	*/

/* C library -- read						*/
/* int read (int fildes, void *buf, unsigned nbyte);		*/

	.file	"read.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(read,function)

#include "SYS.h"

	SYSCALL_RESTART(read)
	RET

	SET_SIZE(read)
