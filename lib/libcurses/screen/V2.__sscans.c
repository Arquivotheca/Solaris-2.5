/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)V2.__sscans.c	1.5	92/07/14 SMI"	/* SVr4.0 1.8	*/

# include	"curses_inc.h"
#ifdef __STDC__
#include	<stdarg.h>
#else
# include	<varargs.h>
#endif
#ifdef _VR2_COMPAT_CODE
/*
	This file is provided for compatibility reasons only
	and will go away someday. Programs should reference
	vwscanw() instead.
 */


extern int vwscanw();
__sscans(win, fmt, ap)
WINDOW	*win;
char *fmt;
va_list	ap;
{
	return vwscanw(win, fmt, ap);
}
#endif
