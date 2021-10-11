/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)mbtowc.c	1.14	92/09/12 SMI"	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#pragma weak mbtowc = _mbtowc
#pragma weak mblen = _mblen
#endif

#include "synonyms.h"
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <widec.h>

int
_mbtowc(wchar_t *wchar, const char *s, size_t n)
{
	register int length;
	register int c;
	const char *olds = s;

	/*
	 * Bit assignment of wchar_t of SunOS 5.x is based on
	 * that of MNLS 4.0 but not exactly same.
	 * The biggest difference between the two systems
	 * is that MNLS 4.0 use a special bit assignment for
	 * single-byte (i.e. European) locales while SunOS 5.x
	 * doesn't.
	 * MNLS 4.0 does not clearly document what value should
	 * wchar_t has for a control character that belongs to
	 * the Control Character Set 1.  In both systems, the
	 * control characters don't change values when char
	 * is copied to wchar_t.
	 */


	if (s == (const char *)0)
		return (0);
	if (n == 0)
		return (-1);
	c = (unsigned char)*s++;

	if (wchar){ /* Do the real converion. */
		register wchar_t intcode;
		wchar_t mask;

		if (isascii(c)) {
			*wchar = c;
			return (c ? 1 : 0);
		}
		intcode = 0;
		if (c == SS2) {
			if ((length = eucw2)==0)
				goto lab1;
			mask = WCHAR_CS2;
			goto lab2;
		} else if (c == SS3) {
			if ((length = eucw3)==0)
				goto lab1;
			mask = WCHAR_CS3;
			goto lab2;
		}

lab1:
		if (iscntrl(c)) {
			*wchar = c;
			return (1);
		}
		length = eucw1 - 1;
		mask = WCHAR_CS1;
		intcode = c & WCHAR_S_MASK;
lab2:
		if (length + 1 > n || length < 0)
			return (-1);
		while (length--) {
			c = (unsigned char)*s++;
			if (isascii(c) || iscntrl(c)){
				errno = EILSEQ; /* AT&T doesn't set this. */
				return (-1); /* Illegal EUC sequence. */
			}
			intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
		}
		*wchar = intcode | mask;
	}else{ /* wchar==0; just calculate the length of the multibyte char */
		/*
		 * Note that mbtowc(0, s, n) does more than looking at the
		 * first byte of s.  It returns the length of s only
		 * if s can be really converted to a valid wchar_t value.
		 * It returns -1 otherwise.
		 */
		if (isascii(c)) {
			return (c ? 1 : 0);
		}
		if (c == SS2) {
			if ((length = eucw2)==0)
				goto lab3;
			goto lab4;
		} else if (c == SS3) {
			if ((length = eucw3)==0)
				goto lab3;
			goto lab4;
		}
lab3:
		if (iscntrl(c)) {
			return (1);
		}
		length = eucw1 - 1;
lab4:
		if (length + 1 > n || length < 0)
			return (-1);
		while (length--) {
			c = (unsigned char)*s++;
			if (isascii(c) || iscntrl(c)){
				errno = EILSEQ; /* AT&T doesn't set this. */
				return (-1); /* Illegal EUC sequence. */
			}
		}
	}
	return (s - olds);
}

#undef mblen

int
_mblen(const char *s, size_t n)
{
	return (_mbtowc((wchar_t *)0, s, n));
}
