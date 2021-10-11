/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)waddnwstr.c 1.1 93/05/05 SMI"
 
#include	"curses_inc.h"

/*
**	Add to 'win' at most n 'characters' of code starting at (cury,curx)
*/
waddnwstr(win,code,n)
WINDOW	*win;
wchar_t	*code;
int	n;
{
	register char	*sp;
	extern char 	*_strcode2byte();

	/* translate the process code to character code */
	if((sp = _strcode2byte(code,NULL,n)) == NULL)
		return ERR;

	/* now call waddnstr to do the real work */
	return waddnstr(win,sp,-1);
}
