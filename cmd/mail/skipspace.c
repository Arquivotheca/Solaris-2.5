/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)skipspace.c	1.5	92/07/21 SMI"
	 	/* SVr4.0 1.5	*/
#include <ctype.h>

/*
 * Return pointer to first non-blank character in p
 */
char *
skipspace(p)
register char	*p;
{
	while(*p && isspace(*p)) {
		p++;
	}
	return (p);
}
