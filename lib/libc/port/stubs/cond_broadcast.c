/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)cond_broadcast.c 1.5	94/01/26 SMI"

#pragma weak _cond_broadcast = _cond_broadcast_stub
#pragma weak cond_broadcast = _cond_broadcast_stub

int
_cond_broadcast_stub()
{
	return (0);
}
