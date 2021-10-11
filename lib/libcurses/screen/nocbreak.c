/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)nocbreak.c	1.7	95/01/09 SMI"	/* SVr4.0 1.5	*/

#include	"curses_inc.h"

nocbreak()
{
#ifdef	SYSV
    /* See comment in cbreak.c about ICRNL. */
    PROGTTYS.c_iflag |= ICRNL;
    PROGTTYS.c_lflag |= ICANON;
    PROGTTYS.c_cc[VEOF] = _CTRL('D');
    PROGTTYS.c_cc[VEOL] = 0;
#else	/* SYSV */
    PROGTTY.sg_flags &= ~(CBREAK | CRMOD);
#endif	/* SYSV */

#ifdef	DEBUG
#ifdef	SYSV
    if (outf)
	fprintf(outf, "nocbreak(), file %x, flags %x\n",
	    cur_term->Filedes, PROGTTYS.c_lflag);
#else	/* SYSV */
    if (outf)
	fprintf(outf, "nocbreak(), file %x, flags %x\n",
	    cur_term->Filedes, PROGTTY.sg_flags);
#endif	/* SYSV */
#endif	/* DEBUG */

    cur_term->_fl_rawmode = FALSE;
    cur_term->_delay = -1;
    reset_prog_mode();
#ifdef	FIONREAD
    cur_term->timeout = 0;
#endif	/* FIONREAD */
    return (OK);
}
