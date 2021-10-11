/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)printjob.c	1.4	93/10/07 SMI"	/* SVr4.0 1.1	*/

#include "lpNet.h"
#include "lpd.h"

/* 
 * This is basically a stub routine which just acknowledges 
 * the receipt of the command from the LPD system.
 */
void
printjob(void)
{
	ACK();
}
