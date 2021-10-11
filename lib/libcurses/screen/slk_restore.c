/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)slk_restore.c	1.5	92/07/14 SMI"	/* SVr4.0 1.4	*/

#include	"curses_inc.h"

/* Restore screen labels. */

slk_restore()
{
    if (SP->slk)
    {
	SP->slk->_changed = TRUE;
	(void) slk_touch();
	(void) slk_refresh();
    }
    return (OK);
}
