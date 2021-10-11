/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)sema_held.c 1.5	94/01/26 SMI"

#pragma weak _sema_held = _sema_held_stub
#pragma weak sema_held = _sema_held_stub

int
_sema_held_stub()
{
	return (1);
}
