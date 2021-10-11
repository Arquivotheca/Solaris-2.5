/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_kill.c 1.5	94/01/26 SMI"

#pragma weak _thr_kill = _thr_kill_stub
#pragma weak thr_kill = _thr_kill_stub

int
_thr_kill_stub()
{
	return (0);
}
