/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Nov 02 '87		*/


/*
 * Copyright (c) 1991 Sun Microsystems Inc.
 */

#ident	"@(#)trwctype.c	1.10	93/12/11 SMI"

/*
 *	_trwctype(c) converts lower/upper-case character to the opposite
 *	case, if there is any.
 */

#include <ctype.h>
#include <stdlib.h>
#include <widec.h>
#include <wctype.h>
#ifdef _REENTRANT
#include <synch.h>

extern mutex_t _locale_lock;
#endif /* _REENTRANT */
#include "mtlibw.h"

#define	_mbyte	_ctype[520]

extern	int	_lflag;
extern	struct	_wctype *_wcptr[];

wchar_t
_trwctype(c, x)
register wchar_t c;
register int	x;
{
	register int	s;
	register wchar_t w;
	wchar_t res;

	mutex_lock(&_locale_lock);
	if (_lflag == 0)
		_loadtab();
	if (iscodeset1(c))
		s = 0;
	else if (iscodeset2(c))
		s = 1;
	else if (iscodeset3(c))
		s = 2;
	else {
		mutex_unlock(&_locale_lock);
		return (c);
	}
	w = c & (WCHAR_S_MASK | WCHAR_S_MASK << WCHAR_SHIFT |
		    WCHAR_S_MASK << (WCHAR_SHIFT * 2));
	if (_wcptr[s] == 0 || _wcptr[s]->code == 0 ||
	    w < _wcptr[s]->cmin || w > _wcptr[s]->cmax) {
		mutex_unlock(&_locale_lock);
		return (c);
	}
	if (!_iswctype(c, x)) {
		mutex_unlock(&_locale_lock);
		return (c);
	}
	w = _wcptr[s]->code[w - _wcptr[s]->cmin];
	/* The next line is for wchrtbl's data formatting as of 3/21/92 */
	w = ((w & 0x7f) | ((w & 0x7f00) >> 1) | ((w & 0x7f0000) >> 2));
	res = (w | (s == 0 ? WCHAR_CS1 : s == 1 ? WCHAR_CS2 : WCHAR_CS3));
	mutex_unlock(&_locale_lock);
	return (res);
}


#pragma weak towupper = _towupper
wint_t
_towupper(wint_t c)
{
	if (c > 255)
		return (_trwctype((wchar_t)c, _L));
	else
		return (islower(c) ? toupper(c) : c);

}

#pragma weak towlower = _towlower
wint_t
_towlower(wint_t c)
{
	if (c > 255)
		return (_trwctype((wchar_t)c, _U));
	else
		return (isupper(c) ? tolower(c) : c);

}
