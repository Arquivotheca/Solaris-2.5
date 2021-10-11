/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static char sccsid[] =
	"@(#)interactive.c 1.6 88/02/07 SMI"; /* from UCB 5.4 10/21/86 */
#endif not lint

#ident	"@(#)interactive.c 1.7 94/08/10"

#include <setjmp.h>
#include "restore.h"
#include <ctype.h>
#include <euc.h>
#include <wctype.h>
#include <limits.h>
extern eucwidth_t wp;

#define	round(a, b) (((a) + (b) - 1) / (b) * (b))

/*
 * Things to handle interruptions.
 */
static jmp_buf reset;
static int reset_OK;
static char *nextarg = NULL;

static int dontexpand;
/*
 * XXX  This value is ASCII (but not language) dependent.  In
 * ASCII, it is the DEL character (unlikely to appear in paths).
 * If you are compiling on an EBCDC-based machine, re-define
 * this (0x7f is '"') to be something like 0x7 (DEL).  It's
 * either this hack or re-write the algorithm...
 */
#define	DELIMCHAR	((char)0x7f)

/*
 * Structure and routines associated with listing directories.
 */
struct afile {
	ino_t	fnum;		/* inode number of file */
	char	*fname;		/* file name */
	short	fflags;		/* extraction flags, if any */
	char	ftype;		/* file type, e.g. LEAF or NODE */
};
struct arglist {
	struct afile	*head;	/* start of argument list */
	struct afile	*last;	/* end of argument list */
	struct afile	*base;	/* current list arena */
	int		nent;	/* maximum size of list */
	char		*cmd;	/* the current command */
};

#ifdef __STDC__
static void getcmd(char *, char *, char *, struct arglist *);
static void expandarg(char *, struct arglist *);
static int expand(char *, int, struct arglist *);
static void printlist(char *, ino_t, char *);
static int gmatch(wchar_t *, wchar_t *);
static int addg(struct direct *, char *, char *, struct arglist *);
static int mkentry(char *, ino_t, struct arglist *);
static void formatf(struct arglist *);
static char *copynext(char *, char *);
static int fcmp(struct afile *, struct afile *);
static char *fmtentry(struct afile *);
#else
static void getcmd();
static void expandarg();
static int expand();
static void printlist();
static int gmatch();
static int addg();
static int mkentry();
static void formatf();
static char *copynext();
static int fcmp();
static char *fmtentry();
#endif

/*
 * Read and execute commands from the terminal.
 */
