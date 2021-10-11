/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)putpmsg.s	1.6	92/07/14 SMI"	/* SVr4.0 1.2	*/

/* C library -- putpmsg						*/
/* int putpmsg (int fd, struct const strbuf *ctlptr,
	struct const strbuf *dataptr, int *flags)		*/

	.file	"putpmsg.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(putpmsg,function)

#include "SYS.h"

	SYSCALL_RESTART(putpmsg)
	RET

	SET_SIZE(putpmsg)
