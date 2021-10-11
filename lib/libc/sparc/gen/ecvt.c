/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ecvt.c	1.7	92/07/14 SMI"	/* SVr4.0 1.16	*/
/*LINTLIBRARY*/
/*
 *	ecvt converts to decimal
 *	the number of digits is specified by ndigit
 *	decpt is set to the position of the decimal point
 *	sign is set to 0 for positive, 1 for negative
 *
 */
#ifndef DSHLIB
#ifdef __STDC__
	#pragma weak ecvt = _ecvt
	#pragma weak fcvt = _fcvt
	#pragma weak qecvt = _qecvt
	#pragma weak qfcvt = _qfcvt
	#pragma weak qgcvt = _qgcvt
#endif
#endif

#include "synonyms.h"
#include <floatingpoint.h>


static char *buf = 0;

char *
ecvt(number, ndigits, decpt, sign)
	double	number;
	int	ndigits;
	int	*decpt;
	int	*sign;
{

	if (buf == 0) 
		buf = (char *) malloc ((DECIMAL_STRING_LENGTH));

	return (econvert(number, ndigits, decpt, sign, buf));
}

char *
fcvt(number, ndigits, decpt, sign)
	double	number;
	int	ndigits;
	int	*decpt;
	int	*sign;
{

	if (buf == 0) 
		buf = (char *) malloc (DECIMAL_STRING_LENGTH);

	return (fconvert(number, ndigits, decpt, sign, buf));
}

char           *
qecvt(number, ndigits, decpt, sign)
	long double	number;
	int             ndigits;
	int		*decpt;
	int		*sign;
{

	if (buf == 0) 
		buf = (char *) malloc (DECIMAL_STRING_LENGTH);

	return (qeconvert(&number, ndigits, decpt, sign, buf));
}

char           *
qfcvt(number, ndigits, decpt, sign)
	long double	number;
	int             ndigits;
	int		*decpt;
	int		*sign;
{

	if (buf == 0) 
		buf = (char *) malloc (DECIMAL_STRING_LENGTH);

	return (qfconvert(&number, ndigits, decpt, sign, buf));
}

char           *
qgcvt(number, ndigits, buffer)
	long double	number;
	int             ndigits;
	char		*buffer;
{
	return (qgconvert(&number, ndigits, 0, buffer));
}
