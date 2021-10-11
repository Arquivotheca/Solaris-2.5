#ident	"@(#)readit.c 1.25 93/10/15"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * readit gets the configuration file and fills in all the structures which
 * drive the dump executor
 */

static int	curfileoffset;		/* how far into descr file */
static int	thisset = -1;		/* which tapeset is being read */
static struct devcycle_f *lastdevcycle;
char	*tmpdir = TMPDIR;	/* default for dumpex and dumped */

#define	KEY_BAD		-1
static char *cf_keywords[] = {
#define	KEY_TAPELIB	0
	"tapelib",
#define	KEY_DUMPLIB	1
	"dumpmach",
#define	KEY_DUMPDEVS	2
	"dumpdevs",
#define	KEY_TAPESUP	3
	"tapesup",
#define	KEY_KEEP	4
	"keep",
#define	KEY_MASTERCYCLE	5
	"mastercycle",
#define	KEY_SET		6
	"set",
#define	KEY_FULLCYCLE	7
	"fullcycle",
#define	KEY_NOTIFY	8
	"notify",
#define	KEY_LONGPLAY	9
	"longplay",
#define	KEY_BLOCK	10
	"block",
#define	KEY_RDEVUSER	11
	"rdevuser",
#define	KEY_CRON	12
	"cron",
#define	KEY_TMPDIR	13
	"tmpdir",
	0
};

static int
classify(line)			/* return which type of line */
	char	*line;
{
	char	**keyword;
	char	*p, *q;

	int	keynum;
	for (keyword = cf_keywords, keynum = 0; *keyword; keyword++, keynum++) {
		for (p = line, q = *keyword; *q; p++, q++) {
			if (*p != *q)
				break;
		}
		if (*q == 0)
			return (keynum);
	}
	return (KEY_BAD);
}

void
openconfig(filename)
	char	*filename;
{
	char	line[MAXLINELEN];
	if (index(filename, '/') != 0)
		die(gettext(
"Specify configuration file names without\n\
the filename component (i.e., no /)\n"));
	infid = fopen(filename, nswitch == 0 ? "r+" : "r");
	if (infid == NULL)
		die(gettext("Cannot open configuration file `%s'\n"), filename);
	if (fgets(line, MAXLINELEN, infid) == NULL)
		die(gettext(
		    "Empty configuration file specified (`%s')\n"), filename);
	if (strcmp(configfilesecurity, line) == 0) {
		rewind(infid);
		return;
	}
	if (strcmp(tapelibfilesecurity, line) == 0)
		die(gettext(
"You specified a tape library file instead of a\n\
\tdumpex configuration file (`%s')\n"),
			filename);
	die(gettext(
"The dumpex configuration file you specified (`%s')\n\
does not contain a valid dump configuration\n"),
		filename);
	/* NOTREACHED */
}

