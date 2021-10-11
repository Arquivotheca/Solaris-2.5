/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rand_r.c	1.4	89/10/23 SMI"	/* SVr4.0 1.5	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#ifdef __STDC__
#pragma weak rand_r = _rand_r
#endif

#include "synonyms.h"

int
_rand_r(unsigned int *randx)
{
	return(((*randx = *randx * 1103515245L + 12345)>>16) & 0x7fff);
}
