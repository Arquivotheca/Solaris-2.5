/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_exit.c 1.5	94/01/26 SMI"

#pragma weak _thr_exit = _thr_exit_stub
#pragma weak thr_exit = _thr_exit_stub

int
_thr_exit_stub()
{
	return (0);
}
