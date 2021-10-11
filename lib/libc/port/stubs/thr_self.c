/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_self.c 1.5	94/01/26 SMI"

#pragma weak _thr_self = _thr_self_stub
#pragma weak thr_self = _thr_self_stub

int
_thr_self_stub()
{
	return (1);
}
