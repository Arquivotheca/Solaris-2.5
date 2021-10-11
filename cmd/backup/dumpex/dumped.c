#ident	"@(#)dumped.c 1.39 93/10/15"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef USG
#include <netdb.h>
#else
#include <sys/param.h>
#endif
#include <time.h>
#include <pwd.h>
#include <signal.h>
#include <errno.h>
#include <curses.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <stdarg.h>

int	changedbottom;		/* dump level or filesystem changes */
int	changedtop;		/* parameter changes */
int	changedcron;		/* scheduling changes */
int	expert;			/* expert mode on/off */
int	addedfs;		/* a filesystem was added */

static int promptrow;		/* remembers where cursor should go */
static int promptcol;		/* remembers where cursor should go */

#define	PROMPTWID 3
#define	INPUTPLACE 3


#ifdef __STDC__
extern void die2(const char *format, ...);
static void usage(void);
static void welcome(void);
#else
extern void die2();
static void usage();
static void welcome();
#endif

static void
usage(void)
{
	(void) fprintf(stderr, gettext("Usage: %s configfile\n"), progname);
}

int	nfilesystems;
int	ndumpsets;

main(argc, argv)
	char	*argv[];
{
	char	in[SCREENWID];
	char	*yes = gettext("yes");
	int	i;
	struct devcycle_f *d;
	struct stat statbuf;

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

	checkroot(0);
	thisisedit = 1;
	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) == -1)
		die2(gettext("Cannot determine this host's name\n"));
	(void) umask(0006);


	for (argc--, argv++; argc; argc--, argv++) {
		if (argv[0][0] == '-') {
			if (argv[0][2] != '\0')
				die2(gettext(
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
		    "%s: You must supply a configuration file name\n"),
			progname);
		usage();
		exit(1);
	}
	(void) strcpy(filename, argv[0]);

	(void) sprintf(confdir, "%s/dumps", gethsmpath(etcdir));
	if (stat(confdir, &statbuf) < 0 && mkdir(confdir, 0700) < 0)
		die2(gettext("Cannot create configuration directory `%s'\n"),
			confdir);
	if (chdir(confdir) == -1)
		die2(gettext("Cannot chdir to %s; run this program as root\n"),
			confdir);
	if (nswitch == 0)
		lockfid = exlock(filename, gettext(
	"An executor or another editor is currently using this file.\n\
Please try again when that process has released it.\n"));
	openconfig(filename);	/* stays open mostly */
	readit();

	if (inlines[0].if_text == NULL ||
	    strcmp(inlines[0].if_text, configfilesecurity) != 0)
		die2(gettext("Bad header line on `%s'\n"), filename);

	/* one time only: count the tapesets and set up fs: */
	d = NULL;
	ndumpsets = 0;
	for (i = 0; i < 100; i++) {
		if (cf_tapeset[i]) {
			if (d == NULL)
				d = cf_tapeset[i]->ts_devlist;
			ndumpsets++;
		}
	}
	nfilesystems = 0;
	if (d) {
		for (i = 0; d; d = d->dc_next, i++) {
			nfilesystems++;
			fs[i] = d;
		}
	}
	setup();
	if (LINES < 24 || COLS < 80) {
		cleanup();
		(void) fprintf(stderr, gettext(
	"The screen must have at least 24 rows and 80 columns.\n\r"));
		(void) fprintf(stderr, gettext(
		    "Your current screen has %d rows and %d columns.\n\r"),
			LINES, COLS);
		exit(1);
	}
	helpinit(LINES, COLS);
	for (;;) {
		int	c;
		welcome();
		move(promptrow, promptcol);
		c = zgetch();
		switch (c) {
		case '\014':
			clear();
			continue;
		case 'a':
			newfilesys();
			continue;
		case 'b':
			newdevices();
			continue;
		case 'c':
			newmailees();
			continue;
		case 'd':
			newcron();
			continue;
		case 'e':
			newkeeps();
			continue;
		case 'f':
			newrdevuser();
			continue;
		case 'g':
			if (!expert)
				break;
			newtapelibraryname();
			continue;
		case 'h':
			if (!expert)
				break;
			newdumplibraryname();
			continue;
		case 'i':
			if (!expert)
				break;
			cf_longplay = !cf_longplay;
			changedtop = 1;
			continue;
		case 'j':
			if (!expert)
				break;
			newtapesup();
			continue;
		case 'k':
			if (!expert)
				break;
			newblockfac();
			continue;
		case 'l':
			if (!expert)
				break;
			newdumpsets();
			continue;
		case 'm':
			if (!expert)
				break;
			newtmpdir();
			continue;
		case 'q':
			dumped_quit();
			cleanup();
			exit(0);
			/* NOTREACHED */
		case 'x':
			expert = !expert;
			erase();
			continue;
		case '?':
			scr_help(HELP_MAIN, expert);
			continue;
		}
		(void) fprintf(stderr, gettext("\007"));
	}
	/*NOTREACHED*/
#ifdef lint
	return (0);
#endif
}

