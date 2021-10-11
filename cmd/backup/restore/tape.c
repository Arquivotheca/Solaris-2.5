/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)tape.c	1.47	94/08/10 SMI"

#include <setjmp.h>
#include "restore.h"
#include <config.h>
#include <byteorder.h>
#include <rmt.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <utime.h>
#include <sys/errno.h>
#ifdef USG
/*
 * XXX Hack for FDKEJECT that enables compilation
 * under both jup_alpha3 and jup_alpha5
 */
#include <sys/dkio.h>
#ifndef FDKEJECT
#include <sys/fdio.h>
#define	FDKEJECT	FDEJECT
#endif
#include <sys/sysmacros.h>	/* for expdev */
#else
#include <sun/dkio.h>
#endif

#define	MAXINO	65535		/* KLUDGE */

#define	MAXTAPES	128

static long	fssize = MAXBSIZE;
int mt = -1;
static int	continuemap = 0;
int		nometamucilseek = 0;
static char	device_list[256];
char		magtape[BUFSIZ];
int		pipein = 0;
static char	*archivefile;
static int	bct;
static int	numtrec;
static char	*tbf = NULL;
static int	recsread;
static union	u_spcl endoftapemark;
static struct	s_spcl dumpinfo;
static long	blksread;
static long	tapea;
static u_char	tapesread[MAXTAPES];
static jmp_buf	restart;
static int	gettingfile = 0;	/* restart has a valid frame */

static int	ofile;
static char	*map, *beginmap;
static char	*endmap;
daddr_t		rec_position;
static char	lnkbuf[MAXPATHLEN + 1];
static int	pathlen;
	char	*host;		/* used in dumprmt.c */
static int	inodeinfo;	/* Have starting volume information */
static int	hostinfo;	/* Have dump host information */

extern int metamucil_mounts;

#ifdef __STDC__
static void setdumpnum(void);
static void metacheck(struct s_spcl *);
static void xtrmeta(char *, long);
static void metaskip(char *, long);
static void xtrfile(char *, long);
static void xtrskip(char *, long);
static void xtrlnkfile(char *, long);
static void xtrlnkskip(char *, long);
static void xtrmap(char *, long);
static void xtrmapskip(char *, long);
static void readtape(char *);
static int checkvol(struct s_spcl *, long);
static void accthdr(struct s_spcl *);
static int ishead(struct s_spcl *);
static checktype(struct s_spcl *, int);
static int checksum(int *);
#else
static void setdumpnum();
static void metacheck();
static void xtrmeta();
static void metaskip();
static void xtrfile();
static void xtrskip();
static void xtrlnkfile();
static void xtrlnkskip();
static void xtrmap();
static void xtrmapskip();
static void readtape();
static int checkvol();
static void accthdr();
static int ishead();
static checktype();
static int checksum();
#endif

/*
 * Set up an input source
 */
void
setinput(source, archive)
	char *source;
	char *archive;
{

	flsht();
	archivefile = archive;
	if (bflag)
		newtapebuf(ntrec);
	else
		/* let's hope this is evaluated at compile time! */
		newtapebuf(CARTRIDGETREC > HIGHDENSITYTREC ?
		    (NTREC > CARTRIDGETREC ? NTREC : CARTRIDGETREC) :
		    (NTREC > HIGHDENSITYTREC ? NTREC : HIGHDENSITYTREC));
	terminal = stdin;
	if (metamucil_mode == METAMUCIL && source && *source == '+') {
		static char sourcedev[1024];

		(void) strcpy(device_list, source);
		source = (char *)getdevice();
		(void) strcpy(sourcedev, source);
		source = sourcedev;
	}
	metamucil_setinput(source, archive);
}


#ifdef USG
/*
 * XXX: for SVr4
 */
#define	setreuid(a, b)	seteuid(b)
#endif

void
metamucil_setinput(source, archive)
	char *source;
	char *archive;
{
	char *p;

	if (source == NULL) {
		/*
		 * invoked from recover with no device specification.
		 * Don't bother to default
		 */
		host = NULL;
		magtape[0] = '\0';
		if (setreuid(-1, getuid()) == -1)
			perror("setreuid");
		return;
	}

	if (strchr(source, ':')) {
		char *tape;

		/*
		 * In metamucil, we toggle between the real uid of
		 * the process and root.  This is necessary since
		 * we must be root to establish a rmt connection, but
		 * we don't wish to let arbitrary users take advantage
		 * of the fact that this is a setuid program while
		 * restoring files.
		 */
		if (setreuid(-1, 0) == -1)
			perror("setreuid");
		host = source;
		tape = strchr(host, ':');
		*tape++ = '\0';
		(void) strcpy(magtape, tape);
		if (rmthost(host, ntrec) == 0)
			done(1);
		/*
		 * done with rmt setup, make effective uid match
		 * real uid.
		 */
		if (setreuid(-1, getuid()) == -1)
			perror("setreuid");
	} else {
		host = NULL;
		if (setreuid(-1, getuid()) == -1)
			perror("setreuid");
		if (strcmp(source, "-") == 0) {
			/*
			 * Since input is coming from a pipe we must establish
			 * our own connection to the terminal.
			 */
			terminal = fopen("/dev/tty", "r");
			if (terminal == NULL) {
				perror(gettext("Cannot open(\"/dev/tty\")"));
				terminal = fopen("/dev/null", "r");
				if (terminal == NULL) {
					perror(gettext(
						"Cannot open(\"/dev/null\")"));
					done(1);
				}
			}
			pipein++;
			if (archive) {
				(void) fprintf(stderr, gettext(
		"Cannot specify an archive file when reading from a pipe"));
				done(1);
			}
		}
		(void) strcpy(magtape, source);
	}
}

void
newtapebuf(size)
	long size;
{
	static tbfsize = -1;

	ntrec = size;
	if (size <= tbfsize)
		return;
	if (tbf != NULL)
		free(tbf);
	tbf = (char *)malloc(size * TP_BSIZE);
	if (tbf == NULL) {
		(void) fprintf(stderr,
			gettext("Cannot allocate space for buffer\n"));
		done(1);
	}
	tbfsize = size;
}

/*
 * Verify that the tape drive can be accessed and
 * that it actually is a dump tape.
 */
