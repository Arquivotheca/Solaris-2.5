#ident	"@(#)tapedbfix.c 1.14 93/10/13"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

/*
 * dumpdm -s rmtc -V tapelist | grep ^rmtc_tl: | sort +5n > /tmp/b
 *
 * Lines from dumpdm:
 *	    file	f/s		time
 *    tapename	level		device
 * rmtc_tl:00000 1 5 rmtc:/ /dev/md0a 669518925 Wed Mar 20 18:28:45 1991
 *	0	 1 2 3		4	5	...
 *
 */

#include "structs.h"
#include "tapelib.h"

struct tapeinfo_f {
	int	t_level;	/* for this dump level */
	int	t_written;	/* last time when tape was written */
	int	t_expdate;	/* expires this time */
	int	t_expcycle;	/* expires this cycle */
	struct tapeinfo_f *t_next;	/* other levels out here */
};

#define	MAXTAPES 100000		/* 00000..99999 */
static struct tapeinfo_f *alltapes[MAXTAPES];	/* 10^5 tapes max; init NULL */

#define	NLEVELS 10
static int	max_save[NLEVELS];	/* longest save time per level */

#define	SECS_IN_DAY	(60*60*24)

static void
usage(void)
{
	(void) fprintf(stderr, gettext("Usage: %s configfile\n"), progname);
}

static struct tapeinfo_f *
findtapeinfo(tapenum, level)
{
	struct tapeinfo_f *t;
	for (t = alltapes[tapenum]; t; t = t->t_next)
		if (t->t_level == level)
			return (t);
	t = (struct tapeinfo_f *) calloc(1, sizeof (struct tapeinfo_f));
	if (t == NULL)
		die(gettext("Out of memory\n"));
	t->t_level = level;
	t->t_next = alltapes[tapenum];
	alltapes[tapenum] = t;
	return (t);
}

