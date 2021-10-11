/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)winsnwstr.c 1.1 93/05/05 SMI"
 
#include	"curses_inc.h"

/*
**	Insert to 'win' at most n 'characters' of code starting at (cury,curx)
*/
winsnwstr(win,code,n)
WINDOW	*win;
wchar_t	*code;
int	n;
	{
	register char	*sp;
	extern char 	*_strcode2byte();

	/* translate the process code to character code */
	if((sp = _strcode2byte(code,NULL,n)) == NULL)
		return ERR;

	/* now call winsnstr to do the real work */
	return winsnstr(win,sp,-1);
	}
