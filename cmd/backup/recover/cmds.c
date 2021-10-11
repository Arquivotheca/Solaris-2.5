#ident	"@(#)cmds.c 1.27 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include "cmds.h"
#ifdef USG
#include <sys/mkdev.h>
#endif

/*
 * Date and time formats (from ls(1))
 *	b --- abbreviated month name
 *	e --- day number
 *	Y --- year in the form ccyy
 *	H --- hour (24 hour version)
 *	M --- minute
 */
#define	OLDFORMAT	" %b %e  %Y "		/* files older than 6 months */
#define	NEWFORMAT	" %b %e %H:%M "		/* files newer than 6 months */

struct filedata {
	int	blksize;			/* size in blocks */
	mode_t	mode;
	uid_t	owner;
	gid_t	group;
	int	size;				/* size in bytes */
	time_t	date;				/* mod time */
	char	exflag;				/* on extract list? */
	char	*name;				/* file name */
	char	*linkval;			/* symlink value */
};

#ifdef __STDC__
static void new_filelist(void);
static void extend_filelist(void);
static void listdir(char *, u_long, struct dir_block *,
	time_t, int, struct filedata *, char *);
static int formatoutput(char *, struct dir_entry *, time_t,
	int, char *, struct filedata *, char *);
static void longoutput(struct filedata *);
static void output_mode(mode_t);
static void fmtmode(char *, int);
static void printone(struct filedata *, int);
static void printlist(int);
static int fcmp(struct filedata *, struct filedata *);
static int afcmp(struct afile *, struct afile *);
static int tcmp(struct filedata *, struct filedata *);
/*
 * From libdump
 */
extern time_t getreldate(char *, struct timeb *);
#else
static void new_filelist();
static void extend_filelist();
static void listdir();
static int formatoutput();
static void longoutput();
static void output_mode();
static void fmtmode();
static void printone();
static void printlist();
static int fcmp();
static int afcmp();
static int tcmp();
extern time_t getreldate();
#endif

/*
 * the `setdate' command
 */
setdate(dp)
	char *dp;
{
	time_t now, newtime;

	now = time((time_t *)0);
	newtime = getreldate(dp, NULL);
	if (newtime == -1) {
		(void) fprintf(stderr, gettext(
			"could not convert time specification `%s'\n"), dp);
		return (0);
	}
	if (newtime > now) {
		(void) fprintf(stderr, gettext(
			"time specification in the future ignored\n"));
		return (0);
	}
	return (newtime);
}

/*
 * the `cd' command
 */
void
change_directory(host, curdir, cnt, ap, timestamp)
	char *host;
	char *curdir;
	int cnt;
	struct afile *ap;
	time_t timestamp;
{
	struct dir_entry *ep;
	struct dnode dn;
	char buf[MAXPATHLEN];

	if (cnt == 0 || strcmp(ap->name, "/") == 0) {
		(void) strcpy(curdir, "/");
		return;
	}
	(void) strcpy(buf, ap->name);
	ep = ap->dep;
	if (ep == NULL_DIRENTRY) {
		if (ap->expanded == 0)
			(void) printf(gettext("'%s' not found\n"), buf);
		else
			(void) printf(gettext("bad directory\n"));
	} else {
		if (ep->de_directory != NONEXISTENT_BLOCK) {
			if (getdnode(host, &dn, ep, VEXEC,
					timestamp, LOOKUP_DEFAULT,
					ap->name) == 0) {
				/*
				 * date or permission problem...
				 */
				if (ap->expanded)
					/*
					 * if name was expanded from
					 * wildcard we can't divulge the
					 * name in case it was private...
					 */
					(void) printf(gettext(
						"bad directory\n"));
				else
					(void) printf(gettext(
						"cannot cd to '%s'\n"), buf);
			} else {
				if (S_ISDIR(dn.dn_mode)) {
					smashpath(buf);
					(void) strcpy(curdir, buf);
				} else {
					(void) printf(gettext(
						"'%s' is not a directory\n"),
						buf);
				}
			}
		} else {
			(void) printf(gettext("'%s' is not a directory\n"),
				buf);
		}
	}
}

/*
 * change to the current user's home directory
 */
