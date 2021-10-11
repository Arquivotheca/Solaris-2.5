/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)cond_timedwait.c 1.5	94/01/26 SMI"

#pragma weak _cond_timedwait = _cond_timedwait_stub
#pragma weak cond_timedwait = _cond_timedwait_stub

int
_cond_timedwait_stub()
{
	return (0);
}
