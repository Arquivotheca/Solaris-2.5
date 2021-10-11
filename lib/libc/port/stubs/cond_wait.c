/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)cond_wait.c 1.5	94/01/26 SMI"

#pragma weak _cond_wait = _cond_wait_stub
#pragma weak cond_wait = _cond_wait_stub

int
_cond_wait_stub()
{
	return (0);
}
