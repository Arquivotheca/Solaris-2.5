#ident	"@(#)args.c 1.28 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <pwd.h>
#include <sys/stat.h>
#include "cmds.h"

/*
 * XXX  This value is ASCII (but not language) dependent.  In
 * ASCII, it is the DEL character (unlikely to appear in paths).
 * If you are compiling on an EBCDC-based machine, re-define
 * this (0x7f is '"') to be something like 0x7 (DEL).  It's
 * either this hack or re-write the algorithm...
 */
#define	DELIMCHAR	((char)0x7f)

static int pipeout;
static int lookup_type;

static struct rawargs {
	int exflag;
	char *argstr;
} *arglist;
static int nargs, arglistsize;

#ifdef __STDC__
static int gatherargs(struct cmdinfo *, char *, char *);
static FILE *redirfile(char *, char *, char *);
static int nextword(char **, char *);
static void addarg(char *, int);
static void reinitargs(void);
static int buildarg(char *, char *, struct arglist *, int, char *, time_t, int);
static int expand(char *, int, struct arglist *, char *, time_t, int);
static int gmatch(char *, char *);
static int addg(char *, char *, char *, struct arglist *, char *, time_t, int);
static void mkentry(char *, u_long, struct dir_block *, struct dir_entry *,
	struct arglist *, int);
#else
static int gatherargs();
static FILE *redirfile();
static int nextword();
static void addarg();
static void reinitargs();
static int buildarg();
static int expand();
static int gmatch();
static int addg();
static void mkentry();
#endif

