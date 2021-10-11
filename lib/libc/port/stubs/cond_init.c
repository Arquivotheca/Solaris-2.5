/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)cond_init.c 1.5	94/01/26 SMI"

#pragma weak _cond_init = _cond_init_stub
#pragma weak cond_init = _cond_init_stub

int
_cond_init_stub()
{
	return (0);
}
