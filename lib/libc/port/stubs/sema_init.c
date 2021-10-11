/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)sema_init.c 1.5	94/01/26 SMI"

#pragma weak _sema_init = _sema_init_stub
#pragma weak sema_init = _sema_init_stub

int
_sema_init_stub()
{
	return (0);
}
