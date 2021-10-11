/*	Copyright (c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc	*/
/*	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.		*/
/*		All rights reserved.					*/

/*		PROPRIETARY NOTICE (Combined)				*/
/*	This source code is unpublished proprietary information		*/
/*	constituting, or derived under license from AT&T's UNIX(r) 	*/
/*	System V.  In addition, portions of such source code were 	*/
/*	derived from Berkeley 4.3 BSD under license from the Regents	*/
/*	of the University of California.  Notice of copyright on this	*/
/*	source code product does not indicate publication.		*/

#ident	"@(#)man.c	1.27	95/01/11 SMI"	/* SVr4.0 1.3	*/

/*
 * man
 * links to apropos, whatis, and catman
 * This version uses more for underlining and paging.
 */

#include <stdio.h>
#include <ctype.h>
#include <sgtty.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#define	MACROF 	"tmac.an"		/* name of <locale> macro file */
#define	TMAC_AN	"-man"		/* default macro file */

/*
 * The default search path for man subtrees.
 */

#define	MANDIR		"/usr/share/man" 	/* default mandir */
#define	MAKEWHATIS	"/usr/lib/makewhatis"
#define	WHATIS		"windex"
#define	TEMPLATE	"/tmp/mpXXXXXX"
#define	CONFIG		"man.cf"

/*
 * Names for formatting and display programs.  The values given
 * below are reasonable defaults, but sites with source may
 * wish to modify them to match the local environment.  The
 * value for TCAT is particularly problematic as there's no
 * accepted standard value available for it.  (The definition
 * below assumes C.A.T. troff output and prints it).
 */

#define	MORE	"more -s" 		/* default paging filter */
#define	CAT_S	"/usr/bin/cat -s"	/* for '-' opt (no more) */
#define	CAT_	"/usr/bin/cat"		/* for when output is not a tty */
#define	TROFF	"troff"			/* local name for troff */
#define	TCAT	"lp -c -T troff"	/* command to "display" troff output */

#define	SOLIMIT		10	/* maximum allowed .so chain length */
#define	MAXDIRS		128	/* max # of subdirs per manpath */
#define	MAXPAGES	32	/* max # for multiple pages */
#define	PLEN		3	/* prefix length {man, cat, fmt} */
#define	TMPLEN		7	/* length of tmpfile prefix */
#define	MAXTOKENS 	64
#define	MAXSUFFIX	10	/* length of section suffix */

#define	DOT_SO		".so "
#define	PREPROC_SPEC	"'\\\" "

#define	DPRINTF		if (debug && !catmando) \
				(void) printf

#define	sys(s)		(debug ? puts(s) && 0 : system(s))
#define	eq(a, b)	(strcmp(a, b) == 0)
#define	match(a, b, c)	(strncmp(a, b, c) == 0)

/*
 * A list of known preprocessors to precede the formatter itself
 * in the formatting pipeline.  Preprocessors are specified by
 * starting a manual page with a line of the form:
 *	'\" X
 * where X is a string consisting of letters from the p_tag fields
 * below.
 */
struct preprocessor {
	char	p_tag;
	char	*p_nroff,
		*p_troff;
} preprocessors [] = {
	{'c',	"cw",				"cw"},
	{'e',	"neqn /usr/share/lib/pub/eqnchar",
			"eqn /usr/share/lib/pub/eqnchar"},
	{'p',	"pic",				"pic"},
	{'r',	"refer",			"refer"},
	{'t',	"tbl",				"tbl"},
	{'v',	"vgrind -f",			"vgrind -f"},
	{0,	0,				0}
};

struct suffix {
	char *ds;
	char *fs;
} psecs[MAXPAGES];

/*
 * Subdirectories to search for unformatted/formatted man page
 * versions, in nroff and troff variations.  The searching
 * code in manual() is structured to expect there to be two
 * subdirectories apiece, the first for unformatted files
 * and the second for formatted ones.
 */
char	*nroffdirs[] = { "man", "cat", 0 };
char	*troffdirs[] = { "man", "fmt", 0 };

#define	MAN_USAGE "\
usage:\tman [-] [-adFlrt] [-M path] [-T macro-package ] [ -s section ] \
name ...\n\
\tman [-M path] -k keyword ...\n\tman [-M path] -f file ..."
#define	CATMAN_USAGE "\
usage:\tcatman [-p] [-n] [-w] [-t] [-M path] [-T macro-package ] [sections]"

char *opts[] = {
	"FfkrP:M:T:ts:lad",	/* man */
	"wpnP:M:T:t"		/* catman */
};

struct man_node {
	char *path;		/* mandir path */
	char **secv;		/* submandir suffices */
	struct man_node *next;
};

char	*pages[MAXPAGES];
char	**endp = pages;

/*
 * flags (options)
 */
int	nomore;
int	troffit;
int	debug;
int	Tflag;
int	sargs;
int	margs;
int	force;
int	found;
int	list;
int	all;
int	whatis;
int	apropos;
int	catmando;
int	nowhatis;
int	whatonly;
int	use_default;

char	*CAT	= CAT_;
char	macros[MAXPATHLEN];
char	*manpath;
char	*mansec;
char	*pager;
char	*troffcmd;
char	*troffcat;
char	**subdirs;

