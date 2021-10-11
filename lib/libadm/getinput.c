/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*LINTLIBRARY*/
#ident	"@(#)getinput.c	1.4	94/01/12 SMI"       /* SVr4.0 1.1 */

#include <stdio.h>
#include <ctype.h>
#include <limits.h>

getinput(s)
char *s;
{
	char input[MAX_INPUT];
	char *copy, *pt;

	if(!fgets(input, MAX_INPUT, stdin))
		return(1);

	copy = s;
	pt = input;

	while(isspace(*pt))
		++pt;

	while(*pt)
		*copy++ = *pt++;
	*copy = '\0';

	if(copy != s) {
		copy--;
		while(isspace(*copy))
			*copy-- = '\0';
	}
	return(0);
}
