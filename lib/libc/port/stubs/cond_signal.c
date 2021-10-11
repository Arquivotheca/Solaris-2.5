/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)cond_signal.c 1.5	94/01/26 SMI"

#pragma weak _cond_signal = _cond_signal_stub
#pragma weak cond_signal = _cond_signal_stub

int
_cond_signal_stub()
{
	return (0);
}
