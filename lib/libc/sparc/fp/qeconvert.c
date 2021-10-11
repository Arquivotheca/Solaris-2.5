/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)qeconvert.c	1.4	92/07/14 SMI"

#ifdef __STDC__  
	#pragma weak qeconvert = _qeconvert 
	#pragma weak qfconvert = _qfconvert 
	#pragma weak qgconvert = _qgconvert 
#endif

#include "synonyms.h"
#include "base_conversion.h"

extern enum fp_direction_type _QgetRD(); 

char           *
qeconvert(arg, ndigits, decpt, sign, buf)
	quadruple      *arg;
	int             ndigits, *decpt, *sign;
	char           *buf;
{
	decimal_mode    dm;
	decimal_record  dr;
	fp_exception_field_type ef;
	int             i;

	dm.rd = _QgetRD();	/* Rounding direction. */
	dm.df = floating_form;	/* E format. */
	dm.ndigits = ndigits;	/* Number of significant digits. */
	quadruple_to_decimal(arg, &dm, &dr, &ef);
	*sign = dr.sign;
	switch (dr.fpclass) {
	case fp_normal:
	case fp_subnormal:
		*decpt = dr.exponent + ndigits;
		for (i = 0; i < ndigits; i++)
			buf[i] = dr.ds[i];
		buf[ndigits] = 0;
		break;
	case fp_zero:
		*decpt = 1;
		for (i = 0; i < ndigits; i++)
			buf[i] = '0';
		buf[ndigits] = 0;
		break;
	default:
		*decpt = 0;
		__infnanstring(dr.fpclass, ndigits, buf);
		break;
	}
	return buf;		/* For compatibility with ecvt. */
}

char           *
qfconvert(arg, ndigits, decpt, sign, buf)
	quadruple      *arg;
	int             ndigits, *decpt, *sign;
	char           *buf;
{
	decimal_mode    dm;
	decimal_record  dr;
	fp_exception_field_type ef;
	int             i;

	dm.rd = _QgetRD();	/* Rounding direction. */
	dm.df = fixed_form;	/* F format. */
	dm.ndigits = ndigits;	/* Number of digits after point. */
	quadruple_to_decimal(arg, &dm, &dr, &ef);
	*sign = dr.sign;
	*decpt = 0;
	if ((ef & (1 << fp_overflow)) != 0) {	/* Overflowed decimal record. */
		buf[0] = 0;
		return buf;
	}
	switch (dr.fpclass) {
	case fp_normal:
	case fp_subnormal:
		if (ndigits >= 0)
			*decpt = dr.ndigits - ndigits;
		else
			*decpt = dr.ndigits;
		for (i = 0; i < dr.ndigits; i++)
			buf[i] = dr.ds[i];
		buf[dr.ndigits] = 0;
		break;
	case fp_zero:
		buf[0] = '0';
		for (i = 1; i < ndigits; i++)
			buf[i] = '0';
		buf[i] = 0;
		break;
	default:
		__infnanstring(dr.fpclass, ndigits, buf);
		break;
	}
	return buf;		/* For compatibility with fcvt. */
}

char           *
qgconvert(number, ndigit, trailing, buf)
	quadruple      *number;
	int             ndigit, trailing;
	char           *buf;
{
	decimal_mode    dm;
	decimal_record  dr;
	fp_exception_field_type fef;

	dm.rd = _QgetRD();
	dm.df = floating_form;
	dm.ndigits = ndigit;
	quadruple_to_decimal(number, &dm, &dr, &fef);
	__k_gconvert(ndigit, &dr, trailing, buf);
	return (buf);
}