getcmd(in, cmd, arg, curdir, localdir, ap, host, timestamp)
	char *in;
	int *cmd;
	char *arg;
	char *curdir, *localdir;
	struct arglist *ap;
	char *host;
	time_t	timestamp;
{
	char buffer[MAXPATHLEN];
	int argcnt, anyargs;
	char *p;
	int thiscmd;
	int followlink;
	struct cmdinfo *cmdinfo;
	int argidx;

	lookup_type = LOOKUP_DEFAULT;

	outfp = stdout;
	pipeout = 0;
	argcnt = 0;
	followlink = 0;
	if (strcmp(in, "") == 0) {
		*cmd = CMD_NULL;
		return (0);
	}

	reinitargs();
	p = in;
	*cmd = CMD_INVAL;
	(void) nextword(&p, buffer);
	cmdinfo = parsecmd(buffer);

	if (cmdinfo->id == CMD_NULL ||
			cmdinfo->id == CMD_AMBIGUOUS ||
			cmdinfo->id == CMD_INVAL) {
		*cmd = cmdinfo->id;
		return (0);
	}

	if (gatherargs(cmdinfo, p, localdir)) {
		*cmd = CMD_NULL;
		return (0);
	}

	if (nargs < cmdinfo->minargs) {
		(void) fprintf(stderr,
		    gettext("%s: insufficient arguments\n"), cmdinfo->name);
		*cmd = CMD_NULL;
		return (0);
	} else if (cmdinfo->maxargs != -1 && nargs > cmdinfo->maxargs) {
		(void) fprintf(stderr,
			gettext("%s: too many arguments\n"), cmdinfo->name);
		*cmd = CMD_NULL;
		return (0);
	}
	argidx = 0;
	thiscmd = cmdinfo->id;
	switch (thiscmd) {
	case CMD_NULL:
		*cmd = CMD_NULL;
		return (0);
	case CMD_AMBIGUOUS:
		*cmd = CMD_AMBIGUOUS;
		return (0);
	case CMD_ADD:
		*cmd = CMD_ADD;
		break;
	case CMD_ADDNAME:
		/*
		 * here we look for one arg which we match
		 * in the database, then another arg which
		 * is an arbitrary string.
		 */
		argcnt = buildarg(curdir, arglist[argidx].argstr, ap,
				arglist[argidx].exflag, host, timestamp, 0);
		if (argcnt != 1) {
			(void) fprintf(stderr, gettext(
				"addname: ambiguous file specification\n"));
			*cmd = CMD_NULL;
			return (0);
		}
		argidx++;
		(void) strcpy(arg, arglist[argidx].argstr);
		*cmd = CMD_ADDNAME;
		return (1);
	case CMD_CD:
		*cmd = CMD_CD;
		followlink = 1;
		break;
	case CMD_DELETE:
		*cmd = CMD_DELETE;
		break;
	case CMD_EXTRACT:
		*cmd = CMD_EXTRACT;
		return (0);
	case CMD_FASTRECOVER:
		*cmd = CMD_FASTRECOVER;
		(void) strcpy(arg, arglist[argidx].argstr);
		return (1);
	case CMD_FASTFIND:
		*cmd = CMD_FASTFIND;
		(void) strcpy(arg, arglist[argidx].argstr);
		return (1);
	case CMD_HELP:
		*cmd = CMD_HELP;
		if (nargs)
			(void) strcpy(arg, arglist[argidx].argstr);
		return (nargs);
	case CMD_LS:
		*cmd = CMD_LS;
		followlink = 1;
		break;
	case CMD_LL:
		*cmd = CMD_LL;
		break;
	case CMD_LPWD:
		*cmd = CMD_LPWD;
		return (0);
	case CMD_LCD:
		*cmd = CMD_LCD;
		if (nargs) {
			(void) strcpy(arg, arglist[argidx].argstr);
			return (1);
		} else {
			return (0);
		}
		/*NOTREACHED*/
		break;
	case CMD_LIST:
		*cmd = CMD_LIST;
		return (0);
	case CMD_NOTIFY:
		*cmd = CMD_NOTIFY;
		(void) strcpy(arg, arglist[argidx].argstr);
		return (1);
	case CMD_PWD:
		*cmd = CMD_PWD;
		return (0);
	case CMD_QUIT:
		*cmd = CMD_QUIT;
		return (0);
	case CMD_RRESTORE:
		*cmd = CMD_RRESTORE;
		followlink = 1;
		break;
	case CMD_SETDATE:
		*cmd = CMD_SETDATE;
		if (nargs) {
			*arg = '\0';
			do {
				(void) strcat(arg, arglist[argidx].argstr);
				(void) strcat(arg, " ");
				argidx++;
			} while (argidx < nargs);
			return (1);
		} else {
			return (0);
		}
		/*NOTREACHED*/
		break;
	case CMD_SETHOST:
		*cmd = CMD_SETHOST;
		(void) strcpy(arg, arglist[argidx].argstr);
		return (1);
	case CMD_SHOWSET:
		*cmd = CMD_SHOWSET;
		return (0);
	case CMD_SHOWDUMP:
		*cmd = CMD_SHOWDUMP;
		break;
	case CMD_SETMODE:
		*cmd = CMD_SETMODE;
		if (nargs) {
			(void) strcpy(arg, arglist[argidx].argstr);
			return (1);
		}
		return (0);
	case CMD_VERSIONS:

		/*
		 * we want the versions command to always work,
		 * regardless of the date or lookup mode setting.
		 * Thus, we always do a translucent lookup
		 * starting at the present time.
		 */
		*cmd = CMD_VERSIONS;
		lookup_type = LOOKUP_TRANSLUCENT;
		timestamp = time((time_t *)0);
		break;
	case CMD_XRESTORE:
		*cmd = CMD_XRESTORE;
		followlink = 1;
		break;
	}

	if (*cmd == CMD_INVAL) {
		return (0);
	}

	anyargs = 0;
	while (argidx < nargs) {
		anyargs++;
		argcnt += buildarg(curdir, arglist[argidx].argstr, ap,
				arglist[argidx].exflag, host,
				timestamp, followlink);
		argidx++;
	}
	switch (*cmd) {
	case CMD_ADD:
	case CMD_DELETE:
	case CMD_LS:
	case CMD_LL:
	case CMD_VERSIONS:
	case CMD_SHOWDUMP:
	case CMD_XRESTORE:
	case CMD_RRESTORE:
		if (!anyargs)
			argcnt += buildarg(curdir, "", ap, 0, host,
					timestamp, followlink);
		break;
	case CMD_CD:
		if (anyargs && argcnt == 0)
			/*
			 * distinguish between an invalid dir specification
			 * and a cd cmd without arguments...
			 */
			argcnt = -1;
		break;
	}
	reinitargs();
	return (argcnt);
}

