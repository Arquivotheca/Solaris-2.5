/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_box.c	1.5	92/07/14 SMI"	/* SVr4.0 1.2	*/

#define		NOMACROS
#include	"curses_inc.h"

box(win, v, h)
WINDOW	*win;
chtype	v, h;
{
    return (wborder(win, v, v, h, h,
			 (chtype) 0, (chtype) 0, (chtype) 0, (chtype) 0));
}
