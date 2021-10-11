/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)memcpy.c	1.8	95/09/27 SMI"	/* SVr4.0 1.3.1.5	*/

/*LINTLIBRARY*/
/*
 * Copy s0 to s, always copy n bytes.
 * Return s
 */

#pragma	weak	memcpy = _memcpy

#include "synonyms.h"
#include <stddef.h>
#include <string.h>

VOID * 
memcpy(s, s0, n)
VOID *s;
#ifdef __STDC__
const VOID *s0;
#else
VOID *s0;
#endif
register size_t n;
{
	if (n != 0) {
   	    register char *s1 = s;
#ifdef __STDC__
	    register const char *s2 = s0;
#else
	    register  char *s2 = s0;
#endif
	    do {
		*s1++ = *s2++;
	    } while (--n != 0);
	}
	return ( s );
}
