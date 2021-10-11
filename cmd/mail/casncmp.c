/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)casncmp.c	1.4	92/07/16 SMI" 	/* SVr4.0 1.4	*/
/*
    NAME
	casncmp - compare strings ignoring case

    SYNOPSIS
	int casncmp(char *s1, char *s2, int n)

    DESCRIPTION
	Compare two strings ignoring case differences.
	Stop after n bytes or the trailing NUL.
*/
#include <ctype.h>
int
casncmp(s1, s2, n)
register char *s1, *s2;
register n;
{
	if (s1 == s2)
		return(0);
	while ((--n >= 0) && (tolower(*s1) == tolower(*s2))) {
		s2++;
		if (*s1++ == '\0')
			return (0);
	}
	return ((n < 0)? 0: (*s1 - *s2));
}
