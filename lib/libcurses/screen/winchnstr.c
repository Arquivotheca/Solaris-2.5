/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)winchnstr.c	1.6	93/05/05 SMI"	/* SVr4.0 1.2	*/
 
#include	"curses_inc.h"

/*
 * Read in ncols worth of data from window win and assign the
 * chars to string. NULL terminate string upon completion.
 * Return the number of chtypes copied.
 */

winchnstr(win,string,ncols)
register	WINDOW	*win;
chtype		*string;
int		ncols;
{
    register	chtype	*ptr = &(win->_y[win->_cury][win->_curx]);
    register	int	counter = 0;
    register	int	maxcols = win->_maxx - win->_curx;
    int			eucw, scrw, s, wc;
    chtype		rawc, attr;

    if (ncols < 0)
	ncols = MAXINT;

    while (ISCBIT(*ptr))
    {
	ptr--;
	maxcols++;
    }

    while ((counter < ncols) && maxcols > 0)
    {
	eucw = mbeucw(RBYTE(*ptr));
	scrw = mbscrw(RBYTE(*ptr));

	if (counter + eucw > ncols)
	    break;
	for (s = 0; s < scrw; s++, maxcols--, ptr++)
	{
	    attr = _ATTR(*ptr);
	    rawc = _CHAR(*ptr);
	    if ((wc = RBYTE(rawc)) == MBIT)
		continue;
	    *string++ = wc | attr;
	    counter++;
	    if ((wc = LBYTE(rawc) | MBIT) == MBIT)
		continue;
	    *string++ = wc | attr;
	    counter++;
	}
    }
    if (counter < ncols)
	*string = (chtype) 0;
    return (counter);
}
