/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tmpfile.c	1.9	92/09/05 SMI"	/* SVr4.0 1.12	*/

/*LINTLIBRARY*/
/*
 *	tmpfile - return a pointer to an update file that can be
 *		used for scratch. The file will automatically
 *		go away if the program using it terminates.
 */
#include "synonyms.h"
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

extern int unlink();

static char seed[] = { 'a', 'a', 'a', '\0' };
#ifdef _REENTRANT
static mutex_t seed_lk = DEFAULTMUTEX;
#endif _REENTRANT

#define XS "\bXXXXXX"		/* a '\b' character is prepended to this
				* string to avoid conflicts with names
				* generated by tmpnam() */

FILE *
tmpfile()
{
	char	tfname[L_tmpnam];
	register FILE	*p;
	register char	*q;

	(void) strcpy(tfname, P_tmpdir);
	_mutex_lock(&seed_lk);
	(void) strcat(tfname, seed);
	(void) strcat(tfname, XS);

	q = seed;
	while(*q == 'z')
		*q++ = 'a';
	if (*q != '\0')
		++*q;
	_mutex_unlock(&seed_lk);

	(void) mktemp(tfname);
	if((p = fopen(tfname, "w+")) == NULL)
		return NULL;
	else
		(void) unlink(tfname);
	return(p);
}