static int
gatherargs(cmd, p, localdir)
	struct cmdinfo *cmd;
	char *p;
	char *localdir;
{
	char *current;
	int rc;
	char *mode;
	char buffer[MAXPATHLEN];

	current = p;
	rc = nextword(&current, buffer);
	while (buffer[0]) {
		if (buffer[0] == '>') {
			if ((cmd->redir & REDIR_OUTPUT) == 0) {
				(void) fprintf(stderr, gettext(
				    "%s: output re-direction not allowed\n"),
					cmd->name);
				return (1);
			} else {
				if (buffer[1] == '>')
					mode = "a";
				else
					mode = "w";
				(void) nextword(&current, buffer);
				if ((outfp = redirfile(localdir,
						buffer, mode)) == NULL) {
					outfp = stdout;
					return (1);
				}
			}
		} else if (buffer[0] == '<') {
			FILE *fp;

			if ((cmd->redir & REDIR_INPUT) == 0) {
				(void) fprintf(stderr, gettext(
					"%s: input re-direction not allowed\n"),
					cmd->name);
				return (1);
			} else {
				char arg[MAXPATHLEN], *p;

				mode = "r";
				(void) nextword(&current, buffer);
				fp = redirfile(localdir, buffer, mode);
				if (fp == NULL)
					return (1);
				while (fgets(buffer, MAXPATHLEN, fp)) {
					p = buffer;
					(void) nextword(&p, arg);
					while (arg[0]) {
						addarg(arg, 0);
						(void) nextword(&p, arg);
					}
				}
				(void) fclose(fp);
			}
		} else {
			addarg(buffer, rc);
		}
		rc = nextword(&current, buffer);
	}
	return (0);
}

static FILE *
redirfile(localdir, name, mode)
	char *localdir;
	char *name;
	char *mode;
{
	FILE *fp;
	char fname[MAXPATHLEN];

	if (name[0] == '\0') {
		(void) fprintf(stderr,
			gettext("Missing file name for redirect\n"));
		return (NULL);
	}

	if (name[0] == '/') {
		(void) strcpy(fname, name);
	} else if (name[0] == '~') {
		(void) strcpy(fname, name);
		if (mkuserdir(fname))
			return (NULL);
	} else {
		(void) sprintf(fname, "%s/%s", localdir, name);
	}

	if ((fp = fopen(fname, mode)) == NULL) {
		(void) fprintf(stderr, gettext(
			"Can't open redirect file `%s'\n"), fname);
	}
	return (fp);
}

static int
nextword(s, buf)
	char **s;
	char *buf;
{
	register char *p, *r;
	int quoted, expand, meta, gotword;

	p = *s;
	r = buf;
	quoted = meta = gotword = 0;
	expand = 1;

	while (*p && !gotword) {
		switch (*p) {
		case '"':
			if (quoted) {
				gotword++;
			} else {
				quoted = 1;
				/* expand = 0; */
			}
			break;
		case '*':
		case '?':
		case '[':
			meta++;
			*r++ = *p;
			break;
		case '>':
			if (r != buf) {
				gotword++;
				continue;
			}
			*r++ = *p;
			if (*(p+1) == '>')
				*r++ = *++p;
			gotword++;
			break;
		case '<':
			if (r != buf) {
				gotword++;
				continue;
			}
			*r++ = *p;
			gotword++;
			break;
		case ' ':
			if (r != buf) {
				if (!quoted)
					gotword++;
				else
					*r++ = *p;
			}
			break;
		case '\0':
		case '\n':
			break;
		default:
			*r++ = *p;
		}
		if (*p)
			p++;
	}
	*r = '\0';
	*s = p;
	return (meta && expand);
}

/*
 * add `s' to argument vector and increment argument count.
 */
static void
addarg(s, flag)
	char *s;
	int flag;
{
#define	ARGSIZE	100

	if (nargs >= arglistsize) {
		if (arglistsize == 0)
			arglist = (struct rawargs *)malloc(
				ARGSIZE*sizeof (struct rawargs));
		else
			arglist = (struct rawargs *)realloc((char *)arglist,
				(unsigned)(arglistsize+ARGSIZE)*
				sizeof (struct rawargs));
		if (arglist == (struct rawargs *)0) {
			panic(gettext("out of memory"));
			/* NOTREACHED */
		}
		arglistsize += ARGSIZE;
	}
	arglist[nargs].argstr = (char *)malloc((unsigned)strlen(s)+1);
	if (arglist[nargs].argstr == NULL) {
		panic(gettext("out of memory"));
		/* NOTREACHED */
	}
	(void) strcpy(arglist[nargs].argstr, s);
	arglist[nargs].exflag = flag;
	nargs++;
}