void
#ifdef __STDC__
runcmdshell(void)
#else
runcmdshell()
#endif
{
	register struct entry *np;
	ino_t ino;
	static struct arglist alist = { 0, 0, 0, 0, 0 };
	char curdir[MAXPATHLEN];
	char name[MAXPATHLEN];
	char cmd[BUFSIZ];

	canon("/", curdir);
loop:
	if (setjmp(reset) != 0) {
		for (; alist.head < alist.last; alist.head++)
			freename(alist.head->fname);
		nextarg = NULL;
		volno = 0;
	}
	reset_OK = 1;
	getcmd(curdir, cmd, name, &alist);
	switch (cmd[0]) {
	/*
	 * Add elements to the extraction list.
	 */
	case 'a':
		if (strncmp(cmd, "add", strlen(cmd)) != 0)
			goto bad;
		ino = dirlookup(name);
		if (ino == 0)
			break;
		if (mflag)
			pathcheck(name);
		treescan(name, ino, addfile);
		break;
	/*
	 * Change working directory.
	 */
	case 'c':
		if (strncmp(cmd, "cd", strlen(cmd)) != 0)
			goto bad;
		ino = dirlookup(name);
		if (ino == 0)
			break;
		if (inodetype(ino) == LEAF) {
			(void) fprintf(stderr,
				gettext("%s: not a directory\n"), name);
			break;
		}
		(void) strcpy(curdir, name);
		break;
	/*
	 * Delete elements from the extraction list.
	 */
	case 'd':
		if (strncmp(cmd, "delete", strlen(cmd)) != 0)
			goto bad;
		np = lookupname(name);
		if (np == NIL || (np->e_flags & NEW) == 0) {
			(void) fprintf(stderr,
				gettext("%s: not on extraction list\n"), name);
			break;
		}
		treescan(name, np->e_ino, deletefile);
		break;
	/*
	 * Extract the requested list.
	 */
	case 'e':
		if (strncmp(cmd, "extract", strlen(cmd)) != 0)
			goto bad;
		createfiles();
		createlinks();
		setdirmodes();
		if (dflag)
			checkrestore();
		volno = 0;
		break;
	/*
	 * List available commands.
	 */
	case 'h':
		if (strncmp(cmd, "help", strlen(cmd)) != 0)
			goto bad;
		/*FALLTHROUGH*/
	case '?':
		(void) fprintf(stderr, "%s",
			gettext("Available commands are:\n\
\tls [arg] - list directory\n\
\tcd arg - change directory\n\
\tpwd - print current directory\n\
\tadd [arg] - add `arg' to list of files to be extracted\n\
\tdelete [arg] - delete `arg' from list of files to be extracted\n\
\textract - extract requested files\n\
\tsetmodes - set modes of requested directories\n\
\tquit - immediately exit program\n\
\twhat - list dump header information\n\
\tverbose - toggle verbose flag (useful with ``ls'')\n\
\thelp or `?' - print this list\n\
If no `arg' is supplied, the current directory is used\n"));
		break;
	/*
	 * List a directory.
	 */
	case 'l':
		if (strncmp(cmd, "ls", strlen(cmd)) != 0)
			goto bad;
		ino = dirlookup(name);
		if (ino == 0)
			break;
		printlist(name, ino, curdir);
		break;
	/*
	 * Print current directory.
	 */
	case 'p':
		if (strncmp(cmd, "pwd", strlen(cmd)) != 0)
			goto bad;
		if (curdir[1] == '\0')
			(void) fprintf(stderr, "/\n");
		else
			(void) fprintf(stderr, "%s\n", &curdir[1]);
		break;
	/*
	 * Quit.
	 */
	case 'q':
		if (strncmp(cmd, "quit", strlen(cmd)) != 0)
			goto bad;
		reset_OK = 0;
		return;
	case 'x':
		if (strncmp(cmd, "xit", strlen(cmd)) != 0)
			goto bad;
		return;
	/*
	 * Toggle verbose mode.
	 */
	case 'v':
		if (strncmp(cmd, "verbose", strlen(cmd)) != 0)
			goto bad;
		if (vflag) {
			(void) fprintf(stderr, gettext("verbose mode off\n"));
			vflag = 0;
			break;
		}
		(void) fprintf(stderr, gettext("verbose mode on\n"));
		vflag++;
		break;
	/*
	 * Just restore requested directory modes.
	 */
	case 's':
		if (strncmp(cmd, "setmodes", strlen(cmd)) != 0)
			goto bad;
		setdirmodes();
		break;
	/*
	 * Print out dump header information.
	 */
	case 'w':
		if (strncmp(cmd, "what", strlen(cmd)) != 0)
			goto bad;
		printdumpinfo();
		break;
	/*
	 * Turn on debugging.
	 */
	case 'D':
		if (strncmp(cmd, "Debug", strlen(cmd)) != 0)
			goto bad;
		if (dflag) {
			(void) fprintf(stderr, gettext("debugging mode off\n"));
			dflag = 0;
			break;
		}
		(void) fprintf(stderr, gettext("debugging mode on\n"));
		dflag++;
		break;
	/*
	 * Unknown command.
	 */
	default:
	bad:
		(void) fprintf(stderr,
			gettext("%s: unknown command; type ? for help\n"), cmd);
		break;
	}
	goto loop;
}

/*
 * Read and parse an interactive command.
 * The first word on the line is assigned to "cmd". If
 * there are no arguments on the command line, then "curdir"
 * is returned as the argument. If there are arguments
 * on the line they are returned one at a time on each
 * successive call to getcmd. Each argument is first assigned
 * to "name". If it does not start with "/" the pathname in
 * "curdir" is prepended to it. Finally "canon" is called to
 * eliminate any embedded ".." components.
 */
