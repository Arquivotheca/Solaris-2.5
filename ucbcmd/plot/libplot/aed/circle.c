/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)circle.c	1.1	90/08/19 SMI"	/* SVr4.0 1.1	*/

/*
 * Copyright (c) 1983, 1984 1985, 1986, 1987, 1988, Sun Microsystems, Inc.
 * All Rights Reserved.
 */

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */


#include "aed.h"

/*---------------------------------------------------------
 *	Circle draws a circle.
 *
 *	Results:	None.
 *
 *	Side Effects:
 *	A circle of radius r is drawn at (x,y).
 *---------------------------------------------------------
 */
circle(x, y, r)
int x, y, r;
{
    char buf[3];
    setcolor("01");
    putc('Q', stdout);
    outxy20(x, y);
    putc('O', stdout);
    chex((r*scale)>>12, buf, 2);
    fputs(buf, stdout);
    (void) fflush(stdout);
}
