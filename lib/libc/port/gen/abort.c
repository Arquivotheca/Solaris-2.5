/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)abort.c	1.11	95/03/08 SMI"	/* SVr4.0 1.17	*/
/*	3.0 SID #	1.4	*/
/*LINTLIBRARY*/
/*
 *	abort() - terminate current process with dump via SIGABRT
 */

#include "synonyms.h"
#include <signal.h>
#include <stdlib.h>

extern void _cleanup();
static pass = 0;	/* counts how many times abort has been called */

void
abort()
{
	sigset_t	set;
	struct sigaction	act;

	if (!sigaction(SIGABRT, NULL, &act) &&
	    act.sa_handler != SIG_DFL && act.sa_handler != SIG_IGN) {
		/*
		 * User handler is installed, invokes user handler before
		 * taking default action.
		 *
		 * Send SIGABRT, unblock SIGABRT if blocked.
		 * If there is pending signal SIGABRT, we only need to unblock
		 * SIGABRT.
		 */
		if (!sigprocmask(SIG_SETMASK, NULL, &set) &&
		    sigismember(&set, SIGABRT)) {
			if (!sigpending(&set) && !sigismember(&set, SIGABRT))
				kill(getpid(), SIGABRT);
			sigrelse(SIGABRT);
		} else
			kill(getpid(), SIGABRT);
	}

	if (++pass == 1)
		_cleanup();

	for (;;) {
		(void) signal(SIGABRT, SIG_DFL);
		(void) sigrelse(SIGABRT);
		kill(getpid(), SIGABRT);
	}
}
