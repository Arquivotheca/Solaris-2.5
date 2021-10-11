/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_errnop.c 1.6	94/01/26 SMI"

#pragma weak _thr_errnop = _thr_errnop_stub
#pragma weak thr_errnop = _thr_errnop_stub

int *
_thr_errnop_stub()
{
	return (0);
}
