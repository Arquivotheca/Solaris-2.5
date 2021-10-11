/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_cmpe.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

extern  _Q_set_exception();

enum fcc_type
_Q_cmpe(x,y)
	quadruple x,y;
{
	unpacked	px,py,pz;
	enum fcc_type	fcc;
	unsigned	cexc  = 0;
	_fp_unpack(&px,&x,fp_op_extended,&cexc);
	_fp_unpack(&py,&y,fp_op_extended,&cexc);
	fcc = _fp_compare(&px,&py,1,&cexc);/* quiet NaN exceptional */
	_Q_set_exception(cexc);
	return fcc;
}
