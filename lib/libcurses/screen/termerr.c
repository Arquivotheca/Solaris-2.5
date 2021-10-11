/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)termerr.c	1.7	94/03/17 SMI"	/* SVr4.0 1.6	*/

#include	"curses_inc.h"
#include	"_curs_gettext.h"
#include <signal.h>   /* use this file to determine if this is SVR4.0 system */
#include <locale.h>

char    *term_err_strings[8];
int static first_term_err_message = 0;

void
termerr()
{
	if (first_term_err_message == 0) {
		first_term_err_message = 1;
		term_err_strings[0] =
#ifdef SIGSTOP  /* SVR4.0 and beyond */
	_curs_gettext("/usr/share/lib/terminfo is unaccessible");
#else
	_curs_gettext("/usr/lib/terminfo is unaccessible");
#endif
		term_err_strings[1] =
	_curs_gettext("I don't know anything about your \"%s\" terminal");
		term_err_strings[2] =
	_curs_gettext("corrupted terminfo entry");
		term_err_strings[3] =
	_curs_gettext("terminfo entry too long");
		term_err_strings[4] =
	_curs_gettext("TERMINFO pathname for device exceeds 512 characters");
		term_err_strings[5] =
#ifdef DEBUG
		"malloc returned NULL in function \"%s\"";
#else
	_curs_gettext("malloc returned NULL");
#endif /* DEBUG */
		term_err_strings[6] =
	_curs_gettext("terminfo file for \"%s\" terminal is not readable");
	}

	(void) fprintf(stderr, _curs_gettext("Sorry, "));
	(void) fprintf(stderr, term_err_strings[term_errno-1], term_parm_err);
	(void) fprintf(stderr, ".\r\n");
}
