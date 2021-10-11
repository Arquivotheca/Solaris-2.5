/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)mutex_init.c 1.5	94/01/26 SMI"

#pragma weak _mutex_init = _mutex_init_stub
#pragma weak mutex_init = _mutex_init_stub

int
_mutex_init_stub()
{
	return (0);
}
