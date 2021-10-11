/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)w.c	1.7	93/09/23 SMI"	/* SVr4.0 1.5	*/

/*
 *		PROPRIETARY NOTICE (Combined)

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
*/

/*
 * This is the new w command which takes advantage of
 * the /proc interface to gain access to the information
 * of all the processes currently on the system.
 *
 * This program also implements 'uptime'.
 *
 * Maintenance note:
 *
 * Much of this code is replicated in whodo.c.  If you're
 * fixing bugs here, then you should probably fix 'em there too.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <utmp.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/procfs.h>		/* /proc header file */
#include <locale.h>
#include <unistd.h>
#include <kstat.h>

#define	NMAX		sizeof (ut->ut_name)
#define	LMAX		sizeof (ut->ut_line)
#define	DIV60(t)	((t+30)/60)	/* x/60 rounded */

#ifdef ERR
#undef ERR
#endif
#define	ERR		(-1)

#define	HSIZE		256		/* size of process hash table 	*/
#define	PROCDIR		"/proc"
#define	INITPROCESS	(pid_t)1	/* init process pid */
#define	NONE		'n'		/* no state */
#define	RUNNING		'r'		/* runnable process */
#define	ZOMBIE		'z'		/* zombie process */
#define	VISITED		'v'		/* marked node as visited */

#define	FSHIFT	8		/* bits to right of fixed binary point */
#define	FSCALE 	(1<<FSHIFT)

struct uproc {
	pid_t	p_upid;			/* process id */
	char	p_state;		/* numeric value of process state */
	dev_t   p_ttyd;			/* controlling tty of process */
	time_t  p_time;			/* seconds of user & system time */
	time_t	p_ctime;		/* seconds of child user & sys time */
	int	p_igintr;		/* 1 = ignores SIGQUIT and SIGINT */
	char    p_comm[PRARGSZ+1];	/* command */
	char    p_args[PRARGSZ+1];	/* command line arguments */
	struct uproc	*p_child,	/* first child pointer */
			*p_sibling,	/* sibling pointer */
			*p_pgrpl,	/* pgrp link */
			*p_link;	/* hash table chain pointer */
};

/*
 *  	define hash table for struct uproc
 *	Hash function uses process id
 *	and the size of the hash table(HSIZE)
 *	to determine process index into the table.
 */
static struct uproc	pr_htbl[HSIZE];

static struct 	uproc	*findhash(pid_t);
static time_t  	findidle(char *);
static void	clnarglist(char *);
static void	showtotals(struct uproc *);
static void	calctotals(struct uproc *);
static void	prttime(time_t, char *);
static void	prtat(long *time);
static void	checkampm(char *str);

static	void	fail(int, char *, ...);
static	void	*safe_kstat_data_lookup(kstat_t *, char *);

static int	fd;
static char	*prog;		/* pointer to invocation name */
static int	header = 1;	/* true if -h flag: don't print heading */
static int	lflag = 1;	/* true if -s flag: short format */
static char	*sel_user;	/* login of particular user selected */
static char 	firstchar;	/* first char of name of prog invoked as */
static int	login;		/* true if invoked as login shell */
static time_t	now;		/* current time of day */
static time_t	uptime;		/* time of last reboot & elapsed time since */
static int	nusers;		/* number of users logged in now */
static time_t	idle;		/* number of minutes user is idle */
static time_t	jobtime;	/* total cpu time visible */
static char	doing[520];	/* process attached to terminal */
static time_t	proctime;	/* cpu time of process in doing */
static pid_t	curpid, empty;
static int	add_times;	/* boolean: add the cpu times or not */

static kstat_ctl_t	*kc;	/* libkstat cookie */

