/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)waddwch.c 1.1 93/05/10 SMI"
 
#include	"curses_inc.h"


/*
**	Add to 'win' a character at (curx,cury).
*/
waddwch(win,c)
WINDOW	*win;
chtype	c;
{
	int	i, width;
	char	buf[CSMAX];
	chtype	a;
	wchar_t	code;
	char	*p;

	a = c & A_WATTRIBUTES;
	code = c & A_WCHARTEXT;

	/* translate the process code to character code */
	if ((width = _curs_wctomb(buf, code & TRIM)) < 0)
		return ERR;

	/* now call waddch to do the real work */
	p = buf;
	while(width--)
		if (waddch(win, a|(0xFF & *p++)) == ERR)
			return ERR;
	return OK;
}
