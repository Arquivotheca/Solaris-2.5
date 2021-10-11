/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_stksegment.c 1.2	94/11/02 SMI"

#pragma weak _thr_stksegment = _thr_stksegment_stub
#pragma weak thr_stksegment = _thr_stksegment_stub

#include <errno.h>

int
_thr_stksegment_stub()
{
	return (ENOTSUP);
}
