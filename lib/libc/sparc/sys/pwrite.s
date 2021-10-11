/*	Copyright (c) 1989 by Sun Microsystems, Inc.	*/

.ident	"@(#)pwrite.s	1.2	92/07/14 SMI"	/* SVr4.0 1.9	*/

/* C library -- pwrite						*/
/* int pwrite (int fildes, const void *buf, unsigned nbyte, off_t offset);	*/

	.file	"pwrite.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(pwrite,function)

#include "SYS.h"

	SYSCALL_RESTART(pwrite)
	RET

	SET_SIZE(pwrite)
