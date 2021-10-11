/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)mutex_held.c 1.7	94/01/26 SMI"

#pragma weak _mutex_held = _mutex_held_stub
#pragma weak mutex_held = _mutex_held_stub

int
_mutex_held_stub()
{
	return (1);
}