static void
#ifdef __STDC__
reinitargs(void)
#else
reinitargs()
#endif
{
	register int i;
	char *t;

	for (i = 0; i < nargs; i++) {
		if (arglist[i].argstr) {
			t = arglist[i].argstr;
			arglist[i].argstr = NULL;
			free(t);
		}
	}
	if (arglist) {
		t = (char *)arglist;
		arglist = (struct rawargs *)0;
		free(t);
	}
	nargs = arglistsize = 0;
}

/*
 * meta-character support routines stolen from "restore/interactive.c"
 */
static int
buildarg(curdir, arg, ap, doexpand, host, timestamp, followlink)
	char *curdir;
	char *arg;
	struct arglist *ap;
	int doexpand;
	char *host;
	time_t timestamp;
	int followlink;
{
	char buf[MAXPATHLEN];
	char rbuf[MAXPATHLEN];
	int size;
	struct dir_block *bp;
	struct dir_entry *ep;
	u_long dirblk;
	int dirlen, arglen;

	dirlen = strlen(curdir);
	arglen = strlen(arg);
	if (arglen > MAXPATHLEN) {
		(void) fprintf(stderr, gettext("name too long\n"));
		return (0);
	} else if (*arg == '/') {
		(void) strcpy(buf, arg);
	} else if (*arg == '~') {
		(void) strcpy(buf, arg);
		if (mkuserdir(buf))
			return (0);
	} else if (*arg == 0) {
		(void) strcpy(buf, curdir);
	} else {
		if ((dirlen + arglen + 2) > MAXPATHLEN) {
			(void) fprintf(stderr, gettext("name too long\n"));
			return (0);
		}
		(void) sprintf(buf, "%s/%s",
				strcmp(curdir, "/") == 0 ? "" : curdir, arg);
	}
	size = 0;
	if (doexpand) {
		size = expand(buf, 0, ap, host, timestamp, followlink);
	} else {
		/*
		 * resolve the path.  Every component except the last
		 * must be executable, and the last must be readable.
		 */
		if (pathcheck(buf, rbuf,  host, timestamp, 0, followlink,
					&bp, &dirblk) == NULL_DIRENTRY) {
			(void) printf(gettext(
				"%s: No such file or directory\n"), buf);
			return (0);
		}
		(void) strcpy(buf, rbuf);
	}
	if (size == 0) {
		ep = dir_path_lookup(&dirblk, &bp, buf);
		if (ep == NULL_DIRENTRY) {
			if (strpbrk(buf, "*?[") && doexpand)
				(void) printf(gettext("No match.\n"));
			else
				(void) printf(gettext("%s not found\n"), buf);
		} else {
			size = 1;
			mkentry(buf, dirblk, bp, ep, ap, 0);
		}
	}
	return (size);
}

/* static */
mkuserdir(buf)
	char *buf;
{
	struct passwd *pw;
	register char *name, *p;
	char newpath[MAXPATHLEN];
	int dirlen;

	if (*buf != '~')
		return (1);

	if (p = strchr(buf, '/')) {
		*p++ = '\0';
		(void) strcpy(newpath, p);
		p = newpath;
	}
	name = buf+1;
	if (*name) {
		if ((pw = getpwnam(name)) == NULL) {
			(void) fprintf(stderr, gettext("unknown user: %s\n"),
				name);
			return (1);
		}
	} else {
		if ((pw = getpwuid(getuid())) == NULL) {
			(void) fprintf(stderr,
				gettext("Cannot get your home dir!\n"));
			return (1);
		}
	}
	if ((dirlen = strlen(pw->pw_dir)) > MAXPATHLEN) {
		(void) fprintf(stderr, gettext("name too long\n"));
		return (1);
	}
	(void) strcpy(buf, pw->pw_dir);
	if (p && *p) {
		if ((int)(dirlen + strlen(p) + 2) > MAXPATHLEN) {
			(void) fprintf(stderr, gettext("name too long\n"));
			return (1);
		}
		(void) strcat(buf, "/");
		(void) strcat(buf, p);
	}
	return (0);
}

