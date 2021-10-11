/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_qtos.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

extern  _Q_set_exception();

#ifdef __STDC__
float _Q_qtos(quadruple x)
#else
FLOATFUNCTIONTYPE _Q_qtos(x)
	quadruple x;
#endif
{
	unpacked	px;
	float		s;
	unsigned	cexc = 0;
	_fp_unpack(&px,&x,fp_op_extended,&cexc);
	_fp_pack(&px,&s,fp_op_single,&cexc);
	_Q_set_exception(cexc);
#ifdef __STDC__
	return s;
#else
	RETURNFLOAT(s);
#endif
}
