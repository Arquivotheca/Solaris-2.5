/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)mutex_destroy.c 1.5	94/01/26 SMI"

#pragma weak _mutex_destroy = _mutex_destroy_stub
#pragma weak mutex_destroy = _mutex_destroy_stub

int
_mutex_destroy_stub()
{
	return (0);
}
