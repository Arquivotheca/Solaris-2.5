/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)setqiflush.c	1.6	95/01/09 SMI"	/* SVr4.0 1.3	*/

#include	"curses_inc.h"

/*
**	Set/unset flushing the output queue on interrupts or quits.
*/

void
_setqiflush(yes)
bool	yes;
{
#ifdef SYSV
    if (yes)
	cur_term->Nttybs.c_lflag &= ~NOFLSH;
    else
	cur_term->Nttybs.c_lflag |= NOFLSH;
    reset_prog_mode();
#else	/* BSD */
#endif /* SYSV */
}
