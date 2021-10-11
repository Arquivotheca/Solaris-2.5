/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)wcstombs.c 1.1 93/05/05 SMI"
 
/*LINTLIBRARY*/

#include <widec.h>
#include "synonyms.h"
#include <stdlib.h>
#include <limits.h>
#include <string.h>

size_t
_curs_wcstombs(s, pwcs, n)
char *s;
const wchar_t *pwcs;
size_t n;
{
	int	val = 0;
	int	total = 0;
	char	temp[MB_LEN_MAX];
	int	i;

	for (;;) {
		if (*pwcs == 0) {
			*s = '\0';
			break;
		}
		if ((val = _curs_wctomb(temp, *pwcs++)) == -1)
			return(val);
		if ((total += val) > n) {
			total -= val;
			break;
		}
		for (i=0; i<val; i++)
			*s++ = temp[i];
	}
	return(total);
}
