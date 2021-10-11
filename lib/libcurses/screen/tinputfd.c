/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tinputfd.c	1.5	92/07/14 SMI"	/* SVr4.0 1.1	*/

#include	"curses_inc.h"

/* Set the input channel for the current terminal. */

void
tinputfd(fd)
int	fd;
{
    cur_term->_inputfd = fd;
    cur_term->_delay = -1;

    /* so that tgetch will reset it to be _inputd */
    /* cur_term->_check_fd = -2; */
}
