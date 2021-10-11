/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_funpackllong.c	1.2	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"
#include <stdio.h>

void __unpackllong(pu, px)
	unpacked	*pu;	/* unpacked result */
	int		*px;	/* packed integer */
{
	unsigned ux[2];
	/* fprintf(stderr, "high: %d low: %d\n", px[0], px[1]); */

	pu->sticky = pu->rounded = 0;
	if ((px[0] | px[1]) == 0) {
		pu->sign = 0;
		pu->fpclass = fp_zero;
	} else {
		(*pu).sign = px[0] < 0;
		(*pu).fpclass = fp_normal;
		(*pu).exponent = LONGLONG_BIAS;
		if (px[0] < 0) {
			ux[0] = ~px[0];
			ux[1] = ~px[1];
			if (++ux[1] == 0)
				++ux[0];
		} else {
			ux[0] =  px[0];
			ux[1] = px[1];
		}
		(*pu).significand[0] = ux[0]>>15;
		(*pu).significand[1] = (((ux[0]&0x7fff)<<17) | (ux[1]>>15));
		(*pu).significand[2] = (ux[1]&0x7fff)<<17;
		(*pu).significand[3] = 0;
		__fpu_normalize(pu);
	}
}
