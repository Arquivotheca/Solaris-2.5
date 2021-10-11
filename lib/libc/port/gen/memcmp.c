/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)memcmp.c	1.9	95/09/27 SMI"	/* SVr4.0 1.3.1.2	*/

/*LINTLIBRARY*/
/*
 * Compare n bytes:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */

#pragma	weak	memcmp = _memcmp

#include "synonyms.h"
#include <string.h>
#include <stddef.h>

int
memcmp(s1, s2, n)
#ifdef __STDC__
const VOID *s1;
const VOID *s2;
register size_t n;
{
	if (s1 != s2 && n != 0) {
		register const unsigned char *ps1 = s1;
		register const unsigned char *ps2 = s2;
#else
VOID *s1;
VOID *s2;
register size_t n;
{
	if (s1 != s2 && n != 0) {
		register unsigned char *ps1 = s1;
		register unsigned char *ps2 = s2;
#endif
		do {
			if (*ps1++ != *ps2++)
				return(ps1[-1] - ps2[-1]);
		} while (--n != 0);
	}
	return (NULL);
}
