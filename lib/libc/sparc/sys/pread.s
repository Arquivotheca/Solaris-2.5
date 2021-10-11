/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)pread.s	1.2	92/07/14 SMI"	/* SVr4.0 1.9	*/

/* C library -- pread						*/
/* int pread (int fildes, void *buf, unsigned nbyte, off_t offset);	*/

	.file	"pread.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(pread,function)

#include "SYS.h"

	SYSCALL_RESTART(pread)
	RET

	SET_SIZE(pread)
