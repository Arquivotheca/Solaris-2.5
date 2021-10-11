/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_ulltoq.c	1.2	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"


quadruple
_Q_ulltoq(x)
unsigned long long x;
{
	unpacked	px;
	double 		one = 1.0;
	quadruple	q, c;
	int 		*pc = (int*)&c;
	int		*plonglong = (int *)&x;
	unsigned	cexc = 0;
	/* fprintf(stderr, "high: %d low: %d\n", x[0], x[1]); */
	pc[0] = pc[1] = pc[2] = pc[3] = 0;	/* pc = 2^63 */
	if (*(int*)&one == 0) pc[3] = 0x403e0000; else pc[0] = 0x403e0000;

	if ((plonglong[0]&0x80000000) != 0) {
		plonglong[0] ^= 0x80000000;
		_fp_unpack(&px, &x, fp_op_longlong, &cexc);
		/* display_unpacked(&px); */
		_fp_pack(&px, &q, fp_op_extended, &cexc);
		q = _Q_add(q, c);
	} else {
		_fp_unpack(&px, &x, fp_op_longlong, &cexc);
		/* display_unpacked(&px); */
		_fp_pack(&px, &q, fp_op_extended, &cexc);
	}
	_Q_set_exception(0);		/* clear cexc */
	return (q);
}
