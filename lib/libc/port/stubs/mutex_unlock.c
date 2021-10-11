/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)mutex_unlock.c 1.5	94/01/26 SMI"

#pragma weak _mutex_unlock = _mutex_unlock_stub
#pragma weak mutex_unlock = _mutex_unlock_stub

int
_mutex_unlock_stub()
{
	return (0);
}
