/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_getspecific.c 1.5	94/01/26 SMI"

#pragma weak _thr_getspecific = _thr_getspecific_stub
#pragma weak thr_getspecific = _thr_getspecific_stub

int
_thr_getspecific_stub()
{
	return (0);
}
