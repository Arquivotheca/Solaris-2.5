/*
 * Copyright (c) 1991, Sun Microsystems Inc.
 */
#ident	"@(#)wsncasecmp.c	1.2	92/09/25 SMI"

/*
 * Compare strings ignoring case difference.
 *	returns:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 * All letters are converted to the lowercase and compared.
 */

#include <stdlib.h>
#include <widec.h>
extern wint_t	_towlower(wint_t);

#pragma weak wsncasecmp = _wsncasecmp
int
_wsncasecmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
	if (s1 == s2)
		return (0);

	n++;
	while (--n > 0 && _towlower(*s1) == _towlower(*s2++))
		if (*s1++ == 0)
			return (0);
	return ((n==0) ? 0 : (_towlower(*s1) - _towlower(*--s2)));
}
