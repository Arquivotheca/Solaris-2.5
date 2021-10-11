/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)strtoull.c	1.1	91/08/19 SMI"

/*LINTLIBRARY*/

#ifdef __STDC__
#pragma weak strtoull = _strtoull
#endif

#include "synonyms.h"
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#define	DIGIT(x)	(isdigit(x) ? (x) - '0' : \
			    islower(x) ? (x) + 10 - 'a' : (x) + 10 - 'A')
#define	MBASE	('z' - 'a' + 1 + 10)

/* The following macro is a local version of isalnum() which limits
 * alphabetic characters to the ranges a-z and A-Z; locale dependent
 * characters will not return 1. The members of a-z and A-Z are
 * assumed to be in ascending order and contiguous
 */
#define	lisalnum(x)	(isdigit(x) || \
			    ((x) >= 'a' && (x) <= 'z') || \
			    ((x) >= 'A' && (x) <= 'Z'))

#ifdef __STDC__
unsigned long long
strtoull(register const char *str, char **nptr, int base)
#else
unsigned long long
strtoull(str, nptr, base)
register const char *str;
char **nptr;
int base;
#endif
{
	register unsigned long long val;
	register int c;
	int xx;
	unsigned long long	multmax;
	unsigned long long	ullong_max;
	int neg = 0;
#ifdef __STDC__
	const char 	**ptr = (const char **)nptr;

	if (ptr != (const char **)0)
#else
	char 	**ptr = (char **)nptr;

	if (ptr != (char **)0)
#endif
		*ptr = str; /* in case no number is formed */

	ullong_max = ULLONG_MAX;   /* from a local version of limits.h */

	if (base < 0 || base > MBASE || base == 1) {
		errno = EINVAL;
		return (0); /* base is invalid -- should be a fatal error */
	}
	if (!isalnum(c = *str)) {
		while (isspace(c))
			c = *++str;
		switch (c) {
		case '-':
			neg++;
			/* FALLTHROUGH */
		case '+':
			c = *++str;
		}
	}
	if (base == 0)
		if (c != '0')
			base = 10;
		else if (str[1] == 'x' || str[1] == 'X')
			base = 16;
		else
			base = 8;
	/*
	 * for any base > 10, the digits incrementally following
	 *	9 are assumed to be "abc...z" or "ABC...Z"
	 */
	if (!lisalnum(c) || (xx = DIGIT(c)) >= base)
		return (0); /* no number formed */
	if (base == 16 && c == '0' && (str[1] == 'x' || str[1] == 'X') &&
		isxdigit(str[2]))
		c = *(str += 2); /* skip over leading "0x" or "0X" */

	multmax = ullong_max / (unsigned long long)base;
	val = DIGIT(c);
	for (c = *++str; lisalnum(c) && (xx = DIGIT(c)) < base; ) {
		if (val > multmax)
			goto overflow;
		val *= base;
		if (ullong_max - val < xx)
			goto overflow;
		val += xx;
		c = *++str;
	}
#ifdef __STDC__
	if (ptr != (const char **)0)
#else
	if (ptr != (char **)0)
#endif
		*ptr = str;
	return (neg ? -val : val);

overflow:
	for (c = *++str; lisalnum(c) && (xx = DIGIT(c)) < base; (c = *++str));
#ifdef __STDC__
	if (ptr != (const char **)0)
#else
	if (ptr != (char **)0)
#endif
		*ptr = str;
	errno = ERANGE;
	return (ullong_max);
}
