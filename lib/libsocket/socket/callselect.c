/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)callselect.c	1.3	93/09/30 SMI"   /* SVr4.0 1.1  */

/*
 * dummy routine to force select() to be linked from archive libc
 */
callselect()
{
	select(0, 0, 0, 0, 0);
}
