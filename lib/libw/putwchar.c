/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)putwchar.c	1.4	92/09/05 SMI"	/* from JAE2.0 1.0 */

/*
 * A subroutine version of the macro putchar
 */
/* #include "shlib.h" */
#include <stdio.h>
#include <stdlib.h>
#include <widec.h>

#undef putwchar

int
putwchar(c)
register c;
{
	return(putwc(c, stdout));
}
