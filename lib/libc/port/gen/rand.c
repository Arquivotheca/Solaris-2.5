/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rand.c	1.6	92/07/14 SMI"	/* SVr4.0 1.5	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#include "synonyms.h"

static long randx=1;

void
srand(x)
unsigned x;
{
	randx = x;
}

int
rand()
{
	return(((randx = randx * 1103515245L + 12345)>>16) & 0x7fff);
}
