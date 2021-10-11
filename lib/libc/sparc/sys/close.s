/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)close.s	1.5	92/07/14 SMI"	/* SVr4.0 1.8	*/

/* C library -- close						*/
/* int close(int fildes)					*/

	.file	"close.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(close,function)

#include "SYS.h"

	SYSCALL(close)
	RETC

	SET_SIZE(close)
