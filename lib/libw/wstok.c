/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wstok.c	1.12	93/12/02 SMI"	/* from JAE2.0 1.0 */

/*
 * uses wcspbrk and wcsspn to break string into tokens on
 * sequentially subsequent calls. returns WNULL when no
 * non-separator characters remain.
 * 'subsequent' calls are calls with first argument WNULL.
 */

#ifndef WNULL
#define	WNULL	(wchar_t *)0
#endif

#include <stdlib.h>
#include <wchar.h>
#include <thread.h>
#include "mtlibw.h"

#pragma weak wcstok = _wcstok
#pragma weak wstok = _wstok

static wchar_t **
_tsdget(thread_key_t *key, int size)
{
	wchar_t *loc = 0;

	if (_thr_getspecific(*key, &loc) != 0) {
		if (_thr_keycreate(key, free) != 0) {
			return (0);
		}
	}
	if (!loc) {
		if (_thr_setspecific(*key, (void **)(loc = malloc(size)))
				!= 0) {
			if (loc)
				(void) free(loc);
			return (0);
		}
	}
	return ((wchar_t **) loc);
}

wchar_t *
_wcstok(wchar_t *string, const wchar_t *sepset)
{
	static wchar_t *statlasts;
	static thread_key_t key = 0;
	register wchar_t *q, *r;
	wchar_t **lasts = (_thr_main() ? &statlasts
			    : _tsdget(&key, sizeof (wchar_t *)));

	/* first or subsequent call */
	if ((string == WNULL && (string = *lasts) == 0) ||
	    ((q = string + wcsspn(string, sepset)) && *q == L'\0'))
		return (WNULL);

	/* sepset becomes next string after separator */
	if ((r = wcspbrk(q, sepset)) == WNULL)	/* move past token */
		*lasts = 0;	/* indicate this is last token */
	else {
		*r = L'\0';
		*lasts = r+1;
	}
	return (q);
}

wchar_t *
_wstok(wchar_t *string, const wchar_t *sepset)
{
	return (wcstok(string, sepset));
}