/* static */
struct dir_entry *
pathcheck(path, rpath, host, timestamp, nlink, followlink, rbp, rblknum)
	char *path;
	char *rpath;
	char *host;
	time_t timestamp;
	int nlink;
	int followlink;
	struct dir_block **rbp;
	u_long *rblknum;
{
	char *morepath = path;
	char comp[MAXNAMLEN];
	u_long curblk;
	struct dir_block *bp;
	struct dir_entry *ep;
	struct dnode dn;
	u_long dumpid;
	char *p;
	extern char *dbserv;
	char tmp_path[MAXPATHLEN];
	int perm;
	int rpathlen, complen;

	if (strcmp(path, "/") == 0)
		morepath = ".";
	curblk = DIR_ROOTBLK;
	ep = NULL_DIRENTRY;
	rpathlen = 0;
	*rpath = 0;
	while (getpathcomponent(&morepath, comp) == 1) {
		complen = strlen(comp);
		if (curblk == NONEXISTENT_BLOCK)
			return (NULL_DIRENTRY);
		if ((ep = dir_name_lookup(&curblk,
				&bp, comp)) == NULL_DIRENTRY)
			return (NULL_DIRENTRY);

		if (*morepath)
			perm = VEXEC;
		else
			perm = VREAD;
		(void) strcpy(tmp_path, rpath);
		(void) strcat(tmp_path, "/");
		(void) strcat(tmp_path, comp);
		if ((dumpid = getdnode(host, &dn, ep,
				perm, timestamp, lookup_type, tmp_path)) == 0)
			return (NULL_DIRENTRY);
		if (S_ISLNK(dn.dn_mode)) {
			if (*morepath == 0 && followlink == 0) {
				/*
				 * commands like `ll', `versions' and
				 * `add' don't follow a symlink that is
				 * the last component of a path specification
				 */
				if ((rpathlen + complen + 2) > MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					return (NULL_DIRENTRY);
				}
				(void) strcat(rpath, "/");
				(void) strcat(rpath, comp);
				rpathlen += complen+1;
				break;
			}
			if ((p = db_readlink(dbserv, host, dumpid,
					dn.dn_symlink)) == NULL) {
				if (*morepath)
					return (NULL_DIRENTRY);
				if ((rpathlen + complen + 2) > MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					return (NULL_DIRENTRY);
				}
				(void) strcat(rpath, "/");
				(void) strcat(rpath, comp);
				rpathlen += complen+1;
				continue;
			}
			if (++nlink > MAXSYMLINKS) {
				(void) fprintf(stderr,
					gettext("Too many symlinks\n"));
				return (NULL_DIRENTRY);
			}
			if (*p == '/') {
				(void) strcpy(tmp_path, p);
			} else {
				if ((int)(rpathlen + strlen(p) + 2) >
				    MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					return (NULL_DIRENTRY);
				}
				(void) strcpy(tmp_path, rpath);
				(void) strcat(tmp_path, "/");
				(void) strcat(tmp_path, p);
			}
			if ((ep = pathcheck(tmp_path, rpath, host,
					timestamp, nlink, 1,
					&bp, &curblk)) == NULL_DIRENTRY)
				return (NULL_DIRENTRY);
			rpathlen = strlen(rpath);
			if ((dumpid = getdnode(host, &dn, ep,
					perm, timestamp,
					lookup_type, rpath)) == 0)
				return (NULL_DIRENTRY);
			if (S_ISDIR(dn.dn_mode))
				curblk = ep->de_directory;
			else if (*morepath)
				return (NULL_DIRENTRY);
		} else if (!S_ISDIR(dn.dn_mode)) {
			if (*morepath)
				return (NULL_DIRENTRY);
			(void) strcat(rpath, "/");
			(void) strcat(rpath, comp);
			rpathlen += complen+1;
		} else {
			(void) strcat(rpath, "/");
			(void) strcat(rpath, comp);
			rpathlen += complen+1;
			curblk = ep->de_directory;
		}
	}
	smashpath(rpath);
	*rbp = bp;
	*rblknum = curblk;
	return (ep);
}

