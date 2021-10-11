/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident  "@(#)wadjcurspos.c 1.1 93/05/05 SMI"
 
#include	<curses_inc.h>

wadjcurspos(win)
register	WINDOW	*win;
/* wmadjcurspos --- moves the cursor to the first column within the 
 * multi-column character somewhere on which the cursor curently is on.
 */
{
    register chtype *_yy, x;

    x = win->_curx;
    _yy = win->_y[win->_cury];
    while ((x>0) && (ISCBIT(_yy[x]))) --x;
    if (win->_curx!=x) {
	    win->_curx = x;
	    return (win->_immed ? wrefresh(win): OK);
    } else {
	    return OK;
    }
}
