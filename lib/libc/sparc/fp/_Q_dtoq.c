/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_dtoq.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

extern  _Q_set_exception();

quadruple
_Q_dtoq(x)
	double x;
{
	unpacked	px;
	quadruple	q;
	unsigned 	cexc = 0;
	_fp_unpack(&px,&x,fp_op_double,&cexc);
	_fp_pack(&px,&q,fp_op_extended,&cexc);
	_Q_set_exception(cexc);
	return q;
}
