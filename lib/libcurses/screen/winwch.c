/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)winwch.c 1.1 93/05/10 SMI"
 
#include	"curses_inc.h"

/*
**	Get a process code at (curx,cury).
*/
chtype winwch(win)
WINDOW	*win;
{
	wchar_t	wchar;
	int	length;
	chtype	a;

	a = (win->_y[win->_cury][win->_curx]) & A_WATTRIBUTES;

	length = _curs_mbtowc(&wchar,wmbinch(win,win->_cury,win->_curx),
							sizeof(wchar_t));
	return(a | wchar);
}
