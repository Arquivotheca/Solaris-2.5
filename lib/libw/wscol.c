/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)wscol.c	1.4	92/07/14 SMI"

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 * Copyright (c) 1988 by Nihon Sun Microsystems K.K.
 */
#include	<stdlib.h>
#include 	<widec.h>
#include	<euc.h>
#include	<ctype.h>

#ifndef NULL
#define NULL	(wchar_t *)0
#endif

int
wscol(const wchar_t * s1)
{  
	
	int	col=0;

	while(*s1) {
		switch ( wcsetno(*s1) ) {
		case	0:
			col += 1;
			break;
		case	1:
			col += scrw1;
			break;
		case	2:
			col += scrw2;
			break;
		case	3:
			col += scrw3;
			break;
		}
		s1++;
	}
	return(col);
}