static int
expand(arg, rflg, ap, host, timestamp, followlink)
	char *arg;
	int rflg;
	struct arglist *ap;
	char *host;
	time_t timestamp;
	int followlink;
{
	int count, size;
	char	dir = 0;
	char	*rescan = 0;
	register char *s, *cs;
	int	sindex, rindex, lindex;
	register char slash;
	register char *rs;
	register char c;
	register int i;
	int freecnt;
	char hold[MAXPATHLEN];
	char rpath[MAXPATHLEN];

	struct dir_block *bp;
	struct dir_entry *ep;
	u_long startblock, nextblock, dirblk;

	/*
	 * expand any symlinks which come before meta chars
	 */
	s = strpbrk(arg, "*?[");
	if (s) {
		while (*--s != '/')
			;
		*s++ = '\0';
		(void) strcpy(hold, s);
		s = hold;
	}
	if (*arg) {
		if (pathcheck(arg, rpath, host, timestamp, 0, 1,
					&bp, &dirblk) == NULL_DIRENTRY)
			return (0);
		(void) strcpy(arg, rpath);
	}
	if (s && *s) {
		(void) strcat(arg, "/");
		(void) strcat(arg, s);
	}

	/*
	 * check for meta chars
	 */
	s = cs = arg;
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

	if ((ep = dir_path_lookup(&dirblk, &bp, s))) {
		struct dnode dn;

		if (ep->de_directory != NONEXISTENT_BLOCK) {
			if (getdnode(host, &dn,
					ep, VEXEC, timestamp, lookup_type, s)) {
				if (S_ISDIR(dn.dn_mode) &&
					permchk(&dn, VREAD, host) == 0) {
					dir++;
				}
			}
		}
	}
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
		startblock = nextblock = ep->de_directory;
		do {
			if ((bp = dir_getblock(nextblock)) == NULL_DIRBLK) {
				return (0);
			}
			/*LINTED [alignment ok]*/
			ep = (struct dir_entry *)bp->db_data;
			/*LINTED [alignment ok]*/
			while (ep != DE_END(bp)) {
				if (*ep->de_name != '.' || *cs == '.') {
					if (gmatch(ep->de_name, cs)) {
						/*
						 * add to arglist
						 */
						count += addg(ep->de_name, s,
							rescan, ap, host,
							timestamp, followlink);
					}
				}
				ep = DE_NEXT(ep);
			}
			nextblock = bp->db_next;
		} while (startblock != nextblock);
		if (rescan) {
			rindex = sindex;
			lindex = ap->last - ap->head;
			if (count) {
				count = 0;
				while (rindex < lindex) {
					size = expand(ap->head[rindex].name,
						1, ap, host, timestamp,
								followlink);
					if (size < 0)
						return (size);
					count += size;
					rindex++;
				}

				/*
				 * free the memory of the
				 * guys we're copying over.
				 */
				freecnt = sindex + (lindex - sindex);
				for (i = sindex; i < freecnt; i++) {
					if (ap->head[i].name) {
						free(ap->head[i].name);
						ap->head[i].name = NULL;
					}
					if (ap->head[i].dbp) {
						free((char *)ap->head[i].dbp);
						ap->head[i].dbp = NULL;
					}
				}

				bcopy((char *)&ap->head[lindex],
					(char *)&ap->head[sindex],
					(ap->last - &ap->head[rindex]) *
					sizeof (*ap->head));
				ap->last -= lindex - sindex;
				*rescan = '/';
			}
		}
	}
	s = arg;
	/*LINTED*/
	while (c = *s)
		*s++ = (c != DELIMCHAR ? c : '/');
	return (count);
}

