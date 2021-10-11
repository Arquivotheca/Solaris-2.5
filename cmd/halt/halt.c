/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)halt.c	1.12	94/12/19	SMI"	/* SVr4.0 1.2   */

/* *****************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice

Notice of copyright on this source code product does not indicate
publication.

	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
		All rights reserved.
****************************************************************** */

/*
 * Halt
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/uadmin.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <locale.h>
#include <libintl.h>
#include <libgen.h>

#define	RB_NOSYNC  1

int audit_halt_setup(), audit_halt_success(), audit_halt_fail();

main(argc, argv)
	int argc;
	char **argv;
{
	int opterr = 0;
	int fcn = 0;
	int nflag = 0;
	int c;
	char *ttyname(), *ttyn = ttyname(2);
	register int qflag = 0;
	int needlog = 1;
	char *user, *getlogin(), *cmdname;
	struct passwd *pw, *getpwuid();

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"  /* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	audit_halt_setup();

	cmdname = basename(*argv);

	openlog(cmdname, 0, LOG_AUTH);

	if (strcmp(cmdname, "halt") == 0)
		fcn = AD_HALT;
	else if (strcmp(cmdname, "poweroff") == 0)
		fcn = AD_POWEROFF;

	while ((c = getopt(argc, argv, "lnqy")) != EOF) {
		switch (c) {
			case 'n':
				nflag |= RB_NOSYNC;
				continue;
			case 'y':
				ttyn = 0;
				continue;
			case 'q':
				qflag++;
				continue;
			case 'l':
				needlog = 0;
				continue;
			case '?':
				opterr++;
				break;
		}
	}

	if (opterr) {
	/*
	 * TRANSLATION_NOTE
	 * don`t translate the word "halt"
	 */
		(void) fprintf(stderr,
			gettext("usage: %s [ -lnqy ]\n"),
			cmdname);
		exit(1);
	}

	if (ttyn && !strncmp(ttyn, "/dev/term/", strlen("/dev/term/"))) {
		/*
		 * TRANSLATION_NOTE
		 * don`t translate the word "halt"
		 * don`t translate ``halt -y''
		 */
		(void) fprintf(stderr,
			gettext("%s: dangerous on a dialup;"),
			cmdname);
		(void) fprintf(stderr,
			gettext("use ``%s -y'' if you are really sure\n"),
			cmdname);

		audit_halt_fail();
		exit(1);
	}

	if (geteuid() != 0) {
		(void) fprintf(stderr,
			gettext("%s: Permission denied\n"),
			cmdname);
		audit_halt_fail();
		exit(1);
	}

	if (needlog) {
		user = getlogin();
		if (user == (char *)0 && (pw = getpwuid(getuid())))
			user = pw->pw_name;
		if (user == (char *)0)
			user = "root";
		syslog(2, "%sed by %s", cmdname, user);
	}

	/* Assume successful unless you log a failure, too. */
	audit_halt_success();

	(void) signal(SIGHUP, SIG_IGN);		/* for network connections */
	if (kill(1, SIGTSTP) == -1) {
		/*
		 * TRANSLATION_NOTE
		 * don`t translate the word "halt"
		 * don`t translate the word "init"
		 */
		(void) fprintf(stderr,
			gettext("%s: can't idle init\n"),
			cmdname);
		audit_halt_fail();
		exit(1);
	}
	sleep(1);
	/*
	 * Make sure we don't get stopped by a jobcontrol shell
	 * once we start killing everybody.
	 */
	(void) signal(SIGTSTP, SIG_IGN);
	(void) signal(SIGTTIN, SIG_IGN);
	(void) signal(SIGTTOU, SIG_IGN);
	(void) signal(SIGTERM, SIG_IGN);
	(void) kill(-1, SIGTERM);	/* one chance to catch it */
	sleep(5);

	if (!qflag && (nflag & RB_NOSYNC) == 0) {
		markdown();
		sync();
		setalarm(5);
		pause();
	}
	reboot(fcn);
	perror("reboot");
	exit(0);
	/* NOTREACHED */
}

reboot(fcn)
	int fcn;
{

	(void) uadmin(A_SHUTDOWN, fcn, 0);
}  

void
dingdong(i)
int i;
{
	/* RRRIIINNNGGG RRRIIINNNGGG */
}

setalarm(n)
	unsigned n;
{
	(void) signal(SIGALRM, dingdong);
	alarm(n);
}

#include <utmp.h>
#include <utmpx.h>
#define	SCPYN(a, b)	strncpy(a, b, sizeof (a))
char	wtmpf[]	= "/var/adm/wtmp";
char	wtmpxf[] = "/var/adm/wtmpx";
struct utmp wtmp;
struct utmpx wtmpx;

markdown()
{
	off_t lseek();
	time_t time();
	int f;

	if ((f = open(wtmpxf, O_WRONLY)) >= 0) {
		lseek(f, 0L, 2);
		SCPYN(wtmpx.ut_line, "~");
		SCPYN(wtmpx.ut_name, "shutdown");
		SCPYN(wtmpx.ut_host, "");
		time(&wtmpx.ut_tv.tv_sec);
		write(f, (char *)&wtmpx, sizeof (wtmpx));
		close(f);
	}
	if ((f = open(wtmpf, O_WRONLY)) >= 0) {
		lseek(f, 0L, 2);
		SCPYN(wtmp.ut_line, "~");
		SCPYN(wtmp.ut_name, "shutdown");
		wtmp.ut_time = wtmpx.ut_tv.tv_sec;
		write(f, (char *)&wtmp, sizeof (wtmp));
		close(f);
	}
}
