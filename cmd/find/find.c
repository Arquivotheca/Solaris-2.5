/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* 	Portions Copyright(c) 1994, 1988, Sun Microsystems Inc.	*/
/*	All Rights Reserved					*/

/*	Parts of this product may be derived from		*/
/*	Mortice Kern Systems Inc. and Berkeley 4.3 BSD systems.	*/
/*	licensed from  Mortice Kern Systems Inc. and 		*/
/*	the University of California.				*/

/*
 * Copyright 1985, 1990 by Mortice Kern Systems Inc.  All rights reserved.
 */


#ident	"@(#)find.c	1.26	94/07/21 SMI"	/* SVr4.0 4.34	*/
/*
 *
 * Rewrite of find program to use nftw(new file tree walk) library function
 * This is intended to be upward compatible to System V release 3.
 * There is one additional feature:
 *	If the last argument to -exec is {} and you specify + rather
 *	than ';', the command will be invoked fewer times with {}
 *	replaced by groups of pathnames.
 */


#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <ftw.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <libgen.h>
#include <ctype.h>
#include <wait.h>
#include <fnmatch.h>
#include <langinfo.h>


#define	A_DAY		(long)(60*60*24)	/* a day full of seconds */
#define	BLKSIZ		512
#define	round(x, s)	(((x)+(s)-1)&~((s)-1))
#ifndef FTW_SLN
#define	FTW_SLN		7
#endif
#define	LINEBUF_SIZE		LINE_MAX	/* input or output lines */
#define	REMOTE_FS		"/etc/dfs/fstypes"
#define	N_FSTYPES		20

/*
 * This is the list of operations
 */
enum Command
{
	PRINT, DEPTH, LOCAL, MOUNT, ATIME, MTIME, CTIME, NEWER,
	NAME, USER, GROUP, INUM, SIZE, LINKS, PERM, EXEC, OK, CPIO, NCPIO,
	TYPE, AND, OR, NOT, LPAREN, RPAREN, CSIZE, VARARGS, FOLLOW,
	PRUNE, NOUSER, NOGRP, FSTYPE, LS
};

enum Type
{
	Unary, Id, Num, Str, Exec, Cpio, Op
};

struct Args
{
	char		name[10];
	enum Command	action;
	enum Type	type;
};

/*
 * Except for pathnames, these are the only legal arguments
 */
static struct Args commands[] =
{
	"!",		NOT,	Op,
	"(",		LPAREN,	Unary,
	")",		RPAREN,	Unary,
	"-a",		AND,	Op,
	"-atime",	ATIME,	Num,
	"-cpio",	CPIO,	Cpio,
	"-ctime",	CTIME,	Num,
	"-depth",	DEPTH,	Unary,
	"-exec",	EXEC,	Exec,
	"-follow",	FOLLOW, Unary,
	"-group",	GROUP,	Num,
	"-inum",	INUM,	Num,
	"-links",	LINKS,	Num,
	"-local",	LOCAL,	Unary,
	"-mount",	MOUNT,	Unary,
	"-mtime",	MTIME,	Num,
	"-name",	NAME,	Str,
	"-ncpio",	NCPIO,  Cpio,
	"-newer",	NEWER,	Str,
	"-o",		OR,	Op,
	"-ok",		OK,	Exec,
	"-perm",	PERM,	Num,
	"-print",	PRINT,	Unary,
	"-size",	SIZE,	Num,
	"-type",	TYPE,	Num,
	"-xdev",	MOUNT,	Unary,
	"-user",	USER,	Num,
	"-prune",	PRUNE,	Unary,
	"-nouser",	NOUSER,	Unary,
	"-nogroup",	NOGRP,	Unary,
	"-fstype",	FSTYPE,	Str,
	"-ls",		LS,	Unary,
	0,
};

union Item
{
	struct Node	*np;
	struct Arglist	*vp;
	time_t		t;
	char		*cp;
	char		**ap;
	long		l;
	int		i;
};

struct Node
{
	struct Node	*next;
	enum Command	action;
	union Item	first;
	union Item	second;
};

/* if no -print, -exec or -ok replace "expression" with "(expression) -print" */
static	struct	Node PRINT_NODE = { 0, PRINT, 0, 0};
static	struct	Node LPAREN_NODE = { &PRINT_NODE, LPAREN, 0, 0};

/*
 * Prototype variable size arglist buffer
 */

struct Arglist
{
	struct Arglist	*next;
	char		*end;
	char		*nextstr;
	char		**firstvar;
	char		**nextvar;
	char		*arglist[1];
};


static int		compile();
static int		execute();
static int		doexec();
static struct Args	*lookup();
static int		ok();
static void		usage();
static struct Arglist	*varargs();
static int		list();
static char		*getgroup();
static FILE		*cmdopen();
static int		cmdclose();
static char		*getshell();
static void 		init_remote_fs();
static char		*getname();
static int		readmode();
static mode_t		getmode();


static int		walkflags = FTW_CHDIR|FTW_PHYS;
static struct Node	*savetnode;
static struct Node	*topnode;
static char		*cpio[] = { "cpio", "-o", 0 };
static char		*ncpio[] = { "cpio", "-oc", 0 };
static char		*cpiol[] = { "cpio", "-oL", 0 };
static char		*ncpiol[] = { "cpio", "-ocL", 0 };
static long		now;
static FILE		*output;
static char		*dummyarg = (char *)-1;
static int		lastval;
static int		varsize;
static struct Arglist	*lastlist;
static char		*cmdname;
static char		*remote_fstypes[N_FSTYPES+1];
static int		fstype_index = 0;
static int		action_expression = 0;	/* -print, -exec, or -ok */
static int		error = 0;