static void
getcmd(curdir, cmd, name, ap)
	char *curdir, *cmd, *name;
	struct arglist *ap;
{
	register char *cp;
	static char input[BUFSIZ];
	char output[BUFSIZ];
#define	rawname input	/* save space by reusing input buffer */

	/*
	 * Check to see if still processing arguments.
	 */
	if (ap->head != ap->last) {
		(void) strcpy(name, ap->head->fname);
		freename(ap->head->fname);
		ap->head++;
		return;
	}
	if (nextarg != NULL)
		goto getnext;
	/*
	 * Read a command line and trim off trailing white space.
	 */
	do {
		(void) fprintf(stderr, "%s > ", progname);
		(void) fflush(stderr);
		(void) fgets(input, BUFSIZ, terminal);
	} while (!feof(terminal) && input[0] == '\n');
	if (feof(terminal)) {
		(void) strcpy(cmd, "quit");
		return;
	}
	for (cp = &input[strlen(input) - 2]; isspace((u_char)*cp); cp--)
		/* trim off trailing white space and newline */;
	*++cp = '\0';
	/*
	 * Copy the command into "cmd".
	 */
	cp = copynext(input, cmd);
	ap->cmd = cmd;
	/*
	 * If no argument, use curdir as the default.
	 */
	if (*cp == '\0') {
		(void) strcpy(name, curdir);
		return;
	}
	nextarg = cp;
	/*
	 * Find the next argument.
	 */
getnext:
	cp = copynext(nextarg, rawname);
	if (*cp == '\0')
		nextarg = NULL;
	else
		nextarg = cp;
	/*
	 * If it an absolute pathname, canonicalize it and return it.
	 */
	if (rawname[0] == '/') {
		canon(rawname, name);
	} else {
		/*
		 * For relative pathnames, prepend the current directory to
		 * it then canonicalize and return it.
		 */
		(void) strcpy(output, curdir);
		(void) strcat(output, "/");
		(void) strcat(output, rawname);
		canon(output, name);
	}
	expandarg(name, ap);
	(void) strcpy(name, ap->head->fname);
	freename(ap->head->fname);
	ap->head++;
#undef	rawname
}

/*
 * Strip off the next token of the input.
 */
static char *
copynext(input, output)
	char *input, *output;
{
	register char *cp, *bp;
	char quote;

	dontexpand = 0;
	for (cp = input; isspace((u_char)*cp); cp++)
		/* skip to argument */;
	bp = output;
	while (!isspace((u_char)*cp) && *cp != '\0') {
		/*
		 * Handle back slashes.
		 */
		if (*cp == '\\') {
			if (*++cp == '\0') {
				(void) fprintf(stderr,
				gettext("command lines cannot be continued\n"));
				continue;
			}
			*bp++ = *cp++;
			continue;
		}
		/*
		 * The usual unquoted case.
		 */
		if (*cp != '\'' && *cp != '"') {
			*bp++ = *cp++;
			continue;
		}
		/*
		 * Handle single and double quotes.
		 */
		quote = *cp++;
		dontexpand = 1;
		while (*cp != quote && *cp != '\0')
			*bp++ = *cp++;
		if (*cp++ == '\0') {
			(void) fprintf(stderr,
				gettext("missing %c\n"), (u_char)quote);
			cp--;
			continue;
		}
	}
	*bp = '\0';
	return (cp);
}

/*
 * Canonicalize file names to always start with ``./'' and
 * remove any imbedded "." and ".." components.
 */