char *check_config(char *path);
struct man_node *build_manpath(char **pathv);
void getpath(struct man_node *manp, char **pv);
void getsect(struct man_node *manp, char **pv);
void get_all_sect(struct man_node *manp);
void catman(struct man_node *manp, char **argv, int argc);
int makecat(char *path, char **dv, int ndirs);
int getdirs(char *path, char **dirv, short flag);
void whatapro(struct man_node *manp, char *word, int apropos);
void lookup_windex(char *whatpath, char *word);
int icmp(register char *s, register char *t);
void more(char **pages, int plain);
void cleanup(char **pages);
void bye(int sig);
char **split(char *s1, char sep, char *empty);
void fullpaths(struct man_node *manp);
void lower(register char *s);
int cmp(const void *arg1, const void *arg2);
void manual(struct man_node *manp, char *name);
void mandir(char **secv, char *path, char *name);
void sortdir(DIR *dp, char **dirv);
int searchdir(char *path, char *dir, char *name);
int windex(char **secv, char *path, char *name);
void section(struct suffix *sp, char *s);
int bfsearch(FILE *fp, char **matchv, char *key);
int compare(register char *s, register char *t);
int format(char *path, char *dir, char *name, char *pg);
char *addlocale(char *path);
int get_manconfig(FILE *fp, char *submandir);

extern	char *optarg;
extern	int optind;
extern	int opterr;

char language[64]; 	/* LC_MESSAGES */
char localedir[64];	/* locale specific path component */

int
main(int argc, char *argv[])
{
	int badopts = 0;
	int c;
	char **pathv;
	char *cmdname;
	struct man_node	*manpage = NULL;

	(void) setlocale(LC_ALL, "");
	(void) strcpy(language, setlocale(LC_MESSAGES, (char *) 0));
	if (strcmp("C", language) != 0)
		(void) sprintf(localedir, "%s", language);

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS-TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	strcpy(macros, TMAC_AN);

	/*
	 * get user defined stuff
	 */
	if ((manpath = getenv("MANPATH")) == NULL)
		manpath = MANDIR;
	/*
	 * get base part of command name
	 */
	if ((cmdname = strrchr(argv[0], '/')) != NULL)
		cmdname++;
	else
		cmdname = argv[0];

	if (eq(cmdname, "apropos") || eq(cmdname, "whatis")) {
		whatis++;
		apropos = (*cmdname == 'a');
		if ((optind = 1) == argc) {
			(void) fprintf(stderr, gettext("%s what?\n"), cmdname);
			exit(2);
		}
		goto doargs;
	} else if (eq(cmdname, "catman"))
		catmando++;

	opterr = 0;
	while ((c = getopt(argc, argv, opts[catmando])) != -1)
		switch (c) {

		/*
		 * man specific options
		 */
		case 'k':
			apropos++;
			/*FALLTHROUGH*/
		case 'f':
			whatis++;
			break;
		case 'F':
			force++;	/* do lookups the hard way */
			break;
		case 's':
			mansec = optarg;
			sargs++;
			break;
		case 'r':
			nomore++, troffit++;
			break;
		case 'l':
			list++;		/* implies all */
			/*FALLTHROUGH*/
		case 'a':
			all++;
			break;
		case 'd':

		/*
		 * catman specific options
		 */
		case 'p':
			debug++;
			break;
		case 'n':
			nowhatis++;
			break;
		case 'w':
			whatonly++;
			break;

		/*
		 * shared options
		 */
		case 'P':	/* Backwards compatibility */
		case 'M':	/* Respecify path for man pages. */
			manpath = optarg;
			margs++;
			break;
		case 'T':	/* Respecify man macros */
			strcpy(macros, optarg);
			Tflag++;
			break;
		case 't':
			troffit++;
			break;
		case '?':
			badopts++;
		}

	/*
	 *  Bad options or no args?
	 *	(catman doesn't need args)
	 */
	if (badopts || (!catmando && optind == argc)) {
		(void) fprintf(stderr, "%s\n", catmando ?
		    gettext(CATMAN_USAGE) : gettext(MAN_USAGE));
		exit(2);
	}

	if (sargs && margs && catmando) {
		(void) fprintf(stderr, "%s\n", gettext(CATMAN_USAGE));
		exit(2);
	}

	if (troffit == 0 && nomore == 0 && !isatty(fileno(stdout)))
		nomore++;

	/*
	 * Collect environment information.
	 */
	if (troffit) {
		if ((troffcmd = getenv("TROFF")) == NULL)
			troffcmd = TROFF;
		if ((troffcat = getenv("TCAT")) == NULL)
			troffcat = TCAT;
	} else {
		if (((pager = getenv("PAGER")) == NULL) ||
		    (*pager == NULL))
			pager = MORE;
	}

doargs:
	subdirs = troffit ? troffdirs : nroffdirs;

	pathv = split(manpath, ':', (char *) 0);

	manpage = build_manpath(pathv);

	fullpaths(manpage);

	if (catmando) {
		catman(manpage, argv+optind, argc-optind);
		exit(0);
	}

	/*
	 * The manual routine contains windows during which
	 * termination would leave a temp file behind.  Thus
	 * we blanket the whole thing with a clean-up routine.
	 */
	if (signal(SIGINT, SIG_IGN) == SIG_DFL) {
		(void) signal(SIGINT, bye);
		(void) signal(SIGQUIT, bye);
		(void) signal(SIGTERM, bye);
	}

	for (; optind < argc; optind++) {
		if (strcmp(argv[optind], "-") == 0) {
			nomore++;
			CAT = CAT_S;
		} else if (whatis)
			whatapro(manpage, argv[optind], apropos);
		else
			manual(manpage, argv[optind]);
	}
	return (0);
	/*NOTREACHED*/
}

/*
 * This routine builds the manpage structure from MANPATH.
 */