extern int	exec();
extern int	nftw();
extern struct	group	*getgruid();
extern time_t	time();
extern int	errno;
extern char	**environ;

main(argc, argv)
char *argv[];
{
	register char *cp;
	register int paths;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	cmdname = argv[0];
	if (time(&now) == (time_t) -1) {
		(void) fprintf(stderr, gettext("%s: time() %s\n"),
			cmdname, strerror(errno));
		exit(1);
	}

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("%s: insufficient number of arguments\n"), cmdname);
		usage();
	}

	for (paths = 1; (cp = argv[paths]) != 0; ++paths) {
		if (*cp == '-')
			break;
		else if ((*cp == '!' || *cp == '(') && *(cp+1) == 0)
			break;
	}

	if (paths == 1) /* no path-list */
		usage();
	output = stdout;
	/* allocate enough space for the compiler */
	topnode = (struct Node *) malloc(argc*sizeof (struct Node));
	savetnode = (struct Node *) malloc(argc*sizeof (struct Node));
	if (compile(argv+paths, topnode, &action_expression) == 0) {
		/* no expression, default to -print */
		topnode = &PRINT_NODE;
	} else if (!action_expression) {
		/* if no action expression add -print */
		LPAREN_NODE.first.np = topnode;
		topnode = &LPAREN_NODE;
	}

	(void) memcpy(savetnode, topnode, (argc*sizeof (struct Node)));

	while (--paths) {
		if (nftw(*++argv, execute, 1000, walkflags)) {
			(void) fprintf(stderr,
			    gettext("%s: cannot open %s: %s\n"),
				cmdname, *argv, strerror(errno));
			error = 1;
		}
		if (paths > 1)
			(void) memcpy(topnode, savetnode,
			    (argc*sizeof (struct Node)));
	}

	/* execute any remaining variable length lists */
	while (lastlist) {
		if (lastlist->end != lastlist->nextstr) {
			*lastlist->nextvar = 0;
			(void) doexec((char *)0, lastlist->arglist);
		}
		lastlist = lastlist->next;
	}
	if (output != stdout)
		return (cmdclose(output));
	return (error);
}

/*
 * compile the arguments
 */

static int
compile(argv, np, actionp)
char **argv;
register struct Node *np;
int *actionp;
{
	register char *b;
	register char **av;
	register struct Node *oldnp = topnode;
	struct Args *argp;
	char **com;
	int i;
	enum Command wasop = PRINT;

	for (av = argv; *av && (argp = lookup(*av)); av++) {
		np->next = 0;
		np->action = argp->action;
		np->second.i = 0;
		if (argp->type == Op) {
			if (wasop == NOT || (wasop && np->action != NOT)) {
				(void) fprintf(stderr,
				gettext("%s: operand follows operand\n"),
						cmdname);
				exit(1);
			}
			if (np->action != NOT && oldnp == 0)
				goto err;
			wasop = argp->action;
		} else {
			wasop = PRINT;
			if (argp->type != Unary) {
				if (!(b = *++av)) {
					(void) fprintf(stderr,
					gettext("%s: incomplete statement\n"),
							cmdname);
					exit(1);
				}
				if (argp->type == Num) {
					if ((argp->action != PERM) ||
					    (*b != '+')) {
						if (*b == '+' || *b == '-') {
							np->second.i = *b;
							b++;
						}
					}
				}
			}
		}
		switch (argp->action) {
		case AND:
			continue;
		case NOT:
			break;
		case OR:
			np->first.np = topnode;
			topnode = np;
			oldnp->next = 0;
			break;

		case LPAREN: {
			struct Node *save = topnode;
			topnode = np+1;
			i = compile(++av, topnode, actionp);
			np->first.np = topnode;
			topnode = save;
			av += i;
			oldnp = np;
			np += i + 1;
			oldnp->next = np;
			continue;
		}

		case RPAREN:
			if (oldnp == 0)
				goto err;
			oldnp->next = 0;
			return (av-argv);

		case FOLLOW:
			walkflags &= ~FTW_PHYS;
			break;
		case MOUNT:
			walkflags |= FTW_MOUNT;
			break;
		case DEPTH:
			walkflags |= FTW_DEPTH;
			break;

		case LOCAL:
			np->first.l = 0;
			np->second.i = '+';
			/*
			 * Make it compatible to df -l for
			 * future enhancement. So, anything
			 * that is not remote, then it is
			 * local.
			 */
			init_remote_fs();
			break;

		case SIZE:
			if (b[strlen(b)-1] == 'c')
				np->action = CSIZE;
			/*FALLTHROUGH*/
		case CTIME:
		case MTIME:
		case ATIME:
		case LINKS:
		case INUM:
			np->first.l = atoi(b);
			break;
		case USER:

		case GROUP: {
			struct	passwd	*pw;
			struct	group *gr;
			i = -1;
			if (argp->action == USER) {
				if ((pw = getpwnam(b)) != 0)
					i = (int)pw->pw_uid;
			} else {
				if ((gr = getgrnam(b)) != 0)
					i = (int)gr->gr_gid;
			}
			if (i == -1) {
				if (fnmatch("[0-9][0-9][0-9]*", b, 0) &&
						fnmatch("[0-9][0-9]", b, 0) &&
						fnmatch("[0-9]", b, 0)) {
					(void) fprintf(stderr, gettext(
					    "%s: cannot find %s name\n"),
						cmdname, *av);
					exit(1);
				}
				i = atoi(b);
			}
			np->first.l = i;
			break;
		}

		case EXEC:
		case OK:
			walkflags &= ~FTW_CHDIR;
			np->first.ap = av;
			(*actionp)++;
			while (1) {
				if ((b = *av) == 0) {
					(void) fprintf(stderr,
					gettext("%s: incomplete statement\n"),
						cmdname);
					exit(1);
				}
				if (strcmp(b, ";") == 0) {
					*av = 0;
					break;
				} else if (strcmp(b, "{}") == 0)
					*av = dummyarg;
				else if (strcmp(b, "+") == 0 &&
					av[-1] == dummyarg &&
					np->action == EXEC) {
					av[-1] = 0;
					np->first.vp = varargs(np->first.ap);
					np->action = VARARGS;
					break;
				}
				av++;
			}
			break;

		case NAME:
			np->first.cp = b;
			break;
		case PERM:
			if (*b == '-')
				++b;

			if (readmode(b) != NULL) {
				(void) fprintf(stderr, gettext(
				    "find: -perm: Bad permision string\n"));
				usage();
			}
			np->first.l = (long) getmode((mode_t)0);
			break;
		case TYPE:
			i = *b;
			np->first.l = i == 'd' ? S_IFDIR :
			    i == 'b' ? S_IFBLK :
			    i == 'c' ? S_IFCHR :
#ifdef S_IFIFO
			    i == 'p' ? S_IFIFO :
#endif
			    i == 'f' ? S_IFREG :
#ifdef S_IFLNK
			    i == 'l' ? S_IFLNK :
#endif
			    0;
			break;

		case CPIO:
			if (walkflags & FTW_PHYS)
				com = cpio;
			else
				com = cpiol;
			goto common;

		case NCPIO: {
			FILE *fd;

			if (walkflags & FTW_PHYS)
				com = ncpio;
			else
				com = ncpiol;
		common:
			/* set up cpio */
			if ((fd = fopen(b, "w")) == NULL) {
				(void) fprintf(stderr,
					gettext("%s: cannot create %s\n"),
					cmdname, b);
				exit(1);
			}

			np->first.l = (long)cmdopen("cpio", com, "w", fd);
			(void) fclose(fd);
			walkflags |= FTW_DEPTH;
			np->action = CPIO;
		}
			/*FALLTHROUGH*/
		case PRINT:
			(*actionp)++;
			break;

		case NEWER: {
			struct stat statb;
			if (stat(b, &statb) < 0) {
				(void) fprintf(stderr,
					gettext("%s: cannot access %s\n"),
					cmdname, b);
				exit(1);
			}
			np->first.l = statb.st_mtime;
			np->second.i = '+';
			break;
		}

		case PRUNE:
		case NOUSER:
		case NOGRP:
			break;
		case FSTYPE:
			np->first.cp = b;
			break;
		case LS:
			(*actionp)++;
			break;
		}

		oldnp = np++;
		oldnp->next = np;
	}

	if ((*av) || (wasop))
		goto err;
	oldnp->next = 0;
	return (av-argv);
err:
	if (*av)
		(void) fprintf(stderr,
		    gettext("%s: bad option %s\n"), cmdname, *av);
	else
		(void) fprintf(stderr, gettext("%s: bad option\n"), cmdname);
	usage();
	/*NOTREACHED*/
}