void
canon(rawname, canonname)
	char *rawname, *canonname;
{
	register char *cp, *np;

	if (strcmp(rawname, ".") == 0 || strncmp(rawname, "./", 2) == 0)
		(void) strcpy(canonname, "");
	else if (rawname[0] == '/')
		(void) strcpy(canonname, ".");
	else
		(void) strcpy(canonname, "./");
	(void) strcat(canonname, rawname);
	/*
	 * Eliminate multiple and trailing '/'s
	 */
	for (cp = np = canonname; *np != '\0'; cp++) {
		*cp = *np++;
		while (*cp == '/' && *np == '/')
			np++;
	}
	*cp = '\0';
	if (*--cp == '/')
		*cp = '\0';
	/*
	 * Eliminate extraneous "." and ".." from pathnames.
	 */
	np = canonname;
	while (*np != '\0') {
		np++;
		cp = np;
		while (*np != '/' && *np != '\0')
			np++;
		if (np - cp == 1 && *cp == '.') {
			cp--;
			(void) strcpy(cp, np);
			np = cp;
		}
		if (np - cp == 2 && strncmp(cp, "..", 2) == 0) {
			cp--;
			while (cp > &canonname[1] && *--cp != '/')
				/* find beginning of name */;
			(void) strcpy(cp, np);
			np = cp;
		}
	}
}

/*
 * globals (file name generation)
 *
 * "*" in params matches r.e ".*"
 * "?" in params matches r.e. "."
 * "[...]" in params matches character class
 * "[...a-z...]" in params matches a through z.
 */
static void
expandarg(arg, ap)
	char *arg;
	register struct arglist *ap;
{
	static struct afile single;
	int size;

	ap->head = ap->last = (struct afile *)0;
	if (dontexpand)
		size = 0;
	else
		size = expand(arg, 0, ap);
	if (size == 0) {
		struct entry *ep;

		ep = lookupname(arg);
		single.fnum = ep ? ep->e_ino : 0;	/* XXX */
		single.fname = savename(arg);
		ap->head = &single;
		ap->last = ap->head + 1;
		return;
	}
	qsort((char *)ap->head, ap->last - ap->head, sizeof (*ap->head),
		(int (*)(const void *, const void *)) fcmp);
}

/*
 * Expand a file name
 */
static int
expand(as, rflg, ap)
	char *as;
	int rflg;
	register struct arglist *ap;
{
	int		count, size;
	char		dir = 0;
	char		*rescan = 0;
	RST_DIR		*dirp;
	register char	*s, *cs;
	int		sindex, rindexa, lindex;
	struct direct	*dp;
	register char	slash;
	register char	*rs;
	register char	c;
	wchar_t 	w_fname[PATH_MAX+1];
	wchar_t		w_pname[PATH_MAX+1];

	/*
	 * check for meta chars
	 */
	s = cs = as;
	slash = 0;
	while (*cs != '*' && *cs != '?' && *cs != '[') {
		if (*cs++ == 0) {
			if (rflg && slash)
				break;
			else
				return (0);
		} else if (*cs == '/') {
			slash++;
		}
	}
	for (;;) {
		if (cs == s) {
			s = "";
			break;
		} else if (*--cs == '/') {
			*cs = 0;
			if (s == cs)
				s = "/";
			break;
		}
	}
	if ((dirp = rst_opendir(s)) != NULL)
		dir++;
	count = 0;
	if (*cs == 0)
		*cs++ = DELIMCHAR;
	if (dir) {
		/*
		 * check for rescan
		 */
		rs = cs;
		do {
			if (*rs == '/') {
				rescan = rs;
				*rs = 0;
			}
		} while (*rs++);
		sindex = ap->last - ap->head;
		while ((dp = rst_readdir(dirp)) != NULL && dp->d_ino != 0) {
			if (!dflag && BIT(dp->d_ino, dumpmap) == 0)
				continue;
			if ((*dp->d_name == '.' && *cs != '.'))
				continue;
			(void) mbstowcs(w_fname, dp->d_name, PATH_MAX);
			(void) mbstowcs(w_pname, cs, PATH_MAX);
			if (gmatch(w_fname, w_pname)) {
				if (addg(dp, s, rescan, ap) < 0)
					return (-1);
				count++;
			}
		}
		if (rescan) {
			rindexa = sindex;
			lindex = ap->last - ap->head;
			if (count) {
				count = 0;
				while (rindexa < lindex) {
					size = expand(ap->head[rindexa].fname,
					    1, ap);
					if (size < 0)
						return (size);
					count += size;
					rindexa++;
				}
			}
			bcopy((char *)&ap->head[lindex],
			    (char *)&ap->head[sindex],
			    (ap->last - &ap->head[rindexa]) *
				sizeof (*ap->head));
			ap->last -= lindex - sindex;
			*rescan = '/';
		}
	}
	s = as;
	while ((c = *s) != '\0')
		*s++ = (c != DELIMCHAR ? c : '/');
	return (count);
}

