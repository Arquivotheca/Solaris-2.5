/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wsrchr.c	1.6	93/12/02 SMI"	/* from JAE2.0 1.0 */

/*
 * Return the ptr in sp at which the character c last appears;
 * Null if not found.
 */

#include <stdlib.h>
#include <wchar.h>

#ifndef WNULL
#define	WNULL	(wchar_t *)0
#endif

#pragma weak wcsrchr = _wcsrchr
#pragma weak wsrchr = _wsrchr

wchar_t *
_wcsrchr(const wchar_t *sp, wchar_t c)
{
	const wchar_t *r = WNULL;

	do {
		if (*sp == c)
			r = sp; /* found c in sp */
	} while (*sp++);
	return ((wchar_t *)r);
}

wchar_t *
_wsrchr(const wchar_t *sp, wchar_t c)
{
	return (wcsrchr(sp, c));
}