int
main(int argc, char *argv[])
{
	register struct utmp	*ut;
	struct utmp		*utmpbegin;
	struct uproc		*up, *parent, *pgrp;
	struct prpsinfo		info;
	struct sigaction	*actinfo = NULL;
	struct prstatus		statinfo;
	unsigned	utmpend;
	unsigned	size;
	struct stat	sbuf;
	DIR   		*dirp;
	struct	dirent	*dp;
	char		pname[64];
	int		procfd;
	char		*cp;
	register	int i;
	int		days, hrs, mins;
	kstat_t		*ksp;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	login = (argv[0][0] == '-');
	cp = strrchr(argv[0], '/');
	firstchar = login ? argv[0][1] : (cp == 0) ? argv[0][0] : cp[1];
	prog = argv[0];

	while (argc > 1) {
		if (argv[1][0] == '-') {
			for (i = 1; argv[1][i]; i++) {
				switch (argv[1][i]) {

				case 'h':
					header = 0;
					break;

				case 'l':
					lflag++;
					break;
				case 's':
					lflag = 0;
					break;

				case 'u':
				case 'w':
					firstchar = argv[1][i];
					break;

				default:
					(void) fprintf(stderr, gettext(
					    "%s: bad flag %s\n"),
					    prog, argv[1]);
					exit(1);
				}
			}
		} else {
			if (!isalnum(argv[1][0]) || argc > 2) {
				(void) printf(gettext(
				    "usage: %s [ -hlsuw ] [ user ]\n"), prog);
				exit(1);
			} else
				sel_user = argv[1];
		}
		argc--; argv++;
	}

	if ((kc = kstat_open()) == NULL)
		fail(1, "kstat_open(): can't open /dev/kstat");

	/*
	 * read the UTMP_FILE (contains information about each logged in user)
	 */
	if (stat(UTMP_FILE, &sbuf) == ERR) {
		(void) fprintf(stderr, gettext("%s: stat error of %s: %s\n"),
			prog, UTMP_FILE, strerror(errno));
		exit(1);
	}
	size = (unsigned)sbuf.st_size;
	if ((ut = malloc(size)) == NULL) {
		(void) fprintf(stderr, gettext("%s: malloc error of %s: %s\n"),
			prog, UTMP_FILE, strerror(errno));
		exit(1);
	}
	if ((fd = open(UTMP_FILE, O_RDONLY)) == ERR) {
		(void) fprintf(stderr, gettext("%s: open error of %s: %s\n"),
			prog, UTMP_FILE, strerror(errno));
		exit(1);
	}
	if (read(fd, (char *)ut, size) == ERR) {
		(void) fprintf(stderr, gettext("%s: read error of %s: %s\n"),
			prog, UTMP_FILE, strerror(errno));
		exit(1);
	}
	utmpbegin = ut;			/* ptr to start of utmp data */
	utmpend = (unsigned)ut + size;	/* ptr to end of utmp data */
	(void) close(fd);

	(void) time(&now);	/* get current time */

	if (header) {	/* print a header */
		prtat(&now);
		for (ut = utmpbegin; ut < (struct utmp *)utmpend; ut++) {
			if (ut->ut_type == USER_PROCESS) {
				if (! nonuser(*ut))
					nusers++;
			} else if (ut->ut_type == BOOT_TIME) {
				uptime = now - ut->ut_time;
				uptime += 30;
				days = uptime / (60*60*24);
				uptime %= (60*60*24);
				hrs = uptime / (60*60);
				uptime %= (60*60);
				mins = uptime / 60;

				(void) printf(gettext("  up"));
				if (days > 0)
					(void) printf(gettext(
					    " %d day(s),"), days);
				if (hrs > 0 && mins > 0) {
					(void) printf(" %2d:%02d,", hrs, mins);
				} else {
					if (hrs > 0)
						(void) printf(gettext(
						    " %d hr(s),"), hrs);
					if (mins > 0)
						(void) printf(gettext(
						    " %d min(s),"), mins);
				}
			}
		}

		ut = utmpbegin;	/* rewind utmp data */
		(void) printf(gettext((nusers == 1) ?
		    "  %d user" : "  %d users"), nusers);

		/*
		 * Print 1, 5, and 15 minute load averages.
		 */
		if ((ksp = kstat_lookup(kc, "unix", 0, "system_misc")) == NULL)
			fail(0,
			    "kstat_lookup('unix', 0, 'system_misc') failed");
		if (kstat_read(kc, ksp, NULL) == -1)
			fail(1,
			    "kstat_read(%x, '%s') failed", kc, ksp->ks_name);
		(void) printf(gettext(",  load average: %.2f, %.2f, %.2f\n"),
		    (double) ((kstat_named_t *) safe_kstat_data_lookup(ksp,
		    "avenrun_1min"))->value.l / FSCALE,
		    (double) ((kstat_named_t *) safe_kstat_data_lookup(ksp,
		    "avenrun_5min"))->value.l / FSCALE,
		    (double) ((kstat_named_t *) safe_kstat_data_lookup(ksp,
		    "avenrun_15min"))->value.l / FSCALE);

		if (firstchar == 'u')	/* uptime command */
			exit(0);

		if (lflag)
			(void) printf(dcgettext(NULL, "User     tty           "
			    "login@  idle   JCPU   PCPU  what\n", LC_TIME));
		else
			(void) printf(dcgettext(NULL,
			    "User     tty           idle   what\n", LC_TIME));

		(void) fflush(stdout);
	}

	/*
	 * loop through /proc, reading info about each process
	 * and build the parent/child tree
	 */
	if (!(dirp = opendir(PROCDIR))) {
		(void) fprintf(stderr, gettext("%s: could not open %s: %s\n"),
			prog, PROCDIR, strerror(errno));
		exit(1);
	}

	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;
		(void) sprintf(pname, "%s/%s", PROCDIR, dp->d_name);
retry:
		if ((procfd = open(pname, O_RDONLY)) == -1) {
			continue;
		}
		if (ioctl(procfd, PIOCPSINFO, (char *)&info) == -1) {
			int err = errno;
			(void) close(procfd);
			if (err == EAGAIN)
				goto retry;
			if (err != ENOENT)
				(void) fprintf(stderr, gettext(
				"%s: ioctl (PIOCPSINFO) failed on %s: %s\n"),
				    prog, pname, strerror(err));
			continue;
		}

		up = findhash(info.pr_pid);
		up->p_ttyd = info.pr_ttydev;
		up->p_state = (info.pr_zomb? ZOMBIE : RUNNING);
		up->p_time = 0;
		up->p_ctime = 0;
		up->p_igintr = 0;
		(void) strncpy(up->p_comm, info.pr_fname,
		    sizeof (info.pr_fname));
		up->p_args[0] = 0;

		if (up->p_state != NONE && up->p_state != ZOMBIE) {
			if (ioctl(procfd, PIOCSTATUS,
			    (char *)&statinfo) == -1) {
				int err = errno;
				(void) close(procfd);
				if (err == EAGAIN)
					goto retry;
				if (err != ENOENT)
					(void) fprintf(stderr, gettext(
				"%s: ioctl (PIOCSTATUS) failed on %s: %s\n"),
					    prog, pname, strerror(err));
				continue;
			}
			up->p_time = statinfo.pr_utime.tv_sec +
			    statinfo.pr_stime.tv_sec;	/* seconds */
			up->p_ctime = statinfo.pr_cutime.tv_sec +
			    statinfo.pr_cstime.tv_sec;
			if (actinfo == NULL) {
				int maxsig;
				if (ioctl(procfd, PIOCMAXSIG, &maxsig) == -1) {
					(void) close(procfd);
					goto retry;
				}
				actinfo = malloc(
					maxsig * sizeof (struct sigaction));
				if (!actinfo) {
					(void) fprintf(stderr, gettext(
					    "%s: out of memory!: %s\n"),
					    prog, strerror(errno));
					exit(1);
				}
			}
			if (ioctl(procfd, PIOCACTION, actinfo) == -1) {
				int err = errno;
				(void) close(procfd);
				if (err == EAGAIN)
					goto retry;
				if (err != ENOENT)
					(void) fprintf(stderr, gettext(
				"%s: ioctl (PIOCACTION) failed on %s: %s\n"),
					    prog, pname, strerror(err));
				continue;
			}
			up->p_igintr =
				actinfo[SIGINT-1].sa_handler == SIG_IGN &&
				actinfo[SIGQUIT-1].sa_handler == SIG_IGN;

			/*
			 * Process args.
			 */
			up->p_args[0] = 0;
			clnarglist(info.pr_psargs);
			(void) strcat(up->p_args, info.pr_psargs);
			if (up->p_args[0] == 0 ||
			    up->p_args[0] == '-' && up->p_args[1] <= ' ' ||
			    up->p_args[0] == '?') {
				(void) strcat(up->p_args, " (");
				(void) strcat(up->p_args, up->p_comm);
				(void) strcat(up->p_args, ")");
			}
		}

		/*
		 * link pgrp together in case parents go away
		 * Pgrp chain is a single linked list originating
		 * from the pgrp leader to its group member.
		 */
		if (info.pr_pgrp != info.pr_pid) {	/* not pgrp leader */
			pgrp = findhash(info.pr_pgrp);
			up->p_pgrpl = pgrp->p_pgrpl;
			pgrp->p_pgrpl = up;
		}
		parent = findhash(info.pr_ppid);

		/* if this is the new member, link it in */
		if (parent->p_upid != INITPROCESS) {
			if (parent->p_child) {
				up->p_sibling = parent->p_child;
				up->p_child = 0;
			}
			parent->p_child = up;
		}

		(void) close(procfd);
	}

	(void) closedir(dirp);
	(void) time(&now);	/* get current time */

	/*
	 * loop through utmp file, printing process info
	 * about each logged in user
	 */
	for (; ut < (struct utmp *)utmpend; ut++) {
		if (ut->ut_type != USER_PROCESS)
			continue;
		if (sel_user && strncmp(ut->ut_name, sel_user, NMAX) != 0)
			continue;	/* we're looking for somebody else */

		/* print login name of the user */
		(void) printf("%-*.*s ", NMAX, NMAX, ut->ut_name);

		/* print tty user is on */
		(void) printf("%-*.*s", LMAX, LMAX, ut->ut_line);

		/* print when the user logged in */
		if (lflag)
			prtat(&ut->ut_time);

		/* print idle time */
		idle = findidle(ut->ut_line);
		if (idle >= 36 * 60)
			(void) printf(dcgettext(NULL,
			    "%2ddays ", LC_TIME), (idle + 12 * 60) / (24 * 60));
		else
			prttime(idle, " ");
		showtotals(findhash(ut->ut_pid));
	}
	return (0);
}