/*
 * Check for a name match
 */
static int
gmatch(s, p)
	register wchar_t	*s, *p;
{
	register int	scc;
	wchar_t		c;
	char		ok;
	int		lc;

	scc = *s++;
	switch (c = *p++) {

	case '[':
		ok = 0;
		lc = -1;
		while (c = *p++) {
			if (c == ']') {
				return (ok ? gmatch(s, p) : 0);
			} else if (c == '-') {
				wchar_t rc = *p++;
				/*
				 * Check both ends must belong to
				 * the same codeset.
				 */
				if (wcsetno(lc) != wcsetno(rc)) {
					/*
					 * If not, ignore the '-'
					 * operator and [x-y] is
					 * treated as if it were
					 * [xy].
					 */
					if (scc == lc)
						ok++;
					if (scc == (lc = rc))
						ok++;
				} else if (lc <= scc && scc <= rc)
					ok++;
			} else {
				lc = c;
				if (scc == lc)
					ok++;
			}
		}
		return (0);

	default:
		if (c != scc)
			return (0);
		/*FALLTHROUGH*/

	case '?':
		return (scc ? gmatch(s, p) : 0);

	case '*':
		if (*p == 0)
			return (1);
		s--;
		while (*s) {
			if (gmatch(s++, p))
				return (1);
		}
		return (0);

	case 0:
		return (scc == 0);
	}
}

/*
 * Construct a matched name.
 */
static int
addg(dp, as1, as3, ap)
	struct direct	*dp;
	char		*as1, *as3;
	struct arglist	*ap;
{
	register char	*s1, *s2;
	register int	c;
	char		buf[BUFSIZ];

	s2 = buf;
	s1 = as1;
	while ((c = *s1++) != '\0') {
		if (c == DELIMCHAR) {
			*s2++ = '/';
			break;
		}
		*s2++ = (char)c;
	}
	s1 = dp->d_name;
	while (*s2 = *s1++)
		s2++;
	s1 = as3;
	if (s1 != '\0') {
		*s2++ = '/';
		while (*s2++ = *++s1)
			/* void */;
	}
	if (mkentry(buf, dp->d_ino, ap) == FAIL)
		return (-1);
	return (0);
}

/*
 * Do an "ls" style listing of a directory
 */
static void
printlist(name, ino, basename)
	char *name;
	ino_t ino;
	char *basename;
{
	register struct afile *fp;
	register struct direct *dp;
	static struct arglist alist = { 0, 0, 0, 0, "ls" };
	struct afile single;
	RST_DIR *dirp;

	if ((dirp = rst_opendir(name)) == NULL) {
		single.fnum = ino;
		single.fname = savename(name + strlen(basename) + 1);
		alist.head = &single;
		alist.last = alist.head + 1;
	} else {
		alist.head = (struct afile *)0;
		(void) fprintf(stderr, "%s:\n", name);
		while (dp = rst_readdir(dirp)) {
			if (dp == NULL || dp->d_ino == 0)
				break;
			if (!dflag && BIT(dp->d_ino, dumpmap) == 0)
				continue;
			if (vflag == 0 &&
			    (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0))
				continue;
			if (!mkentry(dp->d_name, dp->d_ino, &alist))
				return;
		}
	}
	if (alist.head != 0) {
		qsort((char *)alist.head, alist.last - alist.head,
			sizeof (*alist.head),
			(int (*)(const void *, const void *)) fcmp);
		formatf(&alist);
		for (fp = alist.head; fp < alist.last; fp++)
			freename(fp->fname);
	}
	if (dirp != NULL)
		(void) fprintf(stderr, "\n");
}

/*
 * Read the contents of a directory.
 */
