/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wspbrk.c	1.6	93/12/02 SMI"	/* from JAE2.0 1.0 */

/*
 * Return ptr to first occurance of any character from 'brkset'
 * in the wchar_t array 'string'; NULL if none exists.
 */

#include <stdlib.h>
#include <wchar.h>

#ifndef WNULL
#define	 WNULL	(wchar_t *)0
#endif

#pragma weak wcspbrk = _wcspbrk
#pragma weak wspbrk = _wspbrk

wchar_t *
_wcspbrk(const wchar_t *string, const wchar_t *brkset)
{
	const wchar_t *p;

	do {
		for (p = brkset; *p && *p != *string; ++p)
			;
		if (*p)
			return ((wchar_t *)string);
	} while (*string++);
	return (WNULL);
}

wchar_t *
_wspbrk(const wchar_t *string, const wchar_t *brkset)
{
	return (wcspbrk(string, brkset));
}
