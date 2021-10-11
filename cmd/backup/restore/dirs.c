/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)dirs.c	1.14	94/08/10 SMI"

#include "restore.h"
#include <byteorder.h>
#include <sys/file.h>
#include <utime.h>

/*
 * Symbol table of directories read from tape.
 */
#define	HASHSIZE	1000
#define	INOHASH(val) (val % HASHSIZE)
struct inotab {
	struct inotab *t_next;
	ino_t	t_ino;
	daddr_t	t_seekpt;
	long	t_size;
};
static struct inotab *inotab[HASHSIZE];

/*
 * Information retained about directories.
 */
static struct modeinfo {
	ino_t	ino;
	time_t	timep[2];
	mode_t	mode;
	uid_t	uid;
	gid_t	gid;
	long	metasize;
} node;

/*
 * Global variables for this file.
 */
static daddr_t	seekpt;
static FILE	*df, *mf;
static char	dirfile[32] = "#";	/* No file */
static char	modefile[32] = "#";	/* No file */

static RST_DIR	*dirp;

/*
 * Format of old style directories.
 */
#define	ODIRSIZ 14
struct odirect {
	u_short	d_ino;
	char	d_name[ODIRSIZ];
};

#ifdef __STDC__
static ino_t search(ino_t, char	*);
static void putdir(char *, long);
static void putent(struct direct *);
static void skipmetadata(FILE *, long);
static void flushent(void);
static void dcvt(struct odirect *, struct direct *);
static RST_DIR *rst_initdirfile(char *);
static daddr_t rst_telldir(RST_DIR *);
static void rst_seekdir(RST_DIR *, daddr_t, daddr_t);
static struct inotab *allocinotab(ino_t, struct dinode *, daddr_t);
static void nodeflush(void);
static struct inotab *inotablookup(ino_t);
#else
static ino_t search();
static void putdir();
static void putent();
static void skipmetadata();
static void flushent();
static void dcvt();
static RST_DIR *rst_initdirfile();
static daddr_t rst_telldir();
static void rst_seekdir();
static struct inotab *allocinotab();
static void nodeflush();
static struct inotab *inotablookup();
#endif

/*
 *	Extract directory contents, building up a directory structure
 *	on disk for extraction by name.
 *	If genmode is requested, save mode, owner, and times for all
 *	directories on the tape.
 */
void
extractdirs(genmode)
	int genmode;
{
	register int i, ts;
	register struct dinode *ip;
	struct inotab *itp;
	struct direct nulldir;

	vprintf(stdout, gettext("Extract directories from tape\n"));
	(void) sprintf(dirfile, "/tmp/rstdir%ld", dumpdate);
	df = fopen(dirfile, "w");
	if (df == 0) {
		(void) fprintf(stderr,
		    gettext("%s: %s - cannot create directory temporary\n"),
			progname, dirfile);
		perror("fopen");
		done(1);
	}
	if (genmode != 0) {
		(void) sprintf(modefile, "/tmp/rstmode%ld", dumpdate);
		mf = fopen(modefile, "w");
		if (mf == 0) {
			(void) fprintf(stderr,
			    gettext("%s: %s - cannot create modefile \n"),
				progname, modefile);
			perror("fopen");
			done(1);
		}
	}
	nulldir.d_ino = 0;
	nulldir.d_namlen = 1;
	(void) strcpy(nulldir.d_name, "/");
	nulldir.d_reclen = DIRSIZ(&nulldir);
	for (;;) {
		curfile.name = gettext("<directory file - name unknown>");
		curfile.action = USING;
		ip = curfile.dip;
		ts = curfile.ts;
		if (ts != TS_END && ts != TS_INODE) {
			getfile(null, null);
			continue;
		}
		if ((ts == TS_INODE && (ip->di_mode & IFMT) != IFDIR) ||
		    (ts == TS_END)) {
			(void) fflush(df);
			if (ferror(df))
				panic("%s: %s\n", dirfile, strerror(errno));
			(void) fclose(df);
			dirp = rst_initdirfile(dirfile);
			if (dirp == NULL)
				perror("initdirfile");
			if (mf != NULL) {
				(void) fflush(mf);
				if (ferror(mf))
					panic("%s: %s\n",
					    modefile, strerror(errno));
				(void) fclose(mf);
			}
			i = dirlookup(".");
			if (i == 0)
				panic(gettext(
					"Root directory is not on tape\n"));
			return;
		}
		itp = allocinotab(curfile.ino, ip, seekpt);
		getfile(putdir, null);
		if (mf)
			nodeflush();
		putent(&nulldir);
		flushent();
		itp->t_size = seekpt - itp->t_seekpt;
	}
}

