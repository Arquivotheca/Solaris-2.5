/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc. 
 */

#ident	"@(#)_Qfcompare.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

enum fcc_type
_fp_compare(px, py, strict, cexc)
	unpacked       *px, *py;
	int             strict;	/* 0 if quiet NaN unexceptional, 1 if
				 * exceptional */
	unsigned       *cexc; 	/* current exception */

{
	enum fcc_type   cc;
	int  n;

	if ((px->fpclass == fp_quiet) || (py->fpclass == fp_quiet) ||
	    (px->fpclass == fp_signaling) || (py->fpclass == fp_signaling)) {
		if (strict)				/* NaN */
			__fpu_set_exception(fp_invalid,cexc);
		cc = fcc_unordered;
	} else if ((px->fpclass == fp_zero) && (py->fpclass == fp_zero))
		cc = fcc_equal;
	/* both zeros */
	else if (px->sign < py->sign)
		cc = fcc_greater;
	else if (px->sign > py->sign)
		cc = fcc_less;
	else {			/* signs the same, compute magnitude cc */
		if ((int) px->fpclass > (int) py->fpclass)
			cc = fcc_greater;
		else if ((int) px->fpclass < (int) py->fpclass)
			cc = fcc_less;
		else
		 /* same classes */ if (px->fpclass == fp_infinity)
			cc = fcc_equal;	/* same infinity */
		else if (px->exponent > py->exponent)
			cc = fcc_greater;
		else if (px->exponent < py->exponent)
			cc = fcc_less;
		else {	/* equal exponents */ 
			n = __fpu_cmpli(px->significand,py->significand,4);
			if(n>0) cc = fcc_greater;
			else if(n<0) cc = fcc_less;
			else cc = fcc_equal;
		}
		if (px->sign)
			switch (cc) {	/* negative numbers */
			case fcc_less:
				cc = fcc_greater;
				break;
			case fcc_greater:
				cc = fcc_less;
				break;
			}
	}
	return (cc);
}
