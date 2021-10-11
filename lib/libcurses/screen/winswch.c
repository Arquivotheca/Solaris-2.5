/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)winswch.c 1.1 93/05/10 SMI"
 
#include	"curses_inc.h"

/*
**	Insert to 'win' a process code at (curx,cury).
*/
winswch(win,c)
WINDOW	*win;
chtype	c;
	{
	int	i, width;
	char	buf[CSMAX];
	chtype	a;
	wchar_t	code;
	a = c & A_WATTRIBUTES;
	code = c & A_WCHARTEXT;

	/* translate the process code to character code */
	if ((width = _curs_wctomb(buf, code & TRIM)) < 0)
		return ERR;

	/* now call winsch to do the real work */
	for(i = 0; i < width; ++i)
		if(winsch(win,a|(unsigned char)buf[i]) == ERR)
			return ERR;
	return OK;
	}