struct man_node *
build_manpath(char **pathv)
{
	struct man_node *manpage = NULL;
	struct man_node *currp = NULL;
	struct man_node *lastp = NULL;
	char **p;
	int s;

	s = sizeof (struct man_node);
	for (p = pathv; *p; p++) {

		if (manpage == NULL)
			currp = lastp = manpage = (struct man_node *)malloc(s);
		else
			currp =  (struct man_node *)malloc(s);

		getpath(currp, p);
		getsect(currp, p);

		currp->next = NULL;
		if (currp != manpage)
			lastp->next = currp;
		lastp = currp;
	}

	return (manpage);
}

/*
 * Stores the mandir path into the manp structure.
 */

void
getpath(struct man_node *manp, char **pv)
{
	char *s;
	int i = 0;

	s = *pv;

	while (*s != NULL && *s != ',')
		i++, s++;

	manp->path = malloc(i+1);
	strncpy(manp->path, *pv, i);
	*(manp->path + i) = '\0';
}

/*
 * Stores the mandir's corresponding sections (submandir
 * directories) into the manp structure.
 */

void
getsect(struct man_node *manp, char **pv)
{
	char *sections;
	char **sectp;

	if (sargs) {
		manp->secv = split(mansec, ',', (char *) 0);

		for (sectp = manp->secv; *sectp; sectp++)
			lower(*sectp);
	} else if ((sections = strchr(*pv, ',')) != NULL) {
		if (debug)
			fprintf(stdout, "%s: from -M option, MANSECTS=%s\n",
			    manp->path, sections);
		manp->secv = split(++sections, ',', (char *) 0);
		for (sectp = manp->secv; *sectp; sectp++)
			lower(*sectp);

		if (*manp->secv == NULL)
			get_all_sect(manp);
	} else if ((sections = check_config(*pv)) != NULL) {
		if (debug)
			fprintf(stdout, "%s: from %s, MANSECTS=%s\n",
			    manp->path, CONFIG, sections);
		manp->secv = split(sections, ',', (char *) 0);

		for (sectp = manp->secv; *sectp; sectp++)
			lower(*sectp);

		if (*manp->secv == NULL)
			get_all_sect(manp);
	} else {
		if (debug)
			fprintf(stdout,
			    "%s: search the sections lexicographically\n",
			    manp->path);
		get_all_sect(manp);
	}
}

/*
 * Get suffices of all sub-mandir directories in a mandir.
 */

void
get_all_sect(struct man_node *manp)
{
	DIR *dp;
	char *dirv[MAXDIRS];
	char **dv;
	char **p;
	char prev[MAXSUFFIX];
	char tmp[MAXSUFFIX];

	if ((dp = opendir(manp->path)) == 0)
		return;

	sortdir(dp, dirv);

	manp->secv = (char **)malloc(MAXTOKENS * sizeof (char *));

	memset(tmp, 0, MAXSUFFIX);
	memset(prev, 0, MAXSUFFIX);
	for (dv = dirv, p = manp->secv; *dv; dv++) {
		if (strcmp(*dv, CONFIG) == 0)
			continue;
		sprintf(tmp, "%s", *dv + PLEN);

		if (strcmp(prev, tmp) == 0)
			continue;
		sprintf(prev, "%s", *dv + PLEN);
		*p++ = *dv + PLEN;
	}
	*p = 0;
}

/*
 * Format man pages (build cat pages); if no
 * sections are specified, build all of them.
 */

void
catman(struct man_node *manp, char **argv, int argc)
{
	char cmdbuf[BUFSIZ];
	char *dv[MAXDIRS];
	int changed;
	struct man_node *p;
	int ndirs = 0;
	char *ldir;

	for (p = manp; p != NULL; p = p->next) {
		if (debug)
			fprintf(stdout, "\nmandir path = %s\n", p->path);
		ndirs = 0;
		/*
		 * Build cat pages
		 */
		ldir = addlocale(p->path);
		if (!whatonly) {
			if (*localedir != '\0') {
				ndirs = getdirs(ldir, dv, 1);
				if (ndirs != 0) {
					changed = argc ?
						makecat(ldir, argv, argc) :
						makecat(ldir, dv, ndirs);
				}
			}

			/* locale mandir not found or locale is not set */
			if (ndirs == 0) {
				ndirs = getdirs(p->path, dv, 1);
				changed = argc ?
					makecat(p->path, argv, argc) :
					makecat(p->path, dv, ndirs);
			}
		}
		/*
		 * Build whatis database
		 *  print error message if locale is set and man dir not found
		 */
		if (whatonly || (!nowhatis && changed)) {
			if (*localedir != '\0') {
				if ((ndirs = getdirs(ldir, dv, 1)) != 0)
					(void) sprintf(cmdbuf, \
				"/usr/bin/sh %s %s", MAKEWHATIS, ldir);
				else
					(void) fprintf(stderr, gettext(" \
locale is %s, invalid manpath %s\n"), localedir, ldir);
				}
			else
				(void) sprintf(cmdbuf, "/usr/bin/sh %s %s",
				    MAKEWHATIS, p->path);
			sys(cmdbuf);
		}
	}
}

/*
 * Build cat pages for given sections
 */