void
#ifdef __STDC__
setup(void)
#else
setup()
#endif
{
	int i, j, *ip;
	struct stat stbuf;
	unsigned mapsize;

	if (metamucil_mounts) {
		rec_position = 0;
		metamucil_getvol(&spcl);
		volno = 1;
		if (dumpnum > 1) {
			setdumpnum();
			flsht();
		} else {
			bct--;  /* since we do gethead() again below... */
			blksread--;
			tapea--;
		}
		goto proceed;
	}
	vprintf(stdout, gettext("Verify volume and initialize maps\n"));
	if (archivefile) {
		if ((mt = open(archivefile, 0)) < 0) {
			(void) fprintf(stderr, "%s: %s\n", archivefile,
					strerror(errno));
			done(1);
		}
		volno = 0;
	} else if (host) {
		if ((mt = rmtopen(magtape, 0)) < 0) {
			(void) fprintf(stderr, "%s: %s\n", magtape,
					strerror(errno));
			done(1);
		}
		volno = 1;
	} else {
		if (pipein)
			mt = 0;
		else if ((mt = open(magtape, 0)) < 0) {
			(void) fprintf(stderr, "%s: %s\n", magtape,
					strerror(errno));
			done(1);
		}
		volno = 1;
	}
	setdumpnum();
	flsht();
	if (!pipein && !bflag)
		if (archivefile)
			findtapeblksize(ARCHIVE_FILE);
		else
			findtapeblksize(TAPE_FILE);
proceed:
	if (gethead(&spcl) == FAIL) {
		bct--; /* push back this block */
		blksread--;
		tapea--;
		cvtflag++;
		if (gethead(&spcl) == FAIL) {
			(void) fprintf(stderr,
				gettext("Volume is not in dump format\n"));
			done(1);
		}
		(void) fprintf(stderr,
			gettext("Converting to new file system format.\n"));
	}
	if (vflag)
		byteorder_banner(byteorder, stdout);
	if (pipein) {
		endoftapemark.s_spcl.c_magic = cvtflag ? OFS_MAGIC : NFS_MAGIC;
		endoftapemark.s_spcl.c_type = TS_END;

		/*
		 * include this since the `resync' loop in findinode
		 * expects to find a header with the c_date field
		 * filled in.
		 */
		endoftapemark.s_spcl.c_date = spcl.c_date;

		ip = (int *)&endoftapemark;
		j = sizeof (union u_spcl) / sizeof (int);
		i = 0;
		do
			i += *ip++;
		while (--j);
		endoftapemark.s_spcl.c_checksum = CHECKSUM - i;
	}
	if (vflag && command != 't')
		printdumpinfo();
	dumptime = spcl.c_ddate;
	dumpdate = spcl.c_date;
	if (stat(".", &stbuf) < 0) {
		perror(gettext("cannot stat ."));
		done(1);
	}
	if (stbuf.st_blksize >= TP_BSIZE && stbuf.st_blksize <= MAXBSIZE)
		fssize = stbuf.st_blksize;
	if (((fssize - 1) & fssize) != 0) {
		(void) fprintf(stderr, gettext("bad block size %d\n"), fssize);
		done(1);
	}
	if (checkvol(&spcl, (long)1) == FAIL) {
		(void) fprintf(stderr,
			gettext("This is not volume 1 of the dump\n"));
		done(1);
	}
	if (readhdr(&spcl) == FAIL)
		panic(gettext("no header after volume mark!\n"));
	findinode(&spcl);	/* sets curfile, resyncs the tape */
	if (checktype(&spcl, TS_CLRI) == FAIL) {
		(void) fprintf(stderr,
			gettext("Cannot find file removal list\n"));
		done(1);
	}
	maxino = (spcl.c_count * TP_BSIZE * NBBY) + 1;
	dprintf(stdout, "maxino = %lu\n", maxino);
	/*
	 * Allocate space for at least MAXINO inodes to allow us
	 * to restore partial dump tapes written before dump was
	 * fixed to write out the entire inode map.
	 */
	mapsize = howmany(maxino > MAXINO ? maxino : MAXINO, NBBY);
	beginmap = map = calloc((unsigned)1, mapsize);
	if (map == (char *)NIL)
		panic(gettext("no memory for file removal list\n"));
	endmap = map + mapsize;
	clrimap = map;
	curfile.action = USING;
	continuemap = 1;
	getfile(xtrmap, xtrmapskip);
	if (MAXINO > maxino)
		maxino = MAXINO;
	if (checktype(&spcl, TS_BITS) == FAIL) {
		/* if we have TS_CLRI then no TS_BITS then a TS_END */
		/* then we have an empty dump file */
		if (gethead(&spcl) == GOOD && checktype(&spcl, TS_END) == GOOD)
			done(0);
		/* otherwise we have an error */
		(void) fprintf(stderr, gettext("Cannot find file dump list\n"));
		done(1);
	}
	mapsize = howmany(maxino, NBBY);
	beginmap = map = calloc((unsigned)1, mapsize);
	if (map == (char *)NULL)
		panic(gettext("no memory for file dump list\n"));
	endmap = map + mapsize;
	dumpmap = map;
	curfile.action = USING;
	continuemap = 1;
	getfile(xtrmap, xtrmapskip);
	continuemap = 0;
}

/*
 * Initialize fssize variable for 'R' command to work.
 */
void
#ifdef __STDC__
setupR(void)
#else
setupR()
#endif
{
	struct stat stbuf;

	if (stat(".", &stbuf) < 0) {
		perror(gettext("cannot stat ."));
		done(1);
	}
	if (stbuf.st_blksize >= TP_BSIZE && stbuf.st_blksize <= MAXBSIZE)
		fssize = stbuf.st_blksize;
	if (((fssize - 1) & fssize) != 0) {
		(void) fprintf(stderr, gettext("bad block size %d\n"), fssize);
		done(1);
	}
}

/*
 * Prompt user to load a new dump volume.
 * "Nextvol" is the next suggested volume to use.
 * This suggested volume is enforced when doing full
 * or incremental restores, but can be overrridden by
 * the user when only extracting a subset of the files.
 *
 * first_time is used with archive files and can have 1 of 3 states:
 *	FT_STATE_1	Tape has not been read yet
 *	FT_STATE_2	Tape has been read but not positioned past directory
 *			information
 *	FT_STATE_3	Tape has been read and is reading file information
 */
#define	FT_STATE_1	1
#define	FT_STATE_2	2
#define	FT_STATE_3	3