/*
 *  Prints the CPU time for all processes & children,
 *  and the cpu time for interesting process,
 *  and what the user is doing.
 */
static void
showtotals(register struct uproc *up)
{
	jobtime = 0;
	proctime = 0;
	empty = 1;
	curpid = -1;
	add_times = 1;

	calctotals(up);

	if (lflag) {
		/* print CPU time for all processes & children */
		/* and need to convert clock ticks to seconds first */
		prttime((time_t)jobtime, " ");

		/* print cpu time for interesting process */
		/* and need to convert clock ticks to seconds first */
		prttime((time_t)proctime, " ");
	}
	/* what user is doing, current process */
	(void) printf(" %-.32s\n", doing);
}

/*
 *  This recursive routine descends the process
 *  tree starting from the given process pointer(up).
 *  It used depth-first search strategy and also marked
 *  each node as visited as it traversed down the tree.
 *  It calculates the process time for all processes &
 *  children.  It also finds the interesting process
 *  and determines its cpu time and command.
 */
static void
calctotals(register struct uproc *up)
{
	register struct uproc   *zp;

	/*
	 * Once a node has been visited, stop adding cpu times
	 * for its children so they don't get totalled twice.
	 * Still look for the interesting job for this utmp
	 * entry, however.
	 */
	if (up->p_state == VISITED)
		add_times = 0;
	up->p_state = VISITED;
	if (up->p_state == NONE || up->p_state == ZOMBIE)
		return;

	if (empty && !up->p_igintr) {
		empty = 0;
		curpid = -1;
	}

	if (up->p_upid > curpid && (!up->p_igintr || empty)) {
		curpid = up->p_upid;
		(void) strcpy(doing, up->p_args);
	}

	if (add_times == 1) {
		jobtime += up->p_time + up->p_ctime;
		proctime += up->p_time;
	}

	/* descend for its children */
	if (up->p_child) {
		calctotals(up->p_child);
		for (zp = up->p_child->p_sibling; zp; zp = zp->p_sibling)
			calctotals(zp);
	}
}

