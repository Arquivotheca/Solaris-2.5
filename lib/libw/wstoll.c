/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ident	"@(#)wstoll.c	1.1	93/04/06 SMI"


/*LINTLIBRARY*/
#include <widec.h>
#include <wctype.h>
#define	DIGIT(x)	(iswdigit(x) ? (x) - L'0' : \
			iswlower(x) ? (x) + 10 - L'a' : (x) + 10 - L'A')
#define	MBASE	(L'z' - L'a' + 1 + 10)

#pragma weak wstoll = _wstoll

long long
_wstoll(const wchar_t *str, wchar_t **ptr, int base)
{
	register long long val;
	register wchar_t c;
	int xx, neg = 0;
	int	n;

	if (ptr != (wchar_t **)0)
		*ptr = (wchar_t *)str; /* in case no number is formed */
	if (base < 0 || base > MBASE) {
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
		return (0); /* no number formed */
	}
	if (base == 16 && c == L'0' && iswxdigit(str[2]) &&
	    (str[1] == L'x' || str[1] == L'X')) {
		c = *(str += 2); /* skip over leading "0x" or "0X" */
	}
	val = -DIGIT(c);
	while (iswalnum(c = *++str) && (xx = DIGIT(c)) < base){
		/* accumulate neg avoids surprises near MAXLONG */
		val = base * val - xx;
	}
	if (ptr != (wchar_t **)0)
		*ptr = (wchar_t *)str;
	return (neg ? val : -val);
}

#undef watoll
/*
 * watoll() is defined as a macro in <widec.h> from 4/94.
 * It was a real function in libc in the earlier releases.
 * For binary comapatibility of the apps that were compiled
 * with earlier releases of Solaris 2.x which had watoll,
 * we provide watoll() as a function here as well.
 * PSARC opinion: PSARC/1993/121, approved on 3/11/93
 */
#pragma weak watoll = _watoll
long long
_watoll(const wchar_t *p)
{
	return (_wstoll(p, (wchar_t **)0, 10));
}
