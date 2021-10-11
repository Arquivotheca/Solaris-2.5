/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_mvwinchstr.c	1.5	92/07/14 SMI"	/* SVr4.0 1.1	*/

#define	NOMACROS

#include	"curses_inc.h"

mvwinchstr(win, y, x, str)
WINDOW *win;
int	y, x;
chtype	*str;
{
    return (wmove(win, y, x)==ERR?ERR:winchstr(win, str));
}
