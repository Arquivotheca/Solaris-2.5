/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strtrns.c	1.6	92/07/14 SMI"	/* SVr4.0 1.1.2.2	*/

#ifdef __STDC__
	#pragma weak strtrns = _strtrns
#endif
#include "synonyms.h"

/*
	Copy `str' to `result' replacing any character found
	in both `str' and `old' with the corresponding character from `new'.
	Return `result'.
*/

char *
strtrns(str,old,new,result)
#ifdef __STDC__
register const char *str;
const char *old, *new; 
#else
register char *str;
char *old, *new; 
#endif
char *result;
{
	register char *r;
#ifdef __STDC__
	register const char *o;
#else
	register char *o;
#endif
	for (r = result; *r = *str++; r++)
		for (o = old; *o; )
			if (*r == *o++) {
				*r = new[o - old -1];
				break;
			}
	return(result);
}
