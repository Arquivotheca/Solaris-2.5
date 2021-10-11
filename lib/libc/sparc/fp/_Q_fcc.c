/*
 * Copyright (c) 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_Q_fcc.c	1.3	92/07/14 SMI"

/* integer function _Q_feq, _Q_fne, _Q_fgt, _Q_fge, _Q_flt, _Q_fle */

#include "synonyms.h"
#include "_Qquad.h"

int _Q_feq(x,y)
	quadruple x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmp(x,y);
	return (fcc_equal==fcc);
}

int _Q_fne(x,y)
	quadruple x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmp(x,y);
	return (fcc_equal!=fcc);
}

int _Q_fgt(x,y)
	quadruple x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_greater==fcc);
}

int _Q_fge(x,y)
	quadruple x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_greater==fcc||fcc_equal==fcc);
}

int _Q_flt(x,y)
	quadruple x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_less==fcc);
}

int _Q_fle(x,y)
	quadruple x,y;
{
	enum fcc_type	fcc;
	fcc = _Q_cmpe(x,y);
	return (fcc_less==fcc||fcc_equal==fcc);
}
