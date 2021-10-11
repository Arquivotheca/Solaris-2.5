/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lpq.c	1.8	93/05/27 SMI"	/* SVr4.0 1.2	*/

/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Spool Queue examination program
 *
 * lpq [+[n]] [-Pprinter] [user...] [job...]
 *
 * + means continually scan queue until empty
 * -P used to identify printer as per lpr/lprm
 */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include "msgs.h"
#include "lpd.h"
#include "oam_def.h"
#include "lp.h"
#include "printers.h"

char		*User[MAXUSERS];	/* users to process */
int		 Nusers;		/* # of users in user array */
char		*Request[MAXREQUESTS];	/* job number of spool entries */
int		 Nrequests;		/* # of spool requests */
static int	 repeat;		/* + flag indicator */
static int	 slptime = 30;		/* pause between screen refereshes */
int		 lflag;			/* long output option */

#ifdef TERMCAP
int		 dumb;
static char	 PC;
static char	*CL;
static char	*CM;
static char	*SO;
static char	*SE;
static char	*TI;
static char	*TE;

char	*tgetstr();
#endif

static	void	 startup(void);
static	void	 usage(void);
	void	 done(int);

extern int remote_cmd;

main(argc, argv)
int	 argc;
char	*argv[];
{
	register char	*arg;
	register int 	 n;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	Name = argv[0];
	Lhost = gethostname();
	remote_cmd = 1;		/* enable displayq to go remote for status */
	if (!(Printer = getenv("PRINTER")))
		if (!(Printer = getenv("LPDEST")))
			if (!(Printer = getdefault()))
				Printer = DEFLP;
	while (--argc) {
		if ((arg = *++argv)[0] == '+') {
			if (arg[1] != '\0')
				if ((n = atoi(&arg[1])) > 0)
					slptime = n;
			repeat++;
		} else if (arg[0] == '-')
			switch (arg[1]) {
			case 'P':		/* printer name */
				if (arg[2])
					Printer = &arg[2];
				else if (argc > 1) {
					argc--;
					Printer = *++argv;
				}
				break;

			case 'l':		/* long output */
				lflag++;
				break;

			default:
				usage();
		} else {
			if(isdigit(arg[0])) {
				if (Nrequests >= MAXREQUESTS)
					fatal(gettext("too many requests"));
				/*
				** If lpq job# is given construct req-id
				*/
				Request[Nrequests++] = mkreqid(Printer, arg);
			} else if (isrequest(arg)) {
				if (Nrequests >= MAXREQUESTS)
					fatal(gettext("too many requests"));
				Request[Nrequests++] = arg;
			} else {
				if (Nusers >= MAXUSERS)
					fatal(gettext("too many users"));
				User[Nusers++] = arg;
			}
		}
	}
	startup();
	if (repeat) {
#ifdef TERMCAP
		dumb = termcap();
		if (TI)
			tputs(TI, 0, putchar);
#endif
		do {
#ifdef TERMCAP
			if (!dumb) {
				tputs(CL, 0, putchar);
				tputs(tgoto(CM, 0, 0), 0, putchar);
			}
#endif
			if ((n = displayq(lflag)) > 0)
				sleep(slptime);
		} while (n > 0);
#ifdef TERMCAP
		if (!dumb) {
			if (SO)
				tputs(SO, 0, putchar);
			printf(gettext("Hit return to continue"));
			if (SO && SE)
				tputs(SE, 0, putchar);
			while (getchar() != '\n')
				;
			if (TE)
				tputs(TE, 0, putchar);
		}
#endif
	} else
		displayq(lflag);

	done(0);
	/* NOTREACHED */
}

static void
usage(void)
{
	printf(gettext("usage: lpq [-Pprinter] [-l] [+[n]] [[request-id] [user] [job#] ...]\n"));
	exit(1);
}

void
catch(int s)
{
	done(2);
}

static void
startup(void)
{
	register int	try = 0;

	if (sigset(SIGHUP, SIG_IGN) != SIG_IGN)
		(void)sigset(SIGHUP, catch);
	if (sigset(SIGINT, SIG_IGN) != SIG_IGN)
		(void)sigset(SIGINT, catch);
	if (sigset(SIGQUIT, SIG_IGN) != SIG_IGN)
		(void)sigset(SIGQUIT, catch);
	if (sigset(SIGTERM, SIG_IGN) != SIG_IGN)
		(void)sigset(SIGTERM, catch);

    	for (;;) {
		if (mopen() == 0) 
			break;
		else if (errno == ENOSPC && try++ < 5) {
			sleep(3);
			continue;
		} else {
	    		lp_fatal(E_LP_MOPEN);
			/*NOTREACHED*/
		}
	}
}

/**
 ** done() - CLEAN UP AND EXIT
 **/
void
done(int rc)
{
	(void)mclose();
	exit(rc);
	/*NOTREACHED*/
}

/*VARARGS2*/
/*ARGSUSED*/
void
logit(int type, char *msg, ...)
{
#ifdef DEBUG
	va_list		 argp;

	if (!(type & LOG_MASK))
		return;
	va_start(argp, msg);
	(void)vfprintf(stderr, msg, argp);
	va_end(argp);
	fputc('\n', stderr);
#endif
}

#ifdef TERMCAP
static char *
#if 0
capstrings[] = { "clear", "cup", "smso", "rmso", "smcup", "rmcup", 0 };
#else
capstrings[] = { "cl", "cm", "so", "se", "ti", "te", 0 };
#endif

static char **
caps[] = { &CL, &CM, &SO, &SE, &TI, &TE };

static
termcap()
{
	char		*term;
	char		 tbuf[2096];
	static char	 buf[1024];
	char		*bp = buf;
	register char	*cp, **p, ***q;

	if (isatty(1) && (term = getenv("TERM")) && tgetent(tbuf, term) > 0) {
		for (p = capstrings, q = caps; *p; p++, q++)
			**q = tgetstr(*p, &bp);
		if (cp = tgetstr("pad", &bp))
			PC = *cp;
	}
	return(!CL || !CM);
}
#endif
