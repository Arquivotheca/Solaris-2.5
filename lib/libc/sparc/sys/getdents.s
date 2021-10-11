/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)getdents.s	1.6	92/07/14 SMI"	/* SVr4.0 1.2.1.7	*/

/* C library -- getdents					*/
/* int getdents (int fildes, struct dirent *buf, size_t count)	*/

	.file	"getdents.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(getdents,function)

#include "SYS.h"

	SYSCALL(getdents)
	RET

	SET_SIZE(getdents)
