/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)scrollok.c	1.5	92/07/14 SMI"	/* SVr4.0 1.8	*/

#include	"curses_inc.h"

scrollok(win,bf)
WINDOW	*win;
bool	bf;
{
    win->_scroll = (bf) ? TRUE : FALSE;
    return (OK);
}