/*
 * print out a usage message
 */

static void
usage()
{
	(void) fprintf(stderr,
	    gettext("%s: path-list predicate-list\n"), cmdname);
	exit(1);
}

/*
 * This is the function that gets executed at each node
 */

static int
execute(name, statb, type, state)
char *name;
struct stat *statb;
struct FTW *state;
{
	register struct Node *np = topnode;
	register int val;
	time_t t;
	register long l;
	int not = 1;

	if (type == FTW_NS) {
		(void) fprintf(stderr, gettext("%s: stat() error %s: %s\n"),
			cmdname, name, strerror(errno));
		error = 1;
		return (0);
	} else if (type == FTW_DNR) {
		(void) fprintf(stderr, gettext("%s: cannot read dir %s: %s\n"),
			cmdname, name, strerror(errno));
		error = 1;
		return (0);
	} else if (type == FTW_SLN) {
		(void) fprintf(stderr,
			gettext("%s: cannot follow symbolic link %s: %s\n"),
			cmdname, name, strerror(errno));
		error = 1;
		return (0);
	}

	while (np) {
		switch (np->action) {
		case NOT:
			not = !not;
			np = np->next;
			continue;

		case OR:
		case LPAREN: {
			struct Node *save = topnode;
			topnode = np->first.np;
			(void) execute(name, statb, type, state);
			val = lastval;
			topnode = save;
			if (np->action == OR) {
				if (val)
					return (0);
				val = 1;
			}
			break;
		}

		case LOCAL: {
			int	nremfs;
			val = 1;
			/*
			 * If file system type matches the remote
			 * file system type, then it is not local.
			 */
			for (nremfs = 0; nremfs < fstype_index; nremfs++) {
				if (strcmp(remote_fstypes[nremfs],
						statb->st_fstype) == 0) {
					val = 0;
					break;
				}
			}
			break;
		}

		case TYPE:
			l = (long)statb->st_mode&S_IFMT;
			goto num;

		case PERM:
			l = (long)statb->st_mode&07777;
			if (np->second.i == '-')
				val = ((l&np->first.l) == np->first.l);
			else
				val = (l == np->first.l);
			break;

		case INUM:
			l = (long)statb->st_ino;
			goto num;
		case NEWER:
			l = statb->st_mtime;
			goto num;
		case ATIME:
			t = statb->st_atime;
			goto days;
		case CTIME:
			t = statb->st_ctime;
			goto days;
		case MTIME:
			t = statb->st_mtime;
		days:
			l = (now-t)/A_DAY;
			goto num;
		case CSIZE:
			l = statb->st_size;
			goto num;
		case SIZE:
			l = round(statb->st_size, BLKSIZ)/BLKSIZ;
			goto num;
		case USER:
			l = (long)statb->st_uid;
			goto num;
		case GROUP:
			l = (long)statb->st_gid;
			goto num;
		case LINKS:
			l = (long)statb->st_nlink;
		num:
			if (np->second.i == '+')
				val = (l > np->first.l);
			else if (np->second.i == '-')
				val = (l < np->first.l);
			else
				val = (l == np->first.l);
			break;
		case OK:
			val = ok(name, np->first.ap);
			break;
		case EXEC:
			val = doexec(name, np->first.ap);
			break;

		case VARARGS: {
			register struct Arglist *ap = np->first.vp;
			register char *cp;
			cp = ap->nextstr - (strlen(name)+1);
			if (cp >= (char *)(ap->nextvar+3)) {
				/* there is room just copy the name */
				val = 1;
				(void) strcpy(cp, name);
				*ap->nextvar++ = cp;
				ap->nextstr = cp;
			} else {
				/* no more room, exec command */
				*ap->nextvar++ = name;
				*ap->nextvar = 0;
				val = doexec((char *)0, ap->arglist);
				ap->nextstr = ap->end;
				ap->nextvar = ap->firstvar;
			}
			break;
		}

		case DEPTH:
		case MOUNT:
		case FOLLOW:
			val = 1;
			break;

		case NAME: {
			if ((name+state->base)[0] == '.' &&
				np->first.cp[0] != '.') {
				val = 0;
				break;
			}
			val = !fnmatch(np->first.cp, name+state->base, 0);
			break;
		}

		case PRUNE:
			if (type == FTW_D)
				state->quit = FTW_PRUNE;
			val = 1;
			break;
		case NOUSER:
			val = ((getpwuid(statb->st_uid)) == 0);
			break;
		case NOGRP:
			val = ((getgrgid(statb->st_gid)) == 0);
			break;
		case FSTYPE:
			val = (strcmp(np->first.cp, statb->st_fstype) == 0);
			break;
		case CPIO:
			output = (FILE *)np->first.l;
			(void) fprintf(output, "%s\n", name);
			val = 1;
			break;
		case PRINT:
			(void) fprintf(stdout, "%s\n", name);
			val = 1;
			break;
		case LS:
			(void) list(name, statb);
			val = 1;
			break;
		}
		/*
		 * evaluate 'val' and 'not' (exclusive-or)
		 * if no inversion (not == 1), return only when val == 0
		 * (primary not true). Otherwise, invert the primary
		 * and return when the primary is true.
		 * 'Lastval' saves the last result (fail or pass) when
		 * returning back to the calling routine.
		 */
		if (val^not) {
			lastval = 0;
			return (0);
		}
		lastval = 1;
		not = 1;
		np = np->next;
	}
	return (0);
}

