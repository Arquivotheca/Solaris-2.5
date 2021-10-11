/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)winstr.c	1.6	93/05/05 SMI"	/* SVr4.0 1.3	*/
 
#include	"curses_inc.h"

winstr(win, str)
register	WINDOW	*win;
register	char	*str;
{
    register	int	counter = 0;
    int			cy = win->_cury;
    register	chtype	*ptr = &(win->_y[cy][win->_curx]),
			*pmax = &(win->_y[cy][win->_maxx]);
    chtype		*p1st = &(win->_y[cy][0]);
    chtype		wc;
    int			ew, sw, s;

    while (ISCBIT(*ptr) && (p1st < ptr))
	ptr--;

    while (ptr < pmax)
    {
	wc = RBYTE(*ptr);
	sw = mbscrw(wc);
	ew = mbeucw(wc);
	for (s = 0; s < sw; s++, ptr++)
	{
	    if ((wc = RBYTE(*ptr)) == MBIT)
		continue;
	    str[counter++] = wc;
	    if ((wc = LBYTE(*ptr) | MBIT) == MBIT)
		continue;
	    str[counter++] = wc;
	}
    }
    str[counter] = '\0';

    return (counter);
}
