/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_ftoull.c	1.2	92/07/14 SMI"

#include "synonyms.h"
#include <floatingpoint.h>
#include <limits.h>

extern _Q_set_exception();

/*
 * Convert a double precision floating point # into a 64-bit unsigned int.
 *
 * For compatibility with Sun's other conversion routines, pretend result
 * is signed if input is negative.
 */

unsigned long long __dtoull(i0, i1)
	register int i0; register unsigned i1;	/* really a double */
{
	register int exp;	/* exponent */
	register unsigned m0;	/* most significant word of mantissa */
	register unsigned m1;	/* least sig. word of mantissa */
	unsigned _fp_current_exceptions = 0;

	/*
	 * Extract the exponent and check boundary conditions.
	 * Notice that the exponent is equal to the bit number where
	 * we want the most significant bit to live.
	 */
	exp = ((i0 >> 20) & 0x7ff) - 0x3ff;
	if (exp < 0) {
	return ((unsigned long long)0); /* abs(x) < 1.0, so round to 0 */
	} else if (exp > 63)  {
		/* abs(x) > MAXLLONG; return {MIN,MAX}ULLONG and as */
		/* overflow, Inf, NaN set fp_invalid exception */
		_fp_current_exceptions |= (1 << (int) fp_invalid);
		_Q_set_exception(_fp_current_exceptions);
		if (i0 < 0)
			return ((unsigned long long)LLONG_MIN);
		else
			return (ULLONG_MAX); /* MAXLONG */
	}

	/* Extract the mantissa. */
	m0 = 0x80000000 | ((i0 << 11) & 0x7ffff800) | ((i1 >> 21) & 0x7ff);
	m1 = i1 << 11;

	/*
	 * The most significant bit of the mantissa is now in bit 63 of m0:m1.
	 * Shift right by (63 - exp) bits.
	 */
	switch (exp) {
	case 63:
		break;
	case 31:
		m1 = m0;
		m0 = 0;
		break;
	default:
		if (exp > 31) {
			m1 = (m0 << (exp - 31)) | (m1 >> (63 - exp));
			m0 = (m0 >> (63 - exp));
		} else {
			m1 = (m0 >> (31 - exp));
			m0 = 0;
		}
		break;
	}

	if (i0 < 0) {
		if ((int) m0 < 0) {	/* x < MINLLONG; return MINLLONG */
			m0 = 0x80000000;
			m1 = 0;
		} else {
			m0 = ~m0;
			m1 = ~m1;
			if (++m1 == 0)
				m0++;
		}
	}

	_Q_set_exception(_fp_current_exceptions);
	return (((unsigned long long)m0 << 32) | m1);
}

/*
 * Convert a floating point number into a 64-bit unsigned int.
 *
 * For compatibility with Sun's other conversion routines, pretend result
 * is signed if input is negative.
 */