int
makecat(char *path, char **dv, int ndirs)
{
	DIR *dp;
	struct dirent *d;
	struct stat sbuf;
	char mandir[MAXPATHLEN+1];
	char catdir[MAXPATHLEN+1];
	char *dirp;
	int i, fmt;

	for (i = fmt = 0; i < ndirs; i++) {
		(void) sprintf(mandir, "%s/%s%s", path, subdirs[0], dv[i]);
		(void) sprintf(catdir, "%s/%s%s", path, subdirs[1], dv[i]);
		dirp = strrchr(mandir, '/') + 1;

		if ((dp = opendir(mandir)) == 0) {
			if (strcmp(mandir, CONFIG) == 0)
				perror(mandir);
			continue;
		}
		if (debug)
			fprintf(stdout,
			    "Building cat pages for mandir = %s\n", path);
		if (stat(catdir, &sbuf) < 0) {
			umask(02);
			if (debug)
				(void) fprintf(stdout, "mkdir %s\n", catdir);
			else {
				if (mkdir(catdir, 0775) < 0) {
					perror(catdir);
					continue;
				}
				(void) chmod(catdir, 0775);
			}
		}

		while ((d = readdir(dp))) {
			if (eq(".", d->d_name) || eq("..", d->d_name))
				continue;

			if (format(path, dirp, (char *)0, d->d_name) > 0)
				fmt++;
		}
		(void) closedir(dp);
	}
	return (fmt);
}


/*
 * Get all "man" dirs under a given manpath
 * and return the number found
 */

int
getdirs(char *path, char **dirv, short flag)
{
	DIR *dp;
	struct dirent *d;
	int n = 0;

	if ((dp = opendir(path)) == 0) {
		if (debug) {
			if (*localedir != '\0')
				printf(gettext("\
locale is %s, search in %s\n"), localedir, path);
			perror(path);
		}
		return (0);
	}

	while ((d = readdir(dp))) {
		if (match(subdirs[0], d->d_name, PLEN)) {
			if (flag) {
				*dirv = strdup(d->d_name+PLEN);
				dirv++;
			}
			n++;

		}
	}

	(void) closedir(dp);
	return (n);
}


/*
 * Find matching whatis or apropos entries
 *
 */

void
whatapro(struct man_node *manp, char *word, int apropos)
{
	char whatpath[MAXPATHLEN+1];
	char *p = word;
	struct man_node *b;
	int ndirs = 0;
	char *ldir;

	DPRINTF("word = %s \n", p);
	for (b = manp; b != NULL; b = b->next) {

		/*
		 * get base part of name
		 */
		if (!apropos) {
			if ((p = strrchr(word, '/')) == NULL)
				p = word;
			else
				p++;
		}

		if (*localedir != '\0') {
			ldir = addlocale(b->path);
			ndirs = getdirs(ldir, NULL, 0);
			if (ndirs != 0) {
				(void) sprintf(whatpath, "%s/%s", ldir, WHATIS);
				DPRINTF("\nmandir path = %s\n", ldir);
				lookup_windex(whatpath, p);
			}
		}

		(void) sprintf(whatpath, "%s/%s", b->path, WHATIS);
		DPRINTF("\nmandir path = %s\n", b->path);

		lookup_windex(whatpath, p);
	}
}


void
lookup_windex(char *whatpath, char *word)
{
	register char *s;
	FILE *fp;
	char buf[BUFSIZ];
	char *matches[MAXPAGES];
	char **pp;

	if ((fp = fopen(whatpath, "r")) == NULL) {
		perror(whatpath);
		return;
	}

	if (apropos) {
		while (fgets(buf, sizeof (buf), fp) != NULL)
			for (s = buf; *s; s++)
				if (icmp(word, s) == 0) {
					(void) printf("%s", buf);
					break;
				}
	} else {
		if (bfsearch(fp, matches, word))
			for (pp = matches; *pp; pp++)
				(void) printf("%s", *pp);
	}
	(void) fclose(fp);

}


/*
 * case-insensitive compare unless upper case is used
 * ie)	"mount" matches mount, Mount, MOUNT
 *	"Mount" matches Mount, MOUNT
 *	"MOUNT" matches MOUNT only
 */

int
icmp(register char *s, register char *t)
{
	for (; *s == (isupper(*s) ? *t:tolower(*t)); s++, t++)
		if (*s == 0)
			return (0);

	return (*s == 0 ? 0 : *t == 0 ? 1 : *s < *t ? -2 : 2);
}


/*
 * Invoke PAGER with all matching man pages
 */

void
more(char **pages, int plain)
{
	char cmdbuf[BUFSIZ];
	char **vp;

	/*
	 * Dont bother.
	 */
	if (list || (*pages == 0))
		return;

	if (plain && troffit) {
		cleanup(pages);
		return;
	}
	(void) sprintf(cmdbuf, "%s", troffit ? troffcat :
	    plain ? CAT : pager);

	/*
	 * Build arg list
	 */
	for (vp = pages; vp < endp; vp++) {
		(void) strcat(cmdbuf, " ");
		(void) strcat(cmdbuf, *vp);
	}
	sys(cmdbuf);
	cleanup(pages);
}


/*
 * Get rid of dregs.
 */

void
cleanup(char **pages)
{
	char **vp;

	for (vp = pages; vp < endp; vp++)
		if (match(TEMPLATE, *vp, TMPLEN))
			(void) unlink(*vp);

	endp = pages;	/* reset */
}


/*
 * Clean things up after receiving a signal.
 */

/*ARGSUSED*/
void
bye(int sig)
{
	cleanup(pages);
	exit(1);
	/*NOTREACHED*/
}


/*
 * Split a string by specified separator.
 *    if empty == 0 ignore empty components/adjacent separators,
 *    otherwise substitute emtpy.
 *    returns vector to all tokens
 */