main(argc, argv)
	char	*argv[];
{
	char	tapelistcmd[MAXLINELEN];
	char	line[MAXLINELEN];
	char	*p;
	int	i;
	int	j;
	FILE	*in;
	int	tapenum;	/* current tape number */
	int	tapedate;	/* current tape write date */
	int	now = time((time_t *) 0);
	struct tapeinfo_f *t;
	int	level;
	int	fid;
	int	ntapes;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	for (argc--, argv++; argc; argc--, argv++) {
		if (argv[0][0] == '-') {
			if (argv[0][2] != '\0')
				die(gettext(
			"All switches to %s are single characters\n"),
					progname);
			switch (argv[0][1]) {
			case 'd':
				debug = 1;
				break;
			default:
				usage();
				exit(1);
			}
		} else
			break;
	}
	if (argc != 1) {
		(void) fprintf(stderr, gettext(
			"%s: Need a configuration file name from \
which to deduce library info\n"), progname);
		usage();
		exit(1);
	}
	checkroot(0);
	(void) strcpy(filename, argv[0]);
	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) == -1)
		die(gettext("Cannot determine this host's name\n"));

	(void) sprintf(confdir, "%s/dumps", gethsmpath(etcdir));
	if (chdir(confdir) == -1)
		die(gettext("Cannot chdir to %s; run this program as root\n"),
			confdir);
	openconfig(filename);	/* stays open mostly */
	readit();
	(void) fprintf(stderr, gettext(
		"Read configuration file `%s'\n"), filename);

	if ((fid = open(cf_tapelib, 0)) == -1) {
		fid = creat(cf_tapelib, 0660);
		if (fid == -1)
			die(gettext(
			    "Cannot create missing tape library `%s', \
check permissions on %s\n"),
				cf_tapelib, confdir);
		if (write(fid, tapelibfilesecurity, strlen(tapelibfilesecurity))
		    != strlen(tapelibfilesecurity))
			die(gettext("Cannot write to new tape file\n"));
		if (close(fid) == -1)
			die(gettext("%s failed (%d)\n"), "close", 22);
		(void) printf("%s: created %s\n", progname, cf_tapelib);
	} else
		if (close(fid) == -1)
			die(gettext("%s failed (%d)\n"), "close", 77);

	tl_open(cf_tapelib, NULL);

	(void) sprintf(tapelistcmd,
	    "%s/dumpdm -s %s -V tapelist | grep \\^%s: | sort +5n",
		gethsmpath(sbindir), cf_dumplib, cf_tapelib);
	(void) printf(gettext("%s: Invoking dump database extraction:\n%s\n"),
		progname, tapelistcmd);
	in = popen(tapelistcmd, "r");
	for (ntapes = 0; fgets(line, MAXLINELEN, in); ntapes++) {
		chop(line);

		split(line, " ");	/* yields splitfields[] & */
					/* nsplitfields */

#define	NTAPELISTFIELDS 11
		if (nsplitfields != NTAPELISTFIELDS) {
			(void) printf(gettext(
		"Ignoring bad input line (%d fields instead of %d):\n%s\n"),
				nsplitfields, NTAPELISTFIELDS, line);
			continue;
		}
		/* isolate tape number: */
		p = index(splitfields[0], ':');
		if (p == NULL)
			die(gettext(
			    "Cannot find `:' in tape name for\n%s\n"), line);
		tapenum = atoi(p + 1);
		tapedate = atoi(splitfields[5]);

		/* save data: */
		t = findtapeinfo(tapenum, atoi(splitfields[2]));
		if (tapedate > t->t_written)
			t->t_written = tapedate;
	}
	(void) printf(gettext("%s: Processed %d dump records from %s\n"),
		progname, ntapes, "dumpdm");
	if (ntapes == 0)
		die(gettext(
		    "No tapes found!  Maybe dump database server is busy.\n"));

	/* find maximum save level for each keep spec: */
	for (j = 0; j < ncf_keep; j++) {
		(void) sscanf(&cf_keep[j].k_level, "%1d", &level);
		if (cf_keep[j].k_days < 0)
			max_save[level] = -1;
		if (max_save[level] >= 0 && cf_keep[j].k_days > max_save[level])
			max_save[level] = cf_keep[j].k_days;
	}
	for (j = 0; j < NLEVELS; j++)
		if (max_save[level] == 0)
			max_save[level] = 3;	/* min 3 days */

	/* process each level, setting new exp. dates & cycles for each tape */
	for (i = 0; i < MAXTAPES; i++) {
		int	justbigger;
		int	tape_age_in_days;

		for (t = alltapes[i]; t; t = t->t_next) {
#define	MAXBIGGER 9999999
			justbigger = MAXBIGGER;
			tape_age_in_days = (now - t->t_written) / SECS_IN_DAY;

			/* find appropriate keep spec: */
			for (j = 0; j < ncf_keep; j++) {
				if (cf_keep[j].k_level == splitfields[2][0] &&
				    cf_keep[j].k_days > tape_age_in_days &&
				    cf_keep[j].k_days < justbigger) {
					justbigger = cf_keep[j].k_days;
				}
			}

			/* No tape expires before 3 cycles from now: */
			t->t_expcycle = cf_mastercycle + 3;

			if (justbigger == MAXBIGGER) {
				int	n;
				(void) sscanf(&splitfields[2][0], "%1d", &n);
				justbigger = max_save[n];
			}
			if (justbigger < 0)
				t->t_expdate = justbigger;	/* infinity */
			else
				t->t_expdate =
				    t->t_written + justbigger * SECS_IN_DAY;

			/*
			 * Recent tapes we should try to do good cycle
			 * calculations
			 */
			/* but I don't rightly see how to do it right now */
		}
	}

	ntapes = 0;
	for (i = 0; i < MAXTAPES; i++) {
		int	maxexp, maxcycle;
		struct tapedesc_f statbuffer;
		int	tapechanged;

		if (alltapes[i] == NULL)
			continue;

		tapechanged = 0;
		maxexp = 0;
		maxcycle = 0;
		for (t = alltapes[i]; t; t = t->t_next) {
			if (t->t_expdate < 0 ||
			    (maxexp >= 0 && t->t_expdate > maxexp))
				maxexp = t->t_expdate;
			if (t->t_expcycle > maxcycle)
				maxcycle = t->t_expcycle;
		}

		tl_read(i, &statbuffer);
		if ((statbuffer.t_status & TL_STATMASK) != TL_USED) {
			statbuffer.t_status =
				(statbuffer.t_status & ~TL_STATMASK) | TL_USED;
			tapechanged = 1;
			(void) printf(gettext(
				"%05.5d: Marked as in-use\n"), i);
		}
		if ((statbuffer.t_status & TL_LABELED) == 0) {
			statbuffer.t_status |= TL_LABELED;
			(void) printf(gettext(
				"%05.5d: Marked as labeled\n"), i);
			tapechanged = 1;
		}
		if (statbuffer.t_expdate >= 0) {	/* non-infinite? */
			if (maxexp < 0 || (maxexp > statbuffer.t_expdate)) {
				statbuffer.t_expdate = maxexp;
				(void) printf(gettext(
			    "%05.5d: Updated expiration date to %d\n"),
					i, maxexp);
				tapechanged = 1;
			}
		}
		if (statbuffer.t_expcycle < maxcycle) {
			statbuffer.t_expcycle = maxcycle;
			(void) printf(gettext(
				"%05.5d: Updated expiration cycle to %d\n"),
				i, maxcycle);
			tapechanged = 1;
		}
		if (tapechanged) {
			tl_write(i, &statbuffer);
			ntapes++;
		}
	}
	(void) printf(gettext("\n%s: complete.  Updated %d tape records.\n"),
		progname, ntapes);
	(void) printf(gettext("Use %s manually to add scratch tapes\n"),
		"dumptm");
	exit(0);
#ifdef lint
	return (0);
#endif
}
