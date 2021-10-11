/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rw_tryrdlock.c 1.5	94/01/26 SMI"

#pragma weak _rw_tryrdlock = _rw_tryrdlock_stub
#pragma weak rw_tryrdlock = _rw_tryrdlock_stub

int
_rw_tryrdlock_stub()
{
	return (0);
}
