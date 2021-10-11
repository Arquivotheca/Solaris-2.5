/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_min_stack.c 1.1	95/08/24 SMI"

#pragma weak _thr_min_stack = _thr_min_stack_stub
#pragma weak thr_min_stack = _thr_min_stack_stub

#include <errno.h>

int
_thr_min_stack_stub()
{
	return (-1);
}