void
getvol(nextvol)
	long nextvol;
{
	long newvol, savecnt, savetapea, wantnext, i;
	union u_spcl tmpspcl;
#define	tmpbuf tmpspcl.s_spcl
	char buf[TP_BSIZE];
	static int first_time = FT_STATE_1;

	if (nextvol == 1) {
		for (i = 0;  i < MAXTAPES;  i++)
			tapesread[i] = 0;
		gettingfile = 0;
	}
	if (pipein) {
		if (nextvol != 1)
			panic(gettext("changing volumes on pipe input\n"));
		if (volno == 1)
			return;
		goto gethdr;
	}
	savecnt = blksread;
	savetapea = tapea;
again:
	if (pipein)
		done(1); /* pipes do not get a second chance */
	if (command == 'R' || command == 'r' || curfile.action != SKIP) {
		wantnext = 1;
		newvol = nextvol;
	} else {
		wantnext = 0;
		newvol = 0;
	}
	if (!metamucil_mounts)
	while (newvol <= 0) {
		int n = 0;

		for (i = 0;  i < MAXTAPES;  i++)
			if (tapesread[i])
				n++;
		if (n == 0) {
			(void) fprintf(stderr, "%s", gettext(
"You have not read any volumes yet.\n\
Unless you know which volume your file(s) are on you should start\n\
with the last volume and work towards the first.\n"));
		} else {
			(void) fprintf(stderr,
				gettext("You have read volumes"));
			(void) strcpy(tbf, ": ");
			for (i = 0; i < MAXTAPES; i++)
				if (tapesread[i]) {
					(void) fprintf(stderr,
						"%s%ld", tbf, i+1);
					(void) strcpy(tbf, ", ");
				}
			(void) fprintf(stderr, "\n");
		}
		do	{
			(void) fprintf(stderr,
				gettext("Specify next volume #: "));
			(void) fflush(stderr);
			(void) fgets(tbf, BUFSIZ, terminal);
		} while (!feof(terminal) && tbf[0] == '\n');
		if (feof(terminal))
			done(1);
		newvol = atoi(tbf);
		if (newvol <= 0) {
			(void) fprintf(stderr, gettext(
				"Volume numbers are positive numerics\n"));
		}
		if (newvol > MAXTAPES) {
			(void) fprintf(stderr, gettext(
				"This program can only deal with %d volumes\n"),
				MAXTAPES);
			newvol = 0;
		}
	}
	if (metamucil_mounts) {
		if (nextvol == 1 && volno == 1)
			return;
		flsht();
		metamucil_getvol(&tmpbuf);
		volno = tmpbuf.c_volume;
		goto proceed;
	}
	if (newvol == volno) {
		tapesread[volno-1]++;
		return;
	}
	closemt();
	/*
	 * XXX: if we are switching devices, we should probably try
	 * the device once without prompting to enable unattended
	 * operation.
	 */
	get_next_device();
	if (host)
		(void) fprintf(stderr, gettext("Mount volume %d\n\
then enter volume name on host %s (default: %s) "),
			newvol, host,  magtape);
	else
		(void) fprintf(stderr, gettext("Mount volume %d\n\
then enter volume name (default: %s) "),
			newvol, magtape);
	(void) fflush(stderr);
	(void) fgets(tbf, BUFSIZ, terminal);
	if (feof(terminal))
		done(1);
	if (tbf[0] != '\n') {
		(void) strcpy(magtape, tbf);
		magtape[strlen(magtape) - 1] = '\0';
	}
	if ((host != 0 && (mt = rmtopen(magtape, 0)) == -1) ||
	    (host == 0 && (mt = open(magtape, 0)) == -1)) {
		(void) fprintf(stderr, gettext("Cannot open %s\n"), magtape);
		volno = -1;
		goto again;
	}
gethdr:
	volno = newvol;
	setdumpnum();
	flsht();
	if (!pipein && !bflag && archivefile && (first_time == FT_STATE_1)) {
		first_time = FT_STATE_2;
		findtapeblksize(TAPE_FILE);
	}
	if (readhdr(&tmpbuf) == FAIL) {
		(void) fprintf(stderr,
			gettext("volume is not in dump format\n"));
		volno = 0;
		goto again;
	}
	if (checkvol(&tmpbuf, volno) == FAIL) {
		(void) fprintf(stderr, gettext("Wrong volume (%d)\n"),
			tmpbuf.c_volume);
		volno = 0;
		goto again;
	}
proceed:
	if (tmpbuf.c_date != dumpdate || tmpbuf.c_ddate != dumptime) {
		char *tmp_ct;	/*
				 * This is used to save return
				 * value from lctime().
				 */
		tmp_ct = strdup(lctime(&tmpbuf.c_date));
		if (tmp_ct == (char *)0) {
			(void) fprintf(stderr, gettext(
				"Cannot allocate space for time string\n"));
			done(1);
		}

		(void) fprintf(stderr,
			gettext("Wrong dump date\n\tgot: %s\twanted: %s"),
			tmp_ct,  lctime(&dumpdate));
		volno = 0;
		free(tmp_ct);
		goto again;
	}
	tapesread[volno-1]++;
	blksread = savecnt;
	tapea = savetapea;
	/*
	 * If continuing from the previous volume, skip over any
	 * blocks read already at the end of the previous volume.
	 *
	 * If coming to this volume at random, skip to the beginning
	 * of the next record.
	 */
	if (tmpbuf.c_type == TS_TAPE && (tmpbuf.c_flags & DR_NEWHEADER)) {
		if (!wantnext) {
			if (archivefile && first_time == FT_STATE_2) {
				first_time = FT_STATE_3;
			}
			recsread = tmpbuf.c_firstrec;
			tapea = tmpbuf.c_tapea;
			for (i = tmpbuf.c_count; i > 0; i--)
				readtape(buf);
		} else if (tmpbuf.c_firstrec != 0) {
			savecnt = blksread;
			savetapea = tapea;

			if (archivefile && first_time == FT_STATE_2) {
			/* subtract 2, 1 for archive file's TS_END and 1 for */
			/* tape's TS_TAPE */
				first_time = FT_STATE_3;
				i = tapea - tmpbuf.c_tapea - 2;
			} else {
				i = tapea - tmpbuf.c_tapea;
			}
			if (i > 0)
				dprintf(stdout, gettext(
				    "restore skipping %d duplicate records\n"),
					i);
			else if (i < 0)
				dprintf(stdout, gettext(
				    "restore duplicate record botch (%d)\n"),
					i);
			while (--i >= 0)
				readtape(buf);
			blksread = savecnt;
			tapea = savetapea + 1; /* <= (void) gethead() below */
		}
	}
	if (curfile.action == USING) {
		if (volno == 1)
			panic(gettext("active file into volume 1\n"));
		return;
	}
	(void) gethead(&spcl);
	findinode(&spcl); /* do we always restart files in full? */
	if (gettingfile) { /* i.e. will we lose metadata? */
		gettingfile = 0;
		longjmp(restart, 1); /* will this set f1 & f2? */
	}
}

/*
 * handle multiple dumps per tape by skipping forward to the
 * appropriate one.
 */
static void
#ifdef __STDC__
setdumpnum(void)
#else
setdumpnum()
#endif
{
	struct mtop tcom;

	if (dumpnum == 1 || volno != 1)
		return;
	if (pipein) {
		(void) fprintf(stderr,
			gettext("Cannot have multiple dumps on pipe input\n"));
		done(1);
	}
	tcom.mt_op = MTFSF;
	tcom.mt_count = dumpnum - 1;
	if (host)
		(void) rmtioctl(MTFSF, dumpnum - 1);
	else
		if (ioctl(mt, (int)MTIOCTOP, (char *)&tcom) < 0)
			perror("ioctl MTFSF");
}

