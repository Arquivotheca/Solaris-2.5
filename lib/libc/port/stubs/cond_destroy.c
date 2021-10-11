/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)cond_destroy.c 1.2	94/01/26 SMI"

#pragma weak _cond_destroy = _cond_destroy_stub
#pragma weak cond_destroy = _cond_destroy_stub

int
_cond_destroy_stub()
{
	return (0);
}