void
readit(void)
{
	char	line[MAXLINELEN];
	char	*p;
	int	linetype;
	int	i;

	ninlines = 0;
	inlines = (struct inline_f *)
		/*LINTED [alignment ok]*/
		checkalloc(GROW * sizeof (struct inline_f));
	maxinlines = GROW;
	ncf_keep = 0;
	/*LINTED [alignment ok]*/
	cf_keep = (struct keep_f *) checkalloc(GROW * sizeof (struct keep_f));
	maxcf_keep = GROW;
	cf_longplay = 0;
	/*LINTED [alignment ok]*/
	cf_notifypeople = (char **) checkalloc(GROW * sizeof (char *));
	maxcf_notifypeople = GROW;
	while (fgets(line, MAXLINELEN, infid)) {
		while (ninlines >= maxinlines) {	/* dynamic growth */
			maxinlines += GROW;
			inlines = (struct inline_f *)
				checkrealloc((char *) inlines,
				/*LINTED [alignment ok]*/
				maxinlines * sizeof (struct inline_f));
		}
		inlines[ninlines].if_fileoffset = curfileoffset;
		curfileoffset += strlen(line);
		inlines[ninlines].if_text = strdup(line);
		ninlines++;

		/* get rid of comments: */
		for (p = line; *p; p++) {
			if (*p == '#' || *p == '\n') {
				*p = '\0';	/* zap */
				break;
			}
		}
		for (p = line; *p; p++) {
			if (*p != ' ' && *p != '\t')
				goto goodline;
		}
		continue;	/* no nonblanks */
goodline:
		linetype = classify(line);
		if (linetype == KEY_BAD) {
			die(gettext(
			    "line %d: Bad keyword on `%s'\n"), ninlines, line);
			/* NOTREACHED */
		}
		/* handle this input line: */
		split(line, " \t");
		switch (linetype) {
		case KEY_TAPELIB:
			if (nsplitfields < 2) {
				warn(gettext(
	"line %d: configuration file `tapelib' keyword needs library name\n"),
					ninlines);
				cf_tapelib = "";
				break;
			}
			cf_tapelib = strdup(splitfields[1]);
			break;

		case KEY_RDEVUSER:
			if (nsplitfields < 2) {
				warn(gettext(
	"line %d: configuration file `rdevuser' keyword needs user name\n"),
					ninlines);
				cf_rdevuser = "";
				break;
			}
			cf_rdevuser = strdup(splitfields[1]);
			break;

		case KEY_CRON:
			if (nsplitfields < 18) {
				warn(gettext(
	"line %d: configuration file `cron' keyword needs 17 numbers\n"),
					ninlines);
				(void) memset((char *)&cf_cron, '\0',
					sizeof (cf_cron));
				break;
			}
			cf_cron.c_enable = atoi(splitfields[1]);
			cf_cron.c_dtime = atoi(splitfields[2]);
			cf_cron.c_ttime = atoi(splitfields[3]);
			cf_cron.c_ena[Mon] = atoi(splitfields[4]);
			cf_cron.c_new[Mon] = atoi(splitfields[5]);
			cf_cron.c_ena[Tue] = atoi(splitfields[6]);
			cf_cron.c_new[Tue] = atoi(splitfields[7]);
			cf_cron.c_ena[Wed] = atoi(splitfields[8]);
			cf_cron.c_new[Wed] = atoi(splitfields[9]);
			cf_cron.c_ena[Thu] = atoi(splitfields[10]);
			cf_cron.c_new[Thu] = atoi(splitfields[11]);
			cf_cron.c_ena[Fri] = atoi(splitfields[12]);
			cf_cron.c_new[Fri] = atoi(splitfields[13]);
			cf_cron.c_ena[Sat] = atoi(splitfields[14]);
			cf_cron.c_new[Sat] = atoi(splitfields[15]);
			cf_cron.c_ena[Sun] = atoi(splitfields[16]);
			cf_cron.c_new[Sun] = atoi(splitfields[17]);
			break;

		case KEY_DUMPLIB:
			if (nsplitfields < 2) {
				warn(gettext(
	"line %d: configuration file `dumpmach' keyword needs machine name\n"),
					ninlines);
				cf_dumplib = "";
				break;
			}
			cf_dumplib = strdup(splitfields[1]);
			break;

		case KEY_DUMPDEVS:
			if (ncf_dumpdevs != 0)
				die(gettext(
			"line %d: Only one dumpdevs line allowed\n"),
					ninlines);
			if (nsplitfields < 2) {
				warn(gettext(
	    "line %d: configuration file `dumpdevs' keyword needs a list\n"),
					ninlines);
				break;
			}
			cf_dumpdevs = (char **)
				/*LINTED [alignment ok]*/
				checkalloc(GROW * sizeof (char *));
			maxcf_dumpdevs = GROW;
			for (i = 1; i < nsplitfields; i++) {
				while (ncf_dumpdevs >= maxcf_dumpdevs) {
					/*
					 * dynamic growth
					 */
					maxcf_dumpdevs += GROW;
					cf_dumpdevs = (char **)
					    checkrealloc((char *) cf_dumpdevs,
					    /*LINTED [alignment ok]*/
					    maxcf_dumpdevs * sizeof (char *));
				}
				cf_dumpdevs[ncf_dumpdevs++] =
					strdup(splitfields[i]);
			}
			break;

		case KEY_TAPESUP:
			if (nsplitfields < 2)
				die(gettext(
	"line %d: configuration file `tapesup' line needs number of tapes\n"),
					ninlines);
			cf_tapesup = atoi(splitfields[1]);
			if (cf_tapesup < 0)
				warn(gettext(
	"line %d: need non-negative integer for number of tapes up `%s'\n"),
					ninlines, line);
			break;

		case KEY_NOTIFY:
			if (nsplitfields < 2) {
				warn(gettext(
	"line %d: configuration file `notify' line needs list of people\n"),
					ninlines);
				break;
			}
			for (i = 1; i < nsplitfields; i++) {
				while (ncf_notifypeople >= maxcf_notifypeople) {
					/*
					 * dynamic growth
					 */
					maxcf_notifypeople += GROW;
					cf_notifypeople = (char **)checkrealloc(
					    (char *)cf_notifypeople,
					    maxcf_notifypeople *
					    /*LINTED [alignment ok]*/
					    sizeof (char *));
				}
				cf_notifypeople[ncf_notifypeople++] =
					strdup(splitfields[i]);
			}
			break;

		case KEY_LONGPLAY:
			cf_longplay = 1;
			break;

		case KEY_KEEP:
			if (nsplitfields < 5)
				die(gettext(
		"line %d: keep specification needs four numbers: `%s'\n"),
					ninlines, line);
			while (ncf_keep >= maxcf_keep) { /* dynamic growth */
				maxcf_keep += GROW;
				cf_keep = (struct keep_f *)
				    checkrealloc((char *) cf_keep,
					/*LINTED [alignment ok]*/
					maxcf_keep * sizeof (struct keep_f));
			}
			cf_keep[ncf_keep].k_level = splitfields[1][0];
			cf_keep[ncf_keep].k_multiple = atoi(splitfields[2]);
			cf_keep[ncf_keep].k_days = atoi(splitfields[3]);
			cf_keep[ncf_keep++].k_minavail = atoi(splitfields[4]);
			break;

		case KEY_SET:
			if (nsplitfields < 2)
				die(gettext(
	"line %d: configuration file `set' keyword needs tapeset number\n"),
					ninlines);
			thisset = atoi(splitfields[1]);
			if (thisset < 1)
				die(gettext(
				    "line %d: tapeset numbers start at 1\n"),
					ninlines);
			if (thisset > cf_maxset)
				cf_maxset = thisset;
			if (cf_tapeset[thisset] != 0)
				die(gettext(
				    "line %d: duplicate tapeset number %d\n"),
					ninlines, thisset);
			cf_tapeset[thisset] = (struct tapeset_f *)
				/*LINTED [alignment ok]*/
				checkcalloc(sizeof (struct tapeset_f));
			break;

		case KEY_MASTERCYCLE:
			if (nsplitfields < 2)
				die(gettext(
	"line %d: configuration file `mastercycle' keyword needs number\n"),
					ninlines);
			cf_mastercycle = atoi(splitfields[1]);
			/*
			 * subscript, not line number
			 */
			cf_mastercycleline = ninlines - 1;
			if (cf_mastercycle < 0)
				die(gettext(
			"line %d: mastercycle number must be non-negative\n"),
					ninlines);
			break;

		case KEY_BLOCK:
			if (nsplitfields < 2)
				die(gettext(
		"line %d: configuration file `block' keyword needs number\n"),
					ninlines);
			cf_blockfac = atoi(splitfields[1]);
			if (cf_blockfac <= 0)
				die(gettext(
		    "line %d: blocking factor number must be non-negative\n"),
					ninlines);
			break;

		case KEY_FULLCYCLE:
			if (thisset < 0)
				die(gettext(
		"line %d: need a `set' specification before `fullcycle'\n"),
					ninlines);
			if (nsplitfields < 4)
				die(gettext(
				    "line %d: configuration file `fullcycle' \
keyword needs three fields\n"),
				    ninlines);
			{
				struct devcycle_f *d = (struct devcycle_f *)
					/*LINTED [alignment ok]*/
					checkalloc(sizeof (struct devcycle_f));

				/* must link the list in order as we see it: */
				if (cf_tapeset[thisset]->ts_devlist == NULL)
					cf_tapeset[thisset]->ts_devlist = d;
				else
					lastdevcycle->dc_next = d;
				lastdevcycle = d;
				d->dc_next = 0;
				/*
				 * subscript, not linenumber
				 */
				d->dc_linenumber = ninlines - 1;
				d->dc_fullcycle = atoi(splitfields[1]);
				if (d->dc_fullcycle < 0)
					die(gettext(
			"line %d: fullcycle number must be non-negative\n"),
						ninlines);
				d->dc_filesys = strdup(splitfields[2]);
				if (d->dc_filesys[0] != '+' &&
				    d->dc_filesys[0] != '-' &&
				    d->dc_filesys[0] != '*')
					die(gettext(
			"line %d: need +, *, or - as file system leadin\n"),
						ninlines);
				d->dc_dumplevels = strdup(splitfields[3]);
				if (index(splitfields[3], '>') == NULL)
					die(gettext(
			"line %d: fullcycle dumpcycles need > pointer\n"),
						ninlines);
				d->dc_log = newstring();
			}
			break;

		case KEY_TMPDIR:
			if (nsplitfields < 2) {
				warn(gettext(
	"line %d: configuration file `tmpdir' line needs a temp directory\n"),
					ninlines);
				tmpdir = "";
				break;
			}
			tmpdir = strdup(splitfields[1]);
			break;

		default:
			die(gettext(
			    "line %d: bad line classification (%d) `%s'\n"),
				ninlines,
				linetype, line);
			/* NOTREACHED */
		}
	}
	if (debug) {
		(void) printf(gettext("Tape library name is %s\n"), cf_tapelib);
		(void) printf(gettext(
		    "Dump library machine is %s\n"), cf_dumplib);
		(void) printf(gettext("Long play mode is %s\n"),
		    /* XGETTEXT: "on" == enabled */
		    cf_longplay ? gettext("on") :
			/* XGETTEXT: "off" == disabled */
			gettext("off"));
		for (i = 0; i < ncf_dumpdevs; i++)
			(void) printf(gettext(
			    "Dump device [%d] = %s\n"), i, cf_dumpdevs[i]);
		(void) printf(gettext("Number of tapes up is %d\n"),
			cf_tapesup);
		for (i = 0; i < ncf_keep; i++)
			(void) printf("keep  %c %d %d %d\n",
				cf_keep[i].k_level,
				cf_keep[i].k_multiple,
				cf_keep[i].k_days,
				cf_keep[i].k_minavail);
		(void) printf("mastercycle %d\n", cf_mastercycle);
		for (i = 0; i < 100; i++) {
			struct devcycle_f *d;
			if (cf_tapeset[i] == NULL)
				continue;
			for (d = cf_tapeset[i]->ts_devlist; d; d = d->dc_next)
				(void) printf("FC  %5d  %s  %s\n",
					d->dc_fullcycle,
					d->dc_filesys, d->dc_dumplevels);
		}
		for (i = 0; i < ncf_notifypeople; i++)
			(void) printf(gettext("NOTIFY: %s\n"),
				cf_notifypeople[i]);
	}
	if (cf_maxset == 0) {
		cf_maxset = 1;
		cf_tapeset[1] = (struct tapeset_f *)
			/*LINTED [alignment ok]*/
			checkcalloc(sizeof (struct tapeset_f));
	}
}
