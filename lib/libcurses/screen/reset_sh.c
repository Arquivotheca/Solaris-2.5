/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)reset_sh.c	1.5	92/07/14 SMI"	/* SVr4.0 1.5	*/

#include	"curses_inc.h"

reset_shell_mode()
{
#ifdef	DIOCSETT
    /*
     * Restore any virtual terminal setting.  This must be done
     * before the TIOCSETN because DIOCSETT will clobber flags like xtabs.
     */
    cur_term -> old.st_flgs |= TM_SET;
    (void) ioctl(cur_term->Filedes, DIOCSETT, &cur_term -> old);
#endif	/* DIOCSETT */
#ifdef	SYSV
    if (_BRS(SHELLTTYS))
    {
	if (shell_istermios < 0) {
	    int i;

	    SHELLTTY.c_lflag = SHELLTTYS.c_lflag;
	    SHELLTTY.c_oflag = SHELLTTYS.c_oflag;
	    SHELLTTY.c_iflag = SHELLTTYS.c_iflag;
	    SHELLTTY.c_cflag = SHELLTTYS.c_cflag;
	    for (i = 0; i < NCC; i++)
		SHELLTTY.c_cc[i] = SHELLTTYS.c_cc[i];
	    (void) ioctl(cur_term -> Filedes, TCSETAW, &SHELLTTY);
    	} else
	    (void) ioctl(cur_term -> Filedes, TCSETSW, &SHELLTTYS);
#ifdef	LTILDE
	if (cur_term -> newlmode != cur_term -> oldlmode)
	    (void) ioctl(cur_term -> Filedes, TIOCLSET, &cur_term -> oldlmode);
#endif	/* LTILDE */
    }
#else	/* SYSV */
    if (_BR(SHELLTTY))
    {
	(void) ioctl(cur_term -> Filedes, TIOCSETN, &SHELLTTY);
#ifdef	LTILDE
	if (cur_term -> newlmode != cur_term -> oldlmode)
	    (void) ioctl(cur_term -> Filedes, TIOCLSET, &cur_term -> oldlmode);
#endif	/* LTILDE */
    }
#endif	/* SYSV */
    return (OK);
}
