/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_yield.c 1.5	94/01/26 SMI"

#pragma weak _thr_yield = _thr_yield_stub
#pragma weak thr_yield = _thr_yield_stub

int
_thr_yield_stub()
{
	return (0);
}
