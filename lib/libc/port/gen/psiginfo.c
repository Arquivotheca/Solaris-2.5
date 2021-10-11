/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)psiginfo.c	1.10	93/07/08 SMI"	/* SVr4.0 1.2	*/

/*
 * Print the name of the siginfo indicated by "sig", along with the
 * supplied message
 */

#ifdef __STDC__
	#pragma weak psiginfo = _psiginfo
#endif
#include	"synonyms.h"
#include	"_libc_gettext.h"
#include	<signal.h>
#include	<siginfo.h>

#define	strsignal(i)	(_libc_gettext(_sys_siglistp[i]))

void
psiginfo(sip, s)
siginfo_t *sip;
char	*s;
{
	char buf[256];
	register char *c;
	const struct siginfolist *listp;

	if (sip == 0)
		return;


	if (sip->si_code <= 0) {
		sprintf(buf,
			_libc_gettext("%s : %s ( from process  %d )\n"),
			s, strsignal(sip->si_signo), sip->si_pid);
	} else if ((listp = &_sys_siginfolist[sip->si_signo-1]) &&
	    sip->si_code <= listp->nsiginfo) {
		c = _libc_gettext(listp->vsiginfo[sip->si_code-1]);
		switch (sip->si_signo) {
		case SIGSEGV:
		case SIGBUS:
		case SIGILL:
		case SIGFPE:
			sprintf(buf,
				_libc_gettext("%s : %s ( [%x] %s)\n"),
				s, strsignal(sip->si_signo),
				sip->si_addr, c);
			break;
		default:
			sprintf(buf,
				_libc_gettext("%s : %s (%s)\n"),
				s, strsignal(sip->si_signo), c);
			break;
		}
	} else {
		sprintf(buf,
			_libc_gettext("%s : %s\n"),
			s, strsignal(sip->si_signo));
	}
	(void) write(2, buf, strlen(buf));
}
