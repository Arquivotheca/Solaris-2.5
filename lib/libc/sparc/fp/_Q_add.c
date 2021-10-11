/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_add.c	1.5	93/03/08 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

static double zero = 0.0, tiny=1.0e-307, tiny2=1.001e-307, huge=1.0e300;
static dummy();
extern  _Q_set_exception();

quadruple 
_Q_add(x,y)
	quadruple x,y;
{
	unpacked	px,py,pz;
	quadruple	z;
	unsigned 	cexc = 0;

	_fp_unpack(&px,&x,fp_op_extended,&cexc);
	_fp_unpack(&py,&y,fp_op_extended,&cexc);
	_fp_add(&px,&py,&pz,&cexc);
	_fp_pack(&pz,&z,fp_op_extended,&cexc);
	_Q_set_exception(cexc);
	return z;
}

#include <ieeefp.h>

_Q_set_exception(ex)
	unsigned ex;
{
    /* simulate exceptions using double arithmetic */
	double t;
	if(ex==0) t = zero-zero;	/* clear cexc */
	else {
	    if((ex&(1<<fp_invalid))!=0)		t = (zero/zero);
	    if((ex&(1<<fp_overflow))!=0)	t = (huge*huge);
	    if((ex&(1<<fp_underflow))!=0) {
		if((ex&(1<<fp_inexact))!=0||
		 (fpgetmask()&FP_X_UFL)!=0)     t = (tiny*tiny);
		else                            t = tiny2-tiny; /* exact */
	    }
	    if((ex&(1<<fp_division))!=0)	t = (tiny/zero);
	    if((ex&(1<<fp_inexact))!=0)		t = (huge+tiny);
	}
	dummy(t);  /* prevent optimizer eliminating previous expression */
	return 0;
}
	
static 
dummy(x)
	double x;
{
	return 0;
}
