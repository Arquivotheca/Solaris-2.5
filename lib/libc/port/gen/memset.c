/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)memset.c	1.9	95/09/27 SMI"	/* SVr4.0 1.3.1.5	*/

/*LINTLIBRARY*/
/*
 * Set an array of n chars starting at sp to the character c.
 * Return sp.
 */

#pragma	weak	memset = _memset

#include "synonyms.h"
#include <string.h>

VOID *
memset(sp1, c, n)
VOID *sp1;
register int c;
register size_t n;
{
    if (n != 0) {
	register unsigned char *sp = sp1;
	do {
	    *sp++ = (unsigned char) c;
	} while (--n != 0);
    }
    return( sp1 );
}
