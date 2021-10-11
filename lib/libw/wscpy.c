/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)wscpy.c	1.6	93/12/01 SMI"  	/* from JAE2.0 1.0 */

/*
 * Copy string s2 to s1. S1 must be large enough.
 * Return s1.
 */

#include <stdlib.h>
#include <wchar.h>

#pragma weak wcscpy = _wcscpy
#pragma weak wscpy = _wscpy

wchar_t *
_wcscpy(wchar_t *s1, const wchar_t *s2)
{
	register wchar_t *os1 = s1;

	while (*s1++ = *s2++)
		;
	return (os1);
}

wchar_t *
_wscpy(wchar_t *s1, const wchar_t *s2)
{
	return (wcscpy(s1, s2));
}