char **
split(char *s1, char sep, char *empty)
{
	char **tokv, **vp;
	char *mp, *tp;

	tokv = vp = (char **)malloc(MAXTOKENS * sizeof (char *));
	for (mp = strdup(s1); mp && *mp; mp = tp) {
		tp = strchr(mp, sep);
		if (mp == tp) {		/* empty component */
			if (empty == 0) {	/* ignore */
				tp++;
				continue;
			}
			mp = empty;	/* substitute */
		}
		if (tp) {
			*tp = 0;
			tp++;
		}
		*vp++ = mp;
	}
	*vp = 0;
	return (tokv);
}


/*
 * Convert paths to full paths if necessary
 *
 */

void
fullpaths(struct man_node *manp)
{
	char *cwd;
	char *p;
	struct man_node *b;

	if ((cwd = getcwd(NULL, MAXPATHLEN+1)) == (char *)NULL) {
		perror("getcwd");
		exit(1);
	}
	for (b = manp; b != NULL; b = b->next) {
		if (*(b->path) == '/')
			continue;
		if ((p = malloc(strlen(b->path)+strlen(cwd)+2)) == NULL) {
			perror("malloc");
			exit(1);
		}
		(void) sprintf(p, "%s/%s", cwd, b->path);
		b->path = p;
	}
}


/*
 * Map (in place) to lower case
 */

void
lower(register char *s)
{
	if (s == 0)
		return;
	while (*s) {
		if (isupper(*s))
			*s = tolower(*s);
		s++;
	}
}


/*
 * compare for sort()
 * sort first by section-spec, then by prefix {man, cat, fmt}
 *	note: prefix is reverse sorted so that "man" always
 * 	comes before {cat, fmt}
 */

int
cmp(const void *arg1, const void *arg2)
{
	int n;
	char **p1 = (char **)arg1;
	char **p2 = (char **)arg2;

	/* by section */
	if ((n = strcmp(*p1 + PLEN, *p2 + PLEN)))
		return (n);

	/* by prefix reversed */
	return (strncmp(*p2, *p1, PLEN));
}


/*
 * Find a man page ...
 *   Loop through each path specified,
 *   first try the lookup method (whatis database),
 *   and if it doesn't exist, do the hard way.
 */

void
manual(struct man_node *manp, char *name)
{
	struct man_node *p;
	int ndirs = 0;
	char *ldir;

	/*
	 *  for each path in MANPATH
	 */
	found = 0;

	for (p = manp; p != NULL; p = p->next) {
		DPRINTF("\nmandir path = %s\n", p->path);

		if (*localedir != '\0') {
			ldir = addlocale(p->path);
			if (debug)
			    printf("localedir = %s, ldir = %s\n",
				localedir, ldir);
			ndirs = getdirs(ldir, NULL, 0);
			if (ndirs != 0) {
				if (force || windex(p->secv, ldir, name) < 0)
					mandir(p->secv, ldir, name);
			}
		}

		/*
		 * locale mandir not valid or man page in locale
		 * mandir not found
		 */
		if (ndirs == 0 || !found) {
			if (force || windex(p->secv, p->path, name) < 0)
				mandir(p->secv, p->path, name);
		}

		if (found && !all)
			break;
	}

	if (found)
		more(pages, nomore);
	else {
		if (sargs) {
			(void) printf(
			    gettext("No entry for %s in section(s) "
			    "%s of the manual.\n"),
			    name, mansec);
			return;
		} else {
			(void) printf(gettext(
			    "No manual entry for %s.\n"), name, mansec);
			return;
		}
	}
}


/*
 * For a specified manual directory,
 *	read, store, & sort section subdirs,
 *	for each section specified
 *		find and search matching subdirs
 */

void
mandir(char **secv, char *path, char *name)
{
	DIR *dp;
	char *dirv[MAXDIRS];
	char **dv;
	int len, dslen;

	if ((dp = opendir(path)) == 0) {
		if (debug)
			fprintf(stdout, " opendir on %s failed\n", path);
		return;
	}

	if (debug)
		printf("mandir path = %s\n", path);

	sortdir(dp, dirv);
	/*
	 * Search in the order specified by MANSECTS
	 */
	for (; *secv; secv++) {
		DPRINTF("  section = %s\n", *secv);
		len = strlen(*secv);
		for (dv = dirv; *dv; dv++) {
			dslen = strlen(*dv+PLEN);
			if (dslen > len)
				len = dslen;
			if (**secv == '\\') {
				if (!eq(*secv + 1, *dv+PLEN))
					continue;
			} else if (!match(*secv, *dv+PLEN, len))
				continue;

			if (searchdir(path, *dv, name) == 0)
				continue;

			if (!all) {
				(void) closedir(dp);
				return;
			}
			/*
			 * if we found a match in the man dir skip
			 * the corresponding cat dir if it exists
			 */
			if (all && **dv == 'm' && *(dv+1) &&
				eq(*(dv+1)+PLEN, *dv+PLEN))
					dv++;
		}
	}
	(void) closedir(dp);
}

/*
 * Sort directories.
 */

void
sortdir(DIR *dp, char **dirv)
{

	struct dirent *d;
	char **dv;

	dv = dirv;
	while ((d = readdir(dp))) {	/* store dirs */
		if (eq(d->d_name, ".") || eq(d->d_name, ".."))	/* ignore */
			continue;

		/* check if it matches man, cat format */
		if (match(d->d_name, subdirs[0], PLEN) ||
			match(d->d_name, subdirs[1], PLEN)) {
			*dv = malloc(strlen(d->d_name) + 1);
			strcpy(*dv, d->d_name);
			dv++;
		}
	}
	*dv = 0;

	qsort((void *)dirv, dv - dirv, sizeof (char *), cmp);

}


