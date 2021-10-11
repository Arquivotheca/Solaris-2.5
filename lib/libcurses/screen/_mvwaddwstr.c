/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */
 
/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */
 
#ident  "@(#)_mvwaddwstr.c 1.1 93/05/05 SMI"
 
#define		NOMACROS
#include	"curses_inc.h"

int
mvwaddwstr(win,y,x,ws)
WINDOW *win; 
int y; 
int x; 
wchar_t *ws;
{ 
	return((wmove(win,y,x)==ERR?ERR:waddwstr(win,ws))); 
}
