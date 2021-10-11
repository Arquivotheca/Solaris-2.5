/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)memchr.c	1.8	93/03/02 SMI"	/* SVr4.0 1.3.1.7	*/

/*LINTLIBRARY*/
/*
 * Return the ptr in sptr at which the character c1 appears;
 *   NULL if not found in n chars; don't stop at \0.
 */
#include "synonyms.h"
#include <stddef.h>
#include <string.h>

VOID * 
memchr(sptr, c1, n)
#ifdef __STDC__
const VOID *sptr;
#else
VOID *sptr;
#endif
int c1;
register size_t n;
{
	if (n != 0) {
	    register unsigned char c = (unsigned char)c1;
#ifdef __STDC__
	    register const unsigned char *sp = sptr;
#else
	    register unsigned char *sp = sptr; 
#endif
	    do {
		if (*sp++ == c)
			return ((VOID *)--sp);
	    } while (--n != 0);
	}
	return (NULL);
}
