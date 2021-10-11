/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)dumpitime.c 1.11 90/11/09 SMI" /* from UCB 5.2 5/28/86 */

#ident	"@(#)dumpitime.c 1.12 94/08/10"

#include "dump.h"
#include <sys/file.h>
#ifndef LOCK_EX
static 	struct flock fl;
#define	flock(fd, flag) (fl.l_type = (flag), fcntl(fd, F_SETLKW, &fl))
#define	LOCK_EX F_WRLCK
#define	LOCK_SH F_RDLCK
#define	LOCK_UN F_UNLCK
#endif

/*
 * Print a date.  A date of 0 is the beginning of time (the "epoch").
 * If the 2nd argument is non-zero, it is ok to format the date in
 * locale-specific form, otherwise we use ctime.  We must use ctime
 * for dates such as those in the dumpdates file, which must be
 * locale-independent.
 */
char *
prdate(d, localok)
	time_t	d;
	int	localok;	/* =1 if ok to use locale-specific output */
{
	static char buf[256];
	struct tm *tm;
	char *p;

	if (d == 0)
		return (gettext("the epoch"));
	if (localok) {
		tm = localtime(&d);
		(void) strftime(buf, sizeof (buf), "%c", tm);
		p = buf;
	} else {
		p = ctime(&d);
		p[24] = 0;
	}
	return (p);
}

struct	idates	**idatev = 0;
int	nidates = 0;
static	int	idates_in = 0;		/* we have read the increment file */
static	int	recno;

#ifdef __STDC__
static void readitimes(FILE *);
static void recout(FILE	*, struct idates *);
static int getrecord(FILE *, struct idates *);
static int makeidate(struct idates *, char *);
#else
static void readitimes();
static void recout();
static int getrecord();
static int makeidate();
#endif

void
#ifdef __STDC__
inititimes(void)
#else
inititimes()
#endif
{
	FILE *df;
	extern int errno;

	if (idates_in)
		return;
	if ((df = fopen(increm, "r")) == NULL) {
		if (errno == ENOENT)
			msg(gettext(
			    "Warning - dump record file `%s' does not exist\n"),
				increm);
		else {
			msg(gettext("Cannot open dump record file `%s': %s\n"),
				increm, strerror(errno));
			dumpabort();
		}
		return;
	}
	if (uflag && access(increm, W_OK) < 0) {
		msg(gettext("Cannot access dump record file `%s' for update\n"),
		    increm);
		dumpabort();
	}
	(void) flock(fileno(df), LOCK_SH);
	readitimes(df);
	(void) fclose(df);
}

static void
readitimes(df)
	FILE *df;
{
	register int i;
	struct idates *idp;

	recno = 0;
	for (;;) {
		idp = (struct idates *) calloc(1, sizeof (struct idates));
		if (getrecord(df, idp) < 0) {
			free((char *)idp);
			break;
		}
		nidates++;
		idatev = (struct idates **) realloc((void *)idatev,
			(size_t) (nidates * sizeof (struct idates *)));
		idatev[nidates - 1] = idp;
	}
	idates_in = 1;
}

void
#ifdef __STDC__
getitime(void)
#else
getitime()
#endif
{
	register	struct	idates	*ip;
	register	int	i;
			char	*fname;

	fname = disk;
#ifdef FDEBUG

	/* XGETTEXT:  #ifdef FDEBUG only */
	msg(gettext("Looking for name %s in increm = %s for delta = %c\n"),
		fname, increm, (u_char)incno);
#endif
	spcl.c_ddate = 0;
	lastincno = '0';

	inititimes();
	if (idatev == 0)
		return;
	/*
	 *	Go find the entry with the same name for a lower increment
	 *	and older date
	 */
	ITITERATE(i, ip) {
		if (strncmp(fname, ip->id_name, sizeof (ip->id_name)) != 0)
			continue;
		if (!trueinc && ip->id_incno >= incno)
			continue;
		if (ip->id_ddate <= spcl.c_ddate)
			continue;
		spcl.c_ddate = ip->id_ddate;
		lastincno = ip->id_incno;
	}
}

