/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module was created for NLS on Nov 02 '87		*/


/*
 * Copyright (c) 1991 Sun Microsystems Inc.
 */

#ident	"@(#)iswctype.c	1.11	95/01/28 SMI"


/*
 *	_iswctype(c,x) returns true if 'c' is classified x
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
_iswctype(c, x)
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

#pragma weak iswalpha = _iswalpha
int
_iswalpha(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _U|_L));
	else
		return (isalpha(c));
}

#pragma weak iswupper = _iswupper
int
_iswupper(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _U));
	else
		return (isupper(c));
}

#pragma weak iswlower = _iswlower
int
_iswlower(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _L));
	else
		return (islower(c));
}


#pragma weak iswdigit = _iswdigit
int
_iswdigit(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _N));
	else
		return (isdigit(c));
}

#pragma weak iswxdigit = _iswxdigit
int
_iswxdigit(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _X));
	else
		return (isxdigit(c));
}

#pragma weak iswalnum = _iswalnum
int
_iswalnum(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _U|_L|_N));
	else
		return (isalnum(c));
}

#pragma weak iswspace = _iswspace
int
_iswspace(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _S));
	else
		return (isspace(c));
}

#pragma weak iswpunct = _iswpunct
int
_iswpunct(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _P));
	else
		return (ispunct(c));
}

#pragma weak iswprint = _iswprint
int
_iswprint(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c,
		    _P|_U|_L|_N|_B|_E1|_E2|_E3|_E4|_E5|_E6));
	else
		return (isprint(c));
}

#pragma weak iswgraph = _iswgraph
int
_iswgraph(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c,
		    _P|_U|_L|_N|_E1|_E2|_E3|_E4|_E5|_E6));
	else
		return (isgraph(c));
}

#pragma weak iswcntrl = _iswcntrl
int
_iswcntrl(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _C));
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

#pragma weak isphonogram = _isphonogram
int
_isphonogram(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _E1));
	else
		return (0);
}

#pragma weak isideogram = _isideogram
int
_isideogram(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _E2));
	else
		return (0);
}

#pragma weak isenglish = _isenglish
int
_isenglish(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _E3));
	else
		return (0);
}

#pragma weak isnumber = _isnumber
int
_isnumber(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _E4));
	else
		return (0);
}

#pragma weak isspecial = _isspecial
int
_isspecial(wint_t c)
{
	if (c > 255)
		return (_iswctype((wchar_t) c, _E5));
	else
		return (0);
}

#pragma weak iswctype = __iswctype
int
__iswctype(wint_t wc, wctype_t charclass)
{
	if (wc > 255)
		return (_iswctype((wchar_t) wc, charclass));
	else
		return ((__ctype + 1)[wc] & charclass);
}

#pragma weak wctype = __wctype
wctype_t
__wctype(const char *charclass)
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
