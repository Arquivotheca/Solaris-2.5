/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_continue.c 1.5	94/01/26 SMI"

#pragma weak _thr_continue = _thr_continue_stub
#pragma weak thr_continue = _thr_continue_stub

int
_thr_continue_stub()
{
	return (0);
}
