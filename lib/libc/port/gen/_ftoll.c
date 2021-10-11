/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_ftoll.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include <floatingpoint.h>
#include <limits.h>

/*
 * Convert a double precision floating point number into a 64-bit int.
 */
extern _Q_set_exception();

long long __dtoll(i0, i1)
	register int i0; register unsigned i1;	/* really a double */
{
	register int exp;	/* exponent */
	register int m0;	/* most significant word of mantissa */
	register unsigned m1;	/* least sig. word of mantissa */
	unsigned _fp_current_exceptions = 0;

	/*
	 * Extract the exponent and check boundary conditions.
	 * Notice that the exponent is equal to the bit number where
	 * we want the most significant bit to live.
	 */
	exp = ((i0 >> 20) & 0x7ff) - 0x3ff;
	if (exp < 0) {
		return ((long long)0); /* abs(x) < 1.0, so round to 0 */
	}
	else if (exp > 62) {
		/* fp_invalid NOT raised if <i0,i1> == LLONG_MIN */
		if (i0 >= 0 || exp != 63 || (i0 & 0xfffff) != 0 || i1 != 0) {
			/* abs(x) > MAXLLONG; return {MIN,MAX}LLONG and as */
			/* overflow, Inf, NaN set fp_invalid exception */
			_fp_current_exceptions |= (1 << (int) fp_invalid);
			_Q_set_exception(_fp_current_exceptions);
		}
		if (i0 < 0)
			return (LLONG_MIN);
		else
			return (LLONG_MAX); /* MAXLONG */
	}

	/* Extract the mantissa. */
	m0 = 0x40000000 | ((i0 << 10) & 0x3ffffc00) | ((i1 >> 22) & 0x3ff);
	m1 = i1 << 10;

	/*
	 * The most significant bit of the mantissa is now in bit 62 of m0:m1.
	 * Shift right by (62 - exp) bits.
	 */
	switch (exp) {
	case 62:
		break;
	case 30:
		m1 = m0;
		m0 = 0;
		break;
	default:
		if (exp > 30) {
			m1 = (m0 << (exp - 30)) |
				(m1 >> (62 - exp)) & ~(-1 << (exp - 30));
			m0 >>= 62 - exp;
		} else {
			m1 = m0 >> (30 - exp);
			m0 = 0;
		}
		break;
	}

	if (i0 < 0) {
		m0 = ~m0;
		m1 = ~m1;
		if (++m1 == 0)
			m0++;
	}

	_Q_set_exception(_fp_current_exceptions);

	return ((long long)(((unsigned long long)m0 << 32) | m1));
}

/*
 * Convert a floating point number into a 64-bit int.
 */
long long __ftoll(i0)
	register int i0;	/* really a float */
{
	register int exp;	/* exponent */
	register int m0;	/* most significant word of mantissa */
	register unsigned m1;	/* least sig. word of mantissa */
	unsigned _fp_current_exceptions = 0;

	/*
	 * Extract the exponent and check boundary conditions.
	 * Notice that the exponent is equal to the bit number where
	 * we want the most significant bit to live.
	 */
	exp = ((i0 >> 23) & 0xff) - 0x7f;
	if (exp < 0) {
	return ((long long) 0); /* abs(x) < 1.0, so round to 0 */
	}
	else if (exp > 62)  {
		/* fp_invalid NOT raised if <i0> == LLONG_MIN */
		if (i0 >= 0 || exp != 63 || (i0 & 0x7fffff) != 0) {
			/* abs(x) > MAXLLONG; return {MIN,MAX}LLONG and as */
			/* overflow, Inf, NaN set fp_invalid exception */
			_fp_current_exceptions |= (1 << (int) fp_invalid);
			_Q_set_exception(_fp_current_exceptions);
		}
		if (i0 < 0)
			return (LLONG_MIN);
		else
			return (LLONG_MAX); /* MAXLONG */
	}
	/* Extract the mantissa. */
	m0 = 0x40000000 | (i0 << 7) & 0x3fffff80;
	m1 = 0;

	/*
	 * The most significant bit of the mantissa is now in bit 62 of m0:m1.
	 * Shift right by (62 - exp) bits.
	 */
	switch (exp) {
	case 62:
		break;
	case 30:
		m1 = m0;
		m0 = 0;
		break;
	default:
		if (exp > 30) {
			m1 = m0 << (exp - 30);
			m0 >>= 62 - exp;
		} else {
			m1 = m0 >> (30 - exp);
			m0 = 0;
		}
		break;
	}

	if (i0 < 0) {
		m0 = ~m0;
		m1 = ~m1;
		if (++m1 == 0)
			m0++;
	}

	_Q_set_exception(_fp_current_exceptions);
	return ((long long)(((unsigned long long)m0 << 32) | m1));
}

