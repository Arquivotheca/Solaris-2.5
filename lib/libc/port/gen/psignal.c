/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)psignal.c	1.11	94/08/26 SMI"	/* SVr4.0 1.1	*/

/*
 * Print the name of the signal indicated by "sig", along with the
 * supplied message
 */

#ifdef __STDC__
	#pragma weak psignal = _psignal
#endif
#include	"synonyms.h"
#include	"_libc_gettext.h"
#include	<signal.h>
#include	<siginfo.h>

extern const char	**_sys_siglistp;

#define	strsignal(i)	(_libc_gettext(_sys_siglistp[i]))

void
psignal(sig, s)
int	sig;
const char *s;
{
	register char *c;
	register int n;
	char buf[256];

	if (sig < 0 || sig >= NSIG)
		sig = 0;
	c = strsignal(sig);
	n = strlen(s);
	if (n) {
		sprintf(buf, "%s: %s\n", s, c);
	}else{
		sprintf(buf, "%s\n", c);
	}
	(void) write(2, buf, (unsigned)strlen(buf));
}
