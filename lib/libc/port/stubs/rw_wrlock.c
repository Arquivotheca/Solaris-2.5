/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rw_wrlock.c 1.5	94/01/26 SMI"

#pragma weak _rw_wrlock = _rw_wrlock_stub
#pragma weak rw_wrlock = _rw_wrlock_stub

int
_rw_wrlock_stub()
{
	return (0);
}
