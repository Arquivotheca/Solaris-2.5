/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)winchstr.c	1.6	93/05/05 SMI"	/* SVr4.0 1.3	*/
 
#include	"curses_inc.h"

winchstr(win,str)
WINDOW	*win;
chtype	*str;
{
    return (winchnstr(win,str,MAXINT));
}