/*
 * skip over all the directories on the tape
 */
void
skipdirs()
{

	while (curfile.dip != NULL && (curfile.dip->di_mode & IFMT) == IFDIR) {
		skipfile();
	}
}

/*
 *	Recursively find names and inumbers of all files in subtree
 *	pname and pass them off to be processed.
 */
void
treescan(pname, ino, todo)
	char *pname;
	ino_t ino;
	long (*todo)();
{
	register struct inotab *itp;
	register struct direct *dp;
	int namelen;
	daddr_t bpt;
	char locname[MAXPATHLEN + 1];

	itp = inotablookup(ino);
	if (itp == NULL) {
		/*
		 * Pname is name of a simple file or an unchanged directory.
		 */
		(void) (*todo)(pname, ino, LEAF);
		return;
	}
	/*
	 * Pname is a dumped directory name.
	 */
	if ((*todo)(pname, ino, NODE) == FAIL)
		return;
	/*
	 * begin search through the directory
	 * skipping over "." and ".."
	 */
	(void) strncpy(locname, pname, MAXPATHLEN);
	(void) strncat(locname, "/", MAXPATHLEN);
	namelen = strlen(locname);
	rst_seekdir(dirp, itp->t_seekpt, itp->t_seekpt);
	dp = rst_readdir(dirp); /* "." */
	if (dp != NULL && strcmp(dp->d_name, ".") == 0)
		dp = rst_readdir(dirp); /* ".." */
	else
		(void) fprintf(stderr,
		    gettext("Warning: `.' missing from directory %s\n"),
			pname);
	if (dp != NULL && strcmp(dp->d_name, "..") == 0)
		dp = rst_readdir(dirp); /* first real entry */
	else
		(void) fprintf(stderr,
		    gettext("Warning: `..' missing from directory %s\n"),
			pname);
	bpt = rst_telldir(dirp);
	/*
	 * a zero inode signals end of directory
	 */
	while (dp != NULL && dp->d_ino != 0) {
		locname[namelen] = '\0';
		if (namelen + (int)dp->d_namlen >= MAXPATHLEN) {
			(void) fprintf(stderr,
			    gettext("%s%s: name exceeds %d char\n"),
				locname, dp->d_name, MAXPATHLEN);
		} else {
			(void) strncat(locname, dp->d_name, (int)dp->d_namlen);
			treescan(locname, dp->d_ino, todo);
			rst_seekdir(dirp, bpt, itp->t_seekpt);
		}
		dp = rst_readdir(dirp);
		bpt = rst_telldir(dirp);
	}
	if (dp == NULL)
		(void) fprintf(stderr,
			gettext("corrupted directory: %s.\n"), locname);
}

/*
 * Search the directory tree rooted at inode ROOTINO
 * for the path pointed at by n
 */
ino_t
psearch(n)
	char	*n;
{
	register char *cp, *cp1;
	ino_t ino;
	char c;

	ino = ROOTINO;
	if (*(cp = n) == '/')
		cp++;
next:
	cp1 = cp + 1;
	while (*cp1 != '/' && *cp1)
		cp1++;
	c = *cp1;
	*cp1 = 0;
	ino = search(ino, cp);
	if (ino == 0) {
		*cp1 = c;
		return (0);
	}
	*cp1 = c;
	if (c == '/') {
		cp = cp1+1;
		goto next;
	}
	return (ino);
}

/*
 * search the directory inode ino
 * looking for entry cp
 */
static ino_t
search(inum, cp)
	ino_t	inum;
	char	*cp;
{
	register struct direct *dp;
	register struct inotab *itp;
	int len;

	itp = inotablookup(inum);
	if (itp == NULL)
		return (0);
	rst_seekdir(dirp, itp->t_seekpt, itp->t_seekpt);
	len = strlen(cp);
	do {
		dp = rst_readdir(dirp);
		if (dp == NULL || dp->d_ino == 0)
			return (0);
	} while (dp->d_namlen != len || strncmp(dp->d_name, cp, len) != 0);
	return (dp->d_ino);
}

/*
 * Put the directory entries in the directory file
 */