static int
mkentry(name, ino, ap)
	char *name;
	ino_t ino;
	register struct arglist *ap;
{
	register struct afile *fp;

	if (ap->base == NULL) {
		ap->nent = 20;
		ap->base = (struct afile *)calloc((unsigned)ap->nent,
			sizeof (struct afile));
		if (ap->base == NULL) {
			(void) fprintf(stderr,
				gettext("%s: out of memory\n"), ap->cmd);
			return (FAIL);
		}
	}
	if (ap->head == 0)
		ap->head = ap->last = ap->base;
	fp = ap->last;
	fp->fnum = ino;
	fp->fname = savename(name);
	fp++;
	if (fp == ap->head + ap->nent) {
		ap->base = (struct afile *)realloc((char *)ap->base,
		    (unsigned)(2 * ap->nent * sizeof (struct afile)));
		if (ap->base == 0) {
			(void) fprintf(stderr,
				gettext("%s: out of memory\n"), ap->cmd);
			return (FAIL);
		}
		ap->head = ap->base;
		fp = ap->head + ap->nent;
		ap->nent *= 2;
	}
	ap->last = fp;
	return (GOOD);
}

/*
 * Print out a pretty listing of a directory
 */
static void
formatf(ap)
	register struct arglist *ap;
{
	register struct afile *fp;
	struct entry *np;
	int width = 0, w, nentry = ap->last - ap->head;
	int i, j, len, columns, lines;
	char *cp;

	if (ap->head == ap->last)
		return;
	for (fp = ap->head; fp < ap->last; fp++) {
		fp->ftype = inodetype(fp->fnum);
		np = lookupino(fp->fnum);
		if (np != NIL)
			fp->fflags = np->e_flags;
		else
			fp->fflags = 0;
		len = strlen(fmtentry(fp));
		if (len > width)
			width = len;
	}
	width += 2;
	columns = 80 / width;
	if (columns == 0)
		columns = 1;
	lines = (nentry + columns - 1) / columns;
	for (i = 0; i < lines; i++) {
		for (j = 0; j < columns; j++) {
			fp = ap->head + j * lines + i;
			cp = fmtentry(fp);
			(void) fprintf(stderr, "%s", cp);
			if (fp + lines >= ap->last) {
				(void) fprintf(stderr, "\n");
				break;
			}
			w = strlen(cp);
			while (w < width) {
				w++;
				(void) fprintf(stderr, " ");
			}
		}
	}
}

/*
 * Comparison routine for qsort.
 */
static int
fcmp(f1, f2)
	register struct afile *f1, *f2;
{

	return (strcoll(f1->fname, f2->fname));
}

/*
 * Format a directory entry.
 */
static char *
fmtentry(fp)
	register struct afile *fp;
{
	static char fmtres[BUFSIZ];
	static int precision = 0;
	int i;
	register char *cp, *dp;

	if (!vflag) {
		fmtres[0] = '\0';
	} else {
		if (precision == 0)
			for (i = maxino; i > 0; i /= 10)
				precision++;
		(void) sprintf(fmtres, "%*ld ", precision, fp->fnum);
	}
	dp = &fmtres[strlen(fmtres)];
	if (dflag && BIT(fp->fnum, dumpmap) == 0)
		*dp++ = '^';
	else if ((fp->fflags & NEW) != 0)
		*dp++ = '*';
	else
		*dp++ = ' ';
	for (cp = fp->fname; *cp; cp++)
		if (!vflag && (!ISPRINT(*cp, wp)))
			*dp++ = '?';
		else
			*dp++ = *cp;
	if (fp->ftype == NODE)
		*dp++ = '/';
	*dp++ = 0;
	return (fmtres);
}

/*
 * respond to interrupts
 */
/*ARGSUSED*/
void
onintr(sig)
	int	sig;
{
	char	buf[300];

	if (command == 'i' && reset_OK)
		longjmp(reset, 1);
	/* XXX - hsmrestore */
	(void) sprintf(buf, gettext("%s interrupted, continue"), progname);
	if (reply(buf) == FAIL)
		done(1);
}
