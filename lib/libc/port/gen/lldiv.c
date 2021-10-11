/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)lldiv.c	1.1	91/08/19 SMI"

/*LINTLIBRARY*/

#ifdef __STDC__
#pragma weak lldiv = _lldiv
#endif

#include "synonyms.h"
#include <stdlib.h>

#ifdef __STDC__
lldiv_t
lldiv(register long long numer, register long long denom)
#else
lldiv_t
lldiv(numer, denom)
register long long numer;
register long long denom;
#endif
{
	lldiv_t	sd;

	if (numer >= 0 && denom < 0) {
		numer = -numer;
		sd.quot = -(numer / denom);
		sd.rem  = -(numer % denom);
	} else if (numer < 0 && denom > 0) {
		denom = -denom;
		sd.quot = -(numer / denom);
		sd.rem  = numer % denom;
	} else {
		sd.quot = numer / denom;
		sd.rem  = numer % denom;
	}
	return (sd);
}
