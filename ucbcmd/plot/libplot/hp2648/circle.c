/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)circle.c	1.1	90/08/19 SMI"	/* SVr4.0 1.1	*/

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved. The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1983, 1984 1985, 1986, 1987, 1988, Sun Microsystems, Inc.
 * All Rights Reserved.
 */


#include "hp2648.h"

circle (xc,yc,r)
int xc,yc,r;
{
	double costheta,sintheta,x,y,xn;
	int xi,yi;

	if(r<1){
		point(xc,yc);
		return;
	}
	sintheta = 1.0/r;
	costheta = pow(1-sintheta*sintheta,0.5);
	xi = x = r;
	yi = y = 0;
	do {
		point(xc+xi,yc+yi);
		xn = x;
		xi = x = x*costheta + y*sintheta;
		yi = y = y*costheta - xn*sintheta;
	} while( ! (yi==0 && xi >= r-1));
}
