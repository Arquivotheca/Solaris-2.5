/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)vwscanw.c	1.7	93/05/05 SMI"	/* SVr4.0 1.8	*/

/*
 * scanw and friends
 *
 */

# include	"curses_inc.h"

#ifdef __STDC__
#include	<stdarg.h>
#else
#include <varargs.h>
#endif

/*
 *	This routine actually executes the scanf from the window.
 *
 *	This code calls _vsscanf, which is like sscanf except
 * 	that it takes a va_list as an argument pointer instead
 *	of the argument list itself.  We provide one until
 *	such a routine becomes available.
 */

/*VARARGS2*/
vwscanw(win, fmt, ap)
WINDOW	*win;
char *fmt;
va_list	ap;
{
	wchar_t code[256];
	char	*buf;
	extern char *_strcode2byte();
	register int n;

	if (wgetwstr(win, code) == ERR)
		n = ERR;
	else	{
		buf = _strcode2byte(code, NULL, -1);
		n = _vsscanf(buf, fmt, ap);
	}

	va_end(ap);
	return (n);
}
