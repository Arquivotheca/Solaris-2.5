/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_getprio.c 1.5	94/01/26 SMI"

#pragma weak _thr_getprio = _thr_getprio_stub
#pragma weak thr_getprio = _thr_getprio_stub

int
_thr_getprio_stub()
{
	return (0);
}
