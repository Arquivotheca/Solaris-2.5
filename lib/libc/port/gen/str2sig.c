/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)str2sig.c	1.13	94/10/25 SMI"	/* SVr4.0 1.1	*/

#ifdef __STDC__
	#pragma weak str2sig = _str2sig
	#pragma weak sig2str = _sig2str
#endif
#include "synonyms.h"
#include <signal.h>

typedef struct signame {
	const char *sigstr;
	const int   signum;
} signame_t;

static signame_t signames[] = {
	{ "EXIT",	0 },
	{ "HUP",	SIGHUP },
	{ "INT",	SIGINT },
	{ "QUIT",	SIGQUIT },
	{ "ILL",	SIGILL },
	{ "TRAP",	SIGTRAP },
	{ "ABRT",	SIGABRT },
	{ "IOT",	SIGIOT },
	{ "EMT",	SIGEMT },
	{ "FPE",	SIGFPE },
	{ "KILL",	SIGKILL },
	{ "BUS",	SIGBUS },
	{ "SEGV",	SIGSEGV },
	{ "SYS",	SIGSYS },
	{ "PIPE",	SIGPIPE },
	{ "ALRM",	SIGALRM },
	{ "TERM",	SIGTERM },
	{ "USR1",	SIGUSR1 },
	{ "USR2",	SIGUSR2 },
	{ "CLD",	SIGCLD },
	{ "CHLD",	SIGCHLD },
	{ "PWR",	SIGPWR },
	{ "WINCH",	SIGWINCH },
	{ "URG",	SIGURG },
	{ "POLL",	SIGPOLL },
	{ "IO",		SIGPOLL },
	{ "STOP",	SIGSTOP },
	{ "TSTP",	SIGTSTP },
	{ "CONT",	SIGCONT },
	{ "TTIN",	SIGTTIN },
	{ "TTOU",	SIGTTOU },
	{ "VTALRM",	SIGVTALRM },
	{ "PROF",	SIGPROF },
	{ "XCPU",	SIGXCPU },
	{ "XFSZ",	SIGXFSZ },
	{ "WAITING",	SIGWAITING },
	{ "LWP",	SIGLWP },
	{ "FREEZE",	SIGFREEZE },
	{ "THAW",	SIGTHAW },
	{ "CANCEL",	SIGCANCEL },
	{ "RTMIN",	_SIGRTMIN },
	{ "RTMIN+1",	_SIGRTMIN+1 },
	{ "RTMIN+2",	_SIGRTMIN+2 },
	{ "RTMIN+3",	_SIGRTMIN+3 },
	{ "RTMAX-3",	_SIGRTMAX-3 },
	{ "RTMAX-2",	_SIGRTMAX-2 },
	{ "RTMAX-1",	_SIGRTMAX-1 },
	{ "RTMAX",	_SIGRTMAX },
};

#define	SIGCNT	(sizeof (signames)/sizeof (struct signame))

str2sig(s, sigp)
const char *s;
int *sigp;
{
	const struct signame *sp;

	if (*s >= '0' && *s <= '9') {
		int i = atoi(s);
		for (sp = signames; sp < &signames[SIGCNT]; sp++) {
			if (sp->signum == i) {
				*sigp = sp->signum;
				return (0);
			}
		}
		return (-1);
	} else {
		for (sp = signames; sp < &signames[SIGCNT]; sp++) {
			if (strcmp(sp->sigstr, s) == 0) {
				*sigp = sp->signum;
				return (0);
			}
		}
		return (-1);
	}
}

sig2str(i, s)
int i;
char *s;
{
	const struct signame *sp;

	for (sp = signames; sp < &signames[SIGCNT]; sp++) {
		if (sp->signum == i) {
			strcpy(s, sp->sigstr);
			return (0);
		}
	}
	return (-1);
}
