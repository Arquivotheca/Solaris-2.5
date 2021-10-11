/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)readv.s	1.6	92/07/14 SMI"	/* SVr4.0 1.3	*/

/* C library -- readv						*/
/* nt readv(int fildes, void *iovp[], int iovcnt[])		*/

	.file	"readv.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(readv,function)

#include "SYS.h"

	SYSCALL_RESTART(readv)
	RET

	SET_SIZE(readv)
