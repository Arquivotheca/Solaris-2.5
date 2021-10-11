/*	Copyright (c) 1994 Sun Microsystems Inc */
/*	All Rights Reserved. */

#ident	"@(#)thr_main.c 1.8	94/01/26 SMI"

#pragma weak _thr_main = _thr_main_stub
#pragma weak thr_main = _thr_main_stub

int
_thr_main_stub()
{
	return (-1);
}
