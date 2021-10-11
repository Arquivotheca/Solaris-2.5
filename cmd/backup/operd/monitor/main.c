/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)main.c 1.0 91/01/28 SMI"

#ident	"@(#)main.c 1.16 93/06/23"

#include <stdio.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <setjmp.h>
#include <curses.h>
#include <signal.h>
#include "defs.h"

char	*progname;
char	*connected;
char	opserver[BCHOSTNAMELEN];

#ifdef __STDC__
static void run(void);
#else
static void run();
#endif

main(argc, argv)
	int	argc;
	char	*argv[];
{
#ifdef USG
	sigset_t sigs;
#endif
	extern	jmp_buf resetbuf;
	extern	int ready;
	int	result;

	progname = argv[0];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while (--argc && **++argv == '-') {
		switch (*++*argv) {
		case 's':	/* server */
			(void) strcpy(opserver, *++argv);
			if (setopserver(opserver) >= 0)
				connected = opserver;
			--argc;
			break;
		default:
			(void) fprintf(stderr,
			    gettext("usage: %s [ -s server_name ]\n"),
				progname);
			exit(-1);
		}
	}
	if (readconfig((char *)0, (void (*)(const char *, ...))0) != 0) {
		(void) fprintf(stderr,
			gettext("error reading configuration file\n"));
		exit(-1);
	}
	if (!connected)
		(void) getopserver(opserver, sizeof (opserver));
	result = oper_init(opserver, progname, 0);
	if (result == OPERMSG_CONNECTED)
		connected = opserver;
	cmd_init();
	scr_config();			/* initialize curses */
	msg_init();
	if (setjmp(resetbuf) == 1) {	/* restart from SIGPIPE */
		oper_end();
#ifdef USG
		(void) sigfillset(&sigs);
		(void) sigprocmask(SIG_UNBLOCK, &sigs, (sigset_t *)0);
#else
		(void) sigsetmask(0);
#endif
	}
	ready = 1;
	if (result == OPERMSG_CONNECTED) {
		if (oper_login((char *)0, 0) != OPERMSG_SUCCESS)
			connected = (char *)0;
	} else if (result == OPERMSG_ERROR) {
		status(0, gettext("Cannot initialize message system"));
		Exit(-1);
	}
	scr_redraw(1);
	run();
#ifdef lint
	return (0);
#endif
}

static void
#ifdef __STDC__
run(void)
#else
run()
#endif
{
	fd_set	readfds;
	msg_t	msgbuf;
	char	kbdbuf[CANBSIZ];
	int	fd = fileno(stdin);
	int	n;
	time_t	h;

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);			/* monitor stdin */
	for (;;) {
		n = oper_msg(&readfds, &msgbuf);
		switch (n) {
		case OPERMSG_READY:
			if (!FD_ISSET(fd, &readfds))	/* sanity check */
				continue;
			/*
			 * keyboard input ready
			 */
			kbdbuf[0] = (char)getch();
			h = scr_hold(0);
			cmd_dispatch((cmd_t)kbdbuf[0]);
			scr_release(h);
			break;
		case OPERMSG_RCVD:
			/*
			 * got a message
			 */
			msg_dispatch(&msgbuf);
			break;
		case OPERMSG_ERROR:
		default:
			/*
			 * error
			 */
			if (errno && errno != EINTR)
				status(1, "oper_msg: %s", strerror(errno));
			break;
		}
	}
}

void
Exit(exitstat)
	int exitstat;
{
	scr_cleanup();
	if (connected)
		(void) oper_logout(opserver);
	oper_end();
	if (current_status)
		(void) fprintf(stderr, "%s\n", current_status);
	exit(exitstat);
}

char *
strerror(err)
	int err;
{
	extern int sys_nerr;
	extern char *sys_errlist[];

	static char errmsg[32];

	if (err >= 0 && err < sys_nerr)
		return (sys_errlist[err]);

	(void) sprintf(errmsg, gettext("Error %d"), err);
	return (errmsg);
}
