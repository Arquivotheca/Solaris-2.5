/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strspn.c	1.7	92/07/14 SMI"	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/
/*
 * Return the number of characters in the maximum leading segment
 * of string which consists solely of characters from charset.
 */

#include "synonyms.h"
#include <string.h>

size_t
strspn(string, charset)
#ifdef __STDC__
const char *string;
const char *charset;
{
	register const char *p, *q;
#else
char *string;
char *charset;
{
	register char *p, *q;
#endif

	for(q=string; *q != '\0'; ++q) {
		for(p=charset; *p != '\0' && *p != *q; ++p)
			;
		if(*p == '\0')
			break;
	}
	return(q-string);
}
