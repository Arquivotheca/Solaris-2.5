/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_create.c 1.5	94/01/26 SMI"

#pragma weak _thr_create = _thr_create_stub
#pragma weak thr_create = _thr_create_stub

int
_thr_create_stub()
{
	return (-1);
}
