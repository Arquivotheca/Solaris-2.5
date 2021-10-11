/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rw_trywrlock.c 1.5	94/01/26 SMI"

#pragma weak _rw_trywrlock = _rw_trywrlock_stub
#pragma weak rw_trywrlock = _rw_trywrlock_stub

int
_rw_trywrlock_stub()
{
	return (0);
}
