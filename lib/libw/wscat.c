/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wscat.c	1.6	93/12/01 SMI" 	/* from JAE2.0 1.0 */

/*
 * Concatenate s2 on the end of s1. S1's space must be large enough.
 * return s1.
 */

#include <stdlib.h>
#include <wchar.h>

#pragma weak wcscat = _wcscat
#pragma weak wscat = _wscat

wchar_t *
_wcscat(wchar_t *s1, const wchar_t *s2)
{
	register wchar_t *os1 = s1;

	while (*s1++) /* find end of s1 */
		;
	--s1;
	while (*s1++ = *s2++) /* copy s2 to s1 */
			;
	return (os1);
}

wchar_t *
_wscat(wchar_t *s1, const wchar_t *s2)
{
	return (wcscat(s1, s2));
}