void
db_homedir(curdir, homedir, host, timestamp)
	char *curdir, *homedir, *host;
	time_t timestamp;
{
	struct afile ap;
	struct dir_block *bp;
	struct dir_entry *ep;
	u_long blk;
	char fulldir[MAXPATHLEN];

	if (strcmp(homedir, "/")) {
		if (ep = pathcheck(homedir, fulldir, host,
				timestamp, 0, 1, &bp, &blk)) {
			ap.dir_blknum = blk;
			ap.dbp = bp;
			ap.dep = ep;
			ap.name = fulldir;
			ap.expanded = 0;
			change_directory(host, curdir, 1, &ap, timestamp);
			return;
		}
		(void) printf(gettext(
			"Home directory `%s' not in database, `/' used.\n"),
			homedir);
	}
	(void) strcpy(curdir, "/");
}

/*
 * remove '.' and '..' components from a fully qualified path name.
 */
void
smashpath(s)
	char *s;
{
	register char *p1, *p2, *t;
	char buf[MAXPATHLEN];

	p2 = s;
	if (*p2 != '/')
		panic("smashpath");
	p1 = buf;
	while (*p2 && *p2 == '/')
		p2++;
	while (*p2) {
		if (*p2 == '.' && *(p2+1) == '.' &&
		    (*(p2+2) == '/' || *(p2+2) == 0)) {
			p2 += 2;
			while (*p2 && (*p2 == ' ' || *p2 == '/'))
				p2++;
			*p1 = '\0';
			if (t = strrchr(buf, '/')) {
				*t = '\0';
				p1 = t;
			}
		} else if (*p2 == '.' && (*(p2+1) == '/' || *(p2+1) == 0)) {
			p2++;
			if (*p2)
				p2++;
		} else {
			while (*p2 && *p2 == '/')
				p2++;
			if (*p2)
				*p1++ = '/';
			while (*p2 && *p2 != '/')
				*p1++ = *p2++;
			if (*p2)
				p2++;
		}
	}
	*p1 = '\0';
	if (strcmp(buf, ""))
		(void) strcpy(s, buf);
	else
		(void) strcpy(s, "/");
}

/*
 * file listing stuff
 */
static struct filedata *liststart;
static int filecnt, listsize;
#define	LISTINCREMENT	50

static void
#ifdef __STDC__
new_filelist(void)
#else
new_filelist()
#endif
{
	register struct filedata *fdp;
	register int i;

	if (liststart) {
		for (fdp = liststart, i = 0; i < filecnt; i++, fdp++) {
			if (fdp->name)
				free(fdp->name);
			if (fdp->linkval)
				free(fdp->linkval);
		}
		free((char *)liststart);
		liststart = NULL;
		listsize = filecnt = 0;
	}
	liststart = (struct filedata *)calloc(
			LISTINCREMENT, sizeof (struct filedata));
	if (liststart == NULL) {
		panic(gettext("out of memory\n"));
		/* NOTREACHED */
	}
	listsize = LISTINCREMENT;
	filecnt = 0;
}

static void
#ifdef __STDC__
extend_filelist(void)
#else
extend_filelist()
#endif
{
	liststart = (struct filedata *)realloc((char *)liststart,
			(unsigned)((listsize + LISTINCREMENT) *
			sizeof (struct filedata)));
	if (liststart == NULL) {
		panic(gettext("out of memory\n"));
		/* NOTREACHED */
	}
	listsize += LISTINCREMENT;
}

/*
 * the `versions' command
 */
/*ARGSUSED*/
void
showversions(host, curdir, cnt, ap)
	char *host;
	char *curdir;
	int cnt;
	struct arglist *ap;
{
	struct dir_entry *ep;
	u_long irec, firstirec;
	struct instance_record *ir;
	struct dnode *dp;
	register int i;
	struct filedata *fdp;
	unsigned int namelen;
	int gotone;

	struct afile *fp = ap->head;

