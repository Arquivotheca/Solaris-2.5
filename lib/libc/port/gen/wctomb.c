/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)wctomb.c	1.12	92/09/12 SMI"	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/
#ifdef __STDC__
#pragma weak wctomb = _wctomb
#endif

#include "synonyms.h"
#include <ctype.h>
#include <stdlib.h>
#include <widec.h>

int
_wctomb(char *s, wchar_t wchar)
{
	char *olds = s;
	register int size, index;
	unsigned char d;
	if (!s)
		return (0);
	if ((wchar & ~0xff)==0){ /* ASCII or control code. */
		*s++ = (char)wchar;
		return (1);
	}
	switch (wchar & WCHAR_CSMASK) {
	case WCHAR_CS1:
		size = eucw1;
		break;

	case WCHAR_CS2:
		*s++ = (char)SS2;
		size = eucw2;
		break;

	case WCHAR_CS3:
		*s++ = (char)SS3;
		size = eucw3;
		break;

	default:
		return (-1);
	}
	if ((index = size) <= 0)
		return (-1);
	while (index--) {
		d = wchar | 0200;
		wchar >>= WCHAR_SHIFT;
		if (iscntrl(d))
			return (-1);
		s[index] = d;
	}
	return (s + size - olds);
}
