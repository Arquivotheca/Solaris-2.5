/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mbstowcs.c	1.9	94/02/10 SMI"	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/

#include "synonyms.h"
#include <stdlib.h>

size_t
mbstowcs(wchar_t *pwcs, const char *s, size_t n)
{
	int	val;
	size_t	i, count;

	if (pwcs == 0)
		count = strlen(s);
	else
		count = n;

	for (i = 0; i < count; i++) {
		if ((val = mbtowc(pwcs, s, MB_CUR_MAX)) == -1)
			return (val);
		if (val == 0)
			break;
		s += val;
		if (pwcs != NULL)
			pwcs++;
	}
	return (i);
}