static int
gmatch(s, p)
	register char *s;
	register char *p;
{
	register int scc;
	char c;
	char ok;
	int lc;

	scc = *s++;
	switch (c = *p++) {
	case '[':
		ok = 0;
		lc = -1;
		while (c = *p++) {
			if (c == ']') {
				return (ok ? gmatch(s, p) : 0);
			} else if (c == '-') {
				if (lc <= scc && scc <= (*p++))
					ok++;
			} else {
				lc = c;
				if (scc == lc)
					ok++;
			}
		}
		return (0);

	case '\\':
		c = *p++;
		/*FALLTHROUGH*/

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

static int
addg(name, as1, as3, ap, host, timestamp, followlink)
	char *name, *as1, *as3;
	struct arglist *ap;
	char *host;
	time_t timestamp;
	int followlink;
{
	register char *s1, *s2;
	register int c;
	char buf[MAXPATHLEN];
	char buf1[MAXPATHLEN];
	u_long blknum;
	struct dir_block *bp;
	struct dir_entry *ep;
	int namelen;

	namelen = strlen(as1);
	if (namelen > MAXPATHLEN) {
		(void) fprintf(stderr, gettext("name too long\n"));
		return (0);
	}
	s2 = buf;
	s1 = as1;
	/*LINTED*/
	while (c = *s1++) {
		if (c == DELIMCHAR) {
			*s2++ = '/';
			break;
		}
		*s2++ = (char)c;
	}
	namelen = (int) (s2 - buf);
	if ((int)(namelen + strlen(name)) > MAXPATHLEN) {
		(void) fprintf(stderr, gettext("name too long\n"));
		return (0);
	}
	s1 = name;
	if (*(s2-1) != '/') {
		*s2++ = '/';
	}
	while (*s2 = *s1++)
		s2++;
	*s2 = 0;
	namelen = strlen(buf);
	if (as3 && (int)(namelen + strlen(as3)) > MAXPATHLEN) {
		(void) fprintf(stderr, gettext("name too long\n"));
		return (0);
	}
	if ((ep = pathcheck(buf, buf1, host, timestamp, 0,
			followlink, &bp, &blknum)) != NULL_DIRENTRY) {
		/*LINTED*/
		if (s1 = as3) {
			(void) strcpy(buf, buf1);
			s2 = &buf[strlen(buf)];
			*s2++ = '/';
			while (*s2++ = *++s1)
				;
		}
		mkentry(buf, blknum, bp, ep, ap, 1);
		return (1);
	}
	return (0);
}

static void
mkentry(name, blk, bp, ep, ap, expflag)
	char *name;
	u_long blk;
	struct dir_block *bp;
	struct dir_entry *ep;
	register struct arglist *ap;
	int expflag;
{
	register struct afile *fp;

	if (ap->base == NULL) {
		ap->nent = 20;
		ap->base = (struct afile *)calloc((unsigned)ap->nent,
				sizeof (struct afile));
		if (ap->base == NULL) {
			panic("mkentry calloc");
		}
	}
	if (ap->head == NULL)
		ap->head = ap->last = ap->base;
	fp = ap->last;
	fp->dir_blknum = blk;
	fp->expanded = expflag;
	if ((int)(strlen(name)+1) > MAXPATHLEN)
		panic(gettext("name too long"));
	if ((fp->name = (char *)malloc(MAXPATHLEN)) == NULL)
		panic("mkentry malloc");

	/*
	 * keep a local copy of the dir block since we can't count
	 * on it still being cached when we get around to processing
	 * this argument.
	 */
	if ((fp->dbp = (struct dir_block *)malloc(DIR_BLKSIZE)) == NULL)
		panic("mkentry malloc");
	bcopy((char *)bp, (char *)fp->dbp, DIR_BLKSIZE);
	fp->dep = (struct dir_entry *)((int)fp->dbp + ((int)ep - (int)bp));
	(void) strcpy(fp->name, name);
	fp++;
	if (fp == (ap->head + ap->nent)) {
		struct afile *newbase;

		newbase = (struct afile *)realloc((char *)ap->base,
			(unsigned)(2 * ap->nent * sizeof (struct afile)));
		if (newbase == NULL) {
			panic("mkentry realloc");
		}
		ap->head = ap->base = newbase;
		fp = ap->head + ap->nent;
		ap->nent *= 2;
	}
	ap->last = fp;
}

void
freeargs(ap)
	struct arglist *ap;
{
	register struct afile *p = ap->head;

	if (!p)
		return;

	while (p < ap->last) {
		if (p->name) {
			free(p->name);
		}
		if (p->dbp)
			free((char *)p->dbp);
		p++;
	}
	if (ap->base) {
		free((char *)ap->base);
	}
	(void) bzero((char *)ap, sizeof (struct arglist));
}