/*
 * Search a section subdirectory for a
 * given man page, return 1 for success
 */

int
searchdir(char *path, char *dir, char *name)
{
	DIR *sdp;
	struct dirent *sd;
	char sectpath[MAXPATHLEN+1];
	char file[MAXNAMLEN+1];
	char dname[MAXPATHLEN+1];
	char *last;
	int nlen;

	DPRINTF("    scanning = %s\n", dir);
	(void) sprintf(sectpath, "%s/%s", path, dir);
	(void) sprintf(file, "%s.", name);

	if ((sdp = opendir(sectpath)) == 0) {
		if (errno != ENOTDIR)	/* ignore matching cruft */
			perror(dir);
		return (0);
	}
	while ((sd = readdir(sdp))) {
		last = strrchr(sd->d_name, '.');
		nlen = last - sd->d_name;
		(void) sprintf(dname, "%.*s.", nlen, sd->d_name);
		if (eq(dname, file) || eq(sd->d_name, name)) {
			(void) format(path, dir, name, sd->d_name);
			(void) closedir(sdp);
			return (1);
		}
	}
	(void) closedir(sdp);
	return (0);
}


/*
 * Use windex database for quick lookup of man pages
 * instead of mandir() (brute force search)
 */

int
windex(char **secv, char *path, char *name)
{
	FILE *fp;
	struct stat sbuf;
	struct suffix *sp;
	char whatfile[MAXPATHLEN+1];
	char page[MAXPATHLEN+1];
	char *matches[MAXPAGES];
	char *file, *dir;
	char **sv, **vp;
	int len, dslen, exist;

	(void) sprintf(whatfile, "%s/%s", path, WHATIS);
	if ((fp = fopen(whatfile, "r")) == NULL) {
		if (errno == ENOENT)
			return (-1);
		return (0);
	}

	if (debug)
		fprintf(stdout, " search in = %s file\n", whatfile);

	if (bfsearch(fp, matches, name) == 0)
		return (0);

	/*
	 * Save and split sections
	 */
	for (sp = psecs, vp = matches; *vp; vp++, sp++)
		section(sp, *vp);

	sp->ds = 0;

	/*
	 * Search in the order specified
	 * by MANSECTS
	 */
	for (; *secv; secv++) {
		len = strlen(*secv);

		if (debug)
			fprintf(stdout,
			    "  search an entry to match %s.%s\n", name, *secv);
		/*
		 * For every whatis entry that
		 * was matched
		 */
		for (sp = psecs; sp->ds; sp++) {
			dslen = strlen(sp->ds);
			if (dslen > len)
				len = dslen;
			if (**secv == '\\') {
				if (!eq(*secv + 1, sp->ds))
					continue;
			} else if (!match(*secv, sp->ds, len))
				continue;

			for (sv = subdirs; *sv; sv++) {
				(void) sprintf(page,
				    "%s/%s%s/%s%s%s", path, *sv,
				    sp->ds, name, *sp->fs ? "." : "",
				    sp->fs);

				exist = (stat(page, &sbuf) == 0);
				if (exist)
					break;
			}
			if (!exist) {
				(void) fprintf(stderr, gettext(
				    "%s entry incorrect:  %s(%s) not found.\n"),
				    WHATIS, name, sp->ds);
				continue;
			}

			file = strrchr(page, '/'), *file = 0;
			dir = strrchr(page, '/');

			/*
			 * By now we have a match
			 */
			(void) format(path, ++dir, name, ++file);

			if (!all)
				return (0);
		}
	}
	return (0);
}


/*
 * Return pointers to the section-spec
 * and file-suffix of a whatis entry
 */

void
section(struct suffix *sp, char *s)
{
	char *lp, *p;

	lp = strchr(s, '(');
	p = strchr(s, ')');

	if (++lp == 0 || p == 0 || lp == p) {
		(void) fprintf(stderr,
		    gettext("mangled windex entry:\n\t%s\n"), s);
		return;
	}
	*p = 0;

	lower(lp);

	/*
	 * split section-specifier if file-name
	 * suffix differs from section-suffix
	 */
	sp->ds = lp;
	if ((p = strchr(lp, '/'))) {
		*p++ = 0;
		sp->fs = p;
	} else
		sp->fs = lp;
}


/*
 * Binary file search to find matching man
 *   pages in whatis database.
 */

int
bfsearch(FILE *fp, char **matchv, char *key)
{
	char entry[BUFSIZ];
	char **vp;
	long top, bot, mid;
	register c;

	vp = matchv;
	bot = 0;
	(void) fseek(fp, 0L, 2);
	top = ftell(fp);
	for (;;) {
		mid = (top+bot)/2;
		(void) fseek(fp, mid, 0);
		do {
			c = getc(fp);
			mid++;
		} while (c != EOF && c != '\n');
		if (fgets(entry, sizeof (entry), fp) == NULL)
			break;
		switch (compare(key, entry)) {
		case -2:
		case -1:
		case 0:
			if (top <= mid)
				break;
			top = mid;
			continue;
		case 1:
		case 2:
			bot = mid;
			continue;
		}
		break;
	}
	(void) fseek(fp, bot, 0);
	while (ftell(fp) < top) {
		if (fgets(entry, sizeof (entry), fp) == NULL) {
			*matchv = 0;
			return (matchv - vp);
		}
		switch (compare(key, entry)) {
		case -2:
			*matchv = 0;
			return (matchv - vp);
		case -1:
		case 0:
			*matchv++ = strdup(entry);
			break;
		case 1:
		case 2:
			continue;
		}
		break;
	}
	while (fgets(entry, sizeof (entry), fp)) {
		switch (compare(key, entry)) {
		case -1:
		case 0:
			*matchv++ = strdup(entry);
			continue;
		}
		break;
	}
	*matchv = 0;
	return (matchv - vp);
}