static void
putdir(buf, size)
	char *buf;
	long size;
{
	struct direct cvtbuf;
	register struct odirect *odp;
	struct odirect *eodp;
	register struct direct *dp;
	long loc, i;

	if (cvtflag) {
		/*LINTED [buf is char[] in getfile, size % fs_fsize == 0]*/
		eodp = (struct odirect *)&buf[size];
		/*LINTED [buf is char[] in getfile]*/
		for (odp = (struct odirect *)buf; odp < eodp; odp++)
			if (odp->d_ino != 0) {
				dcvt(odp, &cvtbuf);
				putent(&cvtbuf);
			}
	} else {
		loc = 0;
		while (loc < size) {
			/*LINTED [buf is char[] in getfile, loc % 4 == 0]*/
			dp = (struct direct *)(buf + loc);
			normdirect(byteorder, dp);
			i = DIRBLKSIZ - (loc & (DIRBLKSIZ - 1));
			if (dp->d_reclen == 0 || (long)dp->d_reclen > i) {
				loc += i;
				continue;
			}
			loc += dp->d_reclen;
			if (dp->d_ino != 0) {
				putent(dp);
			}
		}
	}
}

/*
 * These variables are "local" to the following two functions.
 */
static char dirbuf[DIRBLKSIZ];
static long dirloc = 0;
static long prev = 0;

/*
 * add a new directory entry to a file.
 */
static void
putent(dp)
	struct direct *dp;
{
	dp->d_reclen = DIRSIZ(dp);
	if (dirloc + (long)dp->d_reclen > DIRBLKSIZ) {
		/*LINTED [prev += dp->d_reclen, prev % 4 == 0]*/
		((struct direct *)(dirbuf + prev))->d_reclen =
		    DIRBLKSIZ - prev;
		(void) fwrite(dirbuf, 1, DIRBLKSIZ, df);
		if (ferror(df))
			panic("%s: %s\n", dirfile, strerror(errno));
		dirloc = 0;
	}
	bcopy((char *)dp, dirbuf + dirloc, (long)dp->d_reclen);
	prev = dirloc;
	dirloc += dp->d_reclen;
}

/*
 * flush out a directory that is finished.
 */
static void
#ifdef __STDC__
flushent(void)
#else
flushent()
#endif
{

	/*LINTED [prev += dp->d_reclen, prev % 4 == 0]*/
	((struct direct *)(dirbuf + prev))->d_reclen = DIRBLKSIZ - prev;
	(void) fwrite(dirbuf, (int)dirloc, 1, df);
	if (ferror(df))
		panic("%s: %s\n", dirfile, strerror(errno));
	seekpt = ftell(df);
	dirloc = 0;
}

static void
dcvt(odp, ndp)
	register struct odirect *odp;
	register struct direct *ndp;
{

	(void) bzero((char *)ndp, (long)(sizeof (*ndp)));
	ndp->d_ino =  odp->d_ino;
	(void) strncpy(ndp->d_name, odp->d_name, ODIRSIZ);
	ndp->d_namlen = strlen(ndp->d_name);
	ndp->d_reclen = DIRSIZ(ndp);
}

/*
 * Initialize the directory file
 */
static RST_DIR *
rst_initdirfile(name)
	char *name;
{
	register RST_DIR *dp;
	register int fd;

	if ((fd = open(name, 0)) == -1)
		return ((RST_DIR *)0);
	if ((dp = (RST_DIR *)malloc(sizeof (RST_DIR))) == NULL) {
		(void) close(fd);
		return ((RST_DIR *)0);
	}
	dp->dd_fd = fd;
	dp->dd_loc = 0;
	return (dp);
}

/*
 * Simulate the opening of a directory
 */
RST_DIR *
rst_opendir(name)
	char *name;
{
	struct inotab *itp;
	ino_t ino;

	if ((ino = dirlookup(name)) > 0 &&
	    (itp = inotablookup(ino)) != NULL) {
		rst_seekdir(dirp, itp->t_seekpt, itp->t_seekpt);
		return (dirp);
	}
	return ((RST_DIR *)0);
}

/*
 * return a pointer into a directory
 */
static daddr_t
rst_telldir(dirp)
	RST_DIR *dirp;
{
	return (lseek(dirp->dd_fd, 0L, 1) - dirp->dd_size + dirp->dd_loc);
}

/*
 * Seek to an entry in a directory.
 * Only values returned by ``rst_telldir'' should be passed to rst_seekdir.
 * This routine handles many directories in a single file.
 * It takes the base of the directory in the file, plus
 * the desired seek offset into it.
 */
