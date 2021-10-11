/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)creat.s	1.5	92/07/14 SMI"	/* SVr4.0 1.8	*/

/* C library -- creat						*/
/* int creat(char *path, mode_t mode)				*/

	.file	"creat.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(creat,function)

#include "SYS.h"

	SYSCALL(creat)
	RET

	SET_SIZE(creat)
