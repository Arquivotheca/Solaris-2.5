/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*	Copyright (c) 1989 by Sun Microsystems, Inc.		*/

.ident	"@(#)alarm.s	1.8 95/03/15	SMI" /* SVr4.0 1.9 */

#include <sys/asm_linkage.h>
#include "SYS.h"

	.file "alarm.s"
/*
 * unsigned
 * _alarm(unsigned t)
 */

	ENTRY(__alarm);
	SYSTRAP(alarm)
	RET
	SET_SIZE(__alarm)

/*
 * unsigned
 * _lwp_alarm(unsigned t)
 */

	ENTRY(__lwp_alarm);
	SYSTRAP(lwp_alarm)
	RET
	SET_SIZE(__lwp_alarm)

/*
 * setitimer(which, v, ov)
 *	int which;
 *	struct itimerval *v, *ov;
 */
	ENTRY(__setitimer)
	SYSTRAP(setitimer)
	RET
	SET_SIZE(__setitimer)
