/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_sigsetmask.c 1.5	94/01/26 SMI"

#pragma weak _thr_sigsetmask = _thr_sigsetmask_stub
#pragma weak thr_sigsetmask = _thr_sigsetmask_stub

int
_thr_sigsetmask_stub()
{
	return (0);
}
