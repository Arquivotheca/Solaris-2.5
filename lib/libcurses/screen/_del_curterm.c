/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_del_curterm.c	1.5	92/07/14 SMI"	/* SVr4.0 1.3	*/

#define	NOMACROS
#include	"curses_inc.h"

del_curterm(terminal)
TERMINAL *terminal;
{
    return (delterm(terminal));
}
