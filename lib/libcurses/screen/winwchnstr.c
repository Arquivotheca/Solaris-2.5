/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)winwchnstr.c 1.1 93/05/10 SMI"
 
#include	"curses_inc.h"

/*
 * Read in ncols worth of data from window win and assign the
 * chars to string. NULL terminate string upon completion.
 * Return the number of chtypes copied.
 */

winwchnstr(win,string,ncols)
register	WINDOW	*win;
chtype		*string;
int		ncols;
{
    register	chtype	*ptr = &(win->_y[win->_cury][win->_curx]);
    register	int	counter = 0;
    register	int	maxcols = win->_maxx - win->_curx;
    int			eucw, scrw, s, wc;
    char		*mp, mbbuf[CSMAX+1];
    wchar_t		wch;
    chtype		rawc;
    chtype		attr;

    if (ncols < 0)
	ncols = MAXINT;

    while (ISCBIT(*ptr))
    {
	ptr--;
	maxcols++;
    }

    while ((counter < ncols) && maxcols > 0)
    {
	attr = *ptr & A_WATTRIBUTES;
	rawc = *ptr & A_WCHARTEXT;
	eucw = mbeucw(RBYTE(rawc));
	scrw = mbscrw(RBYTE(rawc));
	for (mp = mbbuf, s = 0; s < scrw; s++, maxcols--, ptr++)
	{
	    if ((wc = RBYTE(rawc)) == MBIT)
		continue;
	    *mp++ = wc;
	    if ((wc = LBYTE(rawc) | MBIT) == MBIT)
		continue;
	    *mp++ = wc;
	}
	*mp = '\0';
	if (_curs_mbtowc(&wch, mbbuf, CSMAX) <= 0)
	    break;
	*string++ = wch | attr;
	counter++;
    }
    if (counter < ncols)
	*string = (chtype) 0;
    return (counter);
}
