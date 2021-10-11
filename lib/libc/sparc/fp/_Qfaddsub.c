/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Qfaddsub.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

extern enum fp_direction_type _QswapRD();

PRIVATE void
true_add(px, py, pz)
	unpacked       *px, *py, *pz;

{
	unsigned 	c;
	unpacked       *pt;

	if ((int) px->fpclass < (int) py->fpclass) {	/* Reverse. */
		pt = py;
		py = px;
		px = pt;
	}
	/* Now class(x) >= class(y). */
	switch (px->fpclass) {
	case fp_quiet:		/* NaN + x -> NaN */
	case fp_signaling:	/* NaN + x -> NaN */
	case fp_infinity:	/* Inf + x -> Inf */
	case fp_zero:		/* 0 + 0 -> 0 */
		*pz = *px;
		return;
	default:
		if (py->fpclass == fp_zero) {
			*pz = *px;
			return;
		}
	}
	/* Now z is normal or subnormal. */
	/* Now y is normal or subnormal. */
	if (px->exponent < py->exponent) {	/* Reverse. */
		pt = py;
		py = px;
		px = pt;
	}
	/* Now class(x) >= class(y). */
	pz->fpclass = px->fpclass;
	pz->sign = px->sign;
	pz->exponent = px->exponent;
	pz->rounded = pz->sticky  = 0;

	if (px->exponent != py->exponent) {	/* pre-alignment required */
		__fpu_rightshift(py, pz->exponent - py->exponent);
		pz->rounded = py->rounded;
		pz->sticky  = py->sticky;
	}
	c = 0;
	c = __fpu_add3wc(&(pz->significand[3]),px->significand[3],
						py->significand[3],c);
	c = __fpu_add3wc(&(pz->significand[2]),px->significand[2],
						py->significand[2],c);
	c = __fpu_add3wc(&(pz->significand[1]),px->significand[1],
						py->significand[1],c);
	c = __fpu_add3wc(&(pz->significand[0]),px->significand[0],
						py->significand[0],c);

	/* Handle carry out of msb. */
	if(pz->significand[0]>=0x20000) {
		__fpu_rightshift(pz, 1);	/* Carried out bit. */
		pz->exponent ++;	/* Renormalize. */
	}
	return;
}

PRIVATE void
true_sub(px, py, pz,cexc)
	unpacked       *px, *py, *pz;
	unsigned       *cexc;

{
	unsigned       *z,g,s,r,c;
	unpacked       *pt;
	enum fp_direction_type fp_direction;

	if ((int) px->fpclass < (int) py->fpclass) {	/* Reverse. */
		pt = py;
		py = px;
		px = pt;
	}
	/* Now class(x) >= class(y). */
	*pz = *px;		/* Tentative difference: x. */
	switch (pz->fpclass) {
	case fp_quiet:		/* NaN - x -> NaN */
	case fp_signaling:	/* NaN - x -> NaN */
		return;
	case fp_infinity:	/* Inf - x -> Inf */
		if (py->fpclass == fp_infinity) {
			__fpu_error_nan(pz,cexc);	/* Inf - Inf -> NaN */
			pz->fpclass = fp_quiet;
		}
		return;
	case fp_zero:		/* 0 - 0 -> 0 */
		fp_direction = _QswapRD(0);
		pz->sign = (fp_direction == fp_negative);
		_QswapRD(fp_direction);
		return;
	default:
		if (py->fpclass == fp_zero)
			return;
	}

	/* x and y are both normal or subnormal. */

	if (px->exponent < py->exponent) { /* Reverse. */
		pt = py;
		py = px;
		px = pt;
	}
	/* Now exp(x) >= exp(y). */
	pz->fpclass = px->fpclass;
	pz->sign = px->sign;
	pz->exponent = px->exponent;
	pz->rounded = 0;
	pz->sticky = 0;
	z = pz->significand;

	if (px->exponent == py->exponent) {	/* no pre-alignment required */
		c = 0;
		c = __fpu_sub3wc(&z[3],px->significand[3],py->significand[3],c);
		c = __fpu_sub3wc(&z[2],px->significand[2],py->significand[2],c);
		c = __fpu_sub3wc(&z[1],px->significand[1],py->significand[1],c);
		c = __fpu_sub3wc(&z[0],px->significand[0],py->significand[0],c);
		if((z[0]|z[1]|z[2]|z[3])==0) {		/* exact zero result */
			fp_direction = _QswapRD(0);
			pz->sign = (fp_direction == fp_negative);
			pz->fpclass = fp_zero;
			_QswapRD(fp_direction);
			return;
		}
		if(z[0]>=0x20000) {	/* sign reversal occurred */
			pz->sign = py->sign;
			c = 0;
			c = __fpu_neg2wc(&z[3],z[3],c);
			c = __fpu_neg2wc(&z[2],z[2],c);
			c = __fpu_neg2wc(&z[1],z[1],c);
			c = __fpu_neg2wc(&z[0],z[0],c);
		}
		__fpu_normalize(pz);
		return;
	} else {		/* pre-alignment required */
		__fpu_rightshift(py, pz->exponent - py->exponent - 1);
		r = py->rounded; 	/* rounded bit */
		s = py->sticky;		/* sticky bit */
		__fpu_rightshift(py, 1);
		g = py->rounded;	/* guard bit */
		if(s!=0) r = (r==0);
		if((r|s)!=0) g = (g==0);/* guard and rounded bits of z */
		c = ((g|r|s)!=0);
		c = __fpu_sub3wc(&z[3],px->significand[3],py->significand[3],c);
		c = __fpu_sub3wc(&z[2],px->significand[2],py->significand[2],c);
		c = __fpu_sub3wc(&z[1],px->significand[1],py->significand[1],c);
		c = __fpu_sub3wc(&z[0],px->significand[0],py->significand[0],c);

		if(z[0]>=0x10000) { 	/* don't need post-shifted */
			pz->sticky = s|r;
			pz->rounded = g;
		} else {		/* post-shifted left 1 bit */
			pz->sticky = s;
			pz->rounded = r;
			pz->significand[0] = (z[0]<<1)|((z[1]&0x80000000)>>31);
			pz->significand[1] = (z[1]<<1)|((z[2]&0x80000000)>>31);
			pz->significand[2] = (z[2]<<1)|((z[3]&0x80000000)>>31);
			pz->significand[3] = (z[3]<<1)|g;
			pz->exponent      -= 1;
			if(z[0]<0x10000) __fpu_normalize(pz);
		}
		return;
	}
}

void
_fp_add(px, py, pz,cexc)
	unpacked       *px, *py, *pz;
	unsigned       *cexc;

{
	if (px->sign == py->sign)
		true_add(px, py, pz);
	else
		true_sub(px, py, pz,cexc);
}

void
_fp_sub(px, py, pz,cexc)
	unpacked       *px, *py, *pz;
	unsigned       *cexc;

{
	if( (int)(py->fpclass) < (int)(fp_quiet)) py->sign = 1 - py->sign;
	if (px->sign == py->sign)
		true_add(px, py, pz);
	else
		true_sub(px, py, pz,cexc);
}