	term_start_output();
	while (fp < ap->last) {
		ep = fp->dep;
		gotone = 0;

		firstirec = irec = ep->de_instances;
		if (irec == NONEXISTENT_BLOCK)
			panic(gettext("no instance block!"));

		do {
			if ((ir = instance_getrec(irec)) == NULL_IREC) {
				(void) fprintf(stderr, gettext(
					"Cannot get irec %lu\n"), irec);
				break;
			}
			for (i = 0; i < entries_perrec; i++) {
				if (ir->i_entry[i].ie_dumpid == 0)
					continue;
				dp = dnode_get(dbserv, host,
						ir->i_entry[i].ie_dumpid,
						ir->i_entry[i].ie_dnode_index);
				if (dp == NULL_DNODE) {
					/*
					(void) fprintf(stderr, gettext(
						"dnode_get fails\n"));
					*/
					continue;
				}
				if (permchk(dp, VREAD, host))
					continue;

				gotone++;
				if (gotone == 1) {
					new_filelist();
					fdp = liststart;
					filecnt = 1;
					if (fp != ap->head)
						term_putline("\n");
					term_putline(fp->name);
					term_putline(":\n");
				} else {
					fdp++;
					filecnt++;
					if (filecnt > listsize) {
						extend_filelist();
						fdp = &liststart[filecnt - 1];
					}
				}
				fdp->blksize = dp->dn_blocks;
				fdp->mode = dp->dn_mode;
				fdp->owner = dp->dn_uid;
				fdp->group = dp->dn_gid;
				fdp->size = dp->dn_size;
				fdp->date = dp->dn_mtime;
				fdp->exflag = ' ';
				fdp->linkval = NULL;
				if (onextractlist(ir->i_entry[i].ie_dumpid,
						dp->dn_inode))
					fdp->exflag = '*';
				if (S_ISLNK(dp->dn_mode)) {
					char *lp;
					int len;

					lp = db_readlink(dbserv, host,
						ir->i_entry[i].ie_dumpid,
						dp->dn_symlink);
					if (lp == NULL) {
						(void) fprintf(stderr, gettext(
						    "Cannot read symlink\n"));
						continue;
					}
					len = strlen(lp) + 1;
					fdp->linkval = malloc((unsigned)len);
					if (fdp->linkval == NULL) {
						panic(gettext(
							"out of memory\n"));
						/* NOTREACHED */
					}
					(void) strcpy(fdp->linkval, lp);
				}
				namelen = strlen(ep->de_name) + 1;
				fdp->name = malloc(namelen);
				if (fdp->name == NULL) {
					panic(gettext("out of memory\n"));
					/* NOTREACHED */
				}
				(void) strcpy(fdp->name, ep->de_name);
			}
			irec = ir->ir_next;
		} while (irec != firstirec);
#ifdef __STDC__
		qsort((char *)liststart, filecnt, sizeof (struct filedata),
			(int (*)(const void *, const void *)) tcmp);
#else
		qsort((char *)liststart, filecnt, sizeof (struct filedata),
			(int (*)())tcmp);
#endif
		printlist(1);
		fp++;
	}
	term_finish_output();
}


void
multifile_ls(host, ap, timestamp, longlisting)
	char *host;
	struct arglist *ap;
	time_t	timestamp;
	int longlisting;
{
	register struct afile *p = ap->head;
	struct dnode dn;
	char *longname;
	char termbuf[MAXPATHLEN];

#ifdef __STDC__
	qsort((char *)ap->head, (ap->last - ap->head), sizeof (struct afile),
		(int (*)(const void *, const void *)) afcmp);
#else
	qsort((char *)ap->head, (ap->last - ap->head), sizeof (struct afile),
		(int (*)())afcmp);
#endif
	/*
	 * regular files first
	 */
	term_start_output();
	while (p < ap->last) {
		if (getdnode(host, &dn, p->dep,
		    VREAD, timestamp, LOOKUP_DEFAULT, p->name) == 0) {
			/* EMPTY */
#ifdef notdef
			printf(gettext("%s not found\n"),
				p->dep->de_name);
#endif
		} else if (!S_ISDIR(dn.dn_mode)) {
			longname = p->name;
			listfiles(host, p, timestamp, longlisting, longname);
		}
		p++;
	}
	term_putline("\n");

	/*
	 * now directories
	 */
	p = ap->head;
	while (p < ap->last) {
		if (getdnode(host, &dn, p->dep,
		    VEXEC, timestamp, LOOKUP_DEFAULT, p->name) == 0) {
			/* EMPTY */
#ifdef notdef
			printf(gettext("%s not found\n"),
				p->dep->de_name);
#endif
		} else if (S_ISDIR(dn.dn_mode)) {
			if (permchk(&dn, VREAD, host) == 0) {
				(void) sprintf(termbuf, "%s:\n", p->name);
				term_putline(termbuf);
				listfiles(host, p, timestamp,
					longlisting, (char *)0);
			}
			term_putline("\n");
		}
		p++;
	}
	term_finish_output();
}