/*
 * code for the -ok option
 */

static int
ok(name, argv)
char *name;
char *argv[];
{
	int c, yes = 0;

	(void) fflush(stdout); 	/* to flush possible `-print' */

	if ((*argv != dummyarg) && (strcmp(*argv, name)))
		(void) fprintf(stderr, "< %s ... %s >?   ", *argv, name);
	else
		(void) fprintf(stderr, "< {} ... %s >?   ", name);

	(void) fflush(stderr);
	if ((c = tolower(getchar())) == *nl_langinfo(YESSTR))
		yes = 1;
	while (c != '\n')
		if (c == EOF)
			exit(2);
		else
			c = getchar();
	return (yes? doexec(name, argv): 0);
}

/*
 * execute argv with {} replaced by name
 */

static int
doexec(name, argv)
char *name;
register char *argv[];
{
	register char *cp;
	register char **av = argv;
	int r = 0;
	pid_t pid;

	(void) fflush(stdout);		/* to flush possible `-print' */
	if (name) {
		while (cp = *av++) {
			if (cp == dummyarg)
				av[-1] = name;

		}
	}
	if (argv[0] == NULL)	/* null command line */
		return (r);

	if (pid = fork()) {
		while (wait(&r) != pid);
	} else /* child */ {
		(void) execvp(argv[0], argv);
		exit(1);
	}

	return (!r);
}


/*
 *  Table lookup routine
 */
static struct Args *
lookup(word)
register char *word;
{
	register struct Args *argp = commands;
	register int second;
	if (word == 0 || *word == 0)
		return (0);
	second = word[1];
	while (*argp->name) {
		if (second == argp->name[1] && strcmp(word, argp->name) == 0)
			return (argp);
		argp++;
	}
	return (0);
}


/*
 * Get space for variable length argument list
 */

static struct Arglist *
varargs(com)
char **com;
{
	register struct Arglist *ap;
	register int n;
	register char **ep;
	if (varsize == 0) {
		n = 2*sizeof (char **);
		for (ep = environ; *ep; ep++)
			n += (strlen(*ep)+sizeof (char **) + 1);
		varsize = sizeof (struct Arglist)+ARG_MAX-PATH_MAX-n-1;
	}
	ap = (struct Arglist *) malloc(varsize+1);
	ap->end = (char *)ap + varsize;
	ap->nextstr = ap->end;
	ap->nextvar = ap->arglist;
	while (*ap->nextvar++ = *com++);
	ap->nextvar--;
	ap->firstvar = ap->nextvar;
	ap->next = lastlist;
	lastlist = ap;
	return (ap);
}

