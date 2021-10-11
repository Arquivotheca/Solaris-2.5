/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

#pragma ident	"@(#)setitimer.s	1.3	95/02/24 SMI"

/* C library -- setitimer					*/
/* int setitimer (int, const struct itimerval *, struct itimerval *);	*/

	.file	"setitimer.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(setitimer,function)

#include "SYS.h"

	SYSCALL(setitimer)
	RET

	SET_SIZE(setitimer)
