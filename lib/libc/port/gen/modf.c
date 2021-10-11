/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)modf.c	1.10	93/11/12 SMI"	/* SVr4.0 1.10.1.5	*/

/*LINTLIBRARY*/
/*
 * modf(value, iptr) returns the signed fractional part of value
 * and stores the integer part indirectly through iptr.
 *
 */

#ifdef __STDC__
#pragma weak modf = _modf
#endif

#include "synonyms.h"
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <values.h>
#include <nan.h>


double
modf(value, iptr)
double value; /* don't declare register, because of Is NAN orINF! */
register double *iptr;
{
	register double absvalue;

	if (IsNANorINF(value)) { /* check for NAN or INF (IEEE only) */
		*iptr = value;
		if (IsINF(value)) { /* infinity */
			return (value >= 0.0 ? 0.0 : -0.0); /* X3J11.1 spec. */
		}
		else { /* NaN */
			return (value); /* per XPG3 + X3J11.1 spec. */
		}
	}
	if ((absvalue = (value >= 0.0) ? value : -value) >= MAXPOWTWO)
		*iptr = value; /* it must be an integer */
	else {
		*iptr = absvalue + MAXPOWTWO; /* shift fraction off right */
		*iptr -= MAXPOWTWO; /* shift back without fraction */
		while (*iptr > absvalue) /* above arithmetic might round */
			*iptr -= 1.0; /* test again just to be sure */
		if (value < 0.0)
			*iptr = -*iptr;
	}
	return (value - *iptr); /* signed fractional part */
}
