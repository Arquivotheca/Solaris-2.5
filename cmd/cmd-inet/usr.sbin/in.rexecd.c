/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)in.rexecd.c	1.13	94/01/21 SMI"	/* SVr4.0 1.6	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	          All rights reserved.
 *
 */


#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/filio.h>

#include <netinet/in.h>

#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <netdb.h>
#include <syslog.h>

#ifdef SYSV
#include <shadow.h>
#endif /* SYSV */

#ifndef NCARGS
#define	NCARGS	5120
#endif /* NCARGS */

extern	errno;
struct	passwd *getpwnam();

#ifdef SYSV
#define	rindex	strrchr
#define	killpg(a, b)	kill(-(a), (b))
#else
char  *sprintf();
#endif	/* SYSV */

char	*crypt(), *rindex(), *strncat();
/*VARARGS1*/
int	error();

/*
 * remote execute server:
 *	username\0
 *	password\0
 *	command\0
 *	data
 */
/*ARGSUSED*/
main(argc, argv)
	int argc;
	char **argv;
{
	struct sockaddr_in from;
	int fromlen;

	openlog("rexec", LOG_PID | LOG_ODELAY, LOG_DAEMON);
	audit_rexecd_setup();	/* BSM */
	fromlen = sizeof (from);
	if (getpeername(0, (struct sockaddr *) &from, &fromlen) < 0) {
		fprintf(stderr, "%s: ", argv[0]);
		perror("getpeername");
		exit(1);
	}
	doit(0, &from);
	/* NOTREACHED */
}

char	username[20] = "USER=";
char	homedir[64] = "HOME=";
char	shell[64] = "SHELL=";

char	*envinit[] =
#ifdef SYSV
	    {homedir, shell, (char *) 0, username, (char *) 0};
#define	ENVINIT_PATH	2	/* position of PATH in envinit[] */

/*
 *	See PSARC opinion 1992/025
 */
char	userpath[] = "PATH=/usr/bin:";
char	rootpath[] = "PATH=/usr/sbin:/usr/bin";
#else
	    {homedir, shell, "PATH=:/usr/ucb:/bin:/usr/bin", username, 0};
#endif /* SYSV */

struct	sockaddr_in asin = { AF_INET };

doit(f, fromp)
	int f;
	struct sockaddr_in *fromp;
{
	char cmdbuf[NCARGS+1], *cp, *namep;
	char user[16], pass[16];
	struct	hostent *chostp;
	char hostname [MAXHOSTNAMELEN + 1];
	struct passwd *pwd;
	char	*password;
#ifdef SYSV
	struct spwd *shpwd;
#endif /* SYSV */
	int s;
	u_short port;
	pid_t pid;
	int pv[2], cc;
	fd_set readfrom, ready;
	char buf[BUFSIZ], sig;
	int one = 1;

	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
	(void) signal(SIGTERM, SIG_DFL);
#ifdef DEBUG
	{
		int t = open("/dev/tty", 2);
		if (t >= 0) {
#ifdef SYSV
			setsid();
#else
			ioctl(t, TIOCNOTTY, (char *)0);
#endif	/* SYSV */
			(void) close(t);
		}
	}
#endif
        /*
	 * store common info. for audit record
	 */
	chostp = gethostbyaddr((const char *) &fromp->sin_addr,
				sizeof (struct in_addr), fromp->sin_family);
	if (chostp)
		strncpy(hostname, chostp->h_name, sizeof (hostname));
	else
		strncpy(hostname, inet_ntoa(fromp->sin_addr),
			sizeof (hostname));
	dup2(f, 0);
	dup2(f, 1);
	dup2(f, 2);
	(void) alarm(60);
	port = 0;
	for (;;) {
		char c;
		if (read(f, &c, 1) != 1)
			exit(1);
		if (c == 0)
			break;
		port = port * 10 + c - '0';
	}
	(void) alarm(0);
	if (port != 0) {
		s = socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0)
			exit(1);
		if (bind(s, (struct sockaddr *) &asin, sizeof (asin)) < 0)
			exit(1);
		(void) alarm(60);
		fromp->sin_port = htons((u_short)port);
		if (connect(s, (struct sockaddr *) fromp, sizeof (*fromp)) < 0)
			exit(1);
		(void) alarm(0);
	}
	getstr(user, sizeof (user), "username");
	getstr(pass, sizeof (pass), "password");
	getstr(cmdbuf, sizeof (cmdbuf), "command");
	setpwent();
	pwd = getpwnam(user);
#ifdef SYSV
	(void) setspent();	/* shadow password file */
	shpwd = getspnam(user);
