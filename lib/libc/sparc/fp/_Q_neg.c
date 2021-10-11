/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_neg.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"

quadruple 
_Q_neg(x)
	quadruple x;
{
	quadruple z;
	int	*pz = (int*) &z;
	double	dummy = 1.0;
	z = x;
	if((*(int*)&dummy)!=0) {
	    pz[0] ^= 0x80000000; 
	} else {
	    pz[3] ^= 0x80000000;
	}
	_Q_set_exception(0);	/* clear cexc */
	return z;
}