/*
 *   Findhash  finds the appropriate entry in the process
 *   hash table (pr_htbl) for the given pid in case that
 *   pid exists on the hash chain. It returns back a pointer
 *   to that uproc structure. If this is a new pid, it allocates
 *   a new node, initializes it, links it into the chain (after
 *   head) and returns a structure pointer.
 */
static struct uproc *
findhash(pid_t pid)
{
	register struct uproc *up, *tp;

	tp = up = &pr_htbl[pid % HSIZE];
	if (up->p_upid == 0) {			/* empty slot */
		up->p_upid = pid;
		up->p_state = NONE;
		up->p_child = up->p_sibling = up->p_pgrpl = up->p_link = 0;
		return (up);
	}
	if (up->p_upid == pid) {		/* found in hash table */
		return (up);
	}
	for (tp = up->p_link; tp; tp = tp->p_link) {	/* follow chain */
		if (tp->p_upid == pid)
			return (tp);
	}
	tp = malloc(sizeof (*tp));		/* add new node */
	if (!tp) {
		(void) fprintf(stderr, gettext("%s: out of memory!: %s\n"),
			prog, strerror(errno));
		exit(1);
	}
	(void) memset((char *)tp, 0, sizeof (*tp));
	tp->p_upid = pid;
	tp->p_state = NONE;
	tp->p_child = tp->p_sibling = tp->p_pgrpl = 0;
	tp->p_link = up->p_link;		/* insert after head */
	up->p_link = tp;
	return (tp);
}

