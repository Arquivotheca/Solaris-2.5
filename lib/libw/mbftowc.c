/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mbftowc.c	1.7	92/07/14 SMI"	/* SVr4.0 1.2	*/

#include <ctype.h>
#include <stdlib.h>
#include <widec.h>
#include <euc.h>
/* returns number of bytes read by (*f)() */
int _mbftowc(s, wchar, f, peekc)
char *s;
wchar_t *wchar;
int (*f)();
int *peekc;
{
	register int length;
	register wchar_t intcode;
	register c;
	char *olds = s;
	wchar_t mask;

	if ((c = (*f)()) < 0)
		return (0);
	*s++ = c;
	if (isascii(c)) {
		*wchar = c;
		return (1);
	}
	intcode = 0;
	if (c == SS2) {
		if (!(length = eucw2))
			goto lab1;
		mask = WCHAR_CS2;
		goto lab2;
	} else if (c == SS3) {
		if (!(length = eucw3))
			goto lab1;
		mask = WCHAR_CS3;
		goto lab2;
	}

lab1:
	if (iscntrl(c)) {
		*wchar = c;
		return (1);
	}
	mask = WCHAR_CS1;
	length = eucw1 - 1;
	intcode = c & WCHAR_S_MASK;
lab2:
	if (length < 0)
		return (-1);

	while (length--) {
		*s++ = c = (*f)();
		if (isascii(c) || iscntrl(c)) { /* Illegal EUC sequence. */
			if (c >= 0)
				*peekc = c;
			--s;
			return (-(s - olds));
		}
		intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
	}
	*wchar = intcode | mask;
	return (s - olds);
}
