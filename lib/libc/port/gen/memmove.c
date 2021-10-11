/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)memmove.c	1.8	95/09/27 SMI"	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/
/*
 * Copy s0 to s, always copy n bytes.
 * Return s
 * Copying between objects that overlap will take place correctly
 */

#pragma	weak	memmove	= _memmove

#include "synonyms.h"
#include <string.h>

VOID * 
memmove(s, s0, n )
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
		register char *s2 = s0;
#endif
		if (s1 <= s2) {
			do {
				*s1++ = *s2++;
			} while (--n != 0);
		} else {
			s2 += n;
			s1 += n;
			do {
				*--s1 = *--s2;
			} while (--n != 0);
		}
	}
	return ( s );
}
