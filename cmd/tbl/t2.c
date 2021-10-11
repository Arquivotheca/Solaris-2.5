/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved. The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
     
/*
 * Copyright (c) 1983, 1984 1985, 1986, 1987, 1988, Sun Microsystems, Inc.
 * All Rights Reserved.
 */
  
#ident	"@(#)t2.c	1.2	90/02/14 SMI"	/* SVr4.0 1.1	*/

 /* t2.c:  subroutine sequencing for one table */
# include "t..c"
tableput()
{
saveline();
savefill();
ifdivert();
cleanfc();
getcomm();
getspec();
gettbl();
getstop();
checkuse();
choochar();
maktab();
runout();
release();
rstofill();
endoff();
restline();
}
