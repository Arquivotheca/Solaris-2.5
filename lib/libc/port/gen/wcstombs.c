/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)wcstombs.c	1.10	94/02/24 SMI"	/* SVr4.0 1.4	*/

/*LINTLIBRARY*/

#include "synonyms.h"
#include <stdlib.h>
#include <limits.h>
#include <string.h>

size_t
wcstombs(char *s, const wchar_t *pwcs, size_t n)
{
	int	val = 0;
	int	total = 0;
	char	temp[MB_LEN_MAX];
	int	i;

	for (;;) {
		if (s && (total == n))
			break;
		if (*pwcs == 0) {
			if (s)
				*s = '\0';
			break;
		}
		if ((val = wctomb(temp, *pwcs++)) == -1)
			return (val);
		total += val;
		if (s && (total > n)) {
			total -= val;
			break;
		}
		if (s != 0) {
			for (i = 0; i < val; i++)
				*s++ = temp[i];
		}
	}
	return (total);
}
