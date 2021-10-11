/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)mutex_trylock.c 1.5	94/01/26 SMI"

#pragma weak _mutex_trylock = _mutex_trylock_stub
#pragma weak mutex_trylock = _mutex_trylock_stub

int
_mutex_trylock_stub()
{
	return (0);
}
