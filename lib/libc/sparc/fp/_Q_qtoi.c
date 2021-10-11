/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_qtoi.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

extern enum fp_direction_type _QswapRD();

extern  _Q_set_exception();

int
_Q_qtoi(x)
	quadruple x;
{
	unpacked	px;
	int		i;
	enum fp_direction_type fp_direction;
	unsigned	cexc = 0;
	fp_direction = _QswapRD(fp_tozero);
	_fp_unpack(&px,&x,fp_op_extended,&cexc);
	_fp_pack(&px,&i,fp_op_integer,&cexc);
	_Q_set_exception(cexc);
	_QswapRD(fp_direction);
	return i;
}
