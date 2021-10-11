/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)def_prog.c	1.6	95/01/09 SMI"	/* SVr4.0 1.3	*/

#include "curses_inc.h"

def_prog_mode()
{
    /* ioctl errors are ignored so pipes work */
#ifdef SYSV
    if ((prog_istermios = ioctl(cur_term -> Filedes,
		TCGETS, &(PROGTTYS))) < 0) {
	int i;

    	(void) ioctl(cur_term -> Filedes, TCGETA, &(PROGTTY));
	PROGTTYS.c_lflag = PROGTTY.c_lflag;
	PROGTTYS.c_oflag = PROGTTY.c_oflag;
	PROGTTYS.c_iflag = PROGTTY.c_iflag;
	PROGTTYS.c_cflag = PROGTTY.c_cflag;
	for (i = 0; i < NCC; i++)
		PROGTTYS.c_cc[i] = PROGTTY.c_cc[i];
    }
#else
    (void) ioctl(cur_term -> Filedes, TIOCGETP, &(PROGTTY));
#endif
    return (OK);
}