/*
 * filter command support
 * fork and exec cmd(argv) according to mode:
 *
 *	"r"	with fp as stdin of cmd (default stdin), cmd stdout returned
 *	"w"	with fp as stdout of cmd (default stdout), cmd stdin returned
 */

#define	CMDERR	((1<<8)-1)	/* command error exit code		*/
#define	MAXCMDS	8		/* max # simultaneous cmdopen()'s	*/

static struct			/* info for each cmdopen()		*/
{
	FILE	*fp;		/* returned by cmdopen()		*/
	pid_t	pid;		/* pid used by cmdopen()		*/
} cmdproc[MAXCMDS];

static FILE *
cmdopen(cmd, argv, mode, fp)
char	*cmd;
char	**argv;
char	*mode;
FILE	*fp;
{
	register int	proc;
	register int	cmdfd;
	register int	usrfd;
	int		pio[2];

	switch (*mode) {
	case 'r':
		cmdfd = 1;
		usrfd = 0;
		break;
	case 'w':
		cmdfd = 0;
		usrfd = 1;
		break;
	default:
		return (0);
	}

	for (proc = 0; proc < MAXCMDS; proc++)
		if (!cmdproc[proc].fp)
			break;
	if (proc >= MAXCMDS)
		return (0);

	if (pipe(pio))
		return (0);

	switch (cmdproc[proc].pid = fork()) {
	case -1:
		return (0);
	case 0:
		if (fp && fileno(fp) != usrfd) {
			(void) close(usrfd);
			if (dup2(fileno(fp), usrfd) != usrfd)
				_exit(CMDERR);
			(void) close(fileno(fp));
		}
		(void) close(cmdfd);
		if (dup2(pio[cmdfd], cmdfd) != cmdfd)
			_exit(CMDERR);
		(void) close(pio[cmdfd]);
		(void) close(pio[usrfd]);
		(void) execvp(cmd, argv);
		if (errno == ENOEXEC) {
			register char	**p;
			char		**v;

			/*
			 * assume cmd is a shell script
			 */

			p = argv;
			while (*p++);
			if (v = (char **)malloc((p - argv + 1) *
					sizeof (char **))) {
				p = v;
				*p++ = cmd;
				if (*argv) argv++;
				while (*p++ = *argv++);
				(void) execv(getshell(), v);
			}
		}
		_exit(CMDERR);
		/*NOTREACHED*/
	default:
		(void) close(pio[cmdfd]);
		return (cmdproc[proc].fp = fdopen(pio[usrfd], mode));
	}
}

/*
 * close a stream opened by cmdopen()
 * -1 returned if cmdopen() had a problem
 * otherwise exit() status of command is returned
 */

static int
cmdclose(fp)
FILE	*fp;
{
	register int	i;
	register pid_t	p, pid;
	int		status;

	for (i = 0; i < MAXCMDS; i++)
		if (fp == cmdproc[i].fp) break;
	if (i >= MAXCMDS)
		return (-1);
	(void) fclose(fp);
	cmdproc[i].fp = 0;
	pid = cmdproc[i].pid;
	while ((p = wait(&status)) != pid && p != (pid_t)-1);
	if (p == pid) {
		status = (status >> 8) & CMDERR;
		if (status == CMDERR)
			status = -1;
	}
	else
		status = -1;
	return (status);
}

/*
 * return pointer to the full path name of the shell
 *
 * SHELL is read from the environment and must start with /
 *
 * if set-uid or set-gid then the executable and its containing
 * directory must not be writable by the real user
 *
 * /usr/bin/sh is returned by default
 */
extern char	*getenv();
extern char	*strrchr();


char *
getshell()
{
	register char	*s;
	register char	*sh;
	register uid_t	u;
	register int	j;

	if (((sh = getenv("SHELL")) != 0) && *sh == '/') {
		if (u = getuid()) {
			if ((u != geteuid() || getgid() != getegid()) &&
					!access(sh, 2))
				goto defshell;
			s = strrchr(sh, '/');
			*s = 0;
			j = access(sh, 2);
			*s = '/';
			if (!j) goto defshell;
		}
		return (sh);
	}
defshell:
	return ("/usr/bin/sh");
}

/*
 * the following functions implement the added "-ls" option
 */

#include <utmp.h>
#include <sys/mkdev.h>

struct	utmp utmp;
#define	NMAX	(sizeof (utmp.ut_name))
#define	SCPYN(a, b)	(void) strncpy(a, b, NMAX)

#define	NUID	64
#define	NGID	64

static struct ncache {
	int	id;
	char	name[NMAX+1];
} nc[NUID], gc[NGID];

/*
 * This function assumes that the password file is hashed
 * (or some such) to allow fast access based on a name key.
 */
static char *
getname(uid_t uid)
{
	register struct passwd *pw;
	register int cp;

#if	(((NUID) & ((NUID) - 1)) != 0)
	cp = uid % (NUID);
#else
	cp = uid & ((NUID) - 1);
#endif
	if (uid >= 0 && nc[cp].id == uid && nc[cp].name[0])
		return (nc[cp].name);
	pw = getpwuid(uid);
	if (!pw)
		return (0);
	nc[cp].id = uid;
	SCPYN(nc[cp].name, pw->pw_name);
	return (nc[cp].name);
}

/*
 * This function assumes that the group file is hashed
 * (or some such) to allow fast access based on a name key.
 */
