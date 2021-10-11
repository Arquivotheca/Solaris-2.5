/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)_write.s	1.2	92/07/14 SMI"	/* SVr4.0 1.9	*/

/* C library -- write						*/
/* int write (int fildes, void *buf, unsigned nbyte);		*/

	.file	"_write.s"

#include "SYS.h"

	SYSREENTRY(_write)
	mov	SYS_write, %g1
	t	8
	SYSRESTART(.restart__write)
	RET

	SET_SIZE(_write)