#define	HR	(60 * 60)
#define	DAY	(24 * HR)
#define	MON	(30 * DAY)

/*
 * prttime prints a time in hours and minutes or minutes and seconds.
 * The character string tail is printed at the end, obvious
 * strings to pass are "", " ", or "am".
 */
static void
prttime(time_t tim, char *tail)
{
	char		timestr[30];

	if (tim >= 60) {
		(void) sprintf(timestr, dcgettext(NULL, "%3d:%02d", LC_TIME),
		    (int) tim/60, (int) tim%60);
		(void) printf("%s", timestr);
	} else if (tim > 0) {
		(void) sprintf(timestr, dcgettext(NULL, "    %2d", LC_TIME),
		    (int) tim);
		(void) printf("%s", timestr);
	} else
		(void) printf("      ");
	(void) printf("%s", tail);
}

/*
 * prints a 12 hour time given a pointer to a time of day
 */
static void
prtat(long *time)
{
	struct tm	*p;

	p = localtime(time);
	if (now - *time <= 18 * HR) {
		char timestr[50];
		(void) strftime(timestr, sizeof (timestr),
		    dcgettext(NULL, "%l:%M""%p", LC_TIME), p);
		checkampm(timestr);
		(void) printf(" %s", timestr);
	} else if (now - *time <= 7 * DAY) {
		char weekdaytime[20];

		(void) strftime(weekdaytime, sizeof (weekdaytime),
		    dcgettext(NULL, "%a%l%p", LC_TIME), p);
		checkampm(weekdaytime);
		(void) printf(" %s", weekdaytime);
	} else {
		char monthtime[20];

		(void) strftime(monthtime, sizeof (monthtime),
		    dcgettext(NULL, "%e%b%y", LC_TIME), p);
		(void) printf(" %s", monthtime);
	}
}

/*
 * find & return number of minutes current tty has been idle
 */
static time_t
findidle(char *devname)
{
	struct stat stbuf;
	time_t lastaction, diff;
	char ttyname[64];

	(void) strcpy(ttyname, "/dev/");
	(void) strcat(ttyname, devname);
	if (stat(ttyname, &stbuf) != -1) {
		lastaction = stbuf.st_atime;
		diff = now - lastaction;
		diff = DIV60(diff);
		if (diff < 0)
			diff = 0;
	} else
		diff = 0;
	return (diff);
}

/*
 * given a pointer to the argument string get rid of unsavory characters.
 */
static void
clnarglist(char *arglist)
{
	register char	*c;
	register int 	err = 0;

	/* get rid of unsavory characters */
	for (c = arglist; *c != NULL; c++) {
		if ((*c < ' ') || (*c > 0176)) {
			if (err++ > 5) {
				*arglist = NULL;
				break;
			}
			*c = '?';
		}
	}
}

static void
fail(int do_perror, char *message, ...)
{
	va_list args;

	va_start(args, message);
	(void) vfprintf(stderr, gettext(message), args);
	va_end(args);
	if (do_perror)
		(void) fprintf(stderr, ": %s", strerror(errno));
	(void) fprintf(stderr, "\n");
	exit(1);
}

static void *
safe_kstat_data_lookup(kstat_t *ksp, char *name)
{
	void *fp = kstat_data_lookup(ksp, name);

	if (fp == NULL)
		fail(0, "kstat_data_lookup('%s', '%s') failed",
			ksp->ks_name, name);
	return (fp);
}

/* replaces all occurences of AM/PM with am/pm */
static void
checkampm(char *str)
{
	char *ampm;
	while ((ampm = strstr(str, "AM")) != NULL ||
	    (ampm = strstr(str, "PM")) != NULL) {
		*ampm = tolower(*ampm);
		*(ampm+1) = tolower(*(ampm+1));
	}
}
