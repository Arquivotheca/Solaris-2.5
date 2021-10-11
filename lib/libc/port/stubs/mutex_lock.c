/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)mutex_lock.c 1.5	94/01/26 SMI"

#pragma weak _mutex_lock = _mutex_lock_stub
#pragma weak mutex_lock = _mutex_lock_stub

int
_mutex_lock_stub()
{
	return (0);
}
