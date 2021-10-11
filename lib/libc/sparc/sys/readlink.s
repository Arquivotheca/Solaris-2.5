/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)readlink.s	1.6	92/07/14 SMI"	/* SVr4.0 1.1	*/

/* C library -- readlink					*/
/* int readlink(const char *path, void *buf, int bufsiz)	*/

	.file	"readlink.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(readlink,function)

#include "SYS.h"

	SYSCALL(readlink)
	RET

	SET_SIZE(readlink)