static void
welcome(void)
{
	int	line;
	char	*cp;

	/* Assumes that all sets are similar: */
	erase();
	line = 1;
	if (cf_rdevuser == 0)
		cf_rdevuser = "";
	if (cf_dumplib == 0)
		cf_dumplib = "";
	(void) mvprintw(line++, 5, gettext(
	    "----- Dump Configuration Editor -- %s -----"),
		filename);
	line++;
	promptrow = line;
	cp = gettext("              Choose any letter:");
	promptcol = 5 + strlen(cp) + 1;
	(void) mvprintw(line++, 5, cp);
	line++;
	(void) mvprintw(line++, 10, gettext("a.  File Systems..."));
	(void) mvprintw(line++, 10, gettext("b.  Tape Devices..."));
	(void) mvprintw(line++, 10, gettext("c.  Mail Recipients..."));
	(void) mvprintw(line++, 10, gettext("d.  Scheduling..."));
	(void) mvprintw(line++, 10, gettext("e.  Tape Expirations..."));
	(void) mvprintw(line++, 10, gettext("f.  Remote dump user:   %s"),
		cf_rdevuser[0] ? cf_rdevuser : gettext("root"));
	if (expert) {
		(void) mvprintw(line++, 10,
			gettext("g.  Tape library name:  %s"), cf_tapelib);
		(void) mvprintw(line++, 10,
			gettext("h.  Dump lib machine:   %s"), cf_dumplib);
		(void) mvprintw(line++, 10,
			gettext("i.  Long-play mode:     %s  (toggle)"),
			    cf_longplay ? gettext("On") : gettext("Off"));
		(void) mvprintw(line++, 10,
			gettext("j.  Tapes up:           %d"), cf_tapesup);
		(void) mvprintw(line++, 10,
			gettext("k.  Blocking Factor:    %d"), cf_blockfac);
		(void) mvprintw(line++, 10,
			gettext("l.  Dumpsets:           %d"), ndumpsets);
		(void) mvprintw(line++, 10,
			gettext("m.  Temp directory:     %s"), tmpdir);
	}
	line++;
	(void) mvprintw(line++, 10, gettext("q.  --> Quit"));
	(void) mvprintw(line++, 10, gettext("x.  --> Expert Mode %s  (toggle)"),
		expert ? gettext("Off") : gettext("On"));
	(void) mvprintw(line++, 10, gettext("?.  --> Help"));
}

static char dashes[SCREENWID + 1];
static char blanks[SCREENWID + 1];

void
inputborder(void)
{
	int	i;
	for (i = 0; i < SCREENWID; i++) {
		dashes[i] = '=';
		blanks[i] = ' ';
	}
	dashes[i] = '\0';
	blanks[LINEWID] = '\0';
	(void) mvprintw(INBASE, 1, "%s", dashes);
	clrtobot();
}

/* VARARGS2 */
void
inputshow(int linenum, char *mesg, ...)
{
	va_list	args;
	char buf[1000];

#ifdef USG
	va_start(args, mesg);
#else
	va_start(args);
#endif
	inputclear(linenum);
	(void) vsprintf(buf, mesg, args);
	(void) mvprintw(INBASE + linenum, INPUTPLACE, buf);
}

void
inputclear(linenum)
{
	(void) mvprintw(INBASE + linenum, INPUTPLACE, "%s", blanks);
}

/* VARARGS0 */
void
die2(const char *format, ...)
{
	va_list args;

#ifdef USG
	va_start(args, format);
#else
	va_start(args);
#endif
	nocurses();
	(void) fprintf(stderr, "%s: ", progname);
	(void) vfprintf(stderr, format, args);
	va_end(args);
	exit(1);
}

void
replotit(void)
{
	(void) wrefresh(curscr);
}
