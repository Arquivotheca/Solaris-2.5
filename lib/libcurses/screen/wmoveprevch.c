/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident  "@(#)wmoveprevch.c 1.1 93/05/05 SMI"

#include	<curses_inc.h>

wmoveprevch(win)
register	WINDOW	*win;
/* wmoveprevch --- moves the cursor back to the previous char of char
 * cursor is currently on.  This is used to move back over a multi-column
 * character.  When the cursor is on a character at the left-most
 * column, the cursor will stay there.
 */
{
    register chtype *_yy, x;

    wadjcurspos(win);
    x = win->_curx;
    if (x == 0) return ERR; /* Can't back up any more. */
    _yy = win->_y[win->_cury];
    --x;
    while ((x>0) && (ISCBIT(_yy[x]))) --x;
    win->_curx = x;
    win->_flags |= _WINMOVED;
    return (win->_immed ? wrefresh(win): OK);
}
