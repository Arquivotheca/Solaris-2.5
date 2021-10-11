/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tolower.c	1.6	92/07/14 SMI"	/* SVr4.0 1.7	*/

/*
 * If arg is upper-case, return lower-case, otherwise return arg.
 * International version
 */

#include "synonyms.h"
#include <ctype.h>

int
tolower(c)
register int c;
{
	if (isupper(c))
		c = _tolower(c);
	return(c);
}

