/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_setconcurrency.c 1.5	94/01/26 SMI"

#pragma weak _thr_setconcurrency = _thr_setconcurrency_stub
#pragma weak thr_setconcurrency = _thr_setconcurrency_stub

int
_thr_setconcurrency_stub()
{
	return (0);
}
