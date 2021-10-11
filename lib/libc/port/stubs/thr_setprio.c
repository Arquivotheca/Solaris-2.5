/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_setprio.c 1.5	94/01/26 SMI"

#pragma weak _thr_setprio = _thr_setprio_stub
#pragma weak thr_setprio = _thr_setprio_stub

int
_thr_setprio_stub()
{
	return (0);
}
