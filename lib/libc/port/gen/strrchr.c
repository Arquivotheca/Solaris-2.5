/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strrchr.c	1.8	92/07/14 SMI"	/* SVr4.0 1.6	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#include "synonyms.h"
#include <string.h>
#include <stddef.h>

/*
 * Return the ptr in sp at which the character c last
 * appears; NULL if not found
*/

char *
strrchr(sp, c)
register int c;
#ifdef __STDC__
register const char *sp;
{
	register const char *r = NULL;
#else
register char *sp;
{
	register char *r = NULL;
#endif

	do {
		if(*sp == (char)c)
			r = sp;
	} while(*sp++);
	return((char *)r);
}
