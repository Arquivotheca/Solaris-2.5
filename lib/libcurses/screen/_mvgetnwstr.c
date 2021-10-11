/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */
 
/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */
 
#ident  "@(#)_mvgetnwstr.c 1.1 93/05/05 SMI"
 
#define		NOMACROS
#include	"curses_inc.h"

int
mvgetnwstr(y,x,ws,n)
int y; 
int x; 
wchar_t *ws; 
int n;
{ 
	return(mvwgetnwstr(stdscr,y,x,ws,n)); 
}