static void
rst_seekdir(dirp, loc, base)
	register RST_DIR *dirp;
	daddr_t loc, base;
{

	if (loc == rst_telldir(dirp))
		return;
	loc -= base;
	if (loc < 0)
		(void) fprintf(stderr,
			gettext("bad seek pointer to rst_seekdir %d\n"), loc);
	(void) lseek(dirp->dd_fd, base + (loc & ~(DIRBLKSIZ - 1)), 0);
	dirp->dd_loc = loc & (DIRBLKSIZ - 1);
	if (dirp->dd_loc != 0)
		dirp->dd_size = read(dirp->dd_fd, dirp->dd_buf, DIRBLKSIZ);
}

/*
 * get next entry in a directory.
 */
struct direct *
rst_readdir(dirp)
	register RST_DIR *dirp;
{
	register struct direct *dp;

	for (;;) {
		if (dirp->dd_loc == 0) {
			dirp->dd_size = read(dirp->dd_fd, dirp->dd_buf,
			    DIRBLKSIZ);
			if (dirp->dd_size <= 0) {
				dprintf(stderr,
					gettext("error reading directory\n"));
				return ((struct direct *)0);
			}
		}
		if (dirp->dd_loc >= dirp->dd_size) {
			dirp->dd_loc = 0;
			continue;
		}
		/*LINTED [rvalue will be aligned on int boundary]*/
		dp = (struct direct *)(dirp->dd_buf + dirp->dd_loc);
		if (dp->d_reclen == 0 ||
		    (long)dp->d_reclen > DIRBLKSIZ + 1 - dirp->dd_loc) {
			dprintf(stderr,
			    gettext("corrupted directory: bad reclen %d\n"),
				dp->d_reclen);
			return ((struct direct *)0);
		}
		dirp->dd_loc += dp->d_reclen;
		if (dp->d_ino == 0 && strcmp(dp->d_name, "/") != 0)
			continue;
		if (dp->d_ino >= maxino) {
			dprintf(stderr,
				gettext("corrupted directory: bad inum %lu\n"),
				dp->d_ino);
			continue;
		}
		return (dp);
	}
}

/*
 * Set the mode, owner, and times for all new or changed directories
 */
void
#ifdef __STDC__
setdirmodes(void)
#else
setdirmodes()
#endif
{
	FILE *mf;
	struct entry *ep;
	char *cp, *metadata = NULL;
	long metasize = 0;
	int override = -1;

	vprintf(stdout, gettext("Set directory mode, owner, and times.\n"));
	(void) sprintf(modefile, "/tmp/rstmode%ld", dumpdate);
	mf = fopen(modefile, "r");
	if (mf == NULL) {
		perror("fopen");
		(void) fprintf(stderr,
			gettext("cannot open mode file %s\n"), modefile);
		(void) fprintf(stderr,
			gettext("directory mode, owner, and times not set\n"));
		return;
	}
	clearerr(mf);
	for (;;) {
		(void) fread((char *)&node, 1, sizeof (struct modeinfo), mf);
		if (feof(mf))
			break;
		ep = lookupino(node.ino);
		if (command == 'i' || command == 'x') {
			if (ep == NIL) {
				skipmetadata(mf, node.metasize);
				continue;
			}
			if (ep->e_flags & EXISTED) {
				if (override < 0) {
					if (reply(gettext(
				"Directories already exist, set modes anyway"))
					    == FAIL)
						override = 0;
					else
						override = 1;
				}
				if (override == 0) {
					ep->e_flags &= ~NEW;
					skipmetadata(mf, node.metasize);
					continue;
				}
			}
			if (node.ino == ROOTINO &&
			    reply(gettext("set owner/mode for '.'")) == FAIL) {
				skipmetadata(mf, node.metasize);
				continue;
			}
		}
		if (ep == NIL) {
			panic(gettext("cannot find directory inode %d\n"),
				node.ino);
			skipmetadata(mf, node.metasize);
			continue;
		}
		cp = myname(ep);
#ifdef USG
		(void) chown(cp, node.uid, node.gid);
#else
		(void) chown(cp, (int)node.uid, (int)node.gid);
#endif
		(void) chmod(cp, node.mode);
		if (node.metasize > 0) {
			if (node.metasize > metasize)
				metadata = realloc(metadata,
				    metasize = node.metasize);
			if (metadata == NULL)
				panic(gettext("Cannot malloc metadata\n"));
			(void) fread(metadata, 1, node.metasize, mf);
			metaproc(cp, metadata, node.metasize);
		}
		utime(cp, (struct utimbuf *)node.timep);
		ep->e_flags &= ~NEW;
	}
	if (ferror(mf))
		panic(gettext("error setting directory modes\n"));
	if (metadata != NULL)
		(void) free(metadata);
	(void) fclose(mf);
}