void
#ifdef __STDC__
putitime(void)
#else
putitime()
#endif
{
	FILE		*df;
	register	struct	idates	*itwalk;
	register	int	i;
	int		fd;
	char		*fname;

	if (uflag == 0)
		return;
	if (lockpid > 0) {
		lockpid = 0;
		(void) lockfs(filesystem, "unlock");
	}
	if ((df = fopen(increm, "r+")) == NULL) {
		fd = open(increm, O_RDWR | O_CREAT, 0664);
		if (fd == -1) {
			msg("%s: %s\n", increm, strerror(errno));
			dumpabort();
		}
		df = fdopen(fd, "r+");
		if (df == NULL) {
			msg("%s: %s\n", increm, strerror(errno));
			dumpabort();
		}
		msg(gettext("Creating dump record file `%s'\n"), increm);
	}
	fd = fileno(df);
	(void) flock(fd, LOCK_EX);
	fname = disk;
	if (idatev != 0) {
		for (i = 0; i < nidates && idatev[i] != 0; i++)
			free((char *)idatev[i]);
		free((char *)idatev);
	}
	idatev = 0;
	nidates = 0;
	idates_in = 0;
	readitimes(df);
	if (fseek(df, 0L, 0) < 0) {   /* rewind() was redefined in dumptape.c */
		msg(gettext("%s: %s error:\n"),
			increm, "fseek", strerror(errno));
		dumpabort();
	}
	spcl.c_ddate = 0;
	ITITERATE(i, itwalk) {
		if (strncmp(fname, itwalk->id_name,
				sizeof (itwalk->id_name)) != 0)
			continue;
		if (itwalk->id_incno != incno)
			continue;
		goto found;
	}
	/*
	 *	Add one more entry to idatev
	 */
	nidates++;
	idatev = (struct idates **) realloc((void *)idatev,
		(size_t) (nidates * sizeof (struct idates *)));
	itwalk = idatev[nidates - 1] =
		(struct idates *)calloc(1, sizeof (struct idates));
found:
	(void) strncpy(itwalk->id_name, fname, sizeof (itwalk->id_name));
	itwalk->id_incno = incno;
	itwalk->id_ddate = spcl.c_date;

	ITITERATE(i, itwalk) {
		recout(df, itwalk);
	}
	if (ftruncate(fd, ftell(df))) {
		msg(gettext("%s: %s error:\n"),
			increm, "ftruncate", strerror(errno));
		dumpabort();
	}
	(void) fclose(df);
	if (trueinc)
		msg(gettext("True incremental dump on %s\n"),
		    prdate(spcl.c_date, 1));
	else
		msg(gettext("Level %c dump on %s\n"),
		    (u_char)incno, prdate(spcl.c_date, 1));
}

static void
recout(file, what)
	FILE	*file;
	struct	idates	*what;
{
	(void) fprintf(file, DUMPOUTFMT,
		what->id_name,
		(u_char)what->id_incno,
		ctime(&(what->id_ddate)));	/* XXX must be ctime */
}

static int
getrecord(df, idatep)
	FILE	*df;
	struct	idates	*idatep;
{
	char		buf[BUFSIZ];

	if ((fgets(buf, BUFSIZ, df)) != buf)
		return (-1);
	recno++;
	if (makeidate(idatep, buf) < 0) {
		msg(gettext(
		    "Malformed entry in dump record file `%s', line %d\n"),
			increm, recno);
		if (strcmp(increm, NINCREM)) {
			msg(gettext("`%s' not a dump record file\n"), increm);
			dumpabort();
		}
	}

#ifdef FDEBUG
	msg("getrecord: %s %c %s\n",
		idatep->id_name,
		(u_char)idatep->id_incno,
		prdate(idatep->id_ddate, 1));
#endif
	return (0);
}

static int
makeidate(ip, buf)
	struct	idates	*ip;
	char	*buf;
{
	char	un_buf[128];

	(void) sscanf(buf, DUMPINFMT, ip->id_name, &ip->id_incno, un_buf);
	ip->id_ddate = unctime(un_buf);
	if (ip->id_ddate < 0)
		return (-1);
	return (0);
}

/*
 * This is an estimation of the number of TP_BSIZE blocks in the file.
 * It estimates the number of blocks in files with holes by assuming
 * that all of the blocks accounted for by di_blocks are data blocks
 * (when some of the blocks are usually used for indirect pointers);
 * hence the estimate may be high.
 */
void
est(ip)
	struct dinode *ip;
{
	long s, t;

	/*
	 * ip->di_size is the size of the file in bytes.
	 * ip->di_blocks stores the number of sectors actually in the file.
	 * If there are more sectors than the size would indicate, this just
	 *	means that there are indirect blocks in the file or unused
	 *	sectors in the last file block; we can safely ignore these
	 *	(s = t below).
	 * If the file is bigger than the number of sectors would indicate,
	 *	then the file has holes in it.	In this case we must use the
	 *	block count to estimate the number of data blocks used, but
	 *	we use the actual size for estimating the number of indirect
	 *	dump blocks (t vs. s in the indirect block calculation).
	 */
	esize++;
	s = ip->di_blocks / (TP_BSIZE / DEV_BSIZE);
	t = howmany(ip->di_size, TP_BSIZE);
	if (s > t)
		s = t;
	if (ip->di_size > sblock->fs_bsize * NDADDR) {
		/* calculate the number of indirect blocks on the dump tape */
		s += howmany(t - NDADDR * sblock->fs_bsize / TP_BSIZE,
			TP_NINDIR);
	}
	esize += s;
	dbtmpest(ip, s);
}

/*ARGSUSED*/
void
bmapest(map)
	char *map;
{
	esize++;
	esize += howmany(msiz * sizeof (map[0]), TP_BSIZE);
}
