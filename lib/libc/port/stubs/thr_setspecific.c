/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_setspecific.c 1.5	94/01/26 SMI"

#pragma weak _thr_setspecific = _thr_setspecific_stub
#pragma weak thr_setspecific = _thr_setspecific_stub

int
_thr_setspecific_stub()
{
	return (0);
}
