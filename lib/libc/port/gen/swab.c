/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)swab.c	1.6	92/07/14 SMI"	/* SVr4.0 1.10	*/

/*LINTLIBRARY*/
/*
 * Swab bytes
 */

#ifdef __STDC__
	#pragma weak swab = _swab
#endif

#include "synonyms.h"
#include <stdlib.h>

#define	STEP	temp = *from++,*to++ = *from++,*to++ = temp

void
swab(from, to, n)
#ifdef __STDC__
	register const char *from;
#else
	register  char *from;
#endif
	register char *to;
	register int n;
{
	register char temp;

	if (n <= 1)
		return;
	n >>= 1; n++;
	/* round to multiple of 8 */
	while ((--n) & 07)
		STEP;
	n >>= 3;
	while (--n >= 0) {
		STEP; STEP; STEP; STEP;
		STEP; STEP; STEP; STEP;
	}
}
