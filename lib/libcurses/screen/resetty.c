/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)resetty.c	1.6	95/01/09 SMI"	/* SVr4.0 1.5	*/

#include	"curses_inc.h"

resetty()
{
#ifdef	SYSV
    if ((_BRS(SP->save_tty_bufs)) != 0)
    {
	PROGTTYS = SP->save_tty_bufs;
	prog_istermios = 0;
#ifdef	DEBUG
	if (outf)
	    fprintf(outf, "resetty(), file %x, SP %x, flags %x, %x, %x, %x\n",
		cur_term->Filedes, SP, PROGTTYS.c_iflag, PROGTTYS.c_oflag,
		PROGTTYS.c_cflag, PROGTTYS.c_lflag);
#endif	/* DEBUG */
	reset_prog_mode();
    } else if ((_BR(SP->save_tty_buf)) != 0) {
	int i;

	PROGTTY = SP->save_tty_buf;
	prog_istermios = -1;
#ifdef	DEBUG
	if (outf)
	    fprintf(outf, "resetty(), file %x, SP %x, flags %x, %x, %x, %x\n",
		cur_term->Filedes, SP, PROGTTY.c_iflag, PROGTTY.c_oflag,
		PROGTTY.c_cflag, PROGTTY.c_lflag);
#endif	/* DEBUG */
	PROGTTYS.c_lflag = PROGTTY.c_lflag;
	PROGTTYS.c_oflag = PROGTTY.c_oflag;
	PROGTTYS.c_iflag = PROGTTY.c_iflag;
	PROGTTYS.c_cflag = PROGTTY.c_cflag;
	for (i = 0; i < NCC; i++)
		PROGTTYS.c_cc[i] = PROGTTY.c_cc[i];
	reset_prog_mode();
    }
#else	/* SYSV */
    if ((_BR(SP->save_tty_buf)) != 0) {
	PROGTTY = SP->save_tty_buf;
#ifdef	DEBUG
	if (outf)
	    fprintf(outf, "resetty(), file %x, SP %x, flags %x\n",
		cur_term->Filedes, SP, PROGTTY.sg_flags);
#endif	/* DEBUG */
	reset_prog_mode();
    }
#endif	/* SYSV */
    return (OK);
}
