/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 * Copyright (c) 1988 by Nihon Sun Microsystems K.K.
 */

#ident	"@(#)wcstoul.c	1.3	94/06/27 SMI"

/*LINTLIBRARY*/
#include <limits.h>
#include <errno.h>
#include <wchar.h>
#define	DIGIT(x)	(iswdigit(x) ? (x) - L'0' :\
			iswlower(x) ? (x) + 10 - L'a' : (x) + 10 - L'A')
#define	MBASE	(L'z' - L'a' + 1 + 10)

#pragma weak wcstoul = _wcstoul

unsigned long
_wcstoul(const wchar_t *str, wchar_t **ptr, int base)
{
	register unsigned long val;
	register wchar_t c;
	int xx, neg = 0;
	int multmax;
	int	n;

	if (ptr != (wchar_t **)0)
		*ptr = (wchar_t *)str; /* in case no number is formed */
	if (base < 0 || base > MBASE) {
		errno = EINVAL;
		return (0); /* base is invalid -- should be a fatal error */
	}

	if (!iswalnum(c = *str)) {
		while (iswspace(c)) {
			c = *++str;
		}
		switch (c) {
		case L'-':
			neg++;
		case L'+': /* fall-through */
			c = *++str;
		}
	}
	if (base == 0)
		if (c != L'0')
			base = 10;
		else if (str[1] == L'x' || str[1] == L'X')
			base = 16;
		else
			base = 8;
	/*
	 * for any base > 10, the digits incrementally following
	 *	9 are assumed to be "abc...z" or "ABC...Z"
	 */
	if (!iswalnum(c) || (xx = DIGIT(c)) >= base) {
		errno = EINVAL;
		return (0); /* no number formed */
	}
	if (base == 16 && c == L'0' && iswxdigit(str[2]) &&
	    (str[1] == L'x' || str[1] == L'X')) {
		c = *(str += 2); /* skip over leading "0x" or "0X" */
	}
	multmax = ULONG_MAX / base;
	for (val = DIGIT(c);
		iswalnum(c = *++str) && (xx = DIGIT(c)) < base; NULL) {
		/* accumulate neg avoids surprises near MAXLONG */
		if (val > multmax)
			goto overflow;
		val *= base;
		if (ULONG_MAX - val < xx)
			goto overflow;
		val += xx;
	}
	if (ptr != (wchar_t **)0)
		*ptr = (wchar_t *)str;
	return (neg ? -val : val);

overflow:
	while (iswalnum(c = *++str) && (xx = DIGIT(c)) < base);

	if (ptr != (wchar_t **)0)
		*ptr = (wchar_t *)str;
	errno = ERANGE;
	return (ULONG_MAX);
}
