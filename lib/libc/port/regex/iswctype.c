/*
 * XXX Remove this file when libw has been integrated into libc!!!!
 */

#ident	"@(#)iswctype.c 1.1	94/10/12 SMI"

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module was created for NLS on Nov 02 '87		*/


/*
 * Copyright (c) 1991 Sun Microsystems Inc.
 */

#ident	"@(#)iswctype.c	1.7	94/01/25 SMI"


/*
 *	__Xiswctype(c,x) returns true if 'c' is classified x
 */

#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <widec.h>
#ifdef _REENTRANT
#include <synch.h>

extern mutex_t _locale_lock;
#endif /* _REENTRANT */
#include "mtlibw.h"

#define	_mbyte	_ctype[520]

/* variable defined in libc */
extern	int	_lflag;
extern	struct	_wctype	*_wcptr[];


unsigned
__Xiswctype(c, x)
register wchar_t c;
register int	x;
{
	register int	s;
	int res;

	mutex_lock(&_locale_lock);
	if (_lflag == 0)
		if (_loadtab() < 0) {
			mutex_unlock(&_locale_lock);
			return (0);
		}
	if (iscodeset1(c))
		s = 0;
	else if (iscodeset2(c))
		s = 1;
	else if (iscodeset3(c))
		s = 2;
	else {
		mutex_unlock(&_locale_lock);
		return (0);
	}
	c = c & ~WCHAR_CSMASK;

	if (_wcptr[s] == 0 || _wcptr[s]->index == 0 ||
	    c < _wcptr[s]->tmin || c > _wcptr[s]->tmax)
		res = 0;
	else
		res = x & _wcptr[s]->type[_wcptr[s]->index[c-_wcptr[s]->tmin]];
	mutex_unlock(&_locale_lock);
	return (res);
}

int
__iswalpha(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _U|_L));
	else
		return (isalpha(c));
}

int
__iswupper(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _U));
	else
		return (isupper(c));
}

int
__iswlower(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _L));
	else
		return (islower(c));
}


int
__iswdigit(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _N));
	else
		return (isdigit(c));
}

int
__iswxdigit(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _X));
	else
		return (isxdigit(c));
}

int
__iswalnum(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _U|_L|_N));
	else
		return (isalnum(c));
}

int
__iswspace(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _S));
	else
		return (isspace(c));
}

int
__iswpunct(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _P));
	else
		return (ispunct(c));
}

int
__iswprint(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c,
		    _P|_U|_L|_N|_B|_E1|_E2|_E3|_E4|_E5|_E6));
	else
		return (isprint(c));
}

int
__iswgraph(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c,
		    _P|_U|_L|_N|_E1|_E2|_E3|_E4|_E5|_E6));
	else
		return (isgraph(c));
}

int
__iswcntrl(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _C));
	else
		return (iscntrl(c));
}

/*
 *	Note that iswascii() is still implemented as a macro
 *	#pragma weak iswascii = _iswascii
 *	int
 *	_iswascii(wint_t c)
 *	{
 *		return (isascii(c));
 *	}
 */

int
__isphonogram(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _E1));
	else
		return (0);
}

int
__isideogram(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _E2));
	else
		return (0);
}

int
__isenglish(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _E3));
	else
		return (0);
}

int
__isnumber(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _E4));
	else
		return (0);
}

int
__isspecial(wint_t c)
{
	if (c > 255)
		return (__Xiswctype((wchar_t) c, _E5));
	else
		return (0);
}

int
___iswctype(wint_t wc, wctype_t charclass)
{
	if (wc > 255)
		return (__Xiswctype((wchar_t) wc, charclass));
	else
		return ((__ctype + 1)[wc] & charclass);
}

wctype_t
___wctype(const char *charclass)
{
	if (strcmp(charclass, "alnum") == 0)
		return (_U|_L|_N);
	else
	if (strcmp(charclass, "alpha") == 0)
		return (_U|_L);
	else
	if (strcmp(charclass, "blank") == 0)
		return (_B);
	else
	if (strcmp(charclass, "cntrl") == 0)
		return (_C);
	else
	if (strcmp(charclass, "digit") == 0)
		return (_N);
	else
	if (strcmp(charclass, "graph") == 0)
		return (_P|_U|_L|_N|_E1|_E2|_E3|_E4|_E5|_E6);
	else
	if (strcmp(charclass, "lower") == 0)
		return (_L);
	else
	if (strcmp(charclass, "print") == 0)
		return (_P|_U|_L|_N|_B|_E1|_E2|_E3|_E4|_E5|_E6);
	else
	if (strcmp(charclass, "punct") == 0)
		return (_P);
	else
	if (strcmp(charclass, "space") == 0)
		return (_S);
	else
	if (strcmp(charclass, "upper") == 0)
		return (_U);
	else
	if (strcmp(charclass, "xdigit") == 0)
		return (_X);
	else
		return (0);
}