#endif /* SYSV */

	if (pwd == NULL
#ifdef SYSV
	|| shpwd == NULL
#endif /* SYSV */
	) {
		audit_rexecd_fail("Login incorrect", hostname,
			user, cmdbuf);	/* BSM */
		error("Login incorrect.\n");
		exit(1);
	}
#ifdef SYSV
	endspent();
#endif /* SYSV */
	endpwent();

#ifdef SYSV
	password = shpwd->sp_pwdp;
#else
	password = pwd->pw_passwd;
#endif /* SYSV */
	if (*password != '\0') {
		namep = crypt(pass, password);
		if (strcmp(namep, password)) {
			audit_rexecd_fail("Password incorrect", hostname,
				user, cmdbuf);	/* BSM */
			error("Password incorrect.\n");
			exit(1);
		}
	}
	if (chdir(pwd->pw_dir) < 0) {
		audit_rexecd_fail("No remote directory", hostname,
			user, cmdbuf);	/* BSM */
		error("No remote directory.\n");
		exit(1);
	}
	(void) write(2, "\0", 1);
	if (port) {
		(void) pipe(pv);
		pid = fork();
		if (pid == (pid_t)-1)  {
			error("Try again.\n");
			exit(1);
		}
		if (pid) {
			(void) close(0); (void) close(1); (void) close(2);
			(void) close(f); (void) close(pv[1]);
			FD_SET(s, &readfrom);
			FD_SET(pv[0], &readfrom);
			ioctl(pv[0], FIONBIO, (char *)&one);
			/* should set s nbio! */
			do {
				ready = readfrom;
				(void) select(16, &ready, (fd_set *)0,
				    (fd_set *)0, (struct timeval *)0);
				if (FD_ISSET(s, &ready)) {
					if (read(s, &sig, 1) <= 0)
						FD_CLR(s, &readfrom);
					else
						killpg(pid, sig);
				}
				if (FD_ISSET(pv[0], &ready)) {
					cc = read(pv[0], buf, sizeof (buf));
					if (cc <= 0) {
						shutdown(s, 1+1);
						FD_CLR(pv[0], &readfrom);
					} else
						(void) write(s, buf, cc);
				}
			} while (FD_ISSET(s, &readfrom) ||
				 FD_ISSET(pv[0], &readfrom));
			exit(0);
		}
		setpgrp(0, getpid());
		(void) close(s); (void)close(pv[0]);
		dup2(pv[1], 2);
	}
	audit_rexecd_success(hostname, user, cmdbuf);	/* BSM */

	if (*pwd->pw_shell == '\0')
		pwd->pw_shell = "/bin/sh";
	if (f > 2)
		(void) close(f);
	if (setgid((gid_t)pwd->pw_gid) < 0) {
		audit_rexecd_fail("Can't setgid", hostname,
			user, cmdbuf);	/* BSM */
		error("setgid");
		exit(1);
	}
	initgroups(pwd->pw_name, pwd->pw_gid);
	if (setuid((uid_t)pwd->pw_uid) < 0) {
		audit_rexecd_fail("Can't setuid", hostname,
			user, cmdbuf);	/* BSM */
		error("setuid");
		exit(1);
	}
#ifdef	SYSV
	if (pwd->pw_uid)
		envinit[ENVINIT_PATH] = userpath;
	else
		envinit[ENVINIT_PATH] = rootpath;
#endif	/* SYSV */
	strncat(homedir, pwd->pw_dir, sizeof (homedir) - 6);
	strncat(shell, pwd->pw_shell, sizeof (shell) - 7);
	strncat(username, pwd->pw_name, sizeof (username) - 6);
	cp = rindex(pwd->pw_shell, '/');
	if (cp)
		cp++;
	else
		cp = pwd->pw_shell;
	execle(pwd->pw_shell, cp, "-c", cmdbuf, (char *)0, envinit);
	perror(pwd->pw_shell);
	setuid(0);
	audit_rexecd_fail("Can't exec", hostname,
		user, cmdbuf);	/* BSM */
	exit(1);
}

/*VARARGS1*/
error(fmt, a1, a2, a3)
	char *fmt;
	int a1, a2, a3;
{
	char buf[BUFSIZ];

	buf[0] = 1;
	(void) sprintf(buf+1, fmt, a1, a2, a3);
	(void) write(2, buf, strlen(buf));
}

getstr(buf, cnt, err)
	char *buf;
	int cnt;
	char *err;
{
	char c;

	do {
		if (read(0, &c, 1) != 1)
			exit(1);
		*buf++ = c;
		if (--cnt == 0) {
			error("%s too long\n", err);
			exit(1);
		}
	} while (c != 0);
}
