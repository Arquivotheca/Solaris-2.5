/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)sema_trywait.c 1.5	94/01/26 SMI"

#pragma weak _sema_trywait = _sema_trywait_stub
#pragma weak sema_trywait = _sema_trywait_stub

int
_sema_trywait_stub()
{
	return (0);
}
