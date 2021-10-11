/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rw_rdlock.c 1.5	94/01/26 SMI"

#pragma weak _rw_rdlock = _rw_rdlock_stub
#pragma weak rw_rdlock = _rw_rdlock_stub

int
_rw_rdlock_stub()
{
	return (0);
}
