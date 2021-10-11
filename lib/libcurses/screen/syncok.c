/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)syncok.c	1.5	92/07/14 SMI"	/* SVr4.0 1.4	*/

#include	"curses_inc.h"

syncok(win,bf)
WINDOW	*win;
bool	bf;
{
   return (win->_parent ? win->_sync = bf : ERR);
}
