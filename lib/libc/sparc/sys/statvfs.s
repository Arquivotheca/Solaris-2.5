/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)statvfs.s	1.6	92/07/14 SMI"	/* SVr4.0 1.1	*/

/* C library -- statvfs						*/
/* int statvfs(const char *path, struct statvfs *statbuf)	*/

	.file	"statvfs.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(statvfs,function)

#include "SYS.h"

	SYSCALL(statvfs)
	RETC

	SET_SIZE(statvfs)
