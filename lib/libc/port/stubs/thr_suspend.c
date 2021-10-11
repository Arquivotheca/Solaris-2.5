/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_suspend.c 1.5	94/01/26 SMI"

#pragma weak _thr_suspend = _thr_suspend_stub
#pragma weak thr_suspend = _thr_suspend_stub

int
_thr_suspend_stub()
{
	return (0);
}
