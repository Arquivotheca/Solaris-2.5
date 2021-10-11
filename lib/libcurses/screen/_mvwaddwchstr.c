/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */
 
/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */
 
#ident  "@(#)_mvwaddwchstr.c 1.1 93/05/05 SMI"
 
#define		NOMACROS
#include	"curses_inc.h"

int
mvwaddwchstr(win,y,x,str)
WINDOW *win; 
int y; 
int x; 
chtype *str;
{ 
	return((wmove(win,y,x)==ERR?ERR:waddwchstr(win,str))); 
}
