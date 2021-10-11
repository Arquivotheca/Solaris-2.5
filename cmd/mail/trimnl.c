/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)trimnl.c	1.3	92/07/16 SMI" 	/* SVr4.0 1.3	*/

#include <string.h>

/*
 * Trim trailing newlines from string.
 */
int
trimnl(s)
register char 	*s;
{
	register char	*p;

	p = s + strlen(s) - 1;
	while ((*p == '\n') && (p >= s)) {
		*p-- = '\0';
	}
}
