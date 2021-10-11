/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)uname.c	1.13	94/10/10 SMI"	/* SVr4.0 1.29	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		  All rights reserved.
 */

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

#define	ROOTUID	(uid_t)0

static void usage(void);

/* ARGSUSED */
int
main(int argc, char *argv[], char *envp[])
{
	char *nodename;
	char *optstring = "asnrpvmiS:";
	int sflg = 0, nflg = 0, rflg = 0, vflg = 0, mflg = 0;
	int pflg = 0, iflg = 0, Sflg = 0;
	int errflg = 0, optlet;
	struct utsname  unstr, *un;
	char fmt_string[] = " %.*s";
	char *fs = &fmt_string[1];
	char procbuf[SYS_NMLN];

	(void) umask(~(S_IRWXU|S_IRGRP|S_IROTH) & S_IAMB);
	un = &unstr;
	uname(un);

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((optlet = getopt(argc, argv, optstring)) != EOF)
		switch (optlet) {
		case 'a':
			sflg++; nflg++; rflg++; vflg++; mflg++; pflg++; iflg++;
			break;
		case 's':
			sflg++;
			break;
		case 'n':
			nflg++;
			break;
		case 'r':
			rflg++;
			break;
		case 'v':
			vflg++;
			break;
		case 'm':
			mflg++;
			break;
		case 'p':
			pflg++;
			break;
		case 'i':
			iflg++;
			break;
		case 'S':
			Sflg++;
			nodename = optarg;
			break;
		case '?':
			errflg++;
		}

	if (errflg || (optind != argc))
		usage();

	if ((Sflg > 1) ||
	    (Sflg && (sflg || nflg || rflg || vflg || mflg || pflg))) {
		usage();
	}

	/* If we're changing the system name */
	if (Sflg) {
		FILE *file;
		char curname[SYS_NMLN];
		int len = strlen(nodename);
		int curlen, i;

		if (getuid() != ROOTUID) {
			if (geteuid() != ROOTUID) {
				(void) fprintf(stderr, gettext(
					"uname: not super user\n"));
				exit(1);
			}
		}

		/*
		 * The size of the node name must be less than SYS_NMLN.
		 */
		if (len > SYS_NMLN - 1) {
			(void) fprintf(stderr, gettext(
				"uname: name must be <= %d letters\n"),
				SYS_NMLN-1);
			exit(1);
		}

		/*
		 * Only modify the file if the name requested is
		 * different than the name currently stored.
		 * This will mainly be useful at boot time
		 * when 'uname -S' is called with the name stored
		 * in the file as an argument, to change the
		 * name of the machine from the default to the
		 * stored name.  In this case only the string
		 * in the global utsname structure must be changed.
		 */
		if ((file = fopen("/etc/nodename", "r")) != NULL) {
			curlen = fread(curname, sizeof (char), SYS_NMLN, file);
			for (i = 0; i < curlen; i++) {
				if (curname[i] == '\n') {
					curname[i] = '\0';
					break;
				}
			}
			if (i == curlen) {
				curname[curlen] = '\0';
			}
			(void) fclose(file);
		} else {
			curname[0] = '\0';
		}
		if (strcmp(curname, nodename) != 0) {
			if ((file = fopen("/etc/nodename", "w")) == NULL) {
				(void) fprintf(stderr, gettext(
					"uname: error in writing name\n"));
				exit(1);
			}
			if (fprintf(file, "%s\n", nodename) < 0) {
				(void) fprintf(stderr, gettext(
					"uname: error in writing name\n"));
				exit(1);
			}
			(void) fclose(file);
		}

		/*
		 * Replace name in kernel data section.
		 */
		if (sysinfo(SI_SET_HOSTNAME, nodename, len) < 0) {
			(void) fprintf(stderr, gettext(
				"uname: error in setting name\n"));
			exit(1);
		}
		return (0);
	}

	/*
	 * "uname -s" is the default.
	 */
	if (!(sflg || nflg || rflg || vflg || mflg || pflg || iflg))
		sflg++;

	if (sflg) {
		(void) fprintf(stdout, fs, sizeof (un->sysname), un->sysname);
		fs = fmt_string;
	}
	if (nflg) {
		(void) fprintf(stdout, fs, sizeof (un->nodename), un->nodename);
		fs = fmt_string;
	}
	if (rflg) {
		(void) fprintf(stdout, fs, sizeof (un->release), un->release);
		fs = fmt_string;
	}
	if (vflg) {
		(void) fprintf(stdout, fs, sizeof (un->version), un->version);
		fs = fmt_string;
	}
	if (mflg) {
		(void) fprintf(stdout, fs, sizeof (un->machine), un->machine);
		fs = fmt_string;
	}
	if (pflg) {
		if (sysinfo(SI_ARCHITECTURE, procbuf, sizeof (procbuf)) == -1) {
			(void) fprintf(stderr, gettext(
				"uname: sysinfo failed\n"));
			exit(1);
		}
		(void) fprintf(stdout, fs, strlen(procbuf), procbuf);
		fs = fmt_string;
	}
	if (iflg) {
		if (sysinfo(SI_PLATFORM, procbuf, sizeof (procbuf)) == -1) {
			(void) fprintf(stderr, gettext(
				"uname: sysinfo failed\n"));
			exit(1);
		}
		(void) fprintf(stdout, fs, strlen(procbuf), procbuf);
		fs = fmt_string;
	}
	(void) putchar('\n');
	return (0);
}

static void
usage(void)
{
	(void) fprintf(stderr, gettext(
		"usage:  uname [-snrvmapi]\n"
		"        uname [-S system_name]\n"));
	exit(1);
}
