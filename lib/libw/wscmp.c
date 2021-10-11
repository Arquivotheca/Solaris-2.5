/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wscmp.c	1.6	93/12/01 SMI"  /* from JAE2.0 1.0 */

/*
 * Compare strings:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */

#include <stdlib.h>
#include <wchar.h>

#pragma weak wcscmp = _wcscmp
#pragma weak wscmp = _wscmp

int
_wcscmp(const wchar_t *s1, const wchar_t *s2)
{
	if (s1 == s2)
		return (0);

	while (*s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return (*s1 - *--s2);
}

int
_wscmp(const wchar_t *s1, const wchar_t *s2)
{
	return (wcscmp(s1, s2));
}
