/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)erasechar.c	1.6	95/01/09 SMI"	/* SVr4.0 1.3	*/

/*
 * Routines to deal with setting and resetting modes in the tty driver.
 * See also setupterm.c in the termlib part.
 */
#include "curses_inc.h"

char
erasechar()
{
#ifdef SYSV
    return (SHELLTTYS.c_cc[VERASE]);
#else
    return (SHELLTTY.sg_erase);
#endif
}