void
#ifdef __STDC__
printdumpinfo(void)
#else
printdumpinfo()
#endif
{
	int i;

	(void) fprintf(stdout,
		gettext("Dump   date: %s"), lctime(&dumpinfo.c_date));
	(void) fprintf(stdout, gettext("Dumped from: %s"),
	    (dumpinfo.c_ddate == 0) ? gettext("the epoch\n") :
	    lctime(&dumpinfo.c_ddate));
	if (hostinfo) {
		(void) fprintf(stdout,
		    gettext("Level %d dump of %s on %s:%s\n"),
			dumpinfo.c_level, dumpinfo.c_filesys,
		    dumpinfo.c_host, dumpinfo.c_dev);
		(void) fprintf(stdout,
			gettext("Label: %s\n"), dumpinfo.c_label);
	}
	if (inodeinfo) {
		(void) fprintf(stdout,
			gettext("Starting inode numbers by volume:\n"));
		for (i = 1; i <= dumpinfo.c_volume; i++)
			(void) fprintf(stdout, gettext("\tVolume %d: %6d\n"),
			    i, dumpinfo.c_inos[i]);
	}
}

extractfile(name)
	char *name;
{
	mode_t mode;
	time_t timep[2];
	struct entry *ep;

	metamucil_extract_msg();

	curfile.name = name;
	curfile.action = USING;
	timep[0] = curfile.dip->di_atime;
	timep[1] = curfile.dip->di_mtime;
	mode = curfile.dip->di_mode;
	switch (mode & IFMT) {

	default:
		(void) fprintf(stderr, gettext("%s: unknown file mode 0%lo\n"),
			name, (u_long)mode);
		skipfile();
		return (FAIL);

	case IFSOCK:
		vprintf(stdout, gettext("skipped socket %s\n"), name);
		skipfile();
		return (GOOD);

	case IFDIR:
		if (mflag) {
			ep = lookupname(name);
			if (ep == NIL || ep->e_flags & EXTRACT)
				panic(gettext("unextracted directory %s\n"),
					name);
			skipfile();
			return (GOOD);
		}
		vprintf(stdout, gettext("extract file %s\n"), name);
		return (genliteraldir(name, curfile.ino));

	case IFLNK:
		lnkbuf[0] = '\0';
		pathlen = 0;
		getfile(xtrlnkfile, xtrlnkskip);
		if (pathlen == 0) {
			vprintf(stdout, gettext(
				"%s: zero length symbolic link (ignored)\n"),
				name);
			return (GOOD);
		}
		if (linkit(lnkbuf, name, SYMLINK) != GOOD)
			return (FAIL);
		metaset(name);
		return (GOOD);

	case IFCHR:
	case IFBLK:
	case IFIFO:
		vprintf(stdout, gettext("extract special file %s\n"), name);
#ifdef USG
		/* put device rdev into dev_t expanded format */
		if (((curfile.dip->di_ordev & 0xFFFF0000) == 0) ||
		    ((curfile.dip->di_ordev & 0xFFFF0000) == 0xFFFF0000)) {
			curfile.dip->di_ordev =
			    expdev(curfile.dip->di_ordev);
		}
		if (mknod(name, mode, (dev_t)curfile.dip->di_ordev) < 0) {
#else
		if (mknod(name, (int)mode, (int)curfile.dip->di_rdev) < 0) {
#endif
		    struct stat s[1];

		    if ((stat(name, s)) ||
			((s->st_mode & S_IFMT) != (mode & S_IFMT)) ||
			(s->st_rdev != curfile.dip->di_ordev)) {
			    (void) fprintf(stderr, "%s: ", name);
			    (void) fflush(stderr);
			    perror(gettext("cannot create special file"));
			    skipfile();
			    return (FAIL);
		    }
		}
#ifdef USG
		(void) chown(name,
			curfile.dip->di_suid == UID_LONG ?
			    curfile.dip->di_uid : (uid_t)curfile.dip->di_suid,
			curfile.dip->di_sgid == GID_LONG ?
			    curfile.dip->di_gid : (gid_t)curfile.dip->di_sgid);
#else
		(void) chown(name, curfile.dip->di_uid, curfile.dip->di_gid);
#endif
		(void) chmod(name, mode);
		skipfile();
		metaset(name); /* skipfile() got the metadata */
		utime(name, (struct utimbuf *)timep);
		return (GOOD);

	case IFREG:
		vprintf(stdout, gettext("extract file %s\n"), name);
		if ((ofile = creat(name, 0666)) < 0) {
			(void) fprintf(stderr, "%s: ", name);
			(void) fflush(stderr);
			perror(gettext("cannot create file"));
			skipfile();
			return (FAIL);
		}
#ifdef USG
		(void) fchown(ofile,
			curfile.dip->di_suid == UID_LONG ?
			    curfile.dip->di_uid : (uid_t)curfile.dip->di_suid,
			curfile.dip->di_sgid == GID_LONG ?
			    curfile.dip->di_gid : (gid_t)curfile.dip->di_sgid);
#else
		(void) fchown(ofile, curfile.dip->di_uid, curfile.dip->di_gid);
#endif
		(void) fchmod(ofile, mode);
		getfile(xtrfile, xtrskip);
		metaset(name);	/* we don't have metadata until after */
				/* getfile() - maybe fchmod(0) then */
				/* fchmod(real) after this? */
		(void) close(ofile);
		utime(name, (struct utimbuf *)timep);
		return (GOOD);
	}
	/* NOTREACHED */
}

/*
 * skip over bit maps on the tape
 */
void
#ifdef __STDC__
skipmaps(void)
#else
skipmaps()
#endif
{
	continuemap = 1;
	while (checktype(&spcl, TS_CLRI) == GOOD ||
	    checktype(&spcl, TS_BITS) == GOOD)
		skipfile();
	continuemap = 0;
}

/*
 * skip over a file on the tape
 */
void
#ifdef __STDC__
skipfile(void)
#else
skipfile()
#endif
{
	curfile.action = SKIP;
	getfile(null, null);
}

/*
 * Do the file extraction, calling the supplied functions
 * with the blocks
 */
void
getfile(f1, f2)
	void	(*f2)(), (*f1)();
{
	register int i;
	int curblk = 0;
	off_t size = spcl.c_dinode.di_size;
	static char clearedbuf[MAXBSIZE];
	char buf[MAXBSIZE / TP_BSIZE][TP_BSIZE];
	char junk[TP_BSIZE];

	metaset(NULL); /* flush old metadata */
	if (checktype(&spcl, TS_END) == GOOD)
		panic(gettext("ran off end of volume\n"));
	if (ishead(&spcl) == FAIL)
		panic(gettext("not at beginning of a file\n"));
	metacheck(&spcl); /* check for metadata in header */
	if (!gettingfile && setjmp(restart) != 0)
		return;
	gettingfile++;
loop:
	if ((spcl.c_dinode.di_mode & IFMT) == IFSHAD) {
		f1 = xtrmeta;
		f2 = metaskip;
	}
	for (i = 0; i < spcl.c_count; i++) {
		if ((i >= TP_NINDIR) || (spcl.c_addr[i])) {
			readtape(&buf[curblk++][0]);
			if (curblk == fssize / TP_BSIZE) {
				(*f1)(buf, size > TP_BSIZE ?
				    (long) (fssize) :
				    (curblk - 1) * TP_BSIZE + size);
				curblk = 0;
			}
		} else {
			if (curblk > 0) {
				(*f1)(buf, size > TP_BSIZE ?
				    (long) (curblk * TP_BSIZE) :
				    (curblk - 1) * TP_BSIZE + size);
				curblk = 0;
			}
			(*f2)(clearedbuf, size > TP_BSIZE ?
				(long) TP_BSIZE : size);
		}
		if ((size -= TP_BSIZE) <= 0) {
			for (i++; i < spcl.c_count; i++)
				if ((i >= TP_NINDIR) || (spcl.c_addr[i]))
					readtape(junk);
			break;
		}
	}
	if (curblk > 0) {
		(*f1)(buf, (curblk * TP_BSIZE) + size);
		curblk = 0;
	}
	if ((readhdr(&spcl) == GOOD) && (checktype(&spcl, TS_ADDR) == GOOD)) {
		if (continuemap)
			size = spcl.c_count * TP_BSIZE; /* big bitmap */
		else if ((size <= 0) &&
		    ((spcl.c_dinode.di_mode & IFMT) == IFSHAD))
			size = spcl.c_dinode.di_size;

		if (size > 0)
			goto loop;
	}
	if (size > 0)
		dprintf(stdout,
		    gettext("Missing address (header) block for %s\n"),
		    curfile.name);
	findinode(&spcl);
	gettingfile = 0;
}

/*
 * The next routines are called during file extraction to
 * put the data into the right form and place.
 */
static void
xtrfile(buf, size)
	char	*buf;
	long	size;
{

	if (write(ofile, buf, (int) size) == -1) {
		(void) fprintf(stderr,
			gettext("write error extracting inode %d, name %s\n"),
			curfile.ino, curfile.name);
		perror("write");
		done(1);
	}
}

static void
xtrskip(buf, size)
	char *buf;
	long size;
{

#ifdef	lint
	buf = buf;
#endif
	if (lseek(ofile, size, 1) == (long)-1) {
		(void) fprintf(stderr,
			gettext("seek error extracting inode %d, name %s\n"),
			curfile.ino, curfile.name);
		perror("lseek");
		done(1);
	}
}

/* these are local to the next five functions */
static char *metadata = NULL;
static long metasize = 0;

static void
metacheck(head)
	struct s_spcl *head;
{
	if (! (head->c_flags & DR_HASMETA))
		return;
	if ((metadata = malloc(metasize = sizeof (union u_shadow))) == NULL)
		panic(gettext("Cannot malloc for metadata\n"));
	bcopy(&(head->c_shadow), metadata, metasize);
}

static void
xtrmeta(buf, size)
	char *buf;
	long size;
{
	if ((metadata == NULL) && ((spcl.c_dinode.di_mode & IFMT) != IFSHAD))
		return;
	if ((metadata = realloc(metadata, metasize + size)) == NULL)
		panic(gettext("Cannot malloc for metadata\n"));
	bcopy(buf, metadata + metasize, size);
	metasize += size;
}

static void
metaskip(buf, size)
	char *buf;
	long size;
{
	if (metadata == NULL)
		return;
	if ((metadata = realloc(metadata, metasize + size)) == NULL)
		panic(gettext("Cannot malloc for metadata\n"));
	bzero(metadata + metasize, size);
	metasize += size;
}

static void
metaset(name)
	char *name;
{
	if (metadata == NULL)
		return;
	if (name != NULL)
		metaproc(name, metadata, metasize);
	(void) free(metadata);
	metadata = NULL;
	metasize = 0;
}

void
metaget(data, size)
	char **data;
	long *size;
{
	*data = metadata;
	*size = metasize;
}

void
fsd_acl(name, aclp, size)
	char *name, *aclp;
	unsigned size;
{
	static aclent_t *aclent = NULL;
	ufs_acl_t *diskacl;
	static int n = 0;
	int i, j;

	if (aclp == NULL) {
		if (aclent != NULL)
			free(aclent);
		aclent = NULL;
		n = 0;
		return;
	}

	diskacl = (ufs_acl_t *)aclp;
	j = size / sizeof (*diskacl);
	normacls(byteorder, diskacl, j);

	i = n;
	n += j;
	aclent = realloc(aclent, n * sizeof (*aclent));
	if (aclent == NULL)
		panic("Cannot malloc acl list\n");

	j = 0;
	while (i < n) {
		aclent[i].a_type = diskacl[j].acl_tag;
		aclent[i].a_id = diskacl[j].acl_who;
		aclent[i].a_perm = diskacl[j].acl_perm;
		++i;
		++j;
	}
	if (acl(name, SETACL, n, aclent) != n) {
		static int once = 0;

		if ((errno == ENOSYS) || (errno == EINVAL)) {
			if (once == 0) {
			    ++once;
			    fprintf(stderr,
				    gettext("setacl failed: %s\n"),
				    strerror(errno));
			}
		} else
			fprintf(stderr, gettext("setacl on %s failed: %s\n"),
				name, strerror(errno));
	}
}

struct fsdtypes {
	int type;
	void (*function)();
} fsdtypes[] = {
	{FSD_ACL, fsd_acl},
	{FSD_DFACL, fsd_acl},

	{0, NULL}
};

void
metaproc(name, metadata, metasize)
	char *name, *metadata;
	long metasize;
{
	struct fsdtypes *fsdtype;
	ufs_fsd_t *fsd;
	char *c;

	/* for the whole shadow inode, dispatch each piece	*/
	/* to the appropriate function.				*/
	c = metadata;
	while ((c - metadata) < metasize) {
		fsd = (ufs_fsd_t *)c;
		c += fsd->fsd_size;
		if ((fsd->fsd_type == FSD_FREE) ||
		    (fsd->fsd_size <= sizeof (ufs_fsd_t)) ||
		    (c > (metadata + metasize)))
			break;
		for (fsdtype = fsdtypes; fsdtype->type; fsdtype++)
			if (fsdtype->type == fsd->fsd_type)
				(*fsdtype->function)(name, fsd->fsd_data,
	fsd->fsd_size - sizeof (fsd->fsd_type) - sizeof (fsd->fsd_size));
		/* ^^^ be sure to change if fsd ever changes ^^^ */
	}

	/* reset the state of all the functions */
	for (fsdtype = fsdtypes; fsdtype->type; fsdtype++)
		(*fsdtype->function)(NULL, NULL, 0);
}

static void
xtrlnkfile(buf, size)
	char	*buf;
	long	size;
{

	pathlen += size;
	if (pathlen > MAXPATHLEN) {
		(void) fprintf(stderr,
			gettext("symbolic link name: %s->%s%s; too long %d\n"),
			curfile.name, lnkbuf, buf, pathlen);
		done(1);
	}
	buf[size] = '\0';
	(void) strcat(lnkbuf, buf);
}

static void
xtrlnkskip(buf, size)
	char *buf;
	long size;
{

#ifdef	lint
	buf = buf, size = size;
#endif
	(void) fprintf(stderr,
		gettext("unallocated block in symbolic link %s\n"),
		curfile.name);
	done(1);
}

static void
xtrmap(buf, size)
	char	*buf;
	long	size;
{
	/*
	 * in metamucil, bit maps may be extended using TS_ADDR
	 * records.
	 */
	if ((map+size) > endmap) {
		int mapsize, increment;
		int diff;

		increment = howmany(
			((spcl.c_count * TP_BSIZE * NBBY) + 1), NBBY);
		mapsize = endmap - beginmap + increment;
		if (spcl.c_type != TS_ADDR)
			panic(gettext("%s: current record not TS_ADDR"),
				"xtrmap");
		diff = map - beginmap;
		beginmap = realloc(beginmap, mapsize);
		if (beginmap == NULL)
			panic("xtrmap: realloc");
		map = beginmap + diff;
		endmap = beginmap + mapsize;
		(void) bzero(map, endmap - map);
		maxino = NBBY * mapsize + 1;
	}
	bcopy(buf, map, size);
	map += size;
}

/*ARGSUSED*/
static void
xtrmapskip(buf, size)
	char *buf;
	long size;
{
	panic(gettext("hole in map\n"));
}

/*ARGSUSED*/
void
null(buf, size)
	char *buf;
	long size;
{
	return;
}

/*
 * Do the tape i/o, dealing with volume changes
 * etc..
 */
static void
readtape(b)
	char *b;
{
	register long i;
	long rd, newvol;
	int cnt;
	struct s_spcl *sp;

top:
	if (bct < numtrec) {
		/*
		 * check for old-dump floppy EOM -- it may appear in
		 * the middle of a buffer.  The Dflag used to be used for
		 * this, but since it doesn't hurt to always do this we
		 * got rid of the Dflag.
		 */
		/*LINTED [tbf = malloc()]*/
		sp = &((union u_spcl *)&tbf[bct*TP_BSIZE])->s_spcl;
		if (sp->c_magic == NFS_MAGIC && sp->c_type == TS_EOM &&
		    sp->c_date == dumpdate && sp->c_ddate == dumptime) {
			for (i = 0; i < ntrec; i++)
				/*LINTED [tbf = malloc()]*/
				((struct s_spcl *)
					&tbf[i*TP_BSIZE])->c_magic = 0;
			bct = 0;
			cnt = ntrec*TP_BSIZE;
			rd = 0;
			i = 0;
			goto nextvol;
		}
		bcopy(&tbf[(bct++*TP_BSIZE)], b, (long)TP_BSIZE);
		blksread++;
		tapea++;
		return;
	}
	for (i = 0; i < ntrec; i++)
		/*LINTED [tbf = malloc()]*/
		((struct s_spcl *)&tbf[i*TP_BSIZE])->c_magic = 0;
	if (numtrec == 0)
		numtrec = ntrec;
	cnt = ntrec*TP_BSIZE;
	rd = 0;
getmore:
	if (host)
		i = rmtread(&tbf[rd], cnt);
	else
		i = read(mt, &tbf[rd], cnt);
	/*
	 * Check for mid-tape short read error.
	 * If found, return rest of buffer.
	 */
	if (numtrec < ntrec && i != 0) {
		numtrec = ntrec;
		goto top;
	}
	/*
	 * Handle partial block read.
	 */
	if (i > 0 && i != ntrec*TP_BSIZE) {
		if (pipein) {
			rd += i;
			cnt -= i;
			if (cnt > 0)
				goto getmore;
			i = rd;
		} else {
			if (i % TP_BSIZE != 0)
				panic(gettext(
				    "partial block read: %d should be %d\n"),
					i, ntrec * TP_BSIZE);
			numtrec = i / TP_BSIZE;
			if (numtrec == 0)
				/*
				 * it's possible to read only 512 bytes
				 * from a QIC device...
				 */
				i = 0;
		}
	}
	/*
	 * Handle read error.
	 */
	if (i < 0) {
		switch (curfile.action) {
		default:
			(void) fprintf(stderr, gettext(
				"Read error while trying to set up volume\n"));
			break;
		case UNKNOWN:
			(void) fprintf(stderr, gettext(
				"Read error while trying to resynchronize\n"));
			break;
		case USING:
			(void) fprintf(stderr, gettext(
				"Read error while restoring %s\n"),
				curfile.name);
			break;
		case SKIP:
			(void) fprintf(stderr, gettext(
				"Read error while skipping over inode %d\n"),
				curfile.ino);
			break;
		}
		if (!yflag && !reply(gettext("continue")))
			done(1);
		i = ntrec*TP_BSIZE;
		(void) bzero(tbf, i);
		if ((host != 0 && rmtseek(i, 1) < 0) ||
		    (host == 0 && lseek(mt, i, 1) == (long)-1)) {
			perror(gettext("continuation failed"));
			done(1);
		}
	}
	/*
	 * Handle end of tape.  The Dflag used to be used, but since it doesn't
	 * hurt to always check we got rid if it.
	 */

	/*
	 * if the first record in the buffer just read is EOM,
	 * change volumes.
	 */
	/*LINTED [tbf = malloc()]*/
	sp = &((union u_spcl *)tbf)->s_spcl;
	if (i != 0 && sp->c_magic == NFS_MAGIC && sp->c_type == TS_EOM &&
	    sp->c_date == dumpdate && sp->c_ddate == dumptime) {
		i = 0;
	}
nextvol:
	if (i == 0) {
		if (!pipein) {
			newvol = volno + 1;
			volno = 0;
			numtrec = 0;
			getvol(newvol);
			readtape(b);
			return;
		}
		if (rd % TP_BSIZE != 0)
			panic(gettext("partial block read: %d should be %d\n"),
				rd, ntrec * TP_BSIZE);
		bcopy((char *)&endoftapemark, &tbf[rd], (long)TP_BSIZE);
	}
	bct = 0;
	bcopy(&tbf[(bct++*TP_BSIZE)], b, (long)TP_BSIZE);
	blksread++;
	recsread++;
	tapea++;
	rec_position++;
}

void
#ifdef __STDC__
findtapeblksize(int arfile)
#else
findtapeblksize(arfile)
int arfile;
#endif
{
	struct mtget mtget[1];
	register long i;

	for (i = 0; i < ntrec; i++)
		/*LINTED [tbf = malloc()]*/
		((struct s_spcl *)&tbf[i * TP_BSIZE])->c_magic = 0;
	bct = 0;
	if (host && arfile == TAPE_FILE)
		i = rmtread(tbf, ntrec * TP_BSIZE);
	else
		i = read(mt, tbf, ntrec * TP_BSIZE);
	recsread++;
	rec_position++;
	if (i <= 0) {
		perror(gettext("Media read error"));
		done(1);
	}
	if (i % TP_BSIZE != 0) {
		(void) fprintf(stderr, gettext(
		"Record size (%d) is not a multiple of dump block size (%d)\n"),
			i, TP_BSIZE);
		done(1);
	}
	ntrec = i / TP_BSIZE;
	numtrec = ntrec;
	vprintf(stdout, gettext("Media block size is %d\n"), ntrec*2);

	if (host)
		i = rmtstatus(mtget);
	else
		i = ioctl(mt, MTIOCGET, (char *)mtget);
	if ((i < 0) || (mtget->mt_blkno != 1))
		++nometamucilseek;
#ifdef DEBUG
	(void) printf("Fast skipping %s\n", (nometamucilseek) ?
	    "disabled" : "enabled");
#endif /* DEBUG */
}

void
#ifdef __STDC__
flsht(void)
#else
flsht()
#endif
{
	bct = ntrec+1;
}

void
#ifdef __STDC__
closemt(void)
#else
closemt()
#endif
{
	if (mt < 0)
		return;
	if (host)
		rmtclose();
	else if (pipein) {
		char buffy[BUFSIZ];

		while (read(mt, buffy, sizeof (buffy)) > 0)
			;
		(void) close(mt);
	} else {
		/*
		 * Only way to tell if this is a floppy is to issue an ioctl
		 * but why waste one - if the eject fails, tough!
		 */
		(void) ioctl(mt, FDKEJECT, 0);
		(void) close(mt);
	}
}

static int
checkvol(b, t)
	struct s_spcl *b;
	long t;
{

	if (b->c_volume != t)
		return (FAIL);
	return (GOOD);
}

readhdr(b)
	struct s_spcl *b;
{

	if (gethead(b) == FAIL) {
		dprintf(stdout, gettext("readhdr fails at %ld blocks\n"),
			blksread);
		return (FAIL);
	}
	return (GOOD);
}

/*
 * read the tape into buf, then return whether or
 * or not it is a header block.
 */
gethead(buf)
	struct s_spcl *buf;
{
	long i;
	union u_ospcl {
		char dummy[TP_BSIZE];
		struct	s_ospcl {
			long	c_type;
			long	c_date;
			long	c_ddate;
			long	c_volume;
			long	c_tapea;
			u_short	c_inumber;
			long	c_magic;
			long	c_checksum;
			struct odinode {
				unsigned short odi_mode;
				u_short	odi_nlink;
				u_short	odi_uid;
				u_short	odi_gid;
				long	odi_size;
				long	odi_rdev;
				char	odi_addr[36];
				long	odi_atime;
				long	odi_mtime;
				long	odi_ctime;
			} c_dinode;
			long	c_count;
			char	c_baddr[256];
		} s_ospcl;
	} u_ospcl;

	if (cvtflag) {
		readtape((char *)(&u_ospcl.s_ospcl));
		(void) bzero((char *)buf, (long)TP_BSIZE);
		buf->c_type = u_ospcl.s_ospcl.c_type;
		buf->c_date = u_ospcl.s_ospcl.c_date;
		buf->c_ddate = u_ospcl.s_ospcl.c_ddate;
		buf->c_volume = u_ospcl.s_ospcl.c_volume;
		buf->c_tapea = u_ospcl.s_ospcl.c_tapea;
		buf->c_inumber = u_ospcl.s_ospcl.c_inumber;
		buf->c_checksum = u_ospcl.s_ospcl.c_checksum;
		buf->c_magic = u_ospcl.s_ospcl.c_magic;
		buf->c_dinode.di_mode = u_ospcl.s_ospcl.c_dinode.odi_mode;
		buf->c_dinode.di_nlink = u_ospcl.s_ospcl.c_dinode.odi_nlink;
		buf->c_dinode.di_size = u_ospcl.s_ospcl.c_dinode.odi_size;
		buf->c_dinode.di_uid = u_ospcl.s_ospcl.c_dinode.odi_uid;
		buf->c_dinode.di_gid = u_ospcl.s_ospcl.c_dinode.odi_gid;
#ifdef USG
		buf->c_dinode.di_suid = buf->c_dinode.di_uid;
		buf->c_dinode.di_sgid = buf->c_dinode.di_gid;
		buf->c_dinode.di_ordev = u_ospcl.s_ospcl.c_dinode.odi_rdev;
#else
		buf->c_dinode.di_rdev = u_ospcl.s_ospcl.c_dinode.odi_rdev;
#endif
		buf->c_dinode.di_atime = u_ospcl.s_ospcl.c_dinode.odi_atime;
		buf->c_dinode.di_mtime = u_ospcl.s_ospcl.c_dinode.odi_mtime;
		buf->c_dinode.di_ctime = u_ospcl.s_ospcl.c_dinode.odi_ctime;
		buf->c_count = u_ospcl.s_ospcl.c_count;
		bcopy(u_ospcl.s_ospcl.c_baddr, buf->c_addr, (long)256);
		/* we byte-swap the new spclrec, but checksum the old	*/
		/* (see comments in normspcl())				*/
		if (normspcl(byteorder,
		    buf, (int *)(&u_ospcl.s_ospcl), OFS_MAGIC))
			return (FAIL);
		buf->c_magic = NFS_MAGIC;
	} else {
		readtape((char *)buf);
		if (normspcl(byteorder, buf, (int *)buf, NFS_MAGIC))
			return (FAIL);
	}

	switch (buf->c_type) {

	case TS_CLRI:
	case TS_BITS:
		/*
		 * Have to patch up missing information in bit map headers
		 */
		buf->c_inumber = 0;
		buf->c_dinode.di_size = buf->c_count * TP_BSIZE;
		for (i = 0; i < buf->c_count && i < TP_NINDIR; i++)
			buf->c_addr[i] = 1;
		break;

	case TS_TAPE:
	case TS_END:
		if (dumpinfo.c_date == 0) {
			dumpinfo.c_date = spcl.c_date;
			dumpinfo.c_ddate = spcl.c_ddate;
		}
		if (!hostinfo && spcl.c_host[0] != '\0') {
			bcopy(spcl.c_label, dumpinfo.c_label, LBLSIZE);
			bcopy(spcl.c_filesys, dumpinfo.c_filesys, NAMELEN);
			bcopy(spcl.c_dev, dumpinfo.c_dev, NAMELEN);
			bcopy(spcl.c_host, dumpinfo.c_host, NAMELEN);
			dumpinfo.c_level = spcl.c_level;
			hostinfo++;
		}
		if (!inodeinfo && (spcl.c_flags & DR_INODEINFO)) {
			dumpinfo.c_volume = spcl.c_volume;
			bcopy(spcl.c_inos, dumpinfo.c_inos,
			    sizeof (spcl.c_inos));
			inodeinfo++;
		}
		buf->c_inumber = 0;
		break;

	case TS_INODE:
	case TS_ADDR:
		break;

	default:
		panic(gettext("%s: unknown inode type %d\n"),
			"gethead", buf->c_type);
		break;
	}
	if (dflag)
		accthdr(buf);
	return (GOOD);
}

/*
 * Check that a header is where it belongs and predict the next header
 */
static void
accthdr(header)
	struct s_spcl *header;
{
	static ino_t previno = 0x7fffffff;
	static int prevtype;
	static long predict;
	long blks, i;

	if (header->c_type == TS_TAPE) {
		if (header->c_firstrec)
			(void) fprintf(stderr,
				gettext("Volume header begins with record %d"),
				header->c_firstrec);
		else
			(void) fprintf(stderr, gettext("Volume header"));
		(void) fprintf(stderr, "\n");
		previno = 0x7fffffff;
		return;
	}
	if (previno == 0x7fffffff)
		goto newcalc;
	switch (prevtype) {
	case TS_BITS:
		(void) fprintf(stderr, gettext("Dump mask header"));
		break;
	case TS_CLRI:
		(void) fprintf(stderr, gettext("Remove mask header"));
		break;
	case TS_INODE:
		(void) fprintf(stderr,
			gettext("File header, ino %d at record %d"),
			previno, rec_position);
		break;
	case TS_ADDR:
		(void) fprintf(stderr,
			gettext("File continuation header, ino %d"),
			previno);
		break;
	case TS_END:
		(void) fprintf(stderr, gettext("End of media header"));
		break;
	}
	if (predict != blksread - 1)
		(void) fprintf(stderr,
			gettext("; predicted %ld blocks, got %ld blocks"),
			predict, blksread - 1);
	(void) fprintf(stderr, "\n");
newcalc:
	blks = 0;
	if (header->c_type != TS_END)
		for (i = 0; i < header->c_count; i++)
			if ((i >= TP_NINDIR) || (header->c_addr[i] != 0))
				blks++;
	predict = blks;
	blksread = 0;
	prevtype = header->c_type;
	previno = header->c_inumber;
}

/*
 * Try to determine which volume a file resides on.
 */
volnumber(inum)
	ino_t inum;
{
	int i;

	if (inodeinfo == 0)
		return (0);
	for (i = 1; i <= dumpinfo.c_volume; i++)
		if (inum < dumpinfo.c_inos[i])
			break;
	return (i - 1);
}

/*
 * Find an inode header.
 */
void
findinode(header)
	struct s_spcl *header;
{
	static long skipcnt = 0;
	long i;
	char buf[TP_BSIZE];

	curfile.name = gettext("<name unknown>");
	curfile.action = UNKNOWN;
	curfile.dip = (struct dinode *)NIL;
	curfile.ino = 0;
	curfile.ts = 0;
	if (ishead(header) == FAIL) {
		skipcnt++;
		while (gethead(header) == FAIL || header->c_date != dumpdate)
			skipcnt++;
	}
	for (;;) {
		if (checktype(header, TS_ADDR) == GOOD) {
			/*
			 * Skip up to the beginning of the next record
			 */
			for (i = 0; i < header->c_count; i++)
				if ((i >= TP_NINDIR) || (header->c_addr[i]))
					readtape(buf);
			(void) gethead(header);
			continue;
		}
		if (checktype(header, TS_INODE) == GOOD) {
			curfile.dip = &header->c_dinode;
#ifdef USG
			if (curfile.dip->di_suid != UID_LONG)
				curfile.dip->di_uid = curfile.dip->di_suid;
			if (curfile.dip->di_sgid != GID_LONG)
				curfile.dip->di_gid = curfile.dip->di_sgid;
#endif
			curfile.ino = header->c_inumber;
			curfile.ts = TS_INODE;
			break;
		}
		if (checktype(header, TS_END) == GOOD) {
			curfile.ino = maxino;
			curfile.ts = TS_END;
			break;
		}
		if (checktype(header, TS_CLRI) == GOOD) {
			curfile.name = gettext("<file removal list>");
			curfile.ts = TS_CLRI;
			break;
		}
		if (checktype(header, TS_BITS) == GOOD) {
			curfile.name = gettext("<file dump list>");
			curfile.ts = TS_BITS;
			break;
		}
		while (gethead(header) == FAIL)
			skipcnt++;
	}
	if (skipcnt > 0 && metamucil_mounts == 0)
		(void) fprintf(stderr,
			gettext("resync restore, skipped %d blocks\n"),
			skipcnt);
	skipcnt = 0;
}

/*
 * return whether or not the buffer contains a header block
 */
static int
ishead(buf)
	struct s_spcl *buf;
{

	if (buf->c_magic != NFS_MAGIC)
		return (FAIL);
	return (GOOD);
}

static
checktype(b, t)
	struct s_spcl *b;
	int	t;
{

	if (b->c_type != t)
		return (FAIL);
	return (GOOD);
}

int
#ifdef __STDC__
metamucil_seek(daddr_t rec_offset)
#else
metamucil_seek(rec_offset)
	daddr_t rec_offset;
#endif
{
	struct mtop tcom;
	int ret;

	if (nometamucilseek) /* it  should never get this far, but... */
		return (-1);

	tcom.mt_op = MTFSR;
	tcom.mt_count = rec_offset - rec_position;
	if (host)
		ret = rmtioctl(MTFSR, tcom.mt_count);
	else
		ret = ioctl(mt, (int)MTIOCTOP, (char *)&tcom);
	if (ret != -1) {
		blksread += tcom.mt_count * ntrec;
		tapea += tcom.mt_count * ntrec;
		rec_position += tcom.mt_count;
		recsread += tcom.mt_count;
	}
	return (ret);
}

void
#ifdef __STDC__
reset_dump(void)
#else
reset_dump()
#endif
{
	byteorder_destroy(byteorder);
	if ((byteorder = byteorder_create()) == NULL)
		panic(gettext("Cannot reallocate byte order data\n"));
	cvtflag = 0;
	numtrec = blksread = recsread = tapea = 0;
	rec_position = 0;
}

void
#ifdef __STDC__
get_next_device(void)
#else
get_next_device()
#endif
{
	char *p;
	static char sourcedev[1024];

	if (*device_list == 0)
		return;

	p = (char *)getdevice();
	(void) strcpy(sourcedev, p);
	metamucil_setinput(sourcedev, NULL);
}
