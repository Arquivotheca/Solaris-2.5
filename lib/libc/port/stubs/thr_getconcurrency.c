/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_getconcurrency.c 1.5	94/01/26 SMI"

#pragma weak _thr_getconcurrency = _thr_getconcurrency_stub
#pragma weak thr_getconcurrency = _thr_getconcurrency_stub

int
_thr_getconcurrency_stub()
{
	return (0);
}