void
listfiles(host, ap, timestamp, longlisting, longname)
	char *host;
	struct afile *ap;
	time_t	timestamp;
	int longlisting;
	char *longname;
{
	struct dir_block *bp;
	struct dir_entry *ep;
	struct dnode dn;
	struct filedata *fdp;

	ep = ap->dep;
	if (getdnode(host, &dn, ep, VREAD, timestamp,
			LOOKUP_DEFAULT, ap->name) == 0) {
		if (ap->expanded == 0)
			(void) printf(gettext("%s not found\n"), ep->de_name);
		return;
	}
	if (S_ISDIR(dn.dn_mode) &&
			ep->de_directory != NONEXISTENT_BLOCK &&
			permchk(&dn, VEXEC, host) == 0) {
		/* a non-empty directory */
		if ((bp = dir_getblock(ep->de_directory)) == NULL_DIRBLK)
			return;

		new_filelist();
		fdp = liststart;
		listdir(host, ep->de_directory, bp,
				timestamp, longlisting, fdp, ap->name);

#ifdef __STDC__
		qsort((char *)liststart, filecnt, sizeof (struct filedata),
			(int (*)(const void *, const void *)) fcmp);
#else
		qsort((char *)liststart, filecnt, sizeof (struct filedata),
			(int (*)())fcmp);
#endif
		printlist(longlisting);
	} else {
		/* an individual file */
		fdp = (struct filedata *)malloc(sizeof (struct filedata));
		if (fdp == NULL) {
			(void) fprintf(stderr, gettext("out of memory\n"));
			return;
		}
		if (formatoutput(host, ep, timestamp,
				longlisting, longname, fdp, ap->name))
			printone(fdp, longlisting);
		free(fdp->name);
		if (fdp->linkval)
			free(fdp->linkval);
		free((char *)fdp);
	}
}

static void
listdir(host, startblock, bp, timestamp, longlisting, fdp, dirname)
	char *host;
	u_long startblock;
	struct dir_block *bp;
	time_t	timestamp;
	int longlisting;
	struct filedata *fdp;
	char *dirname;
{
	struct dir_entry *ep;
	char fullname[MAXPATHLEN+1];
	int dirlen;

	dirlen = strlen(dirname);
	do {
		/*LINTED [alignment ok]*/
		ep = (struct dir_entry *)bp->db_data;
		/*LINTED [alignment ok]*/
		while (ep != DE_END(bp)) {
			if ((int)(dirlen + ep->de_name_len + 2) > MAXPATHLEN) {
				(void) fprintf(stderr, gettext(
					"name too long\n"));
				ep = DE_NEXT(ep);
				continue;
			}
			(void) sprintf(fullname, "%s/%s", dirname, ep->de_name);
			if (formatoutput(host, ep, timestamp, longlisting,
					(char *)0, fdp, fullname)) {
				filecnt++;
				fdp++;
				if (filecnt >= listsize) {
					extend_filelist();
					fdp = &liststart[filecnt];
				}
			}
			ep = DE_NEXT(ep);
		}
		if (bp->db_next != startblock) {
			if ((bp = dir_getblock(bp->db_next)) == NULL_DIRBLK)
				return;
		} else {
			bp = NULL_DIRBLK;
		}
	} while (bp != NULL_DIRBLK);
}

static int
formatoutput(host, ep, timestamp, longlisting, longname, fdp, name)
	char *host;
	struct dir_entry *ep;
	time_t timestamp;
	int longlisting;
	char *longname;
	struct filedata *fdp;
	char *name;
{
	struct dnode dnhold;
	u_long dumpid;
	int rc = 0;

	if (dumpid = getdnode(host, &dnhold, ep, VREAD,
				timestamp, LOOKUP_DEFAULT, name)) {
		/*
		 * here we have a valid instance in 'dnhold'
		 */
		fdp->linkval = NULL;
		if (longlisting) {
			fdp->blksize = dnhold.dn_blocks;
			fdp->mode = dnhold.dn_mode;
			fdp->owner = dnhold.dn_uid;
			fdp->group = dnhold.dn_gid;
			fdp->size = dnhold.dn_size;
			fdp->date = dnhold.dn_mtime;
			if (S_ISLNK(dnhold.dn_mode)) {
				int len;
				char *lp;

				if ((lp = db_readlink(dbserv, host, dumpid,
						dnhold.dn_symlink)) == NULL) {
					(void) fprintf(stderr, gettext(
						"Cannot read symlink!\n"));
				} else {
					len = strlen(lp)+1;
					fdp->linkval = malloc((unsigned)len);
					if (fdp->linkval == NULL) {
						panic(gettext(
							"out of memory\n"));
						/*NOTREACHED*/
					}
					(void) strcpy(fdp->linkval, lp);
				}
			}
		}

		fdp->exflag = ' ';
		if (onextractlist(dumpid, dnhold.dn_inode))
			fdp->exflag = '*';
		if (longname) {
			fdp->name = malloc((unsigned)(strlen(longname)+1));
			if (fdp->name == NULL) {
				panic(gettext("out of memory\n"));
				/*NOTREACHED*/
			}
			(void) strcpy(fdp->name, longname);
		} else {
			fdp->name = malloc((unsigned)(strlen(ep->de_name)+1));
			if (fdp->name == NULL) {
				panic(gettext("out of memory\n"));
				/*NOTREACHED*/
			}
			(void) strcpy(fdp->name, ep->de_name);
		}
		rc = 1;
	}
	return (rc);
}

