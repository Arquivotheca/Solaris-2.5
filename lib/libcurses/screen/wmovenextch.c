/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident  "@(#)wmovenextch.c 1.1 93/05/05 SMI"

#include	<curses_inc.h>

wmovenextch(win)
register	WINDOW	*win;
/* wmovenextch --- moves the cursor forward to the next char of char
 * cursor is currently on.  This is used to move forward over a multi-column
 * character.  When the cursor is on a character at the right-most
 * column, the cursor will stay there.
 */
{
    register chtype *_yy, x;

    _yy = win->_y[win->_cury];
    x = win->_curx;
    
    if (x+1 > win->_maxx) return ERR; /* Can't move any more. */

    ++x;
    for ( ; ;) {
	    if (x >= win->_maxx) return ERR; /* No more space.. */
	    if (ISCBIT(_yy[x])) {
		    ++x;
	    } else {
		    break;
	    }
    }

    win->_curx=x;
    win->_flags |= _WINMOVED;
    return (win->_immed ? wrefresh(win): OK);
}
