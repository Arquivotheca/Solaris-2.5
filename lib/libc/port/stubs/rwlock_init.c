/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)rwlock_init.c 1.5	94/01/26 SMI"

#pragma weak _rwlock_init = _rwlock_init_stub
#pragma weak rwlock_init = _rwlock_init_stub

int
_rwlock_init_stub()
{
	return (0);
}
