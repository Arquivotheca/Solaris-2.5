/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)memccpy.c	1.10	93/06/04 SMI"	/* SVr4.0 1.3.1.6	*/

/*LINTLIBRARY*/
/*
 * Copy s0 to s, stopping if character c is copied. Copy no more than n bytes.
 * Return a pointer to the byte after character c in the copy,
 * or NULL if c is not found in the first n bytes.
 */
#ifdef __STDC__
#pragma weak memccpy = _memccpy
#endif
#include "synonyms.h"
#include <stddef.h>

VOID *
memccpy(s, s0, c, n)
VOID *s;
#ifdef __STDC__
const VOID *s0;
#else
VOID *s0;
#endif
register int c;
register size_t n;
{
	if (n != 0) {
	    register unsigned char *s1 = s;
#ifdef __STDC__
	    register const unsigned char *s2 = s0;
#else
	    register unsigned char *s2 = s0;
#endif
	    do {
		if ((*s1++ = *s2++) == (unsigned char) c)
			return (s1);
	    } while (--n != 0);
	}
	return (NULL);
}
