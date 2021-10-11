/*	Copyright (c) 1992 AT&T	and SMI */
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tmpnam_r.c	1.0	92/01/02 SMI"	/* SVr4.0 1.11	*/

/*LINTLIBRARY*/
#ifdef __STDC__
#pragma weak tmpnam_r = _tmpnam_r
#endif
#include "synonyms.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

extern char *mktemp(), *strcpy(), *strcat();
static char seed[] = { 'a', 'a', 'a', '\0' };

#ifdef _REENTRANT
static mutex_t seed_lk = DEFAULTMUTEX;
#endif _REENTRANT

char *
_tmpnam_r(s)
char	*s;
{
	register char *p = s, *q;

	if (!s)
                return(NULL);

	(void) strcpy(p, P_tmpdir);
	_mutex_lock(&seed_lk);
	(void) strcat(p, seed);
	(void) strcat(p, "XXXXXX");

	q = seed;
	while(*q == 'z')
		*q++ = 'a';
	if (*q != '\0')
		++*q;
	_mutex_unlock(&seed_lk);
	(void) mktemp(p);
	return(p);
}
