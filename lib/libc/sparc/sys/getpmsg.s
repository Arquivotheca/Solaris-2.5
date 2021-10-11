/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)getpmsg.s	1.6	92/07/14 SMI"	/* SVr4.0 1.2	*/

/* C library -- getpmsg						*/
/* int getpmsg (int fd, struct strbuf *ctlptr,			*/
/*	       struct strbuf *dataptr, int pri, int *flags)	*/

	.file	"getpmsg.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(getpmsg,function)

#include "SYS.h"

	SYSCALL_RESTART(getpmsg)
	RET

	SET_SIZE(getpmsg)
