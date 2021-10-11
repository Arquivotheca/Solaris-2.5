/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*       Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)waitid.s	1.7	92/07/14 SMI"	/* SVr4.0 1.1	*/

/* C library -- waitid						*/
/* int waitid(idtype_t idtype, id_t id, siginfo_t *infop,
	int options)						*/

	.file	"waitid.s"

#include <sys/asm_linkage.h>

	ANSI_PRAGMA_WEAK(waitid,function)

#include "SYS.h"

	SYSREENTRY(waitid)
	SYSTRAP(waitsys)
	SYSRESTART(.restart_waitid)
	RET

	SET_SIZE(waitid)
