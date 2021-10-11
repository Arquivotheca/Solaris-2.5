/*
 * XXX Remove this file when libw has been integrated into libc!!!!
 */

#ident	"@(#)wschr.c 1.1	94/10/12 SMI"

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wschr.c	1.6	93/12/01 SMI"  /* from JAE2.0 1.0 */

/*
 * Return the ptr in sp at which the character c appears;
 * Null if not found.
 */

#include <stdlib.h>
#include <wchar.h>


#ifndef WNULL
#define	WNULL	(wchar_t *)0
#endif

wchar_t *
__wcschr(const wchar_t *sp, wchar_t c)
{
	do {
		if (*sp == c)
			return ((wchar_t *)sp); /* found c in sp */
	} while (*sp++);
	return (WNULL); /* c not found */
}

wchar_t *
__wschr(const wchar_t *sp, wchar_t c)
{
	return (__wcschr(sp, c));
}