static char *
getgroup(gid_t gid)
{
	register struct group *gr;
	register int cp;

#if	(((NGID) & ((NGID) - 1)) != 0)
	cp = gid % (NGID);
#else
	cp = gid & ((NGID) - 1);
#endif
	if (gid >= 0 && gc[cp].id == gid && gc[cp].name[0])
		return (gc[cp].name);
	gr = getgrgid(gid);
	if (!gr)
		return (0);
	gc[cp].id = gid;
	SCPYN(gc[cp].name, gr->gr_name);
	return (gc[cp].name);
}

#define	permoffset(who)		((who) * 3)
#define	permission(who, type)	((type) >> permoffset(who))
#define	kbytes(bytes)		(((bytes) + 1023) / 1024)

static int
list(file, stp)
	char *file;
	register struct stat *stp;
{
	char pmode[32], uname[32], gname[32], fsize[32], ftime[32];
	static long special[] = { S_ISUID, 's', S_ISGID, 's', S_ISVTX, 't' };
	static time_t sixmonthsago = -1;
#ifdef	S_IFLNK
	char flink[MAXPATHLEN + 1];
#endif
	register int who;
	register char *cp;
	time_t now;

	if (file == NULL || stp == NULL)
		return (-1);

	(void) time(&now);
	if (sixmonthsago == -1)
		sixmonthsago = now - 6L*30L*24L*60L*60L;

	switch (stp->st_mode & S_IFMT) {
#ifdef	S_IFDIR
	case S_IFDIR:	/* directory */
		pmode[0] = 'd';
		break;
#endif
#ifdef	S_IFCHR
	case S_IFCHR:	/* character special */
		pmode[0] = 'c';
		break;
#endif
#ifdef	S_IFBLK
	case S_IFBLK:	/* block special */
		pmode[0] = 'b';
		break;
#endif
#ifdef	S_IFIFO
	case S_IFIFO:	/* fifo special */
		pmode[0] = 'p';
		break;
#endif
#ifdef	S_IFLNK
	case S_IFLNK:	/* symbolic link */
		pmode[0] = 'l';
		break;
#endif
#ifdef	S_IFSOCK
	case S_IFSOCK:	/* socket */
		pmode[0] = 's';
		break;
#endif
#ifdef	S_IFREG
	case S_IFREG:	/* regular */
#endif
	default:
		pmode[0] = '-';
		break;
	}

	for (who = 0; who < 3; who++) {
		if (stp->st_mode & permission(who, S_IREAD))
			pmode[permoffset(who) + 1] = 'r';
		else
			pmode[permoffset(who) + 1] = '-';

		if (stp->st_mode & permission(who, S_IWRITE))
			pmode[permoffset(who) + 2] = 'w';
		else
			pmode[permoffset(who) + 2] = '-';

		if (stp->st_mode & special[who * 2])
			pmode[permoffset(who) + 3] = special[who * 2 + 1];
		else if (stp->st_mode & permission(who, S_IEXEC))
			pmode[permoffset(who) + 3] = 'x';
		else
			pmode[permoffset(who) + 3] = '-';
	}
	pmode[permoffset(who) + 1] = '\0';

	cp = getname(stp->st_uid);
	if (cp != NULL)
		(void) sprintf(uname, "%-9.9s", cp);
	else
		(void) sprintf(uname, "%-9ld", stp->st_uid);

	cp = getgroup(stp->st_gid);
	if (cp != NULL)
		(void) sprintf(gname, "%-9.9s", cp);
	else
		(void) sprintf(gname, "%-9ld", stp->st_gid);

	if (pmode[0] == 'b' || pmode[0] == 'c')
		(void) sprintf(fsize, "%3ld,%4ld",
			major(stp->st_rdev), minor(stp->st_rdev));
	else {
		(void) sprintf(fsize, "%8ld", stp->st_size);
#ifdef	S_IFLNK
		if (pmode[0] == 'l') {
			/*
			 * Need to get the tail of the file name, since we have
			 * already chdir()ed into the directory of the file
			 */
			cp = strrchr(file, '/');
			if (cp == NULL)
				cp = file;
			else
				cp++;
			who = readlink(cp, flink, sizeof (flink) - 1);
			if (who >= 0)
				flink[who] = '\0';
			else
				flink[0] = '\0';
		}
#endif
	}

	cp = ctime(&stp->st_mtime);
	if (stp->st_mtime < sixmonthsago || stp->st_mtime > now)
		(void) sprintf(ftime, "%-7.7s %-4.4s", cp + 4, cp + 20);
	else
		(void) sprintf(ftime, "%-12.12s", cp + 4);

	(void) printf("%5lu %4ld %s %2ld %s%s%s %s %s%s%s\n",
		stp->st_ino,				/* inode #	*/
#ifdef	S_IFSOCK
		(long) kbytes(dbtob(stp->st_blocks)),	/* kbytes	*/
#else
		(long) kbytes(stp->st_size),		/* kbytes	*/
#endif
		pmode,					/* protection	*/
		stp->st_nlink,				/* # of links	*/
		uname,					/* owner	*/
		gname,					/* group	*/
		fsize,					/* # of bytes	*/
		ftime,					/* modify time	*/
		file,					/* name		*/
#ifdef	S_IFLNK
		(pmode[0] == 'l') ? " -> " : "",
		(pmode[0] == 'l') ? flink  : ""		/* symlink	*/
#else
		"",
		""
#endif
);

	return (0);
}

static char *
new_string(char *s)
{
	char *p = strdup(s);

	if (p)
		return (p);
	(void) fprintf(stderr, gettext("%s: out of memory\n"), cmdname);
	exit(1);
	/*NOTREACHED*/
}

