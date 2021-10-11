/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strcmp.c	1.7	92/07/14 SMI"	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/
/*
 * Compare strings:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */

#include "synonyms.h"
#include <string.h>

int
strcmp(s1, s2)
#ifdef __STDC__
register const char *s1;
register const char *s2;
#else
register char *s1;
register char *s2;
#endif
{

	if(s1 == s2)
		return(0);
	while(*s1 == *s2++)
		if(*s1++ == '\0')
			return(0);
	return(*s1 - s2[-1]);
}
