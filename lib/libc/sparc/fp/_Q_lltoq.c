/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_lltoq.c	1.2	92/07/14 SMI"

#include "synonyms.h"
#include <stdio.h>
#include "_Qquad.h"
#include "_Qglobals.h"


quadruple
_Q_lltoq(x)
long long x;
{
	unpacked	px;
	quadruple	q;
	unsigned	cexc = 0;

	/* fprintf(stderr, "high: %d low: %d\n", x[0], x[1]); */
	_fp_unpack(&px, &x, fp_op_longlong, &cexc);
	/* display_unpacked(&px); */
	_fp_pack(&px, &q, fp_op_extended, &cexc);
	_Q_set_exception(0);		/* clear cexc */
	return (q);
}