/*
 * Read remote file system types from REMOTE_FS into the
 * remote_fstypes array.
 */
static void
init_remote_fs()
{
	FILE    *fp;
	char    line_buf[LINEBUF_SIZE];

	if ((fp = fopen(REMOTE_FS, "r")) == NULL) {
		(void) fprintf(stderr,
			gettext("%s: Warning: can't open %s, ignored\n"),
				REMOTE_FS, cmdname);
		/* Use default string name for NFS */
		remote_fstypes[fstype_index++] = "nfs";
		return;
	}

	while (fgets(line_buf, sizeof (line_buf), fp) != NULL) {
		char buf[LINEBUF_SIZE];

		(void) sscanf(line_buf, "%s", buf);
		remote_fstypes[fstype_index++] = new_string(buf);

		if (fstype_index == N_FSTYPES)
			break;
	}
	(void) fclose(fp);
}

#define	NPERM	30			/* Largest machine */

/*
 * The PERM struct is the machine that builds permissions.  The p_special
 * field contains what permissions need to be checked at run-time in
 * getmode().  This is one of 'X', 'u', 'g', or 'o'.  It contains '\0' to
 * indicate normal processing.
 */
typedef	struct	PERMST	{
	ushort	p_who;			/* Range of permission (e.g. ugo) */
	ushort	p_perm;			/* Bits to turn on, off, assign */
	u_char	p_op;			/* Operation: + - = */
	u_char	p_special;		/* Special handling? */
}	PERMST;

#ifndef	S_ISVTX
#define	S_ISVTX	0			/* Not .1 */
#endif

/* Mask values */
#define	P_A	(S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO) /* allbits */
#define	P_U	(S_ISUID|S_ISVTX|S_IRWXU)		/* user */
#define	P_G	(S_ISGID|S_ISVTX|S_IRWXG)		/* group */
#define	P_O	(S_ISVTX|S_IRWXO)			/* other */

static	int	iswho(int c);
static	int	isop(int c);
static	int	isperm(PERMST *pp, int c);

static	PERMST	machine[NPERM];		/* Permission construction machine */
static	PERMST	*endp;			/* Last used PERM structure */

static	uint	nowho;			/* No who for this mode (DOS kludge) */

/*
 * Read an ASCII string containing the symbolic/octal mode and
 * compile an automaton that recognizes it.  The return value
 * is NULL if everything is OK, otherwise it is -1.
 */
static int
readmode(ascmode)
const char *ascmode;
{
	register const char *amode = ascmode;
	register PERMST *pp;
	int seen_X;

	nowho = 0;
	seen_X = 0;
	pp = &machine[0];
	if (*amode >= '0' && *amode <= '7') {
		register int mode;

		mode = 0;
		while (*amode >= '0' && *amode <= '7')
			mode = (mode<<3) + *amode++ - '0';
		if (*amode != '\0')
			return (-1);
#if	S_ISUID != 04000 || S_ISGID != 02000 || \
	S_IRUSR != 0400 || S_IWUSR != 0200 || S_IXUSR != 0100 || \
	S_IRGRP != 0040 || S_IWGRP != 0020 || S_IXGRP != 0010 || \
	S_IROTH != 0004 || S_IWOTH != 0002 || S_IXOTH != 0001
		/*
		 * There is no requirement of the octal mode bits being
		 * the same as the S_ macros.
		 */
	{
		mode_t mapping[] = {
			S_IXOTH, S_IWOTH, S_IROTH,
			S_IXGRP, S_IWGRP, S_IRGRP,
			S_IXUSR, S_IWUSR, S_IRUSR,
			S_ISGID, S_ISUID,
			0
		};
		int i, newmode = 0;

		for (i = 0; mapping[i] != 0; i++)
			if (mode & (1<<i))
				newmode |= mapping[i];
		mode = newmode;
	}
#endif
		pp->p_who = P_A;
		pp->p_perm = mode;
		pp->p_op = '=';
	} else	for (;;) {
		register int t;
		register int who = 0;

		while ((t = iswho(*amode)) != 0) {
			++amode;
			who |= t;
		}
		if (who == 0) {
			mode_t currmask;
			(void) umask(currmask = umask((mode_t)0));

			/*
			 * If no who specified, must use contents of
			 * umask to determine which bits to flip.  This
			 * is POSIX/V7/BSD behaviour, but not SVID.
			 */
			who = (~currmask)&P_A;
			++nowho;
		} else
			nowho = 0;
	samewho:
		if (!isop(pp->p_op = *amode++))
			return (-1);
		pp->p_perm = 0;
		pp->p_special = 0;
		while ((t = isperm(pp, *amode)) != 0) {
			if (pp->p_special == 'X') {
				seen_X = 1;

				if (pp->p_perm != 0) {
					ushort op;

					/*
					 * Remember the 'who' for the previous
					 * transformation.
					 */
					pp->p_who = who;
					pp->p_special = 0;

					op = pp->p_op;

					/* Keep 'X' separate */
					++pp;
					pp->p_special = 'X';
					pp->p_op = op;
				}
			} else if (seen_X) {
				ushort op;

				/* Remember the 'who' for the X */
				pp->p_who = who;

				op = pp->p_op;

				/* Keep 'X' separate */
				++pp;
				pp->p_perm = 0;
				pp->p_special = 0;
				pp->p_op = op;
			}
			++amode;
			pp->p_perm |= t;
		}

		/*
		 * These returned 0, but were actually parsed, so
		 * don't look at them again.
		 */
		switch (pp->p_special) {
		case 'u':
		case 'g':
		case 'o':
			++amode;
			break;
		}
		pp->p_who = who;
		switch (*amode) {
		case '\0':
			break;

		case ',':
			++amode;
			++pp;
			continue;

		default:
			++pp;
			goto samewho;
		}
		break;
	}
	endp = pp;
	return (NULL);
}

