/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)mbinch.c 1.1 93/05/05 SMI"
 
#include	"curses_inc.h"

/*
**	Get the (y,x) character of a window and
**	return it in a 0-terminated string.
*/
char	*wmbinch(win,y,x)
WINDOW	*win;
int	y, x;
{
	reg int		k, savx, savy;
	reg chtype	*wp, *ep, wc;
	static char	rs[CSMAX+1];

	k = 0;
	savx = win->_curx;
	savy = win->_cury;

	if(wmbmove(win,y,x) == ERR)
		goto done;
	wp = win->_y[win->_cury] + win->_curx;
	wc = RBYTE(*wp);
	ep = wp + _curs_scrwidth[TYPE(wc & 0377)];

	for(; wp < ep; ++wp)
	{
		if((wc = RBYTE(*wp)) == MBIT)
			break;
		rs[k++] = wc;
		if((wc = LBYTE(*wp)|MBIT) == MBIT)
			break;
		rs[k++] = wc;
	}
done :
	win->_curx = savx;
	win->_cury = savy;
	rs[k] = '\0';
	return rs;
}
