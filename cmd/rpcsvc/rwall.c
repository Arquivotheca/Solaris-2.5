#pragma ident	"@(#)rwall.c	1.8	94/10/21 SMI"

/*
 * rwall.c
 *	The client rwall program
 *
 * Copyright (c) 1984 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <utmp.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <setjmp.h>
#include <pwd.h>
#include <rpcsvc/rwall.h>
#include <netconfig.h>
#include <sys/time.h>

#define	USERS	128
char who[9] = "???";
char *path;
struct netconfig *nconf = NULL;

main(argc, argv)
	int argc;
	char **argv;
{
	int msize;
	char buf[BUFSIZ];
	register i;
	struct utmp utmp[USERS];
	FILE *f;
	int sline;
	char hostname[256];
	int hflag;
	struct passwd *pwd;
	void *handle;

	if (argc < 2)
		usage();
	(void) gethostname(hostname, sizeof (hostname));

	if ((f = fopen("/etc/utmp", "r")) == NULL) {
		fprintf(stderr, "Cannot open /etc/utmp\n");
		exit(1);
	}
	sline = ttyslot(); /* 'utmp' slot no. of sender */
	fread((char *)utmp, sizeof (struct utmp), USERS, f);
	fclose(f);
	if (sline > 0)
		strncpy(who, utmp[sline].ut_name, sizeof (utmp[sline].ut_name));
	else {
		pwd = getpwuid(getuid());
		if (pwd)
			strncpy(who, pwd->pw_name, 9);
	}

	sprintf(buf, "From %s@%s:  ", who, hostname);
	msize = strlen(buf);
	while ((i = getchar()) != EOF) {
		if (msize >= sizeof (buf)) {
			fprintf(stderr, "Message too long\n");
			exit(1);
		}
		buf[msize++] = i;
	}

	handle = setnetpath();
	while ((nconf = getnetpath(handle)) != NULL) {
		if (nconf->nc_semantics == NC_TPI_CLTS) {
			break;
		}
	}
	if (nconf == NULL) {
		fprintf(stderr,
			"No connectionless transports are available.\n");
		endnetpath(handle);
		exit(1);
	}
	buf[msize] = '\0';
	path = buf;
	hflag = 1;
	while (argc > 1) {
		if (argv[1][0] == '-') {
			switch (argv[1][1]) {
				case 'h':
					hflag = 1;
					break;
				case 'n':
					hflag = 0;
					break;
				default:
					usage();
					break;
			}
		} else if (hflag) {
			doit(argv[1]);
		} else {
			char *machine, *user, *domain;

			setnetgrent(argv[1]);
			while (getnetgrent(&machine, &user, &domain)) {
				if (machine)
					doit(machine);
				else
					doall();
			}
			endnetgrent();
		}
		argc--;
		argv++;
	}
	endnetpath(handle);
	exit(0);
}

/*
 * Saw a wild card, so do everything
 */
doall()
{
	fprintf(stderr, "writing to everyone not yet supported\n");
}

#define	PATIENCE 10

doit(hostname)
	char *hostname;
{
	CLIENT *clnt;
	struct timeval tp;

#ifdef DEBUG
	fprintf(stderr, "sending message to %s\n%s\n", hostname, path);
	return;
#endif
	tp.tv_sec = PATIENCE;
	tp.tv_usec = 0;
	clnt = clnt_tp_create_timed(hostname, WALLPROG, WALLVERS, nconf, &tp);
	if (clnt) {
		if (wallproc_wall_1(&path, clnt) == NULL) {
			clnt_perror(clnt, hostname);
		}
		clnt_destroy(clnt);
	} else {
		fprintf(stderr, "rwall: Can't send to %s\n", hostname);
		clnt_pcreateerror(hostname);
	}
	return (0);
}

static
usage()
{
	fprintf(stderr,
		"Usage: rwall host .... [-n netgroup ....] [-h host ...]\n");
	exit(1);
}