/*
 * Given a character from the mode, return the associated
 * value as who (user designation) mask or 0 if this isn't valid.
 */
static int
iswho(c)
register int c;
{
	switch (c) {
	case 'a':
		return (P_A);

	case 'u':
		return (P_U);

	case 'g':
		return (P_G);

	case 'o':
		return (P_O);

	default:
		return (0);
	}
	/* NOTREACHED */
}

/*
 * Return non-zero if this is a valid op code
 * in a symbolic mode.
 */
static int
isop(c)
register int c;
{
	switch (c) {
	case '+':
	case '-':
	case '=':
		return (1);

	default:
		return (0);
	}
	/* NOTREACHED */
}

/*
 * Return the permission bits implied by this character or 0
 * if it isn't valid.  Also returns 0 when the pseudo-permissions 'u', 'g', or
 * 'o' are used, and sets pp->p_special to the one used.
 */
static int
isperm(pp, c)
PERMST *pp;
register int c;
{
	switch (c) {
	case 'u':
	case 'g':
	case 'o':
		pp->p_special = c;
		return (0);

	case 'r':
		return (S_IRUSR|S_IRGRP|S_IROTH);

	case 'w':
		return (S_IWUSR|S_IWGRP|S_IWOTH);

	case 'x':
		return (S_IXUSR|S_IXGRP|S_IXOTH);

#if S_ISVTX != 0
	case 't':
		return (S_ISVTX);
#endif

	case 'X':
		pp->p_special = 'X';
		return (S_IXUSR|S_IXGRP|S_IXOTH);

#if S_ISVTX != 0
	case 'a':
		return (S_ISVTX);
#endif

	case 'h':
		return (S_ISUID);

	/*
	 * This change makes:
	 *	chmod +s file
	 * set the system bit on dos but means that
	 *	chmod u+s file
	 *	chmod g+s file
	 *	chmod a+s file
	 * are all like UNIX.
	 */
	case 's':
		return (nowho ? S_ISGID : S_ISGID|S_ISUID);

	default:
		return (0);
	}
	/* NOTREACHED */
}

/*
 * Execute the automaton that is created by readmode()
 * to generate the final mode that will be used.  This
 * code is passed a starting mode that is usually the original
 * mode of the file being changed (or 0).  Note that this mode must contain
 * the file-type bits as well, so that S_ISDIR will succeed on directories.
 */
static mode_t
getmode(mode_t startmode)
{
	register PERMST *pp;
	mode_t temp;
	mode_t perm;
	mode_t orig = startmode;

	for (pp = &machine[0]; pp <= endp; ++pp) {
		perm = (mode_t) 0;
		/*
		 * For the special modes 'u', 'g' and 'o', the named portion
		 * of the mode refers to after the previous clause has been
		 * processed, while the 'X' mode refers to the contents of the
		 * mode before any clauses have been processed.
		 *
		 * References: P1003.2/D11.2, Section 4.7.7,
		 *  lines 2568-2570, 2578-2583
		 */
		switch (pp->p_special) {
		case 'u':
			temp = startmode & S_IRWXU;
			if (temp & (S_IRUSR|S_IRGRP|S_IROTH))
				perm |= ((S_IRUSR|S_IRGRP|S_IROTH) &
				    pp->p_who);
			if (temp & (S_IWUSR|S_IWGRP|S_IWOTH))
				perm |= ((S_IWUSR|S_IWGRP|S_IWOTH) & pp->p_who);
			if (temp & (S_IXUSR|S_IXGRP|S_IXOTH))
				perm |= ((S_IXUSR|S_IXGRP|S_IXOTH) & pp->p_who);
			break;

		case 'g':
			temp = startmode & S_IRWXG;
			if (temp & (S_IRUSR|S_IRGRP|S_IROTH))
				perm |= ((S_IRUSR|S_IRGRP|S_IROTH) & pp->p_who);
			if (temp & (S_IWUSR|S_IWGRP|S_IWOTH))
				perm |= ((S_IWUSR|S_IWGRP|S_IWOTH) & pp->p_who);
			if (temp & (S_IXUSR|S_IXGRP|S_IXOTH))
				perm |= ((S_IXUSR|S_IXGRP|S_IXOTH) & pp->p_who);
			break;

		case 'o':
			temp = startmode & S_IRWXO;
			if (temp & (S_IRUSR|S_IRGRP|S_IROTH))
				perm |= ((S_IRUSR|S_IRGRP|S_IROTH) & pp->p_who);
			if (temp & (S_IWUSR|S_IWGRP|S_IWOTH))
				perm |= ((S_IWUSR|S_IWGRP|S_IWOTH) & pp->p_who);
			if (temp & (S_IXUSR|S_IXGRP|S_IXOTH))
				perm |= ((S_IXUSR|S_IXGRP|S_IXOTH) & pp->p_who);
			break;

		case 'X':
			perm = pp->p_perm;
			break;

		default:
			perm = pp->p_perm;
			break;
		}
		switch (pp->p_op) {
		case '-':
			startmode &= ~(perm & pp->p_who);
			break;

		case '=':
			startmode &= ~pp->p_who;
		case '+':
			startmode |= (perm & pp->p_who);
			break;
		}
	}
	return (startmode);
}