void
skipmetadata(f, size)
	FILE *f;
	long size;
{
	fseek(f, size, SEEK_CUR);
}

/*
 * Generate a literal copy of a directory.
 */
genliteraldir(name, ino)
	char *name;
	ino_t ino;
{
	register struct inotab *itp;
	int ofile, dp, i, size;
	char buf[BUFSIZ];

	itp = inotablookup(ino);
	if (itp == NULL)
		panic(gettext("Cannot find directory inode %d named %s\n"),
			ino, name);
	if ((ofile = creat(name, 0666)) < 0) {
		(void) fprintf(stderr, "%s: ", name);
		(void) fflush(stderr);
		perror(gettext("cannot create file"));
		return (FAIL);
	}
	rst_seekdir(dirp, itp->t_seekpt, itp->t_seekpt);
	dp = dup(dirp->dd_fd);
	for (i = itp->t_size; i > 0; i -= BUFSIZ) {
		size = i < BUFSIZ ? i : BUFSIZ;
		if (read(dp, buf, (int) size) == -1) {
			(void) fprintf(stderr, gettext(
				"read error extracting inode %d, name %s\n"),
				curfile.ino, curfile.name);
			perror("read");
			done(1);
		}
		if (write(ofile, buf, (int) size) == -1) {
			(void) fprintf(stderr, gettext(
				"write error extracting inode %d, name %s\n"),
				curfile.ino, curfile.name);
			perror("write");
			done(1);
		}
	}
	(void) close(dp);
	(void) close(ofile);
	return (GOOD);
}

/*
 * Determine the type of an inode
 */
inodetype(ino)
	ino_t ino;
{
	struct inotab *itp;

	itp = inotablookup(ino);
	if (itp == NULL)
		return (LEAF);
	return (NODE);
}

/*
 * Allocate and initialize a directory inode entry.
 * If requested, save its pertinent mode, owner, and time info.
 */
static struct inotab *
allocinotab(ino, dip, seekpt)
	ino_t ino;
	struct dinode *dip;
	daddr_t seekpt;
{
	register struct inotab	*itp;

	itp = (struct inotab *)calloc(1, sizeof (struct inotab));
	if (itp == 0)
		panic(gettext("no memory directory table\n"));
	itp->t_next = inotab[INOHASH(ino)];
	inotab[INOHASH(ino)] = itp;
	itp->t_ino = ino;
	itp->t_seekpt = seekpt;
	if (mf == NULL)
		return (itp);
	node.ino = ino;
	node.timep[0] = dip->di_atime;
	node.timep[1] = dip->di_mtime;
	node.mode = dip->di_mode;
#ifdef USG
	node.uid =
		dip->di_suid == UID_LONG ? dip->di_uid : (uid_t)dip->di_suid;
	node.gid =
		dip->di_sgid == GID_LONG ? dip->di_gid : (gid_t)dip->di_sgid;
#else
	node.uid = dip->di_uid;
	node.gid = dip->di_gid;
#endif
	return (itp);
}

void
nodeflush()
{
	char *metadata;
	long metasize;

	metaget(&metadata, &(node.metasize));
	(void) fwrite((char *)&node, 1, sizeof (struct modeinfo), mf);
	if (node.metasize > 0)
		(void) fwrite(metadata, 1, node.metasize, mf);
	if (ferror(mf))
		panic("%s: %s\n", modefile, strerror(errno));
}

/*
 * Look up an inode in the table of directories
 */
static struct inotab *
inotablookup(ino)
	ino_t	ino;
{
	register struct inotab *itp;

	for (itp = inotab[INOHASH(ino)]; itp != NULL; itp = itp->t_next)
		if (itp->t_ino == ino)
			return (itp);
	return ((struct inotab *)0);
}

/*
 * Clean up and exit
 */
void
done(exitcode)
	int exitcode;
{

	closemt();
	if (modefile[0] != '#')
		(void) unlink(modefile);
	if (dirfile[0] != '#')
		(void) unlink(dirfile);
	exit(exitcode);
}
