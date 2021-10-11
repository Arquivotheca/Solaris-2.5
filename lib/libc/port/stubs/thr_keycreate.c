/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_keycreate.c 1.5	94/01/26 SMI"

#pragma weak _thr_keycreate = _thr_keycreate_stub
#pragma weak thr_keycreate = _thr_keycreate_stub

int
_thr_keycreate_stub()
{
	return (0);
}