unsigned long long __ftoull(i0)
	register int i0;	/* really a float */
{
	register int exp;	/* exponent */
	register unsigned  m0;	/* most significant word of mantissa */
	register unsigned m1;	/* least sig. word of mantissa */
	unsigned _fp_current_exceptions = 0;

	/*
	 * Extract the exponent and check boundary conditions.
	 * Notice that the exponent is equal to the bit number where
	 * we want the most significant bit to live.
	 */
	exp = ((i0 >> 23) & 0xff) - 0x7f;
	if (exp < 0) {
		/* abs(x) < 1.0, so round to 0 */
		return ((unsigned long long)0);
	} else if (exp > 63)  {
		/* abs(x) > MAXLLONG; return {MIN,MAX}ULLONG and as */
		/* overflow, Inf, NaN set fp_invalid exception */
		_fp_current_exceptions |= (1 << (int) fp_invalid);
		_Q_set_exception(_fp_current_exceptions);
		if (i0 < 0)
			return ((unsigned long long)LLONG_MIN);
		else
			return (ULLONG_MAX); /* MAXLONG */
	}

	/* Extract the mantissa. */
	m0 = 0x80000000 | (i0 << 8) & 0x7fffff00;
	m1 = 0;

	/*
	 * The most significant bit of the mantissa is now in bit 63 of m0:m1.
	 * Shift right by (63 - exp) bits.
	 */
	switch (exp) {
	case 63:
		break;
	case 31:
		m1 = m0;
		m0 = 0;
		break;
	default:
		if (exp > 31) {
			m1 = m0 << (exp - 31);
			m0 = (m0 >> (63 - exp));
		} else {
			m1 = (m0 >> (31 - exp));
			m0 = 0;
		}
		break;
	}

	if (i0 < 0) {
		if ((int) m0 < 0) {	/* x < MINLLONG; return MINLLONG */
			m0 = 0x80000000;
			m1 = 0;
		} else {
			m0 = ~m0;
			m1 = ~m1;
			if (++m1 == 0)
				m0++;
		}
	}

	_Q_set_exception(_fp_current_exceptions);
	return (((unsigned long long)m0 << 32) | m1);
}

/*
 * Convert an extended precision floating point # into a 64-bit unsigned int.
 *
 * For compatibility with Sun's other conversion routines, pretend result
 * is signed if input is negative.
 */
/*ARGSUSED*/
unsigned long long _Q_qtoull(ld)
long double ld;
{
	register int i0; register unsigned i1, i2, i3;	/* a long double */
	register int exp;	/* exponent */
	register unsigned  m0;	/* most significant word of mantissa */
	register unsigned m1;	/* least sig. word of mantissa */
	unsigned _fp_current_exceptions = 0;
	int	 *plngdbl = (int *)&ld;

	i0 = plngdbl[0];
	i1 = plngdbl[1];
	i2 = plngdbl[2];
	i3 = plngdbl[3];

	/*
	 * Extract the exponent and check boundary conditions.
	 * Notice that the exponent is equal to the bit number where
	 * we want the most significant bit to live.
	 */
	exp = ((i0 >> 16) & 0x7fff) - 0x3fff;
	if (exp < 0) {
		return ((long long)0); /* abs(x) < 1.0, so round to 0 */
	} else if (exp > 63) {
		/* abs(x) > MAXLLONG; return {MIN,MAX}ULLONG and as */
		/* overflow, Inf, NaN set fp_invalid exception */
		_fp_current_exceptions |= (1 << (int) fp_invalid);
		_Q_set_exception(_fp_current_exceptions);
		if (i0 < 0)
			return ((unsigned long long)LLONG_MIN);
		else
			return (ULLONG_MAX); /* MAXLONG */
	}

	/* Extract the mantissa. */
	m0 = 0x80000000 | ((i0<<15) & 0x7fff8000) | ((i1>>17) & 0x7fff);
	m1 = (i1 << 15) | ((i2 >> 17) & 0x7fff);

	/* m0 = i1; */
	/* m1 = i2; */

	/*
	 * The most significant bit of the mantissa is now in bit 63 of m0:m1.
	 * Shift right by (63 - exp) bits.
	 */
	switch (exp) {
	case 63:
		break;
	case 31:
		m1 = m0;
		m0 = 0;
		break;
	default:
		if (exp > 31) {
			m1 = (m0 << (exp - 31)) | (m1 >> (63 - exp));
			m0 = (m0 >> (63 - exp));
		} else {
			m1 = (m0 >> (31 - exp));
			m0 = 0;
		}
		break;
	}

	if (i0 < 0) {
		if ((int) m0 < 0) {	/* x < MINLLONG; return MINLLONG */
			m0 = 0x80000000;
			m1 = 0;
		} else {
			m0 = ~m0;
			m1 = ~m1;
			if (++m1 == 0)
				m0++;
		}
	}

	_Q_set_exception(_fp_current_exceptions);
	return (((unsigned long long)m0 << 32) | m1);
}