static void
longoutput(fdp)
	struct filedata *fdp;
{

#if DEV_BSIZE < 1024
#define	dbtok(n)	howmany(n, 1024 / DEV_BSIZE)
#else
#define	dbtok(n)	((n) * (DEV_BSIZE / 1024))
#endif
	struct passwd *pw;
	struct group *gr;
	dev_t dev;
	extern time_t sixmonthsago, onehourfromnow;
	char termbuf[MAXPATHLEN];


	(void) sprintf(termbuf, "%4ld ",
		(long)(S_ISLNK(fdp->mode) ? 1 : dbtok(fdp->blksize)));
	term_putline(termbuf);
	output_mode(fdp->mode);
	/*
	 * XXX: probably worth keeping a cache of user and
	 * group names...
	 */
	if ((pw = getpwuid(fdp->owner)) != NULL) {
		(void) sprintf(termbuf, " %-9.9s", pw->pw_name);
	} else {
		(void) sprintf(termbuf, " %-9ld", fdp->owner);
	}
	term_putline(termbuf);
	if ((gr = getgrgid(fdp->group)) != NULL) {
		(void) sprintf(termbuf, " %-9.9s", gr->gr_name);
	} else {
		(void) sprintf(termbuf, " %-9ld", fdp->group);
	}
	term_putline(termbuf);
	if (S_ISBLK(fdp->mode) || S_ISCHR(fdp->mode)) {
		/*
		 * For device special files, the r_dev data
		 * will be stored in dn_size
		 */
		dev = fdp->size;
		(void) sprintf(termbuf, " %4lu,%4lu",
			(u_long)major(dev), (u_long)minor(dev));
	} else {
		(void) sprintf(termbuf, " %9d", fdp->size);
	}
	term_putline(termbuf);
	if (fdp->date < sixmonthsago || fdp->date > onehourfromnow)
		(void) cftime(termbuf, OLDFORMAT, &fdp->date);
	else
		(void) cftime(termbuf, NEWFORMAT, &fdp->date);
	term_putline(termbuf);
}

static void
output_mode(mode)
	mode_t	mode;
{
	char lp[20];
	char c;

	switch (mode & S_IFMT) {
	case S_IFDIR:
		c = 'd';
		break;
	case S_IFBLK:
		c = 'b';
		break;
	case S_IFCHR:
		c = 'c';
		break;
	case S_IFSOCK:
		c = 's';
		break;
	case S_IFIFO:
		c = 'p';
		break;
	case S_IFLNK:
		c = 'l';
		break;
	default:
		c = '-';
		break;
	}
	fmtmode(lp, (int)(mode & ~S_IFMT));
	term_putc(c);
	term_putline(lp);
}

/*
 * stolen from 'ls'
 */
static int	m1[] = { 1, S_IREAD>>0, 'r', '-' };
static int	m2[] = { 1, S_IWRITE>>0, 'w', '-' };
static int	m3[] = { 3, S_ISUID|(S_IEXEC>>0), 's', S_IEXEC>>0,
	'x', S_ISUID, 'S', '-' };
static int	m4[] = { 1, S_IREAD>>3, 'r', '-' };
static int	m5[] = { 1, S_IWRITE>>3, 'w', '-' };
static int	m6[] = { 3, S_ISGID|(S_IEXEC>>3), 's', S_IEXEC>>3,
	'x', S_ISGID, 'S', '-' };
