/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)gcvt.c	1.8	92/07/14 SMI"	/* SVr4.0 1.13	*/

/*LINTLIBRARY*/
/*
 * gcvt  - Floating output conversion to
 *
 * pleasant-looking string.
 */

#ifdef __STDC__
	#pragma weak gcvt = _gcvt
#endif

#include "synonyms.h"
#include <floatingpoint.h>

char *
gcvt(number, ndigits, buf)
double	number;
int	ndigits;
char	*buf;
{
	return (gconvert(number, ndigits, 0, buf));
}
