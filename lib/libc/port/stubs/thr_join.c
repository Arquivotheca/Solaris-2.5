/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_join.c 1.5	94/01/26 SMI"

#pragma weak _thr_join = _thr_join_stub
#pragma weak thr_join = _thr_join_stub

int
_thr_join_stub()
{
	return (0);
}
