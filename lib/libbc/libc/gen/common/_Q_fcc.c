#pragma ident	"@(#)_Q_fcc.c	1.5	92/07/20 SMI" 

/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

/* integer function _Q_feq, _Q_fne, _Q_fgt, _Q_fge, _Q_flt, _Q_fle */

#include "_Qquad.h"

int _Q_feq(x,y)
	QUAD x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmp(x,y);
	return (fcc_equal==fcc);
}

int _Q_fne(x,y)
	QUAD x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmp(x,y);
	return (fcc_equal!=fcc);
}

int _Q_fgt(x,y)
	QUAD x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_greater==fcc);
}

int _Q_fge(x,y)
	QUAD x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_greater==fcc||fcc_equal==fcc);
}

int _Q_flt(x,y)
	QUAD x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_less==fcc);
}

int _Q_fle(x,y)
	QUAD x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_less==fcc||fcc_equal==fcc);
}
