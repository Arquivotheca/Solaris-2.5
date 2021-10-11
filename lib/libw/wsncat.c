/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wsncat.c	1.6	93/12/02 SMI" /* from JAE2.0 1.0 */

/*
 * Concatenate s2 on the end of s1. S1's space must be large enough.
 * At most n characters are moved.
 * return s1.
 */

#include <stdlib.h>
#include <wchar.h>

#pragma weak wcsncat = _wcsncat
#pragma weak wsncat = _wsncat

wchar_t *
_wcsncat(wchar_t *s1, const wchar_t *s2, size_t n)
{
	register wchar_t *os1 = s1;

	while (*s1++) /* find end of s1 */
		;
	++n;
	--s1;
	while (*s1++ = *s2++) /* copy s2 to s1 */
		if (--n == 0) {  /* at most n chars */
			*--s1 = 0;
			break;
		}
	return (os1);
}

wchar_t *
_wsncat(wchar_t *s1, const wchar_t *s2, size_t n)
{
	return (wcsncat(s1, s2, n));
}
