/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rw_read_held.c 1.5	94/01/26 SMI"

#pragma weak _rw_read_held = _rw_read_held_stub
#pragma weak rw_read_held = _rw_read_held_stub

int
_rw_read_held_stub()
{
	return (1);
}
