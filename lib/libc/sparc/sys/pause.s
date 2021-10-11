/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)pause.s	1.5	92/07/14 SMI"	/* SVr4.0 1.8	*/

/* C library - pause						*/
/* int pause (void);						*/

	.file	"pause.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(pause,function)

#include "SYS.h"

	SYSCALL(pause)
	RET

	SET_SIZE(pause)
