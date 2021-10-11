/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)curserr.c	1.6	93/05/05 SMI"	/* SVr4.0 1.5	*/
 
#include 	"curses_inc.h"
#include 	"_curs_gettext.h"
#include	<locale.h>

char	*curs_err_strings[4];
int static first_curs_err_message = 0;

void
curserr()
{
	if (first_curs_err_message == 0) {
		first_curs_err_message = 1;
		curs_err_strings[0] =
	_curs_gettext("I don't know how to deal with your \"%s\" terminal");
		curs_err_strings[1] =
	_curs_gettext("I need to know a more specific terminal type than \"%s\"");
		curs_err_strings[2] =
#ifdef DEBUG
		"malloc returned NULL in function \"%s\"";
#else
	_curs_gettext("malloc returned NULL");
#endif /* DEBUG */
	}

	fprintf(stderr, _curs_gettext("Sorry, "));
	fprintf(stderr, curs_err_strings[curs_errno], curs_parm_err);
	fprintf(stderr, ".\r\n");
}
