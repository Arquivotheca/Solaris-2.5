/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)sigprocmask.s	1.3 94/11/03	SMI" /* SVr4.0 1.9 */

#include <sys/asm_linkage.h>
#include "SYS.h"

	.file "sigprocmask.s"
/*
 * void
 * __sigprocmask(how, set, oset)
 */

	ENTRY(__sigprocmask);
	SYSTRAP(sigprocmask)
	RET
	SET_SIZE(__sigprocmask)
