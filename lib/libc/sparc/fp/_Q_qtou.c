/*
 * Copyright (c) 1988, 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_qtou.c	1.3	92/07/14 SMI"

#include "synonyms.h"
#include "_Qquad.h"
#include "_Qglobals.h"

extern enum fp_direction_type _QswapRD();
extern _Q_set_exception();

unsigned
_Q_qtou(x)
	quadruple x;
{
	unpacked	px;
	double	one=1.0;
	quadruple	c;
	unsigned	u,*pc = (unsigned*)&c,r;
	enum fp_direction_type fp_direction;
	unsigned	cexc = 0;

	pc[0]=pc[1]=pc[2]=pc[3]=0;
		/* set c = 2^31 and u = high part of x */
	if(*(int*)&one == 0) {
	    pc[3]=0x401e0000; u = *(3+(unsigned*)&x);
	} else {
	    pc[0]=0x401e0000; u = *(unsigned*)&x;
	}
	r = 0;
	if(u>=0x401e0000&&u<0x401f0000) {
		r = 0x80000000;
		x = _Q_sub(x,c);
	}
	fp_direction = _QswapRD(fp_tozero);
	_fp_unpack(&px,&x,fp_op_extended,&cexc);
	_fp_pack(&px,&u,fp_op_integer,&cexc);
	_Q_set_exception(cexc);
	_QswapRD(fp_direction);
	return (u|r);
}
