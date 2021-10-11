/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rw_unlock.c 1.5	94/01/26 SMI"

#pragma weak _rw_unlock = _rw_unlock_stub
#pragma weak rw_unlock = _rw_unlock_stub

int
_rw_unlock_stub()
{
	return (0);
}
