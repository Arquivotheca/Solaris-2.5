/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)fstatvfs.s	1.6	92/07/14 SMI"	/* SVr4.0 1.1	*/

/* C library -- fstatvfs					*/
/* int fstatvfs(int fildes, struct statvfs *buf)		*/

	.file	"fstatvfs.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(fstatvfs,function)

#include "SYS.h"

	SYSCALL(fstatvfs)
	RETC

	SET_SIZE(fstatvfs)