int
compare(register char *s, register char *t)
{
	for (; *s == *t; s++, t++)
		if (*s == 0 && (*t == '\t' || *t == ' '))
			return (0);
	return (*s == 0 && (*t == '\t' || *t == ' ') ?
	    -1 : *t == 0 ? 1 : *s < *t ? -2 : 2);
}


/*
 * Format a man page and follow .so references
 * if necessary.
 */

int
format(char *path, char *dir, char *name, char *pg)
{
	char manpname[MAXPATHLEN+1], catpname[MAXPATHLEN+1];
	char soed[MAXPATHLEN+1], soref[MAXPATHLEN+1];
	char manbuf[BUFSIZ], cmdbuf[BUFSIZ];
	int socount, updatedcat, regencat;
	struct stat mansb, catsb;
	char *tmpname;
	int catonly = 0;
	struct stat statb;

	found++;
	if (list) {
		(void) printf(gettext("%s (%s)\t-M %s\n"),
		    name, dir+PLEN, path);
		return (-1);
	}
	if (*dir != 'm')
		catonly++;

	(void) sprintf(manpname, "%s/%s%s/%s", path, subdirs[0], dir+PLEN, pg);
	(void) sprintf(catpname, "%s/%s%s/%s", path, subdirs[1], dir+PLEN, pg);

	DPRINTF("      unformatted = %s\n", catonly ? "" : manpname);
	DPRINTF("      formatted = %s\n", catpname);

	/*
	 * Take care of indirect references to other man pages;
	 * i.e., resolve files containing only ".so manx/file.x".
	 * We follow .so chains, replacing title with the .so'ed
	 * file at each stage, and keeping track of how many times
	 * we've done so, so that we can avoid looping.
	*/
	*soed = 0;
	socount = 0;
	for (;;) {
		register FILE *md;
		register char *cp;
		char *s;

		if (catonly)
			break;
		/*
		 * Grab manpname's first line, stashing it in manbuf.
		 */
		if ((md = fopen(manpname, "r")) == NULL) {
			if (*soed && errno == ENOENT) {
				(void) fprintf(stderr,
				    gettext("Can't find referent of "
					".so in %s\n"), soed);
				(void) fflush(stderr);
				return (-1);
			}
			perror(manpname);
			return (-1);
		}
		if (fgets(manbuf, BUFSIZ-1, md) == NULL) {
			(void) fclose(md);
			(void) fprintf(stderr, gettext("%s: null file\n"),
			    manpname);
			(void) fflush(stderr);
			return (-1);
		}
		(void) fclose(md);

		if (strncmp(manbuf, DOT_SO, sizeof (DOT_SO) - 1))
			break;
		if (++socount > SOLIMIT) {
			(void) fprintf(stderr, gettext(".so chain too long\n"));
			(void) fflush(stderr);
			return (-1);
		}
		s = manbuf + sizeof (DOT_SO) - 1;
		cp = strrchr(s, '\n');
		if (cp)
			*cp = '\0';
		/*
		 * Compensate for sloppy typists by stripping
		 * trailing white space.
		 */
		cp = s + strlen(s);
		while (--cp >= s && (*cp == ' ' || *cp == '\t'))
			*cp = '\0';

		/*
		 * Go off and find the next link in the chain.
		 */
		(void) strcpy(soed, manpname);
		(void) strcpy(soref, s);
		(void) sprintf(manpname, "%s/%s", path, s);
		DPRINTF(".so ref = %s\n", s);
	}

	/*
	 * Make symlinks if so'ed and cattin'
	 */
	if (socount && catmando) {
		(void) sprintf(cmdbuf, "cd %s; rm -f %s; ln -s ../%s%s %s",
		    path, catpname, subdirs[1], soref+PLEN, catpname);
		sys(cmdbuf);
		return (1);
	}

	/*
	 * Obtain the cat page that corresponds to the man page.
	 * If it already exists, is up to date, and if we haven't
	 * been told not to use it, use it as it stands.
	 */
	regencat = updatedcat = 0;
	if (!catonly && stat(manpname, &mansb) >= 0 &&
	    (stat(catpname, &catsb) < 0 || catsb.st_mtime < mansb.st_mtime)) {
		/*
		 * Construct a shell command line for formatting manpname.
		 * The resulting file goes initially into /tmp.  If possible,
		 * it will later be moved to catpname.
		 */

		int pipestage = 0;
		int needcol = 0;
		char *cbp = cmdbuf;

		regencat = updatedcat = 1;

		if (!catmando && !debug) {
			(void) fprintf(stderr, gettext(
					"Reformatting page.  Wait..."));
			(void) fflush(stderr);
		}

		/*
		 * cd to path so that relative .so commands will work
		 * correctly
		 */
		(void) sprintf(cbp, "cd %s; ", path);
		cbp += strlen(cbp);

		/*
		 * Check for special formatting requirements by examining
		 * manpname's first line preprocessor specifications.
		 */

		if (strncmp(manbuf, PREPROC_SPEC,
		    sizeof (PREPROC_SPEC) - 1) == 0) {
			register char *ptp;

			ptp = manbuf + sizeof (PREPROC_SPEC) - 1;
			while (*ptp && *ptp != '\n') {
				register struct preprocessor *pp;

				/*
				 * Check for a preprocessor we know about.
				 */
				for (pp = preprocessors; pp->p_tag; pp++) {
					if (pp->p_tag == *ptp)
						break;
				}
				if (pp->p_tag == 0) {
					(void) fprintf(stderr,
					    gettext("unknown preprocessor "
						"specifier %c\n"), *ptp);
					(void) fflush(stderr);
					return (-1);
				}

				/*
				 * Add it to the pipeline.
				 */
				(void) sprintf(cbp, "%s %s | ",
					troffit ? pp->p_troff : pp->p_nroff,
					pipestage++ == 0 ? manpname : "-");
				cbp += strlen(cbp);

				/*
				 * Special treatment: if tbl is among the
				 * preprocessors and we'll process with
				 * nroff, we have to pass things through
				 * col at the end of the pipeline.
				 */
				if (pp->p_tag == 't' && !troffit)
					needcol++;

				ptp++;
			}
		}

		/*
		 * if catman, use the cat page name
		 * otherwise, dup template and create another
		 * (needed for multiple pages)
		 */
		if (catmando)
			tmpname = catpname;
		else
			(void) mktemp((tmpname = strdup(TEMPLATE)));

		if (! Tflag) {
			if (*localedir != '\0') {
				(void) sprintf(macros, "%s/%s", path, MACROF);
				if (debug)
					printf("\nlocale macros = %s ", macros);
				if (stat(macros, &statb) < 0)
					strcpy(macros, TMAC_AN);
				if (debug)
					printf("\nmacros = %s\n", macros);
			}
		}

		(void) sprintf(cbp, "%s %s %s%s > %s",
			troffit ? troffcmd : "nroff -u0 -Tlp",
			macros,
			pipestage == 0 ? manpname : "-",
			troffit ? "" : " | col -x",
			tmpname);

		/* Reformat the page. */

		if (sys(cmdbuf)) {
			(void) fprintf(stderr, gettext(" aborted (sorry)\n"));
			(void) fflush(stderr);
			(void) unlink(tmpname);
			return (-1);
		}
		if (catmando)
			return (1);

		/*
		 * Attempt to move the cat page to its proper home.
		 */
		(void) sprintf(cmdbuf,
			"trap '' 1 15; /usr/bin/mv -f %s %s 2> /dev/null",
			tmpname,
			catpname);
		if (sys(cmdbuf))
			updatedcat = 0;

		if (debug)
			return (1);

		(void) fprintf(stderr, gettext(" done\n"));
		(void) fflush(stderr);
	}

	/*
	 * Save file name (dup if necessary)
	 * to view later
	 * fix for 1123802 - don't save names if we are invoked as catman
	 */
	if (!catmando)
		*endp++ = (regencat && !updatedcat) ? tmpname :
		    strdup(catpname);

	return (regencat);
}

