/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wsncmp.c	1.6	93/12/02 SMI"  /* from JAE2.0 1.0 */

/*
 * Compare strings (at most n characters)
 * 	returns:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */

#include <stdlib.h>
#include <wchar.h>

#pragma weak wcsncmp = _wcsncmp
#pragma weak wsncmp = _wsncmp

int
_wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
	if (s1 == s2)
		return (0);

	n++;
	while (--n > 0 && *s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return ((n == 0) ? 0 : (*s1 - *--s2));
}

int
_wsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
	return (wcsncmp(s1, s2, n));
}