/*
 * Convert an extended precision floating point number into a 64-bit int.
 */
/*ARGSUSED*/
long long _Q_qtoll(longdbl)
long double longdbl;
{
	register int i0; register unsigned i1, i2, i3;	/* a long double */
	int	*plongdouble = (int *)&longdbl;
	register int exp;	/* exponent */
	register int m0;	/* most significant word of mantissa */
	register unsigned m1;	/* least sig. word of mantissa */
	unsigned _fp_current_exceptions = 0;

	i0 = plongdouble[0];
	i1 = plongdouble[1];
	i2 = plongdouble[2];
	i3 = plongdouble[3];

	/*
	 * Extract the exponent and check boundary conditions.
	 * Notice that the exponent is equal to the bit number where
	 * we want the most significant bit to live.
	 */
	exp = ((i0 >> 16) & 0x7fff) - 0x3fff;
	if (exp < 0) {
	return ((long long)0); /* abs(x) < 1.0, so round to 0 */
	}
	else if (exp > 62)	{
		/* fp_invalid NOT raised if <i0,i1,i2,i3> when chopped to
		   64 bits == LLONG_MIN */
		if (i0 >= 0 || exp != 63 || (i0 & 0xffff) != 0 || i1 != 0 ||
			(i2 & 0xfffe0000) != 0) {
			/* abs(x) > MAXLLONG; return {MIN,MAX}LLONG and as */
			/* overflow, Inf, NaN set fp_invalid exception */
			_fp_current_exceptions |= (1 << (int) fp_invalid);
			_Q_set_exception(_fp_current_exceptions);
		}
		if (i0 < 0)
			return (LLONG_MIN);
		else
			return (LLONG_MAX); /* MAXLONG */
	}
	/* Extract the mantissa. */

	m0 = 0x40000000 | ((i0<<14) & 0x3fffc000) | ((i1>>18) & 0x3fff);
	m1 = (i1<<14) | ((i2>>18) & 0x3fff);

	/* m0 = (i1 >> 1) & 0x7fffffff; */
	/* m1 = (i1 << 31) | ((i2 >> 1) & 0x7fffffff); */

	/*
	 * The most significant bit of the mantissa is now in bit 62 of m0:m1.
	 * Shift right by (62 - exp) bits.
	 */
	switch (exp) {
	case 62:
		break;
	case 30:
		m1 = m0;
		m0 = 0;
		break;
	default:
		if (exp > 30) {
			m1 = (m0 << (exp - 30)) |
				(m1 >> (62 - exp)) & ~(-1 << (exp - 30));
			m0 >>= 62 - exp;
		} else {
			m1 = m0 >> (30 - exp);
			m0 = 0;
		}
		break;
	}

	if (i0 < 0) {
		m0 = ~m0;
		m1 = ~m1;
		if (++m1 == 0)
			m0++;
	}

	_Q_set_exception(_fp_current_exceptions);

	return ((long long)(((unsigned long long)m0 << 32) | m1));
}