/*
 * Add <localedir> to the path.
 */

char *
addlocale(char *path)
{

	char *tmp;

	tmp = malloc(strlen(path) + strlen(localedir) + 1);
	(void) sprintf(tmp, "%s/%s", path, localedir);
	return (tmp);

}

/*
 * From the configuration file "man.cf", get the order of suffices of
 * sub-mandirs to be used in the search path for a given mandir.
 */

char *
check_config(char *path)
{
	FILE *fp;
	char submandir[MAXDIRS*2];
	char *sect;
	char fname[MAXPATHLEN];

	(void) sprintf(fname, "%s/%s", path, CONFIG);

	if ((fp = fopen(fname, "r")) == NULL)
		return (NULL);
	else {
		if (get_manconfig(fp, submandir) == -1)
			return (NULL);

		sect = strchr(submandir, '=');
		if (sect != NULL)
			return (++sect);
		else
			return (NULL);
	}
}

/*
 *  This routine is for getting the MANSECTS entry from man.cf.
 *  It sets submandir to the line in man.cf that contains
 *	MANSECTS=sections[,sections]...
 */

int
get_manconfig(FILE *fp, char *submandir)
{
	char *s, *t, *rc;
	char buf[BUFSIZ];

	while ((rc = fgets(buf, sizeof (buf), fp)) != NULL) {

		/*
		 * skip leading blanks
		 */
		for (t = buf; *t != '\0'; t++) {
			if (!isspace(*t))
				break;
		}
		/*
		 * skip line that starts with '#' or empty line
		 */
		if (*t == '#' || *t == '\0')
			continue;

		if (strstr(buf, "MANSECTS") != NULL)
			break;
	}

	/*
	 * the man.cf file doesn't have a MANSECTS entry
	 */
	if (rc == NULL)
		return (-1);

	s = strchr(buf, '\n');
	*s = '\0';	/* replace '\n' with '\0' */

	strcpy(submandir, buf);
	return (0);
}

#ifdef notdef
/*
 * This routine is for debugging purposes. It prints out all the
 * mandir paths.
 */

printmandir(manp)
struct man_node *manp;
{
	struct man_node *p;

	fprintf(stdout, "in printmandir, printing each mandir path ...\n");
	for (p = manp; p != NULL; p = p->next) {
		printf("\tpath = %s\n", p->path);
	}
}

/*
 * This routine is for debugging purposes. It prints out the
 * corresponding sections (submandir directories) of a mandir.
 */

void
printsect(char **s)
{
	char **p;

	fprintf(stdout, "in printsect, printing sections ... \n");
	for (p = s; *p; p++)
		printf("\t%s\n", *p);
}
#endif
