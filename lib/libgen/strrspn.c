/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strrspn.c	1.6	92/07/14 SMI"	/* SVr4.0 1.1.2.2	*/

#ifdef __STDC__
	#pragma weak strrspn = _strrspn
#endif
#include "synonyms.h"

/*
	Trim trailing characters from a string.
	Returns pointer to the first character in the string
	to be trimmed (tc).
*/

#include	<string.h>


char *
strrspn( string, tc )
#ifdef __STDC__
const char	*string;
const char	*tc;	/* characters to trim */
#else
char	*string;
char	*tc;	/* characters to trim */
#endif
{
	char	*p;

	p = (char *)string + strlen( string );  
	while( p != (char *)string )
		if( !strchr( tc, *--p ) )
			return  ++p;

	return  p;
}
