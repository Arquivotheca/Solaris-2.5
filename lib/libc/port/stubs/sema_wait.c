/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)sema_wait.c 1.5	94/01/26 SMI"

#pragma weak _sema_wait = _sema_wait_stub
#pragma weak sema_wait = _sema_wait_stub

int
_sema_wait_stub()
{
	return (0);
}
