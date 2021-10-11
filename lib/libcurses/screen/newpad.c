/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)newpad.c	1.5	92/07/14 SMI"	/* SVr4.0 1.4	*/

#include	"curses_inc.h"

WINDOW	*
newpad(l,nc)
int	l,nc;
{
    WINDOW	*pad;

    pad = newwin(l,nc,0,0);
    if (pad != (WINDOW *) NULL)
	pad->_flags |= _ISPAD;
    return (pad);
}
