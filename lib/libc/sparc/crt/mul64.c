/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)mul64.c	1.1	91/08/19 SMI"

#include "synonyms.h"

unsigned long long _umul32x32to64();

long long __mul64(i, j)
register long long i, j;
{
	register unsigned i0, i1, j0, j1;
	register int sign = 0;
	register long long result = 0;

	if (i < 0) {
		i = -i;
		sign = 1;
	}
	if (j < 0) {
		j = -j;
		sign ^= 1;
	}
	i1 = i;
	j0 = j >> 32;
	j1 = j;

	if (j1) {
		if (i1)
			result = _umul32x32to64(i1, j1);
		if (i0 = i>>32) result += ((unsigned long long)(i0*j1)) << 32;
	}
	if (j0 && i1) result += ((unsigned long long)(i1*j0)) << 32;
	return (sign ? -result : result);
}


unsigned long long __umul64(i, j)
register unsigned long long i, j;
{
	register unsigned i0, i1, j0, j1;
	register unsigned long long result = 0;

	i1 = i;
	j0 = j >> 32;
	j1 = j;

	if (j1) {
		if (i1)
			result = _umul32x32to64(i1, j1);
		if (i0 = i>>32) result += ((unsigned long long)(i0*j1)) << 32;
	}
	if (j0 && i1) result += ((unsigned long long)(i1*j0)) << 32;
	return (result);
}