static int	m7[] = { 1, S_IREAD>>6, 'r', '-' };
static int	m8[] = { 1, S_IWRITE>>6, 'w', '-' };
static int	m9[] = { 3, S_ISVTX|(S_IEXEC>>6), 't', S_IEXEC>>6,
	'x', S_ISVTX, 'T', '-'};

static int	*m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9};

static void
fmtmode(lp, flags)
	char *lp;
	int flags;
{
	int **mp;

/*	mp = &mp[0]; */
	mp = &m[0];	/* XXX [sas] */
	while (mp < &m[sizeof (m)/sizeof (m[0])]) {
		register int *pairp = *mp++;
		register int n = *pairp++;

		while (n-- > 0) {
			if ((flags & *pairp) == *pairp) {
				pairp++;
				break;
			} else
				pairp += 2;
		}
		*lp++ = *pairp;
	}
	*lp = '\0';
}

static void
printone(fdp, longlist)
	struct filedata *fdp;
	int longlist;
{
	register char *p;

	if (longlist) {
		longoutput(fdp);
	}

	term_putc(fdp->exflag);
	for (p = fdp->name; *p; p++) {
		if (isprint((u_char)*p))
			term_putc((u_char)*p);
		else
			term_putc('?');
	}
	if (longlist && S_ISLNK(fdp->mode)) {
		term_putline(" -> ");
		term_putline(fdp->linkval);
	}
	term_putc('\n');
}

static void
printlist(longlist)
	int longlist;
{
	register int i, j;
	register struct filedata *fdp, *end;
	int twidth;
	int width = 0, w, columns, lines;
	register char *p, *tofree;

	if (longlist) {
		for (i = 0, fdp = liststart; i < filecnt; fdp++, i++) {
			printone(fdp, longlist);
			tofree = fdp->name;
			fdp->name = NULL;
			free(tofree);
			if (fdp->linkval) {
				tofree = fdp->linkval;
				fdp->linkval = NULL;
				free(tofree);
			}
		}
	} else if (isatty(fileno(outfp))) {
		/*
		 * output in columns (with code stolen from `ls')
		 */
		twidth = get_termwidth();
		for (fdp = liststart, i = 0; i < filecnt; fdp++, i++) {
			/* name includes 'extract' indicator */
			int len = strlen(fdp->name) + 1;

			if (len > width)
				width = len;
		}

		width += 2;
		columns = twidth / width;
		if (columns == 0)
			columns = 1;

		end = &liststart[filecnt];
		lines = (filecnt + columns - 1) / columns;
		for (i = 0; i < lines; i++) {
			for (j = 0; j < columns; j++) {
				fdp = liststart + j * lines + i;
				term_putc(fdp->exflag);
				for (p = fdp->name; *p; p++)
					if (isprint((u_char)*p))
						term_putc((u_char)*p);
					else
						term_putc('?');
				if (fdp + lines >= end) {
					term_putc('\n');
					tofree = fdp->name;
					fdp->name = NULL;
					free(tofree);
					break;
				}
				/* include extract marker in namelen */
				w = strlen(fdp->name)+1;
				while (w < width) {
					w++;
					term_putc(' ');
				}
				tofree = fdp->name;
				fdp->name = NULL;
				free(tofree);
			}
		}
	} else {
		/*
		 * no columns and no extract list marker when output
		 * is not to tty.
		 */
		for (i = 0, fdp = liststart; i < filecnt; fdp++, i++) {
			for (p = fdp->name; *p; p++) {
				if (isprint((u_char)*p))
					(void) putc((u_char)*p, outfp);
				else
					(void) putc('?', outfp);
			}
			(void) putc('\n', outfp);
			tofree = fdp->name;
			fdp->name = NULL;
			free(tofree);
		}
	}
	tofree = (char *)liststart;
	liststart = NULL;
	listsize = filecnt = 0;
	free(tofree);
}

static int
fcmp(f1, f2)
	register struct filedata *f1;
	register struct filedata *f2;
{
	return (strcoll(f1->name, f2->name));
}

static int
afcmp(f1, f2)
	register struct afile *f1;
	register struct afile *f2;
{
	return (strcoll(f1->name, f2->name));
}

static int
tcmp(f1, f2)
	register struct filedata *f1;
	register struct filedata *f2;
{
	/*
	 * qsort routine for date in descending order -- most recent
	 * to least recent
	 */
	return (f2->date - f1->date);
}
