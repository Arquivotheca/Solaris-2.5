/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wsncpy.c	1.6	93/12/02 SMI"  /* from JAE2.0 1.0 */

/*
 * Copy s2 to s1, truncating or null-padding to always copy n characters.
 * Return s1.
 */

#include <stdlib.h>
#include <wchar.h>

#pragma weak wcsncpy = _wcsncpy
#pragma weak wsncpy = _wsncpy

wchar_t *
_wcsncpy(wchar_t *s1, const wchar_t *s2, size_t n)
{
	register wchar_t *os1 = s1;

	n++;
	while ((--n > 0) && ((*s1++ = *s2++) != 0))
		;
	if (n > 0)
		while (--n > 0)
			*s1++ = 0;
	return (os1);
}

wchar_t *
_wsncpy(wchar_t *s1, const wchar_t *s2, size_t n)
{
	return (wcsncpy(s1, s2, n));
}
