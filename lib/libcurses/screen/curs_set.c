/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)curs_set.c	1.5	92/07/14 SMI"	/* SVr4.0 1.8	*/

#include	"curses_inc.h"

/* Change the style of cursor in use. */

curs_set(visibility)
register	int	visibility;
{
#ifdef __STDC__
    extern  int     _outch(char);
#else
    extern  int     _outch();
#endif
    int		ret = cur_term->_cursorstate;
    char	**cursor_seq = cur_term->cursor_seq;

    if ((visibility < 0) || (visibility > 2) || (!cursor_seq[visibility]))
	ret = ERR;
    else
	if (visibility != ret)
	    tputs(cursor_seq[cur_term->_cursorstate = visibility], 0, _outch);
    (void) fflush(SP->term_file);
    return (ret);
}
