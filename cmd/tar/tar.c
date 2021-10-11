/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* ************************************************************ */

/*
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *			All rights reserved.
 */
/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft 	*/
/*	Corporation and should be treated as Confidential.	*/

#pragma ident	"@(#)tar.c	1.65	95/09/25 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mkdev.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <locale.h>
#include <nl_types.h>
#include <langinfo.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <string.h>
#include <sum.h>
#include <malloc.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <stdlib.h>
#include <stdarg.h>
#include <widec.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <libintl.h>
#include <sys/acl.h>

#ifndef LPNMAX
#include <limits.h>
#define	LPNMAX PATH_MAX
#define	LFNMAX NAME_MAX
#endif /* ! LPNMAX */

#ifndef MINSIZE
#define	MINSIZE 250
#endif
#define	DEF_FILE "/etc/default/tar"

#define	writetape(b)	writetbuf(b, 1)
#define	min(a, b)  ((a) < (b) ? (a) : (b))
#define	max(a, b)  ((a) > (b) ? (a) : (b))

extern	void	sumout(),
		sumupd(),
		sumpro(),
		sumepi();

/* -DDEBUG	ONLY for debugging */
#ifdef	DEBUG
#undef	DEBUG
#define	DEBUG(a, b, c)\
	(void) fprintf(stderr, "DEBUG - "), (void) fprintf(stderr, a, b, c)
#else
#define	DEBUG(a, b, c)
#endif

#define	TBLOCK	512	/* tape block size--should be universal */

#ifdef	BSIZE
#define	SYS_BLOCK BSIZE	/* from sys/param.h:  secondary block size */
#else	/* BSIZE */
#define	SYS_BLOCK 512	/* default if no BSIZE in param.h */
#endif	/* BSIZE */

#define	NBLOCK	20
#define	NAMSIZ	100
#define	PRESIZ	155
#define	MAXNAM	256
#define	MODEMASK 0777777	/* file creation mode mask */
#define	MAXEXT	9	/* reasonable max # extents for a file */
#define	EXTMIN	50	/* min blks left on floppy to split a file */

#define	TBLOCKS(bytes)	(((bytes) / TBLOCK) + (((bytes) % TBLOCK) != 0))
#define	K(tblocks)	((tblocks+1)/2)	/* tblocks to Kbytes for printing */
#define	max(a, b)	((a) > (b) ? (a) : (b))

#define	MAXLEV	18
#define	LEV0	1

#define	TRUE	1
#define	FALSE	0

/* ACL support */

struct	sec_attr {
	char	attr_type;
	char	attr_len[7];
	char	attr_info[1];
} *attr;

static int	append_secattr(char **, int *, int, char *, char);
static void	convert_perm(char *, int);
static void	write_ancillary();

/* Was statically allocated tbuf[NBLOCK] */
static
union hblock {
	char dummy[TBLOCK];
	struct header {
		char name[NAMSIZ];
		char mode[8];
		char uid[8];
		char gid[8];
		char size[12];	/* size of this extent if file split */
		char mtime[12];
		char chksum[8];
		char typeflag;
		char linkname[NAMSIZ];
		char magic[6];
		char version[2];
		char uname[32];
		char gname[32];
		char devmajor[8];
		char devminor[8];
		char prefix[155];
		char extno;		/* extent #, null if not split */
		char extotal;	/* total extents */
		char efsize[10];	/* size of entire file */
	} dbuf;
} dblock, *tbuf;

static
struct gen_hdr {
	ulong	g_mode,		/* Mode of file */
		g_uid,		/* Uid of file */
		g_gid;		/* Gid of file */
	long	g_filesz;	/* Length of file */
	ulong	g_mtime,	/* Modification time */
		g_cksum,	/* Checksum of file */
		g_devmajor,	/* File system of file */
		g_devminor;	/* Major/minor numbers of special files */
} Gen;

static
struct linkbuf {
	ino_t	inum;
	dev_t	devnum;
	int	count;
	char	pathname[MAXNAM];
	struct	linkbuf *nextp;
} *ihead;

/* see comments before build_table() */
#define	TABLE_SIZE 512
struct	file_list	{
	char	*name;			/* Name of file to {in,ex}clude */
	struct	file_list	*next;	/* Linked list */
};
static	struct	file_list	*exclude_tbl[TABLE_SIZE],
				*include_tbl[TABLE_SIZE];

static void add_file_to_table(struct file_list *table[], char *str);
static void assert_string(char *s, char *msg);
static int istape(int fd, int type);
static void backtape(void);
static void build_table(struct file_list *table[], char *file);
static void check_prefix(char **namep);
static void closevol(void);
static void copy(void *dst, void *src);
static void delete_target(char *namep);
static void doDirTimes(char *name, time_t modTime);
static void done(int n);
static void dorep(char *argv[]);
static void dotable(char *argv[]);
static void doxtract(char *argv[]);
static void fatal(char *format, ...);
static void vperror(int exit_status, register char *fmt, ...);
static void flushtape(void);
static void getdir(void);
static void getempty(register long n);
static void longt(register struct stat *st, char aclchar);
static int makeDir(register char *name);
static void mterr(char *operation, int i, int exitcode);
static void newvol(void);
static void passtape(void);
static void putempty(register unsigned long n);
static void putfile(char *longname, char *shortname, char *parent, int lev);
static void readtape(char *buffer);
static void seekdisk(long blocks);
static int setPathTimes(char *path, time_t modTime);
static void splitfile(char *longname, int ifd);
static void tomodes(register struct stat *sp);
static void usage(void);
static void xblocks(unsigned long bytes, int ofile);
static void xsfile(int ofd);
static void resugname(register char *name, int symflag);
static int bcheck(char *bstr);
static int checkdir(register char *name);
static int checksum(void);
#ifdef	EUC
static int checksum_signed(void);
#endif	/* EUC */
static int checkupdate(char *arg);
static int checkw(char c, char *name);
static int cmp(register char *b, register char *s, int n);
static int defset(char *arch);
static int endtape(void);
static int is_in_table(struct file_list *table[], char *str);
static int notsame(void);
static int prefix(register char *s1, register char *s2);
static int response(void);
static void build_dblock(const char *, const char *,
    const char *, const char *, const char, const uid_t,
    const gid_t, const dev_t, const char *);
static wchar_t yesnoresponse(void);
static unsigned int hash(char *str);
static long kcheck(char *kstr);
static daddr_t bsrch(char *s, int n, daddr_t l, daddr_t h);
static void onintr(int sig);
static void onquit(int sig);
static void onhup(int sig);
static uid_t getuidbyname(char *);
static gid_t getgidbyname(char *);
static char *getname(gid_t);
static char *getgroup(gid_t);
static int checkf(char *name, int mode, int howmuch);
static int writetbuf(register char *buffer, register int n);
static int wantit(char *argv[], char **namep);

static	struct stat stbuf;

static	int	checkflag = 0;
static	int	Xflag, Fflag, iflag, hflag, Bflag, Iflag;
static	int	rflag, xflag, vflag, tflag, mt, cflag, mflag, pflag;
static	int	uflag;
static	int	eflag;
static	int	sflag, Sflag;
static	int	oflag;
static	int	bflag, kflag, Aflag;
static 	int	Pflag;			/* POSIX conformant archive */
static	int	term, chksum, wflag, recno,
		first = TRUE, defaults_used = FALSE, linkerrok;
static	int	freemem = 1;
static	int	nblock = NBLOCK;
static	int	Errflg = 0;
static	int	totfiles = 0;

static	dev_t	mt_dev;		/* device containing output file */
static	ino_t	mt_ino;		/* inode number of output file */
static	int	mt_devtype;	/* dev type of archive, from stat structure */

static	int update = 1;		/* for `open' call */

static	daddr_t	low;
static	daddr_t	high;

static	FILE	*tfile;
static	FILE	*vfile = stdout;
static	char	tname[] = "/tmp/tarXXXXXX";
static	char	archive[] = "archive0=";
static	char	*Xfile;
static	char	*usefile;
static	char	*Filefile;
static	char	*Sumfile;
static	FILE	*Sumfp;
static	struct suminfo	Si;

static	int	mulvol;		/* multi-volume option selected */
static	unsigned long	blocklim; /* number of blocks to accept per volume */
static	unsigned long	tapepos; /* current block number to be written */
static	int	NotTape;	/* true if tape is a disk */
static	int	dumping;	/* true if writing a tape or other archive */
static	int	extno;		/* number of extent:  starts at 1 */
static	int	extotal;	/* total extents in this file */
static	long	efsize;		/* size of entire file */
static	ushort	Oumask = 0;	/* old umask value */
static 	int is_posix;	/* true if archive we're reading is POSIX-conformant */

void
main(int argc, char *argv[])
{
	char *cp;
#ifdef XENIX_ONLY
#if SYS_BLOCK > TBLOCK
	struct stat statinfo;
#endif
#endif

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	if (argc < 2)
		usage();

	tfile = NULL;

	/*
	 *  For XPG4 compatibility, we must be able to accept the "--"
	 *  argument normally recognized by getopt; it is used to delimit
	 *  the end opt the options section, and so can only appear in
	 *  the position of the first argument.  We simply skip it.
	 */

	if (strcmp(argv[1], "--") == 0) {
		argv++;
		argc--;
	}

	argv[argc] = NULL;
	argv++;

	/*
	 * Set up default values.
	 * Search the option string looking for the first digit or an 'f'.
	 * If you find a digit, use the 'archive#' entry in DEF_FILE.
	 * If 'f' is given, bypass looking in DEF_FILE altogether.
	 * If no digit or 'f' is given, still look in DEF_FILE but use '0'.
	 */
	if ((usefile = getenv("TAPE")) == (char *)NULL) {
		for (cp = *argv; *cp; ++cp)
			if (isdigit(*cp) || *cp == 'f')
				break;
		if (*cp != 'f') {
			archive[7] = (*cp)? *cp: '0';
			if (!(defaults_used = defset(archive))) {
				usefile = NULL;
				nblock = 1;
				blocklim = 0;
				NotTape = 0;
			}
		}
	}
	for (cp = *argv++; *cp; cp++)
		switch (*cp) {
		case 'f':
			assert_string(*argv, gettext(
			"tar: tapefile must be specified with 'f' option\n"));
			usefile = *argv++;
			break;
		case 'F':
			Fflag++;
			break;
		case 'c':
			cflag++;
			rflag++;
			update = 1;
			break;
		case 'u':
			uflag++;	/* moved code after signals caught */
			rflag++;
			update = 2;
			break;
		case 'r':
			rflag++;
			update = 2;
			break;
		case 'v':
			vflag++;
			break;
		case 'w':
			wflag++;
			break;
		case 'x':
			xflag++;
			break;
		case 'X':
			assert_string(*argv, gettext(
			    "tar: exclude file must be specified with 'X' "
			    "option\n"));
			Xflag = 1;
			Xfile = *argv++;
			build_table(exclude_tbl, Xfile);
			break;
		case 't':
			tflag++;
			break;
		case 'm':
			mflag++;
			break;
		case 'p':
			pflag++;
			break;
		case '-':
			/* ignore this silently */
			break;
		case '0':	/* numeric entries used only for defaults */
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			break;
		case 'b':
			assert_string(*argv, gettext(
			    "tar: blocking factor must be specified "
			    "with 'b' option\n"));
			bflag++;
			nblock = bcheck(*argv++);
			break;
		case 'l':
			linkerrok++;
			break;
		case 'e':
			eflag++;
			break;
		case 'o':
			oflag++;
			break;
		case 'h':
			hflag++;
			break;
		case 'i':
			iflag++;
			break;
		case 'B':
			Bflag++;
			break;
		case 'P':
			Pflag++;
			break;
		default:
			(void) fprintf(stderr, gettext(
			"tar: %c: unknown option\n"), *cp);
			usage();
		}

	if (!rflag && !xflag && !tflag)
		usage();
	if ((rflag && xflag) || (xflag && tflag) || (rflag && tflag)) {
		(void) fprintf(stderr, gettext(
		"tar: specify only one of [txru].\n"));
		usage();
	}
	if (cflag && *argv == NULL && Filefile == NULL)
		fatal(gettext("Missing filenames"));
	if (usefile == NULL)
		fatal(gettext("device argument required"));

	/* alloc a buffer of the right size */
	if ((tbuf = (union hblock *)
		    calloc(sizeof (union hblock) * nblock, sizeof (char))) ==
		(union hblock *) NULL) {
		(void) fprintf(stderr, gettext(
		"tar: cannot allocate physio buffer\n"));
		exit(1);
	}


#ifdef XENIX_ONLY
#if SYS_BLOCK > TBLOCK
	/* if user gave blocksize for non-tape device check integrity */
	(void) fprintf(stderr,
	    "SYS_BLOCK == %d, TBLOCK == %d\n", SYS_BLOCK, TBLOCK);
	if (cflag &&			/* check only needed when writing */
	    NotTape &&
	    stat(usefile, &statinfo) >= 0 &&
	    ((statinfo.st_mode & S_IFMT) == S_IFCHR) &&
	    (nblock % (SYS_BLOCK / TBLOCK)) != 0)
		fatal(gettext(
		"blocksize must be multiple of %d."), SYS_BLOCK/TBLOCK);
#endif
#endif
	if (sflag) {
	    if (Sflag && !mulvol)
		fatal(gettext("'S' option requires 'k' option."));
	    if (!(cflag || xflag || tflag) ||
		(!cflag && (Filefile != NULL || *argv != NULL)))
		(void) fprintf(stderr, gettext(
		"tar: warning: 's' option results are predictable only with "
		"'c' option or 'x' or 't' option and 0 'file' arguments\n"));
	    if (strcmp(Sumfile, "-") == 0)
		Sumfp = stdout;
	    else if ((Sumfp = fopen(Sumfile, "w")) == NULL)
		vperror(1, Sumfile);
	    sumpro(&Si);
	}

	if (rflag) {
		if (cflag && tfile != NULL) {
			usage();
			done(1);
		}
		if (signal(SIGINT, SIG_IGN) != SIG_IGN)
			(void) signal(SIGINT, onintr);
		if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
			(void) signal(SIGHUP, onhup);
		if (signal(SIGQUIT, SIG_IGN) != SIG_IGN)
			(void) signal(SIGQUIT, onquit);
		if (uflag) {
			(void) mktemp(tname);
			if ((tfile = fopen(tname, "w")) == NULL)
				vperror(1, tname);
		}
		if (strcmp(usefile, "-") == 0) {
			if (cflag == 0)
				fatal(gettext(
				"can only create standard output archives."));
			vfile = stderr;
			mt = dup(1);
			++bflag;
		} else {
			if (cflag)
				mt = open(usefile,
				    O_RDWR|O_CREAT|O_TRUNC, 0666);
			else
				mt = open(usefile, O_RDWR);

			if (mt < 0) {
				if (cflag == 0 || (mt =  creat(usefile, 0666))
						< 0)
				vperror(1, usefile);
			}
		}
		/* Get inode and device number of output file */
		(void) fstat(mt, &stbuf);
		mt_ino = stbuf.st_ino;
		mt_dev = stbuf.st_dev;
		mt_devtype = stbuf.st_mode & S_IFMT;
		NotTape = !istape(mt, mt_devtype);

		if (rflag && !cflag && (mt_devtype == S_IFIFO))
			fatal(gettext("cannot append to pipe or FIFO."));

		if (Aflag && vflag)
			(void) printf(
			gettext("Suppressing absolute pathnames\n"));
		dorep(argv);
	} else if (xflag || tflag) {
		/*
		 * for each argument, check to see if there is a "-I file" pair.
		 * if so, move the 3rd argument into "-I"'s place, build_table()
		 * using "file"'s name and increment argc one (the second
		 * increment appears in the for loop) which removes the two
		 * args "-I" and "file" from the argument vector.
		 */
		for (argc = 0; argv[argc]; argc++) {
			if (strcmp(argv[argc], "-I") == 0) {
				if (!argv[argc+1]) {
					(void) fprintf(stderr, gettext(
					"tar: missing argument for -I flag\n"));
					done(2);
				} else {
					Iflag = 1;
					argv[argc] = argv[argc+2];
					build_table(include_tbl, argv[++argc]);
				}
			}
		}
		if (strcmp(usefile, "-") == 0) {
			mt = dup(0);
			++bflag;
			/* try to recover from short reads when reading stdin */
			++Bflag;
		} else if ((mt = open(usefile, 0)) < 0)
			vperror(1, usefile);

		if (xflag) {
			if (Aflag && vflag)
				(void) printf(gettext
				("Suppressing absolute pathnames.\n"));
			doxtract(argv);
		} else if (tflag)
			dotable(argv);
	}
	else
		usage();

	if (sflag) {
		sumepi(&Si);
		sumout(Sumfp, &Si);
		(void) fprintf(Sumfp, "\n");
	}

	done(Errflg);
}

static void
usage(void)
{

	(void) fprintf(stderr, gettext(
	"Usage: tar {txruc}[vfbFXhiBelmopw[0-7]] [tapefile] "
	"[blocksize] [exclude-file] [-I include-file] files ...\n"));
	done(1);
}

/*
 * dorep - do "replacements"
 *
 *	Dorep is responsible for creating ('c'),  appending ('r')
 *	and updating ('u');
 */

static void
dorep(char *argv[])
{
	register char *cp, *cp2, *p;
	char wdir[MAXPATHLEN+2], tempdir[MAXPATHLEN+2], *parent = (char *)NULL;
	char file[MAXPATHLEN], origdir[MAXPATHLEN+1];
	FILE *fp = (FILE *)NULL;
	FILE *ff = (FILE *)NULL;


	if (!cflag) {
		getdir();			/* read header for next file */
		while (!endtape()) {		/* changed from a do while */
			passtape();		/* skip the file data */
			if (term)
				done(Errflg);	/* received signal to stop */
			getdir();
		}
		backtape();			/* was called by endtape */
		if (tfile != NULL) {
			char buf[200];

			(void) sprintf(buf, "sort +0 -1 +1nr %s -o %s; awk '$1 "
			    "!= prev {print; prev=$1}' %s >%sX;mv %sX %s",
				tname, tname, tname, tname, tname, tname);
			(void) fflush(tfile);
			(void) system(buf);
			(void) freopen(tname, "r", tfile);
			(void) fstat(fileno(tfile), &stbuf);
			high = stbuf.st_size;
		}
	}

	dumping = 1;
	if (mulvol) {	/* SP-1 */
		if (nblock && (blocklim%nblock) != 0)
			fatal(gettext(
			"Volume size not a multiple of block size."));
		blocklim -= 2;			/* for trailer records */
		if (vflag)
			(void) fprintf(vfile, gettext(
			"Volume ends at %luK, blocking factor = %dK\n"),
				K(blocklim - 1), K(nblock));
	}

	/*
	 * Save the original directory before it gets
	 * changed.
	 */
	if (getcwd(origdir, (MAXPATHLEN+1)) == NULL) {
		vperror(0, gettext("A parent directory cannot be read"));
		exit(1);
	}

	strcpy(wdir, origdir);

	while ((*argv || fp || ff) && !term) {
		cp2 = 0;
		if (fp || (strcmp(*argv, "-I") == 0)) {
			if (fp == NULL) {
				if (*++argv == NULL) {
					(void) fprintf(stderr, gettext(
					    "tar: missing file name for -I "
					    "flag.\n"));
					done(1);
				} else if ((fp = fopen(*argv++, "r")) == NULL)
					vperror(0, argv[-1]);
				continue;
			} else if ((fgets(file, MAXPATHLEN-1, fp)) == NULL) {
				(void) fclose(fp);
				fp = NULL;
				continue;
			} else {
				cp = cp2 = file;
				if (p = strchr(cp2, '\n'))
					*p = 0;
			}
		} else if ((strcmp(*argv, "-C") == 0) && argv[1]) {
			if (chdir(*++argv) < 0)
				vperror(0, gettext(
				"can't change directories to %s"), *argv);
			else
				(void) getcwd(wdir, (sizeof (wdir)));
			argv++;
			continue;
		} else
			cp = cp2 = strcpy(file, *argv++);

		parent = wdir;
		for (; *cp; cp++)
			if (*cp == '/')
				cp2 = cp;
		if (cp2 != file) {
			*cp2 = '\0';
			if (chdir(file) < 0) {
				vperror(0, gettext(
				"can't change directories to %s"), file);
				continue;
			}
			parent = getcwd(tempdir, (sizeof (tempdir)));
			*cp2 = '/';
			cp2++;
		}

		putfile(file, cp2, parent, LEV0);
		if (chdir(origdir) < 0)
			vperror(0, gettext("cannot change back?: "), origdir);
	}
	putempty(2L);
	flushtape();
	closevol();	/* SP-1 */
	if (linkerrok == 1)
		for (; ihead != NULL; ihead = ihead->nextp) {
			if (ihead->count == 0)
				continue;
			(void) fprintf(stderr, gettext(
			"tar: missing links to %s\n"), ihead->pathname);
			if (eflag)
				done(1);
		}
}


/*
 * endtape - check for tape at end
 *
 *	endtape checks the entry in dblock.dbuf to see if its the
 *	special EOT entry.  Endtape is usually called after getdir().
 *
 *	endtape used to call backtape; it no longer does, he who
 *	wants it backed up must call backtape himself
 *	RETURNS:	0 if not EOT, tape position unaffected
 *			1 if	 EOT, tape position unaffected
 */

static int
endtape(void)
{
	if (dblock.dbuf.name[0] == '\0') {	/* null header = EOT */
		return (1);
	} else
		return (0);
}

/*
 *	getdir - get directory entry from tar tape
 *
 *	getdir reads the next tarblock off the tape and cracks
 *	it as a directory. The checksum must match properly.
 *
 *	If tfile is non-null getdir writes the file name and mod date
 *	to tfile.
 */

static void
getdir(void)
{
	register struct stat *sp;
#ifdef EUC
	static int warn_chksum_sign = 0;
#endif EUC

top:
	readtape((char *) &dblock);
	if (dblock.dbuf.name[0] == '\0')
		return;
	totfiles++;
	sp = &stbuf;
	(void) sscanf(dblock.dbuf.mode, "%8lo", &Gen.g_mode);
	(void) sscanf(dblock.dbuf.uid, "%8lo", &Gen.g_uid);
	(void) sscanf(dblock.dbuf.gid, "%8lo", &Gen.g_gid);
	(void) sscanf(dblock.dbuf.size, "%12lo", &Gen.g_filesz);
	(void) sscanf(dblock.dbuf.mtime, "%12lo", &Gen.g_mtime);
	(void) sscanf(dblock.dbuf.chksum, "%8lo", &Gen.g_cksum);
	(void) sscanf(dblock.dbuf.devmajor, "%8lo", &Gen.g_devmajor);
	(void) sscanf(dblock.dbuf.devminor, "%8lo", &Gen.g_devminor);

	is_posix = (strncmp(dblock.dbuf.magic, "ustar", 5) == 0);

	sp->st_mode = Gen.g_mode;
	sp->st_uid = Gen.g_uid;
	sp->st_gid = Gen.g_gid;
	sp->st_size = Gen.g_filesz;
	sp->st_mtime = Gen.g_mtime;
	chksum = Gen.g_cksum;

	if (dblock.dbuf.extno != '\0') {	/* split file? */
		extno = dblock.dbuf.extno;
		extotal = dblock.dbuf.extotal;
		(void) sscanf(dblock.dbuf.efsize, "%10lo", &efsize);
	} else
		extno = 0;	/* tell others file not split */

#ifdef	EUC
	if (chksum != checksum()) {
		if (chksum != checksum_signed()) {
			(void) fprintf(stderr, gettext(
			    "tar: directory checksum error\n"));
			if (iflag)
				goto top;
			done(2);
		} else {
			if (! warn_chksum_sign) {
				warn_chksum_sign = 1;
				(void) fprintf(stderr, gettext(
			"tar: warning: tar file made with signed checksum\n"));
			}
		}
	}
#else
	if (chksum != checksum()) {
		(void) fprintf(stderr, gettext(
		"tar: directory checksum error\n"));
		if (iflag)
			goto top;
		done(2);
	}
#endif	EUC
	if (tfile != NULL)
		(void) fprintf(tfile,
		    "%s %s\n", dblock.dbuf.name, dblock.dbuf.mtime);
}


/*
 *	passtape - skip over a file on the tape
 *
 *	passtape skips over the next data file on the tape.
 *	The tape directory entry must be in dblock.dbuf. This
 *	routine just eats the number of blocks computed from the
 *	directory size entry; the tape must be (logically) positioned
 *	right after thee directory info.
 */

static void
passtape(void)
{
	long blocks;
	char buf[TBLOCK];

	/*
	 * Types link(1), sym-link(2), char special(3), blk special(4),
	 *  directory(5), and FIFO(6) do not have data blocks associated
	 *  with them so just skip reading the data block.
	 */
	if (dblock.dbuf.typeflag == '1' || dblock.dbuf.typeflag == '2' ||
		dblock.dbuf.typeflag == '3' || dblock.dbuf.typeflag == '4' ||
		dblock.dbuf.typeflag == '5' || dblock.dbuf.typeflag == '6')
		return;
	blocks = TBLOCKS(stbuf.st_size);

	/* if operating on disk, seek instead of reading */
	if (NotTape && !sflag)
		seekdisk(blocks);
	else
		while (blocks-- > 0)
			readtape(buf);
}

static void
putfile(char *longname, char *shortname, char *parent, int lev)
{
	static char *getmem();
	int infile = -1;	/* deliberately invalid */
	unsigned long blocks;
	char buf[TBLOCK];
	char *bigbuf;
	int	maxread;
	int	hint;		/* amount to write to get "in sync" */
	char filetmp[NAMSIZ];
	register char *cp;
	char *name;
	struct dirent *dp;
	DIR *dirp;
	int i = 0;
	long l;
	int split = 0;
	char newparent[MAXNAM+64];
	char *tchar = "";
	char *prefix = "";
	char *tmpbuf = NULL;
	char goodbuf[MAXNAM+1];
	char junkbuf[MAXNAM+1];
	char abuf[PRESIZ+1];
	char *lastslash;
	int	j = 0;
	int	printerr = 1;
	int	slnkerr;
	struct stat symlnbuf;
	aclent_t	*aclp = NULL;
	int		aclcnt;
	int		rc;

	memset(goodbuf, '\0', sizeof (goodbuf));
	memset(junkbuf, '\0', sizeof (junkbuf));
	memset(abuf, '\0', sizeof (abuf));

	if (lev >= MAXLEV) {
		/*
		 * Notice that we have already recursed, so we have already
		 * allocated our frame, so things would in fact work for this
		 * level.  We put the check here rather than before each
		 * recursive call because it is cleaner and less error prone.
		 */
		(void) fprintf(stderr, gettext(
		"tar: directory nesting too deep, %s not dumped\n"), longname);
		return;
	}
	if (!hflag)
		i = lstat(shortname, &stbuf);
	else
		i = stat(shortname, &stbuf);

	if (i < 0) {
		/* Initialize flag to print error mesg. */
		printerr = 1;
		/*
		 * If stat is done, then need to do lstat
		 * to determine whether it's a sym link
		 */
		if (hflag) {
			/* Save returned error */
			slnkerr = errno;

			j = lstat(shortname, &symlnbuf);
			/*
			 * Suppress error message when file
			 * is a symbolic link and option -l
			 * is on.
			 */
			if ((j == 0) && (!linkerrok) &&
				(S_ISLNK(symlnbuf.st_mode)))
				printerr = 0;

			/*
			 * Restore errno in case the lstat
			 * on symbolic link change
			 */
			errno = slnkerr;
		}

		if (printerr)
			(void) fprintf(stderr, gettext(
			"tar: %s: %s\n"), longname, strerror(errno));
		return;
	}

	/*
	 * Check if the input file is the same as the tar file we
	 * are creating
	 */
	if ((mt_ino == stbuf.st_ino) && (mt_dev == stbuf.st_dev)) {
		(void) fprintf(stderr, gettext(
		"tar: %s same as archive file\n"), longname);
		Errflg = 1;
		return;
	}

	if (tfile != NULL && checkupdate(longname) == 0) {
		return;
	}
	if (checkw('r', longname) == 0) {
		return;
	}

	if (Fflag && checkf(shortname, stbuf.st_mode, Fflag) == 0)
		return;

	if (Xflag) {
		if (is_in_table(exclude_tbl, longname)) {
			if (vflag) {
				(void) fprintf(vfile, gettext(
				"a %s excluded\n"), longname);
			}
			return;
		}
	}

	/*
	 * If the length of the fullname is greater than 256,
	 * print out a message and return.
	 */

	if ((split = strlen(longname)) > MAXNAM) {
		(void) fprintf(stderr, gettext(
		"tar: %s: file name too long\n"),
		    longname);
		if (eflag)
			done(1);
		return;
	} else if (split > NAMSIZ) {
		/*
		 * The length of the fullname is greater than 100, so
		 * we must split the filename from the path
		 */
		(void) strcpy(&goodbuf[0], longname);
		tmpbuf = goodbuf;
		lastslash = strrchr(tmpbuf, '/');
		i = (lastslash == NULL ? strlen(tmpbuf) : strlen(lastslash++));
		/*
		 * If the filename is greater than 100 we can't
		 * archive the file
		 */
		if (i-1 > NAMSIZ) {
			(void) fprintf(stderr, gettext(
			"tar: %s: filename is greater than %d\n"),
			    lastslash == NULL ? tmpbuf : lastslash, NAMSIZ);
			if (eflag)
				done(1);
			return;
		}
		(void) strncpy(&junkbuf[0], lastslash, strlen(lastslash));
		/*
		 * If the prefix is greater than 155 we can't archive the
		 * file.
		 */
		if ((split - i) > PRESIZ) {
			(void) fprintf(stderr, gettext(
			"tar: %s: prefix is greater than %d\n"),
			longname, PRESIZ);
			if (eflag)
				done(1);
			return;
		}
		(void) strncpy(&abuf[0], &goodbuf[0], split - i);
		name = junkbuf;
		prefix = abuf;
	} else {
		name = longname;
	}
	if (Aflag)
		if ((prefix != NULL) && (*prefix != '\0'))
			while (*prefix == '/')
				++prefix;
		else
			while (*name == '/')
				++name;

	/* ACL support */
	if (pflag) {
		/*
		 * Get ACL info: dont bother allocating space if there are only
		 * 	standard permissions, i.e. ACL count <= 4
		 */
		if ((aclcnt = acl(shortname, GETACLCNT, 0, NULL)) < 0) {
			(void) fprintf(stderr, gettext(
			    "%s: failed to get acl count\n"), longname);
			return;
		}
		if (aclcnt > MIN_ACL_ENTRIES) {
			if ((aclp = (aclent_t *)malloc(
			    sizeof (aclent_t) * aclcnt)) == NULL) {
				(void) fprintf(stderr, gettext(
				    "Insufficient memory\n"));
				return;
			}
			if ((rc = acl(shortname, GETACL, aclcnt, aclp)) < 0) {
				(void) fprintf(stderr, gettext(
				    "%s: failed to get acl entries\n"),
				    longname);
				return;
			}
		}
		/* else: only traditional permissions, so proceed as usual */
	}

	switch (stbuf.st_mode & S_IFMT) {
	case S_IFDIR:
		stbuf.st_size = 0;
		blocks = TBLOCKS(stbuf.st_size);
		i = 0;
		cp = buf;
		while (*cp++ = longname[i++])
			;
		*--cp = '/';
		*++cp = 0;
		if (!oflag) {
			tomodes(&stbuf);
			build_dblock(name, "\0", "ustar", "00", '5',
			    stbuf.st_uid, stbuf.st_gid, stbuf.st_dev, prefix);
			if (!Pflag) {
				/*
				 * Old archives require a slash at the end
				 * of a directory name.
				 *
				 * XXX
				 * If directory name is too long, will
				 * slash overfill field?
				 */
				if (strlen(name) > (unsigned)NAMSIZ) {
					(void) fprintf(stderr, gettext(
					    "tar: %s: filename is greater "
					    "than %d\n"), name, NAMSIZ);
					if (eflag)
						done(1);
					if (aclp != NULL)
						free(aclp);
					return;
				} else {
					(void) sprintf(dblock.dbuf.name, "%s/",
					    name);
					/*
					 * need to recalculate checksum
					 * because the name changed.
					 */
					(void) sprintf(dblock.dbuf.chksum,
					    "%07o", checksum());
				}
			}

			/* ACL support */
			if (pflag) {
				char	*secinfo = NULL;
				int	len = 0;

				/* append security attributes */
				append_secattr(&secinfo, &len, aclcnt,
				    (char *) aclp, UFSD_ACL);

				/* call append_secattr() if more than one */

				/* write ancillary */
				(void) write_ancillary(&dblock, secinfo, len);
			}
			(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
			dblock.dbuf.typeflag = '5';
			writetape((char *)&dblock);
		}
		if (vflag) {
			if (NotTape)
				DEBUG("seek = %luK\t", K(tapepos), 0);
			(void) fprintf(vfile, "a %s/ ", longname);
			if (NotTape)
				(void) fprintf(vfile, "%luK\n", K(blocks));
			else
				(void) fprintf(vfile,
				    gettext("%lu tape blocks\n"),
				    (unsigned long)blocks);
		}
		if (*shortname != '/')
			(void) sprintf(newparent, "%s/%s", parent, shortname);
		else
			(void) sprintf(newparent, "%s", shortname);
		if (chdir(shortname) < 0) {
			vperror(0, newparent);
			if (aclp != NULL)
				free(aclp);
			return;
		}
		if ((dirp = opendir(".")) == NULL) {
			vperror(0, gettext(
			"can't open directory %s"), longname);
			if (chdir(parent) < 0)
				vperror(0, gettext("cannot change back?: %s"),
				parent);
			if (aclp != NULL)
				free(aclp);
			return;
		}
		while ((dp = readdir(dirp)) != NULL && !term) {
			if ((strcmp(".", dp->d_name) == 0) ||
			    (strcmp("..", dp->d_name) == 0))
				continue;
			(void) strcpy(cp, dp->d_name);
			l = telldir(dirp);
			(void) closedir(dirp);
			putfile(buf, cp, newparent, lev + 1);
			dirp = opendir(".");
			seekdir(dirp, l);
		}
		(void) closedir(dirp);
		if (chdir(parent) < 0)
			vperror(0, gettext("cannot change back?: %s"), parent);
		break;

	case S_IFLNK:
		if (stbuf.st_size + 1 >= NAMSIZ) {
			(void) fprintf(stderr, gettext(
			"tar: %s: symbolic link too long\n"),
			    longname);
			if (eflag)
				done(1);
			if (aclp != NULL)
				free(aclp);
			return;
		}
		/*
		 * Sym-links need header size of zero since you
		 * don't store any data for this type.
		 */
		stbuf.st_size = 0;
		tomodes(&stbuf);
		i = readlink(shortname, filetmp, NAMSIZ - 1);
		if (i < 0) {
			vperror(0, gettext(
			"can't read symbolic link %s"), longname);
			if (aclp != NULL)
				free(aclp);
			return;
		} else {
			filetmp[i] = 0;
		}
		if (vflag)
			(void) fprintf(vfile, gettext(
			"a %s symbolic link to %s\n"),
			    longname, filetmp);
		build_dblock(name, filetmp, "ustar", "00", '2',
		    stbuf.st_uid, stbuf.st_gid, stbuf.st_dev, prefix);
		writetape((char *)&dblock);
		/*
		 * No acls for symlinks: mode is always 777
		 * dont call write ancillary
		 */
		break;
	case S_IFREG:
		if ((infile = open(shortname, 0)) < 0) {
			vperror(0, longname);
			if (aclp != NULL)
				free(aclp);
			return;
		}

		blocks = TBLOCKS(stbuf.st_size);
		if (stbuf.st_nlink > 1) {
			struct linkbuf *lp;
			int found = 0;

			for (lp = ihead; lp != NULL; lp = lp->nextp)
				if (lp->inum == stbuf.st_ino &&
				    lp->devnum == stbuf.st_dev) {
					found++;
					break;
				}
			if (found) {
				if (strlen(lp->pathname) >
				    (size_t)(NAMSIZ -1)) {
					(void) fprintf(stderr, gettext(
					"tar: %s: linked to %s\n"),
					longname, lp->pathname);
					(void) fprintf(stderr, gettext(
					"tar: %s: linked name too long\n"),
					lp->pathname);
					if (eflag)
						done(1);
					(void) close(infile);
					if (aclp != NULL)
						free(aclp);
					return;
				}
				stbuf.st_size = 0;
				tomodes(&stbuf);
				build_dblock(name, lp->pathname,
				    "ustar", "00", '1', stbuf.st_uid,
				    stbuf.st_gid, stbuf.st_dev, prefix);
				if (mulvol && tapepos + 1 >= blocklim)
					newvol();
				writetape((char *) &dblock);
				/*
				 * write_ancillary() is not needed here.
				 * The first link is handled in the following
				 * else statement. No need to process ACLs
				 * for other hard links since they are the
				 * same file.
				 */

				if (vflag) {
					if (NotTape)
						DEBUG("seek = %luK\t",
						    K(tapepos), 0);
					(void) fprintf(vfile, gettext(
					"a %s link to %s\n"),
					    longname, lp->pathname);
				}
				lp->count--;
				(void) close(infile);
				if (aclp != NULL)
					free(aclp);
				return;
			} else {
				lp = (struct linkbuf *) getmem(sizeof (*lp));
				if (lp != NULL) {
					lp->nextp = ihead;
					ihead = lp;
					lp->inum = stbuf.st_ino;
					lp->devnum = stbuf.st_dev;
					lp->count = stbuf.st_nlink - 1;
					(void) strcpy(lp->pathname, longname);
				}
			}
		}
		tomodes(&stbuf);

		/* correctly handle end of volume */
		while (mulvol && tapepos + blocks + 1 > blocklim) {
			/* file won't fit */
			if (eflag) {
				if (blocks <= blocklim) {
					newvol();
					break;
				}
				(void) fprintf(stderr, gettext(
				"tar: Single file cannot fit on volume\n"));
				done(3);
			}
			/* split if floppy has some room and file is large */
			if (((blocklim - tapepos) >= EXTMIN) &&
			    ((blocks + 1) >= blocklim/10)) {
				splitfile(longname, infile);
				(void) close(infile);
				if (aclp != NULL)
					free(aclp);
				return;
			}
			newvol();	/* not worth it--just get new volume */
		}

		DEBUG("putfile: %s wants %lu blocks\n", longname, blocks);
		if (vflag) {
			if (NotTape)
				DEBUG("seek = %luK\t", K(tapepos), 0);
			(void) fprintf(vfile, "a %s ", longname);
			if (NotTape)
				(void) fprintf(vfile, "%luK\n", K(blocks));
			else
				(void) fprintf(vfile,
				    gettext("%lu tape blocks\n"),
				    (unsigned long)blocks);
		}
		build_dblock(name, tchar, "ustar", "00", '0',
		    stbuf.st_uid, stbuf.st_gid, stbuf.st_dev, prefix);

		/* ACL support */
		if (pflag) {
			char	*secinfo = NULL;
			int	len = 0;

			/* append security attributes */
			append_secattr(&secinfo, &len, aclcnt,
			    (char *) aclp, UFSD_ACL);

			/* call append_secattr() if more than one */

			/* write ancillary */
			(void) write_ancillary(&dblock, secinfo, len);
		}
		(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
		dblock.dbuf.typeflag = '0';

		hint = writetape((char *)&dblock);
		maxread = max(stbuf.st_blksize, (nblock * TBLOCK));
		if ((bigbuf = calloc((unsigned)maxread, sizeof (char))) == 0) {
			maxread = TBLOCK;
			bigbuf = buf;
		}

		while (((i =
		    read(infile, bigbuf, min((hint*TBLOCK), maxread))) > 0) &&
		    blocks) {
			register int nblks;

			nblks = ((i-1)/TBLOCK)+1;
			if (nblks > blocks)
				nblks = blocks;
			hint = writetbuf(bigbuf, nblks);
			blocks -= nblks;
		}
		(void) close(infile);
		if (bigbuf != buf)
			free(bigbuf);
		if (i < 0)
			vperror(0, gettext("Read error on %s"), longname);
		else if (blocks != 0 || i != 0) {
			(void) fprintf(stderr, gettext(
			"tar: %s: file changed size\n"), longname);
			if (eflag)
				done(1);
		}
		putempty(blocks);
		break;
	case S_IFIFO:
		blocks = TBLOCKS(stbuf.st_size);
		stbuf.st_size = 0;
		if (stbuf.st_nlink > 1) {
			struct linkbuf *lp;
			int found = 0;

			tomodes(&stbuf);
			for (lp = ihead; lp != NULL; lp = lp->nextp)
				if (lp->inum == stbuf.st_ino &&
				    lp->devnum == stbuf.st_dev) {
					found++;
					break;
				}
			if (found) {
				if (strlen(lp->pathname) >
				    (size_t)(NAMSIZ -1)) {
					(void) fprintf(stderr, gettext(
					"tar: %s: linked to %s\n"), longname,
					lp->pathname);
					(void) fprintf(stderr, gettext(
					"tar: %s: linked name too long\n"),
					lp->pathname);
					if (eflag)
						done(1);
					if (aclp != NULL)
						free(aclp);
					return;
				}
				build_dblock(name, lp->pathname, "ustar",
				    "00", '6', stbuf.st_uid, stbuf.st_gid,
				    stbuf.st_dev, prefix);
				if (mulvol && tapepos + 1 >= blocklim)
					newvol();
				writetape((char *) &dblock);
				if (vflag) {
					if (NotTape)
						DEBUG("seek = %luK\t",
						    K(tapepos), 0);
					(void) fprintf(vfile, gettext(
					"a %s link to %s\n"),
					    longname, lp->pathname);
				}
				lp->count--;
				if (aclp != NULL)
					free(aclp);
				return;
			} else {
				lp = (struct linkbuf *) getmem(sizeof (*lp));
				if (lp != NULL) {
					lp->nextp = ihead;
					ihead = lp;
					lp->inum = stbuf.st_ino;
					lp->devnum = stbuf.st_dev;
					lp->count = stbuf.st_nlink - 1;
					(void) strcpy(lp->pathname, longname);
				}
			}
		}
		tomodes(&stbuf);

		while (mulvol && tapepos + blocks + 1 > blocklim) {
			if (eflag) {
				if (blocks <= blocklim) {
					newvol();
					break;
				}
				(void) fprintf(stderr, gettext(
				"tar: Single file cannot fit on volume\n"));
				done(3);
			}

			if (((blocklim - tapepos) >= EXTMIN) &&
			    ((blocks + 1) >= blocklim/10)) {
				splitfile(longname, infile);
				if (aclp != NULL)
					free(aclp);
				return;
			}
			newvol();
		}

		DEBUG("putfile: %s wants %lu blocks\n", longname, blocks);
		if (vflag) {
			if (NotTape)
				DEBUG("seek = %luK\t", K(tapepos), 0);
			if (NotTape)
				(void) fprintf(vfile, gettext(
				"a %s %luK\n "), longname, K(blocks));
			else
				(void) fprintf(vfile, gettext(
				"a %s %lu tape blocks\n"),
				longname, (unsigned long)blocks);
		}
		build_dblock(name, tchar, "ustar", "00", '6',
		    stbuf.st_uid, stbuf.st_gid, stbuf.st_dev, prefix);

		/* ACL support */
		if (pflag) {
			char	*secinfo = NULL;
			int	len = 0;

			/* append security attributes */
			append_secattr(&secinfo, &len, aclcnt,
			    (char *) aclp, UFSD_ACL);

			/* call append_secattr() if more than one */

			/* write ancillary */
			(void) write_ancillary(&dblock, secinfo, len);
		}
		(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
		dblock.dbuf.typeflag = '6';

		writetape((char *)&dblock);
		break;
	case S_IFCHR:
		blocks = TBLOCKS(stbuf.st_size);
		stbuf.st_size = 0;
		if (stbuf.st_nlink > 1) {
			struct linkbuf *lp;
			int found = 0;

			tomodes(&stbuf);
			for (lp = ihead; lp != NULL; lp = lp->nextp)
				if (lp->inum == stbuf.st_ino &&
				    lp->devnum == stbuf.st_dev) {
					found++;
					break;
				}
			if (found) {
				if (strlen(lp->pathname) >
				    (size_t)(NAMSIZ -1)) {
					(void) fprintf(stderr, gettext(
					"tar: %s: linked to %s\n"), longname,
					lp->pathname);
					(void) fprintf(stderr, gettext(
					"tar: %s: linked name too long\n"),
					lp->pathname);
					if (eflag)
						done(1);
					if (aclp != NULL)
						free(aclp);
					return;
				}
				stbuf.st_size = 0;
				build_dblock(name, lp->pathname, "ustar",
				    "00", '3', stbuf.st_uid, stbuf.st_gid,
				    stbuf.st_dev, prefix);
				if (mulvol && tapepos + 1 >= blocklim)
					newvol();
				writetape((char *) &dblock);
				if (vflag) {
					if (NotTape)
						DEBUG("seek = %luK\t",
						    K(tapepos), 0);
					(void) fprintf(vfile, gettext(
					    "a %s link to %s\n"), longname,
					    lp->pathname);
				}
				lp->count--;
				if (aclp != NULL)
					free(aclp);
				return;
			} else {
				lp = (struct linkbuf *) getmem(sizeof (*lp));
				if (lp != NULL) {
					lp->nextp = ihead;
					ihead = lp;
					lp->inum = stbuf.st_ino;
					lp->devnum = stbuf.st_dev;
					lp->count = stbuf.st_nlink - 1;
					(void) strcpy(lp->pathname, longname);
				}
			}
		}
		tomodes(&stbuf);

		while (mulvol && tapepos + blocks + 1 > blocklim) {
			if (eflag) {
				if (blocks <= blocklim) {
					newvol();
					break;
				}
				(void) fprintf(stderr, gettext(
				"tar: Single file cannot fit on volume\n"));
				done(3);
			}

			if (((blocklim - tapepos) >= EXTMIN) &&
			    ((blocks + 1) >= blocklim/10)) {
				splitfile(longname, infile);
				if (aclp != NULL)
					free(aclp);
				return;
			}
			newvol();
		}

		DEBUG("putfile: %s wants %lu blocks\n", longname, blocks);
		if (vflag) {
			if (NotTape)
				DEBUG("seek = %luK\t", K(tapepos), 0);
			if (NotTape)
				(void) fprintf(vfile, gettext(
				"a %s %luK\n"), longname, K(blocks));
			else
				(void) fprintf(vfile, gettext(
				"a %s %lu tape blocks\n"),
				longname, (unsigned long)blocks);
		}
		build_dblock(name, tchar, "ustar", "00", '3',
		    stbuf.st_uid, stbuf.st_gid, stbuf.st_rdev, prefix);

		/* ACL support */
		if (pflag) {
			char	*secinfo = NULL;
			int	len = 0;

			/* append security attributes */
			append_secattr(&secinfo, &len, aclcnt,
			    (char *) aclp, UFSD_ACL);

			/* call append_secattr() if more than one */

			/* write ancillary */
			(void) write_ancillary(&dblock, secinfo, len);
		}
		(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
		dblock.dbuf.typeflag = '3';

		writetape((char *)&dblock);
		break;
	case S_IFBLK:
		blocks = TBLOCKS(stbuf.st_size);
		stbuf.st_size = 0;
		if (stbuf.st_nlink > 1) {
			struct linkbuf *lp;
			int found = 0;

			tomodes(&stbuf);
			for (lp = ihead; lp != NULL; lp = lp->nextp)
				if (lp->inum == stbuf.st_ino &&
				    lp->devnum == stbuf.st_dev) {
					found++;
					break;
				}
			if (found) {
				if (strlen(lp->pathname) >
				    (size_t)(NAMSIZ -1)) {
					(void) fprintf(stderr, gettext(
					"tar: %s: linked to %s\n"),
					longname, lp->pathname);
					(void) fprintf(stderr, gettext(
					"tar: %s: linked name too long\n"),
					lp->pathname);
					if (eflag)
						done(1);
					if (aclp != NULL)
						free(aclp);
					return;
				}
				stbuf.st_size = 0;
				build_dblock(name, lp->pathname, "ustar",
				    "00", '4', stbuf.st_uid, stbuf.st_gid,
				    stbuf.st_dev, prefix);
				if (mulvol && tapepos + 1 >= blocklim)
					newvol();
				writetape((char *) &dblock);
				if (vflag) {
					if (NotTape)
						DEBUG("seek = %luK\t",
						    K(tapepos), 0);
					(void) fprintf(vfile, gettext(
					"a %s link to %s\n"),
					    longname, lp->pathname);
				}
				lp->count--;
				if (aclp != NULL)
					free(aclp);
				return;
			} else {
				lp = (struct linkbuf *) getmem(sizeof (*lp));
				if (lp != NULL) {
					lp->nextp = ihead;
					ihead = lp;
					lp->inum = stbuf.st_ino;
					lp->devnum = stbuf.st_dev;
					lp->count = stbuf.st_nlink - 1;
					(void) strcpy(lp->pathname, longname);
				}
			}
		}
		tomodes(&stbuf);

		while (mulvol && tapepos + blocks + 1 > blocklim) {
			if (eflag) {
				if (blocks <= blocklim) {
					newvol();
					break;
				}
				(void) fprintf(stderr, gettext(
				"tar: Single file cannot fit on volume\n"));
				done(3);
			}

			if (((blocklim - tapepos) >= EXTMIN) &&
			    ((blocks + 1) >= blocklim/10)) {
				splitfile(longname, infile);
				if (aclp != NULL)
					free(aclp);
				return;
			}
			newvol();
		}

		DEBUG("putfile: %s wants %lu blocks\n", longname, blocks);
		if (vflag) {
			if (NotTape)
				DEBUG("seek = %luK\t", K(tapepos), 0);
			(void) fprintf(vfile, "a %s ", longname);
			if (NotTape)
				(void) fprintf(vfile, "%luK\n", K(blocks));
			else
				(void) fprintf(vfile,
				    gettext("%lu tape blocks\n"),
				    (unsigned long)blocks);
		}
		build_dblock(name, tchar, "ustar", "00", '4',
		    stbuf.st_uid, stbuf.st_gid, stbuf.st_rdev, prefix);

		/* ACL support */
		if (pflag) {
			char	*secinfo = NULL;
			int	len = 0;

			/* append security attributes */
			append_secattr(&secinfo, &len, aclcnt,
			    (char *)aclp, UFSD_ACL);

			/* call append_secattr() if more than one */

			/* write ancillary */
			(void) write_ancillary(&dblock, secinfo, len);
		}
		(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
		dblock.dbuf.typeflag = '4';

		writetape((char *)&dblock);
		break;
	default:
		(void) fprintf(stderr, gettext(
		"tar: %s is not a file. Not dumped\n"),
		    longname);
		if (eflag)
			done(1);
		break;
	}

	/* free up acl stuff */
	if (pflag && aclp != NULL) {
		free(aclp);
		aclp = NULL;
	}
}


/*
 *	splitfile	dump a large file across volumes
 *
 *	splitfile(longname, fd);
 *		char *longname;		full name of file
 *		int ifd;		input file descriptor
 *
 *	NOTE:  only called by putfile() to dump a large file.
 */

static void
splitfile(char *longname, int ifd)
{
	unsigned long blocks, bytes, s;
	char buf[TBLOCK];
	register int i = 0, extents = 0;

	blocks = TBLOCKS(stbuf.st_size);	/* blocks file needs */

	/*
	 * # extents =
	 *	size of file after using up rest of this floppy
	 *		blocks - (blocklim - tapepos) + 1	(for header)
	 *	plus roundup value before divide by blocklim-1
	 *		+ (blocklim - 1) - 1
	 *	all divided by blocklim-1 (one block for each header).
	 * this gives
	 *	(blocks - blocklim + tapepos + 1 + blocklim - 2)/(blocklim-1)
	 * which reduces to the expression used.
	 * one is added to account for this first extent.
	 */
	extents = (blocks + tapepos - 1L)/(blocklim - 1L) + 1;

	if (extents < 2 || extents > MAXEXT) {	/* let's be reasonable */
		(void) fprintf(stderr, gettext(
		"tar: %s needs unusual number of volumes to split\n"
		"tar: %s not dumped\n"), longname, longname);
		return;
	}
	extents = dblock.dbuf.extotal;
	bytes = stbuf.st_size;
	(void) sprintf(dblock.dbuf.efsize, "%lo", bytes);

	(void) fprintf(stderr, gettext(
	"tar: large file %s needs %d extents.\n"
	"tar: current device seek position = %luK\n"),
	longname, extents, K(tapepos));

	s = (blocklim - tapepos - 1) * TBLOCK;
	for (i = 1; i <= extents; i++) {
		if (i > 1) {
			newvol();
			if (i == extents)
				s = bytes;	/* last ext. gets true bytes */
			else
				s = (blocklim - 1)*TBLOCK; /* whole volume */
		}
		bytes -= s;
		blocks = TBLOCKS(s);

		(void) sprintf(dblock.dbuf.size, "%lo", s);
		i = dblock.dbuf.extno;
		(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
		writetape((char *) &dblock);

		if (vflag)
			(void) fprintf(vfile,
			    "+++ a %s %luK [extent #%d of %d]\n",
			    longname, K(blocks), i, extents);
		while (blocks && read(ifd, buf, TBLOCK) > 0) {
			blocks--;
			writetape(buf);
		}
		if (blocks != 0) {
			(void) fprintf(stderr, gettext(
			"tar: %s: file changed size\n"), longname);
			(void) fprintf(stderr, gettext(
			"tar: aborting split file %s\n"), longname);
			(void) close(ifd);
			return;
		}
	}
	(void) close(ifd);
	if (vflag)
		(void) fprintf(vfile, gettext("a %s %luK (in %d extents)\n"),
			longname, K(TBLOCKS(stbuf.st_size)), extents);
}

static void
doxtract(char *argv[])
{
	struct	stat	xtractbuf;	/* stat on file after extracting */
	unsigned long blocks, bytes;
	int ofile = -1;
	int newfile;			/* Does the file already exist  */
	int xcnt = 0;			/* count # files extracted */
	int fcnt = 0;			/* count # files in argv list */
	int dir = 0;
	ushort Uid;
	char *namep, *linkp;		/* for removing absolute paths */
	char dirname[256];
	uid_t duid;
	gid_t dgid;
	int once = 1;
	int symflag;
	int want;
	aclent_t	*aclp = NULL;	/* acl buffer pointer */
	int		aclcnt = 0;	/* acl entries count */
	int		rc;		/* return value */

	dumping = 0;	/* for newvol(), et al:  we are not writing */

	/*
	 * Count the number of files that are to be extracted
	 */
	Uid = getuid();

	for (;;) {
		symflag = 0;
		namep = linkp = (char *) NULL;
		dir = 0;
		ofile = -1;

		if ((want = wantit(argv, &namep)) == 0)
			continue;
		if (want == -1)
			break;

		if (Fflag) {
			char *s;

			if ((s = strrchr(dblock.dbuf.name, '/')) == 0)
				s = dblock.dbuf.name;
			else
				s++;
			if (checkf(s, stbuf.st_mode, Fflag) == 0) {
				passtape();
				continue;
			}
		}

		if (checkw('x', namep) == 0) {
			passtape();
			continue;
		}
		if (once) {
			if (strncmp(dblock.dbuf.magic, "ustar", 5) == 0) {
				if (geteuid() == (uid_t) 0) {
					checkflag = 1;
					pflag = 1;
				} else {
					/* get file creation mask */
					Oumask = umask(0);
					(void) umask(Oumask);
				}
				once = 0;
			} else {
				if (geteuid() == (uid_t) 0) {
					pflag = 1;
					checkflag = 2;
				}
				if (!pflag) {
					/* get file creation mask */
					Oumask = umask(0);
					(void) umask(Oumask);
				}
				once = 0;
			}
		}

		(void) strcpy(&dirname[0], namep);
		if (checkdir(&dirname[0]) &&
			(!is_posix || dblock.dbuf.typeflag == '5')) {
			dir = 1;
			if (vflag) {
				(void) fprintf(vfile, "x %s, 0 bytes, ",
				    &dirname[0]);
				if (NotTape)
					(void) fprintf(vfile, "0K\n");
				else
					(void) fprintf(vfile, gettext(
					"0 tape blocks\n"));
			}
			goto filedone;
		}
		if (dblock.dbuf.typeflag == '6') {	/* FIFO */
			if (rmdir(namep) < 0) {
				if (errno == ENOTDIR)
					(void) unlink(namep);
			}
			linkp = dblock.dbuf.linkname;
			if (*linkp !=  NULL) {
				if (Aflag && *linkp == '/')
					linkp++;
				if (link(linkp, namep) < 0) {
					(void) fprintf(stderr, gettext(
					"tar: %s: cannot link\n"), namep);
					continue;
				}
				if (vflag)
					(void) fprintf(vfile, gettext(
					"%s linked to %s\n"), namep, linkp);
				xcnt++;	 /* increment # files extracted */
				continue;
			}
			if (mknod(namep, (int)(Gen.g_mode|S_IFIFO),
			    (int)Gen.g_devmajor) < 0) {
				vperror(0, gettext("%s: mknod failed"), namep);
				continue;
			}
			bytes = stbuf.st_size;
			blocks = TBLOCKS(bytes);
			if (vflag) {
				(void) fprintf(vfile, "x %s, %lu bytes, ",
				    namep, bytes);
				if (NotTape)
					(void) fprintf(vfile, "%luK\n",
					    K(blocks));
				else
					(void) fprintf(vfile, gettext(
					"%lu tape blocks\n"), blocks);
			}
			goto filedone;
		}
		if (dblock.dbuf.typeflag == '3' && !Uid) { /* CHAR SPECIAL */
			if (rmdir(namep) < 0) {
				if (errno == ENOTDIR)
					(void) unlink(namep);
			}
			linkp = dblock.dbuf.linkname;
			if (*linkp != NULL) {
				if (Aflag && *linkp == '/')
					linkp++;
				if (link(linkp, namep) < 0) {
					(void) fprintf(stderr, gettext(
					"tar: %s: cannot link\n"), namep);
					continue;
				}
				if (vflag)
					(void) fprintf(vfile, gettext(
					"%s linked to %s\n"), namep, linkp);
				xcnt++;	 /* increment # files extracted */
				continue;
			}
			if (mknod(namep, (int)(Gen.g_mode|S_IFCHR),
			    (int)makedev(Gen.g_devmajor, Gen.g_devminor)) < 0) {
				vperror(0, gettext(
				"%s: mknod failed"), namep);
				continue;
			}
			bytes = stbuf.st_size;
			blocks = TBLOCKS(bytes);
			if (vflag) {
				(void) fprintf(vfile, "x %s, %lu bytes, ",
				namep, bytes);
				if (NotTape)
					(void) fprintf(vfile, "%luK\n",
					    K(blocks));
				else
					(void) fprintf(vfile, gettext(
					"%lu tape blocks\n"), blocks);
			}
			goto filedone;
		} else if (dblock.dbuf.typeflag == '3' && Uid) {
			(void) fprintf(stderr, gettext(
			"Can't create special %s\n"), namep);
			continue;
		}

		/* BLOCK SPECIAL */

		if (dblock.dbuf.typeflag == '4' && !Uid) {
			if (rmdir(namep) < 0) {
				if (errno == ENOTDIR)
					(void) unlink(namep);
			}
			linkp = dblock.dbuf.linkname;
			if (*linkp != NULL) {
				if (Aflag && *linkp == '/')
					linkp++;
				if (link(linkp, namep) < 0) {
					(void) fprintf(stderr, gettext(
					"tar: %s: cannot link\n"), namep);
					continue;
				}
				if (vflag)
					(void) fprintf(vfile, gettext(
					"%s linked to %s\n"), namep, linkp);
				xcnt++;	 /* increment # files extracted */
				continue;
			}
			if (mknod(namep, (int)(Gen.g_mode|S_IFBLK),
			    (int)makedev(Gen.g_devmajor, Gen.g_devminor)) < 0) {
				vperror(0, gettext("%s: mknod failed"), namep);
				continue;
			}
			bytes = stbuf.st_size;
			blocks = TBLOCKS(bytes);
			if (vflag) {
				(void) fprintf(vfile, gettext(
				"x %s, %lu bytes, "), namep, bytes);
				if (NotTape)
					(void) fprintf(vfile, "%luK\n",
					    K(blocks));
				else
					(void) fprintf(vfile, gettext(
					"%lu tape blocks\n"), blocks);
			}
			goto filedone;
		} else if (dblock.dbuf.typeflag == '4' && Uid) {
			(void) fprintf(stderr,
			    gettext("Can't create special %s\n"), namep);
			continue;
		}
		if (dblock.dbuf.typeflag == '2') {	/* symlink */
			linkp = dblock.dbuf.linkname;
			if (Aflag && *linkp == '/')
				linkp++;
			if (rmdir(namep) < 0) {
				if (errno == ENOTDIR)
					(void) unlink(namep);
			}
			if (symlink(linkp, namep) < 0) {
				vperror(0, gettext("%s: symbolic link failed"),
				namep);
				continue;
			}
			if (vflag)
				(void) fprintf(vfile, gettext(
				"x %s symbolic link to %s\n"),
				    dblock.dbuf.name, linkp);
			symflag = 1;
			goto filedone;
		}
		if (dblock.dbuf.typeflag == '1') {
			linkp = dblock.dbuf.linkname;
			if (Aflag && *linkp == '/')
				linkp++;
			if (rmdir(namep) < 0) {
				if (errno == ENOTDIR)
					(void) unlink(namep);
			}
			if (link(linkp, namep) < 0) {
				(void) fprintf(stderr, gettext(
				    "tar: %s: cannot link\n"), namep);
				continue;
			}
			if (vflag)
				(void) fprintf(vfile, gettext(
				"%s linked to %s\n"), namep, linkp);
			xcnt++;		/* increment # files extracted */
			continue;
		}

		/* REGULAR FILES */

		if ((dblock.dbuf.typeflag == '0') ||
		    (dblock.dbuf.typeflag == NULL)) {
			delete_target(namep);
			linkp = dblock.dbuf.linkname;
			if (*linkp != NULL) {
				if (Aflag && *linkp == '/')
					linkp++;
				if (link(linkp, namep) < 0) {
					(void) fprintf(stderr, gettext(
					"tar: %s: cannot link\n"), namep);
					continue;
				}
				if (vflag)
					(void) fprintf(vfile, gettext(
					"%s linked to %s\n"), namep, linkp);
				xcnt++;	 /* increment # files extracted */
				continue;
			}
		newfile = ((stat(namep, &xtractbuf) == -1) ? TRUE : FALSE);
		if ((ofile = creat(namep, stbuf.st_mode & MODEMASK)) < 0) {
			(void) fprintf(stderr, gettext(
			"tar: %s - cannot create\n"), namep);
			passtape();
			continue;
		}

		if (extno != 0) {	/* file is in pieces */
			if (extotal < 1 || extotal > MAXEXT)
				(void) fprintf(stderr, gettext(
				    "tar: ignoring bad extent info for %s\n"),
				    namep);
			else {
				xsfile(ofile);	/* extract it */
				goto filedone;
			}
		}
		extno = 0;	/* let everyone know file is not split */
		bytes = stbuf.st_size;
		blocks = TBLOCKS(bytes);
		if (vflag) {
			(void) fprintf(vfile, "x %s, %lu bytes, ",
			    namep, bytes);
			if (NotTape)
				(void) fprintf(vfile, "%luK\n", K(blocks));
			else
				(void) fprintf(vfile,
				    gettext("%lu tape blocks\n"),
				    (unsigned long)blocks);
		}

		xblocks(bytes, ofile);
filedone:
		if (mflag == 0 && !symflag) {
			if (dir)
			    doDirTimes(namep, stbuf.st_mtime);
			else
			    setPathTimes(namep, stbuf.st_mtime);
		}

		/* moved this code from above */
		if (pflag && !symflag)
			(void) chmod(namep, stbuf.st_mode & MODEMASK);
		/*
		 * Because ancillary file preceeds the normal file,
		 * acl info may have been retrieved (in aclp).
		 * All file types are directed here (go filedone).
		 * Always restore ACLs if there are ACLs.
		 */
		if (aclp != NULL) {
			if (acl(namep, SETACL, aclcnt, aclp) < 0) {
				if (pflag) {
					fprintf(stderr, gettext(
					    "%s: failed to set acl entries\n"),
					    namep);
				}
				/* else: silent and continue */
			}
			free(aclp);
			aclp = NULL;
		}

		if (!oflag)
		    resugname(namep, symflag); /* set file ownership */

		if (pflag && newfile == TRUE && !dir &&
				(dblock.dbuf.typeflag == '0' ||
				    dblock.dbuf.typeflag == NULL ||
				    dblock.dbuf.typeflag == '1')) {
			if (fstat(ofile, &xtractbuf) == -1)
				(void) fprintf(stderr, gettext(
				"tar: cannot stat extracted file %s\n"), namep);
			else if ((xtractbuf.st_mode & (MODEMASK & ~S_IFMT))
				!= (stbuf.st_mode & (MODEMASK & ~S_IFMT))) {
				(void) fprintf(stderr, gettext(
				    "tar: warning - file permissions have "
				    "changed for %s (are 0%o, should be "
				    "0%o)\n"),
				    namep, xtractbuf.st_mode, stbuf.st_mode);
			}
		}
		if (ofile != -1) {
			if (close(ofile) != 0)
				vperror(2, gettext("close error"));
		}
		xcnt++;			/* increment # files extracted */
		}
		if (dblock.dbuf.typeflag == 'A') { 	/* acl info */
			char	buf[TBLOCK];
			char	*secp;
			char	*tp;
			int	attrsize;
			int	cnt;


			if (pflag) {
				bytes = stbuf.st_size;
				if ((secp = malloc(bytes)) == NULL) {
					(void) fprintf(stderr, gettext(
					    "Insufficient memory for acl\n"));
					passtape();
					continue;
				}
				tp = secp;
				blocks = TBLOCKS(bytes);
				while (blocks-- > 0) {
					readtape(buf);
					if (bytes <= TBLOCK) {
						memcpy(tp, buf, bytes);
						break;
					} else {
						memcpy(tp, buf, TBLOCK);
						tp += TBLOCK;
					}
					bytes -= TBLOCK;
				}
				/* got all attributes in secp */
				tp = secp;
				do {
					attr = (struct sec_attr *) tp;
					switch (attr->attr_type) {
					case UFSD_ACL:
						(void) sscanf(attr->attr_len,
						    "%7lo", &aclcnt);
						/* header is 8 */
						attrsize = 8 + strlen(
						    &attr->attr_info[0]) + 1;
						aclp = aclfromtext(
						    &attr->attr_info[0], &cnt);
						if (aclp == NULL) {
							fprintf(stderr, gettext(
					"aclfromtext failed\n"));
							break;
						}
						if (aclcnt != cnt) {
							fprintf(stderr, gettext(
							    "aclcnt error\n"));
							break;
						}
						bytes -= attrsize;
						break;

					/* SunFed case goes here */

					default:
						fprintf(stderr, gettext(
				"unrecognized attr type\n"));
						bytes = 0;
						break;
					}

					/* next attributes */
					tp += attrsize;
				} while (bytes > 0);
				free(secp);
			} else
				passtape();
		} /* acl */

	} /* for */

	/*
	 *  Ensure that all the directories still on the directory stack
	 *  get their modification times set correctly by flushing the
	 *  stack.
	 */

	doDirTimes(NULL, (time_t) 0);

	if (sflag) {
		getempty(1L);	/* don't forget extra EOT */
	}

	/*
	 * Check if the number of files extracted is different from the
	 * number of files listed on the command line
	 */
	if (fcnt > xcnt) {
		(void) fprintf(stderr,
		    gettext("tar: %d file(s) not extracted\n"),
		fcnt-xcnt);
		Errflg = 1;
	}
}

/*
 *	xblocks		extract file/extent from tape to output file
 *
 *	xblocks(bytes, ofile);
 *		long bytes;	size of extent or file to be extracted
 *
 *	called by doxtract() and xsfile()
 */
static void
xblocks(unsigned long bytes, int ofile)
{
	long blocks;
	char buf[TBLOCK];

	blocks = TBLOCKS(bytes);
	while (blocks-- > 0) {
		readtape(buf);
		if (bytes > TBLOCK) {
			if (write(ofile, buf, TBLOCK) < 0) {
exwrterr:
				(void) fprintf(stderr, gettext(
				"tar: %s: HELP - extract write error\n"),
				dblock.dbuf.name);
				done(2);
			}
		} else
			if (write(ofile, buf, (int) bytes) < 0)
				goto exwrterr;
		bytes -= TBLOCK;
	}
}



/*
 * 	xsfile	extract split file
 *
 *	xsfile(ofd);	ofd = output file descriptor
 *
 *	file extracted and put in ofd via xblocks()
 *
 *	NOTE:  only called by doxtract() to extract one large file
 */

static	union	hblock	savedblock;	/* to ensure same file across volumes */

static void
xsfile(int ofd)
{
	register i, c;
	char name[NAMSIZ];	/* holds name for diagnostics */
	int extents, totalext;
	unsigned long bytes, totalbytes;

	(void) strncpy(name, dblock.dbuf.name, NAMSIZ); /* save it */
	totalbytes = 0L;	/* in case we read in half the file */
	totalext = 0;		/* these keep count */

	(void) fprintf(stderr, gettext(
	"tar: %s split across %d volumes\n"), name, extotal);

	/* make sure we do extractions in order */
	if (extno != 1) {	/* starting in middle of file? */
		wchar_t yeschar;
		wchar_t nochar;
		mbtowc(&yeschar, nl_langinfo(YESSTR), MB_LEN_MAX);
		mbtowc(&nochar, nl_langinfo(NOSTR), MB_LEN_MAX);
		(void) printf(gettext(
		"tar: first extent read is not #1\n"
		"OK to read file beginning with extent #%d (%wc/%wc) ? "),
		extno, yeschar, nochar);
		if (yesnoresponse() != yeschar) {
canit:
			passtape();
			if (close(ofd) != 0)
				vperror(2, gettext("close error"));
			return;
		}
	}
	extents = extotal;
	i = extno;
	while (1) {
		bytes = stbuf.st_size;
		if (vflag)
			(void) fprintf(vfile,
			    "+++ x %s [extent #%d], %lu bytes, %luK\n",
				name, extno, bytes, K(TBLOCKS(bytes)));
		xblocks(bytes, ofd);

		totalbytes += bytes;
		totalext++;
		if (++i > extents)
			break;

		/* get next volume and verify it's the right one */
		copy(&savedblock, &dblock);
tryagain:
		newvol();
		getdir();
		if (endtape()) {	/* seemingly empty volume */
			(void) fprintf(stderr, gettext(
			"tar: first record is null\n"));
asknicely:
			(void) fprintf(stderr, gettext(
			"tar: need volume with extent #%d of %s\n"), i, name);
			goto tryagain;
		}
		if (notsame()) {
			(void) fprintf(stderr, gettext(
			    "tar: first file on that volume is not "
			    "the same file\n"));
			goto asknicely;
		}
		if (i != extno) {
			(void) fprintf(stderr, gettext(
		"tar: extent #%d received out of order\ntar: should be #%d\n"),
		extno, i);
			(void) fprintf(stderr, gettext(
			    "Ignore error, Abort this file, or "
			    "load New volume (i/a/n) ? "));
			c = response();
			if (c == 'a')
				goto canit;
			if (c != 'i')		/* default to new volume */
				goto asknicely;
			i = extno;		/* okay, start from there */
		}
	}
	bytes = stbuf.st_size;
	if (vflag)
		(void) fprintf(vfile, gettext(
		"x %s (in %d extents), %lu bytes, %luK\n"),
			name, totalext, totalbytes, K(TBLOCKS(totalbytes)));
}



/*
 *	notsame()	check if extract file extent is invalid
 *
 *	returns true if anything differs between savedblock and dblock
 *	except extno (extent number), checksum, or size (extent size).
 *	Determines if this header belongs to the same file as the one we're
 *	extracting.
 *
 *	NOTE:	though rather bulky, it is only called once per file
 *		extension, and it can withstand changes in the definition
 *		of the header structure.
 *
 *	WARNING:	this routine is local to xsfile() above
 */
static int
notsame(void)
{
	return (
	    (strncmp(savedblock.dbuf.name, dblock.dbuf.name, NAMSIZ)) ||
	    (strcmp(savedblock.dbuf.mode, dblock.dbuf.mode)) ||
	    (strcmp(savedblock.dbuf.uid, dblock.dbuf.uid)) ||
	    (strcmp(savedblock.dbuf.gid, dblock.dbuf.gid)) ||
	    (strcmp(savedblock.dbuf.mtime, dblock.dbuf.mtime)) ||
	    (savedblock.dbuf.typeflag != dblock.dbuf.typeflag) ||
	    (strncmp(savedblock.dbuf.linkname, dblock.dbuf.linkname, NAMSIZ)) ||
	    (savedblock.dbuf.extotal != dblock.dbuf.extotal) ||
	    (strcmp(savedblock.dbuf.efsize, dblock.dbuf.efsize)));
}


static void
dotable(char *argv[])
{
	char *curarg;
	int tcnt;			/* count # files tabled */
	int fcnt;			/* count # files in argv list */
	char *namep;
	int want;
	char aclchar = ' ';			/* either blank or '+' */

	dumping = 0;

	/* if not on magtape, maximize seek speed */
	if (NotTape && !bflag) {
#if SYS_BLOCK > TBLOCK
		nblock = SYS_BLOCK / TBLOCK;
#else
		nblock = 1;
#endif
	}
	/*
	 * Count the number of files that are to be tabled
	 */
	fcnt = tcnt = 0;

	for (;;) {

		if ((want = wantit(argv, &namep)) == 0)
			continue;
		if (want == -1)
			break;

tableit:
		if (dblock.dbuf.typeflag != 'A')
			++tcnt;
		/*
		 * ACL support:
		 * aclchar is introduced to indicate if there are
		 * acl entries. longt() now takes one extra argument.
		 */
		if (vflag) {
			if (dblock.dbuf.typeflag == 'A') {
				aclchar = '+';
				passtape();
				continue;
			}
			longt(&stbuf, aclchar);
			aclchar = ' ';
		}

		(void) printf("%s", namep);
		if (extno != 0) {
			if (vflag)
				(void) fprintf(vfile, gettext(
				"\n [extent #%d of %d] %lu bytes total"),
					extno, extotal, efsize);
			else
				(void) fprintf(vfile, gettext(
				" [extent #%d of %d]"), extno, extotal);
		}
		if (dblock.dbuf.typeflag == '1')
			/*
			 * TRANSLATION_NOTE
			 *	Subject is omitted here.
			 *	Translate this as if
			 *		<subject> linked to %s
			 */
			(void) printf(gettext(" linked to %s"),
			dblock.dbuf.linkname);
		if (dblock.dbuf.typeflag == '2')
			(void) printf(gettext(
			/*
			 * TRANSLATION_NOTE
			 *	Subject is omitted here.
			 *	Translate this as if
			 *		<subject> symbolic link to %s
			 */
			" symbolic link to %s"), dblock.dbuf.linkname);
		(void) printf("\n");
		passtape();
	}
	if (sflag) {
		getempty(1L);	/* don't forget extra EOT */
	}
	/*
	 * Check if the number of files tabled is different from the
	 * number of files listed on the command line
	 */
	if (fcnt > tcnt) {
		(void) fprintf(stderr, gettext(
		"tar: %d file(s) not found\n"), fcnt-tcnt);
		Errflg = 1;
	}
}

static void
putempty(register unsigned long n)
{
	char buf[TBLOCK];
	register char *cp;

	for (cp = buf; cp < &buf[TBLOCK]; )
		*cp++ = '\0';
	while (n--)
		writetape(buf);
}

static void
getempty(register long n)
{
	char buf[TBLOCK];
	register char *cp;

	if (!sflag)
		return;
	for (cp = buf; cp < &buf[TBLOCK]; )
		*cp++ = '\0';
	while (n-- > 0)
		sumupd(&Si, buf, TBLOCK);
}

static	ushort	Ftype = S_IFMT;
static	void
verbose(st, aclchar)
struct stat *st;
char aclchar;
{
	register int i, j, temp;
	mode_t mode;
	char modestr[12];

	for (i = 0; i < 11; i++)
		modestr[i] = '-';
	modestr[i] = '\0';

	/* a '+' sign is printed if there is ACL */
	modestr[i-1] = aclchar;

	mode = st->st_mode;
	for (i = 0; i < 3; i++) {
		temp = (mode >> (6 - (i * 3)));
		j = (i * 3) + 1;
		if (S_IROTH & temp)
			modestr[j] = 'r';
		if (S_IWOTH & temp)
			modestr[j + 1] = 'w';
		if (S_IXOTH & temp)
			modestr[j + 2] = 'x';
	}
	temp = st->st_mode & Ftype;
	switch (temp) {
		case (S_IFIFO):
			modestr[0] = 'p';
			break;
		case (S_IFCHR):
			modestr[0] = 'c';
			break;
		case (S_IFDIR):
			modestr[0] = 'd';
			break;
		case (S_IFBLK):
			modestr[0] = 'b';
			break;
		case (S_IFREG): /* was initialized to '-' */
			break;
		case (S_IFLNK):
			modestr[0] = 'l';
			break;
		default:
			/* This field may be zero in old archives */
			if (is_posix)
				(void) fprintf(stderr, gettext(
				"tar: impossible file type"));
	}
	if ((S_ISUID & Gen.g_mode) == S_ISUID)
		modestr[3] = 's';
	if ((S_ISVTX & Gen.g_mode) == S_ISVTX)
		modestr[9] = 't';
	if ((S_ISGID & Gen.g_mode) == S_ISGID && modestr[6] == 'x')
		modestr[6] = 's';
	else if ((S_ENFMT & Gen.g_mode) == S_ENFMT && modestr[6] != 'x')
		modestr[6] = 'l';
	(void) fprintf(vfile, "%s", modestr);
}

static void
longt(register struct stat *st, char aclchar)
{
	char fileDate[30];
	struct tm *tm;

	verbose(st, aclchar);
	(void) fprintf(vfile, "%3d/%-3d", st->st_uid, st->st_gid);
	if (dblock.dbuf.typeflag == '2')
		st->st_size = strlen(dblock.dbuf.linkname);
	(void) fprintf(vfile, "%7lu", st->st_size);
	tm = localtime(&st->st_mtime);
	(void) strftime(fileDate, sizeof (fileDate),
	    dcgettext((const char *)0, "%b %e %R %Y", LC_TIME), tm);
	(void) fprintf(vfile, " %s ", fileDate);
}


/*
 *  checkdir - Attempt to ensure that the path represented in name
 *             exists, and return 1 if this is true and name itself is a
 *             directory.
 *             Return 0 if this path cannot be created or if name is not
 *             a directory.
 */

static int
checkdir(register char *name)
{
	char lastChar;		   /* the last character in name */
	char *cp;		   /* scratch pointer into name */
	char *firstSlash = NULL;   /* first slash in name */
	char *lastSlash = NULL;	   /* last slash in name */
	int  nameLen;		   /* length of name */
	int  trailingSlash;	   /* true if name ends in slash */
	int  leadingSlash;	   /* true if name begins with slash */
	int  markedDir;		   /* true if name denotes a directory */
	int  success;		   /* status of makeDir call */


	/*
	 *  Scan through the name, and locate first and last slashes.
	 */

	for (cp = name; *cp; cp++) {
		if (*cp == '/') {
			if (! firstSlash) {
				firstSlash = cp;
			}
			lastSlash = cp;
		}
	}

	/*
	 *  Determine what you can from the proceeds of the scan.
	 */

	lastChar	= *(cp - 1);
	nameLen		= cp - name;
	trailingSlash	= (lastChar == '/');
	leadingSlash	= (*name == '/');
	markedDir	= (dblock.dbuf.typeflag == '5' || trailingSlash);

	if (! lastSlash && ! markedDir) {
		/*
		 *  The named file does not have any subdrectory
		 *  structure; just bail out.
		 */

		return (0);
	}

	/*
	 *  Make sure that name doesn`t end with slash for the loop.
	 *  This ensures that the makeDir attempt after the loop is
	 *  meaningful.
	 */

	if (trailingSlash) {
		name[nameLen-1] = '\0';
	}

	/*
	 *  Make the path one component at a time.
	 */

	for (cp = strchr(leadingSlash ? name+1 : name, '/');
	    cp;
	    cp = strchr(cp+1, '/')) {
		*cp = '\0';
		success = makeDir(name);
		*cp = '/';

		if (!success) {
			name[nameLen-1] = lastChar;
			return (0);
		}
	}

	/*
	 *  This makes the last component of the name, if it is a
	 *  directory.
	 */

	if (markedDir) {
		if (! makeDir(name)) {
			name[nameLen-1] = lastChar;
			return (0);
		}
	}

	name[nameLen-1] = (lastChar == '/') ? '\0' : lastChar;
	return (markedDir);
}

/*
 * resugname - Restore the user name and group name.  Search the NIS
 *             before using the uid and gid.
 *             (It is presumed that an archive entry cannot be
 *	       simultaneously a symlink and some other type.)
 */

static void
resugname(register char *name,		/* name of the file to be modified */
	    int symflag)		/* true if file is a symbolic link */
{
	uid_t duid;
	gid_t dgid;

	if (checkflag == 1) { /* Extended tar format and euid == 0 */
		duid = getuidbyname(&dblock.dbuf.uname[0]);
		dgid = getgidbyname(&dblock.dbuf.gname[0]);
		if (duid == -1)
		    duid = stbuf.st_uid;
		if (dgid == -1)
		    dgid = stbuf.st_gid;
	} else if (checkflag == 2) { /* tar format and euid == 0 */
		duid = stbuf.st_uid;
		dgid = stbuf.st_gid;
	}
	if ((checkflag == 1) || (checkflag == 2))
	    if (symflag)
		(void) lchown(name, duid, dgid);
	    else
		(void) chown(name, duid, dgid);
}

/*ARGSUSED*/
static void
onintr(int sig)
{
	(void) signal(SIGINT, SIG_IGN);
	term++;
}

/*ARGSUSED*/
static void
onquit(int sig)
{
	(void) signal(SIGQUIT, SIG_IGN);
	term++;
}

/*ARGSUSED*/
static void
onhup(int sig)
{
	(void) signal(SIGHUP, SIG_IGN);
	term++;
}

static void
tomodes(register struct stat *sp)
{
	register char *cp;

	for (cp = dblock.dummy; cp < &dblock.dummy[TBLOCK]; cp++)
		*cp = '\0';
	(void) sprintf(dblock.dbuf.mode, "%07o", sp->st_mode & MODEMASK);
	(void) sprintf(dblock.dbuf.uid, "%07o", sp->st_uid);
	(void) sprintf(dblock.dbuf.gid, "%07o", sp->st_gid);
	(void) sprintf(dblock.dbuf.size, "%011lo", sp->st_size);
	(void) sprintf(dblock.dbuf.mtime, "%011lo", sp->st_mtime);
}

static	int
#ifdef	EUC
/*
 * Warning:  the result of this function depends whether 'char' is a
 * signed or unsigned data type.  This a source of potential
 * non-portability among heterogeneous systems.  It is retained here
 * for backward compatibility.
 */
checksum_signed(void)
#else
checksum(void)
#endif	EUC
{
	register i;
	register char *cp;

	for (cp = dblock.dbuf.chksum;
	    cp < &dblock.dbuf.chksum[sizeof (dblock.dbuf.chksum)]; cp++)
		*cp = ' ';
	i = 0;
	for (cp = dblock.dummy; cp < &dblock.dummy[TBLOCK]; cp++)
		i += *cp;

	return (i);
}

#ifdef	EUC
/*
 * Generate unsigned checksum, regardless of what C compiler is
 * used.  Survives in the face of arbitrary 8-bit clean filenames,
 * e.g., internationalized filenames.
 */
static int
checksum(void)
{
	register unsigned i;
	register unsigned char *cp;

	for (cp = (unsigned char *) dblock.dbuf.chksum;
	    cp < (unsigned char *)
	    &dblock.dbuf.chksum[sizeof (dblock.dbuf.chksum)]; cp++)
		*cp = ' ';
	i = 0;
	for (cp = (unsigned char *) dblock.dummy;
	    cp < (unsigned char *) &dblock.dummy[TBLOCK]; cp++)
		i += *cp;

	return (i);
}
#endif	EUC

static int
checkw(char c, char *name)
{
	if (wflag) {
		(void) fprintf(vfile, "%c ", c);
		if (vflag)
			longt(&stbuf, ' ');	/* do we have acl info here */
		(void) fprintf(vfile, "%s: ", name);
		if (response() == 'y') {
			return (1);
		}
		return (0);
	}
	return (1);
}

static int
checkf(char *name, int mode, int howmuch)
{
	int l;

	if ((mode & S_IFMT) == S_IFDIR) {
		if ((strcmp(name, "SCCS") == 0) || (strcmp(name, "RCS") == 0))
			return (0);
		return (1);
	}
	if ((l = strlen(name)) < 3)
		return (1);
	if (howmuch > 1 && name[l-2] == '.' && name[l-1] == 'o')
		return (0);
	if (howmuch > 1) {
		if (strcmp(name, "core") == 0 || strcmp(name, "errs") == 0 ||
		    strcmp(name, "a.out") == 0)
			return (0);
	}

	/* SHOULD CHECK IF IT IS EXECUTABLE */
	return (1);
}

static int
response(void)
{
	register int c;

	c = getchar();
	if (c != '\n')
		while (getchar() != '\n');
	else c = 'n';
	return ((c >= 'A' && c <= 'Z') ? c + ('a'-'A') : c);
}

static int
checkupdate(char *arg)
{
	char name[MAXNAM];	/* was 100; parameterized */
	long	mtime;
	daddr_t seekp;
	static daddr_t	lookup();

	rewind(tfile);
		if ((seekp = lookup(arg)) < 0)
			return (1);
		(void) fseek(tfile, seekp, 0);
		(void) fscanf(tfile, "%s %lo", name, &mtime);
		return (stbuf.st_mtime > mtime);
/* NOTREACHED */
}


/*
 *	newvol	get new floppy (or tape) volume
 *
 *	newvol();		resets tapepos and first to TRUE, prompts for
 *				for new volume, and waits.
 *	if dumping, end-of-file is written onto the tape.
 */

static void
newvol(void)
{
	register int c;
	extern char *sys_errlist[];

	if (dumping) {
		DEBUG("newvol called with 'dumping' set\n", 0, 0);
		putempty(2L);	/* 2 EOT marks */
		closevol();
		flushtape();
		sync();
		tapepos = 0;
	} else
		first = TRUE;
	if (close(mt) != 0)
		vperror(2, gettext("close error"));
	mt = 0;
	if (sflag) {
		sumepi(&Si);
		sumout(Sumfp, &Si);
		(void) fprintf(Sumfp, "\n");

		sumpro(&Si);
	}
	(void) fprintf(stderr, gettext(
	"tar: \007please insert new volume, then press RETURN."));
	(void) fseek(stdin, 0L, 2);	/* scan over read-ahead */
	while ((c = getchar()) != '\n' && ! term)
		if (c == EOF)
			done(0);
	if (term)
		done(0);
#ifdef LISA
	sleep(3);		/* yecch */
#endif

	errno = 0;

	mt = (strcmp(usefile, "-") == 0) ?
	    dup(1) : open(usefile, dumping ? update : 0);
	if (mt < 0) {
		(void) fprintf(stderr, gettext(
		"tar: cannot reopen %s (%s)\n"),
		dumping ? gettext("output") : gettext("input"), usefile);

(void) fprintf(stderr, "update=%d, usefile=%s, mt=%d, [%s]\n",
	update, usefile, mt, sys_errlist[errno]);

		done(2);
	}
}

/*
 * Write a trailer portion to close out the current output volume.
 */

static void
closevol(void)
{
	if (mulvol && Sflag) {
		/*
		 * blocklim does not count the 2 EOT marks;
		 * tapepos  does     count the 2 EOT marks;
		 * therefore we need the +2 below.
		 */
		putempty(blocklim + 2L - tapepos);
	}
}

static void
done(int n)
{
	(void) unlink(tname);
	if (mt > 0) {
		if ((close(mt) != 0) || (fclose(stdout) != 0)) {
			perror(gettext("tar: close error"));
			exit(2);
		}
	}
	exit(n);
}

static	int
prefix(register char *s1, register char *s2)
{
	while (*s1)
		if (*s1++ != *s2++)
			return (0);
	if (*s2)
		return (*s2 == '/');
	return (1);
}

#define	N	200
static	int	njab;
static	daddr_t
lookup(s)
char *s;
{
	register i;
	daddr_t a;

	for (i = 0; s[i]; i++)
		if (s[i] == ' ')
			break;
	a = bsrch(s, i, low, high);
	return (a);
}

static daddr_t
bsrch(char *s, int n, daddr_t l, daddr_t h)
{
	register i, j;
	char b[N];
	daddr_t m, m1;

	njab = 0;

loop:
	if (l >= h)
		return (-1L);
	m = l + (h-l)/2 - N/2;
	if (m < l)
		m = l;
	(void) fseek(tfile, m, 0);
	(void) fread(b, 1, N, tfile);
	njab++;
	for (i = 0; i < N; i++) {
		if (b[i] == '\n')
			break;
		m++;
	}
	if (m >= h)
		return (-1L);
	m1 = m;
	j = i;
	for (i++; i < N; i++) {
		m1++;
		if (b[i] == '\n')
			break;
	}
	i = cmp(b+j, s, n);
	if (i < 0) {
		h = m;
		goto loop;
	}
	if (i > 0) {
		l = m1;
		goto loop;
	}
	return (m);
}

static int
cmp(register char *b, register char *s, int n)
{
	register i;

	if (b[0] != '\n')
		exit(2);
	for (i = 0; i < n; i++) {
		if (b[i+1] > s[i])
			return (-1);
		if (b[i+1] < s[i])
			return (1);
	}
	return (b[i+1] == ' '? 0 : -1);
}


/*
 *	seekdisk	seek to next file on archive
 *
 *	called by passtape() only
 *
 *	WARNING: expects "nblock" to be set, that is, readtape() to have
 *		already been called.  Since passtape() is only called
 *		after a file header block has been read (why else would
 *		we skip to next file?), this is currently safe.
 *
 *	changed to guarantee SYS_BLOCK boundary
 */

static void
seekdisk(long blocks)
{
	unsigned long seekval;
#if SYS_BLOCK > TBLOCK
	/* handle non-multiple of SYS_BLOCK */
	register nxb;	/* # extra blocks */
#endif

	tapepos += blocks;
	DEBUG("seekdisk(%lu) called\n", blocks, 0);
	if (recno + blocks <= nblock) {
		recno += blocks;
		return;
	}
	if (recno > nblock)
		recno = nblock;
	seekval = blocks - (nblock - recno);
	recno = nblock;	/* so readtape() reads next time through */
#if SYS_BLOCK > TBLOCK
	nxb = (int) (seekval % (long)(SYS_BLOCK / TBLOCK));
	DEBUG("xtrablks=%d seekval=%lu blks\n", nxb, seekval);
	if (nxb && nxb > seekval) /* don't seek--we'll read */
		goto noseek;
	seekval -= (long) nxb;	/* don't seek quite so far */
#endif
	if (lseek(mt, (long) TBLOCK * seekval, 1) == -1L) {
		(void) fprintf(stderr, gettext(
		"tar: device seek error\n"));
		done(3);
	}
#if SYS_BLOCK > TBLOCK
	/* read those extra blocks */
noseek:
	if (nxb) {
		DEBUG("reading extra blocks\n", 0, 0);
		if (read(mt, tbuf, TBLOCK*nblock) < 0) {
			(void) fprintf(stderr, gettext(
			"tar: read error while skipping file\n"));
			done(8);
		}
		recno = nxb;	/* so we don't read in next readtape() */
	}
#endif
}

static void
readtape(char *buffer)
{
	register int i, j;

	++tapepos;
	if (recno >= nblock || first) {
		if (first) {
			/*
			 * set the number of blocks to
			 * read initially.
			 * very confusing!
			 */
			if (bflag)
				j = nblock;
			else if (!NotTape)
				j = NBLOCK;
			else if (defaults_used)
				j = nblock;
			else
				j = NBLOCK;
		} else
			j = nblock;
		if ((i = read(mt, tbuf, TBLOCK*j)) < 0) {
			(void) fprintf(stderr, gettext(
			"tar: tape read error\n"));
			done(3);
		} else if ((!first || Bflag) && i != TBLOCK*j) {
			/*
			 * Short read - try to get the remaining bytes.
			 */

			int remaining = (TBLOCK * j) - i;
			char *b = (char *)tbuf + i;
			int r;

			do {
				if ((r = read(mt, b, remaining)) < 0) {
					(void) fprintf(stderr,
					gettext("tar: tape read error\n"));
					done(3);
				}
				b += r; remaining -= r; i += r;
			} while (remaining > 0 && r != 0);
		}
		if (first) {
			if ((i % TBLOCK) != 0) {
				(void) fprintf(stderr, gettext(
				"tar: tape blocksize error\n"));
				done(3);
			}
			i /= TBLOCK;
			if (vflag && i != nblock && i != 1) {
				if (!NotTape)
					(void) fprintf(stderr,
					gettext("tar: blocksize = %d\n"), i);
			}

			/*
			 * If we are reading a tape, then a short read is
			 * understood to signify that the amount read is
			 * the tape's actual blocking factor.  We adapt
			 * nblock accordingly.  There is no reason to do
			 * this when the device is not blocked.
			 */

			if (!NotTape)
				nblock = i;
		}
		recno = 0;
	}

	first = FALSE;
	copy(buffer, &tbuf[recno++]);
	if (sflag)
		sumupd(&Si, buffer, TBLOCK);
}


/*
 * replacement for writetape.
 * XXX -- need to do sumupd() grot if sflag, no?
 */

static int
writetbuf(register char *buffer, register int n)
{
	register int i;

	tapepos += n;		/* output block count */

	if (recno >= nblock) {
		i = write(mt, (char *)tbuf, TBLOCK*nblock);
		if (i != TBLOCK*nblock)
			mterr("write", i, 2);
		recno = 0;
	}

	/*
	 *  Special case:  We have an empty tape buffer, and the
	 *  users data size is >= the tape block size:  Avoid
	 *  the bcopy and dma direct to tape.  BIG WIN.  Add the
	 *  residual to the tape buffer.
	 */
	while (recno == 0 && n >= nblock) {
		i = write(mt, buffer, TBLOCK*nblock);
		if (i != TBLOCK*nblock)
			mterr("write", i, 2);
		n -= nblock;
		buffer += (nblock * TBLOCK);
	}

	while (n-- > 0) {
		(void) memcpy((char *)&tbuf[recno++], buffer, TBLOCK);
		buffer += TBLOCK;
		if (recno >= nblock) {
			i = write(mt, (char *)tbuf, TBLOCK*nblock);
			if (i != TBLOCK*nblock)
				mterr("write", i, 2);
			recno = 0;
		}
	}

	/* Tell the user how much to write to get in sync */
	return (nblock - recno);
}

/*
 *	backtape - reposition tape after reading soft "EOF" record
 *
 *	Backtape tries to reposition the tape back over the EOF
 *	record.  This is for the -u and -r options so that the
 *	tape can be extended.  This code is not well designed, but
 *	I'm confident that the only callers who care about the
 *	backspace-over-EOF feature are those involved in -u and -r.
 *
 *	The proper way to backup the tape is through the use of mtio.
 *	Earlier spins used lseek combined with reads in a confusing
 *	maneuver that only worked on 4.x, but shouldn't have, even
 *	there.  Lseeks are explicitly not supported for tape devices.
 */

static void
backtape(void)
{
	struct mtop mtcmd;

	DEBUG("backtape() called, recno=%d nblock=%d\n", recno, nblock);

	/*
	 * Backup to the position in the archive where the record
	 * currently sitting in the tbuf buffer is situated.
	 */

	if (NotTape) {
		/*
		 * For non-tape devices, this means lseeking to the
		 * correct position.  The absolute location tapepos-recno
		 * should be the beginning of the current record.
		 */

		if (lseek(mt, (off_t) (TBLOCK*(tapepos-recno)), SEEK_SET) ==
		    (off_t) -1) {
			(void) fprintf(stderr,
			    gettext("tar: lseek to end of archive failed\n"));
			done(4);
		}
	} else {
		/*
		 * For tape devices, we backup over the most recently
		 * read record.
		 */

		mtcmd.mt_op = MTBSR;
		mtcmd.mt_count = 1;

		if (ioctl(mt, MTIOCTOP, &mtcmd) < 0) {
			(void) fprintf(stderr,
				gettext("tar: backspace over record failed\n"));
			done(4);
		}
	}

	/*
	 * Decrement the tape and tbuf buffer indices to prepare for the
	 * coming write to overwrite the soft EOF record.
	 */

	recno--;
	tapepos--;
}


/*
 *	flushtape  write buffered block(s) onto tape
 *
 *      recno points to next free block in tbuf.  If nonzero, a write is done.
 *	Care is taken to write in multiples of SYS_BLOCK when device is
 *	non-magtape in case raw i/o is used.
 *
 *	NOTE: this is called by writetape() to do the actual writing
 */

static void
flushtape(void)
{

	DEBUG("flushtape() called, recno=%d\n", recno, 0);
	if (recno > 0) {	/* anything buffered? */
		if (NotTape) {
#if SYS_BLOCK > TBLOCK
			register i;

			/*
			 * an odd-block write can only happen when
			 * we are at the end of a volume that is not a tape.
			 * Here we round recno up to an even SYS_BLOCK
			 * boundary.
			 */
			if ((i = recno % (SYS_BLOCK / TBLOCK)) != 0) {
				DEBUG("flushtape() %d rounding blocks\n", i, 0);
				recno += i;	/* round up to even SYS_BLOCK */
			}
#endif
			if (recno > nblock)
				recno = nblock;
		}
		DEBUG("writing out %d blocks of %d bytes\n",
			(NotTape ? recno : nblock),
			(NotTape ? recno : nblock) * TBLOCK);

		if (write(mt, tbuf, (NotTape ? recno : nblock) * TBLOCK) < 0) {
			(void) fprintf(stderr, gettext(
			"tar: tape write error\n"));
			done(2);
		}
		if (sflag)
			sumupd(&Si, tbuf, (NotTape ? recno : nblock) * TBLOCK);
		recno = 0;
	}
}

static void
copy(void *dst, void *src)
{
	register char *to = (char *)dst;
	register char *from = (char *)src;
	register int i;

	i = TBLOCK;
	do {
		*to++ = *from++;
	} while (--i);
}

/*
 * kcheck()
 *	- checks the validity of size values for non-tape devices
 *	- if size is zero, mulvol tar is disabled and size is
 *	  assumed to be infinite.
 *	- returns volume size in TBLOCKS
 */

static long
kcheck(char *kstr)
{
	long kval;

	kval = atol(kstr);
	if (kval == 0L) {	/* no multi-volume; size is infinity.  */
		mulvol = 0;	/* definitely not mulvol, but we must  */
		return (0);	/* took out setting of NotTape */
	}
	if (kval < (long) MINSIZE) {
		(void) fprintf(stderr, gettext(
		"tar: sizes below %luK not supported (%lu).\n"),
				(long) MINSIZE, kval);
		if (!kflag)
			(void) fprintf(stderr, gettext(
			"bad size entry for %s in %s.\n"),
				archive, DEF_FILE);
		done(1);
	}
	mulvol++;
	NotTape++;			/* implies non-tape */
	return (kval * 1024L / TBLOCK);	/* convert to TBLOCKS */
}


/*
 * bcheck()
 *	- checks the validity of blocking factors
 *	- returns blocking factor
 */

static int
bcheck(char *bstr)
{
	int bval;

	bval = atoi(bstr);
	if (bval <= 0) {
		(void) fprintf(stderr, gettext(
		"tar: invalid blocksize \"%s\".\n"), bstr);
		if (!bflag)
			(void) fprintf(stderr, gettext(
			    "bad blocksize entry for '%s' in %s.\n"),
			    archive, DEF_FILE);
		done(1);
	}
	return (bval);
}


/*
 * defset()
 *	- reads DEF_FILE for the set of default values specified.
 *	- initializes 'usefile', 'nblock', and 'blocklim', and 'NotTape'.
 *	- 'usefile' points to static data, so will be overwritten
 *	  if this routine is called a second time.
 *	- the pattern specified by 'arch' must be followed by four
 *	  blank-separated fields (1) device (2) blocking,
 *				 (3) size(K), and (4) tape
 *	  for example: archive0=/dev/fd 1 400 n
 *	- the define's below are used in defcntl() to ignore case.
 */

#define	DC_GETFLAGS	0
#define	DC_SETFLAGS	1
#define	DC_CASE		0001
#define	DC_STD		((0) | DC_CASE)

static int
defset(char *arch)
{
	extern int defcntl(), defopen();
	extern char *defread();
	char *bp;

	if (defopen(DEF_FILE) != 0)
		return (FALSE);
	if (defcntl(DC_SETFLAGS, (DC_STD & ~(DC_CASE))) == -1) {
		(void) fprintf(stderr, gettext(
		"tar: error setting parameters for %s.\n"),
				DEF_FILE);
		return (FALSE);			/* & following ones too */
	}
	if ((bp = defread(arch)) == NULL) {
		(void) fprintf(stderr, gettext(
		"tar: missing or invalid '%s' entry in %s.\n"),
				arch, DEF_FILE);
		return (FALSE);
	}
	if ((usefile = strtok(bp, " \t")) == NULL) {
		(void) fprintf(stderr, gettext(
		"tar: '%s' entry in %s is empty!\n"), arch, DEF_FILE);
		return (FALSE);
	}
	if ((bp = strtok(NULL, " \t")) == NULL) {
		(void) fprintf(stderr, gettext(
		    "tar: block component missing in '%s' entry in %s.\n"),
		    arch, DEF_FILE);
		return (FALSE);
	}
	nblock = bcheck(bp);
	if ((bp = strtok(NULL, " \t")) == NULL) {
		(void) fprintf(stderr, gettext(
		    "tar: size component missing in '%s' entry in %s.\n"),
		    arch, DEF_FILE);
		return (FALSE);
	}
	blocklim = kcheck(bp);
	if ((bp = strtok(NULL, " \t")) != NULL)
		NotTape = (*bp == 'n' || *bp == 'N');
	else
		NotTape = (blocklim != 0);
	defopen(NULL);
	DEBUG("defset: archive='%s'; usefile='%s'\n", arch, usefile);
	DEBUG("defset: nblock='%d'; blocklim='%ld'\n",
	    nblock, blocklim);
	DEBUG("defset: not tape = %d\n", NotTape, 0);
	return (TRUE);
}


/*
 * Following code handles excluded and included files.
 * A hash table of file names to be {in,ex}cluded is built.
 * For excluded files, before writing or extracting a file
 * check to see if it is in the exluce_tbl.
 * For included files, the wantit() procedure will check to
 * see if the named file is in the include_tbl.
 */

static void
build_table(struct file_list *table[], char *file)
{
	FILE	*fp;
	char	buf[512];

	if ((fp = fopen(file, "r")) == (FILE *)NULL)
		vperror(1, gettext("could not open %s"), file);
	while (fgets(buf, sizeof (buf), fp) != NULL) {
		buf[strlen(buf) - 1] = '\0';
		add_file_to_table(table, buf);
	}
	(void) fclose(fp);
}


/*
 * Add a file name to the the specified table, if the file name has any
 * trailing '/'s then delete them before inserting into the table
 */

static void
add_file_to_table(struct file_list *table[], char *str)
{
	char	name[MAXNAM];
	unsigned int h;
	struct	file_list	*exp;

	(void) strcpy(name, str);
	while (name[strlen(name) - 1] == '/') {
		name[strlen(name) - 1] = NULL;
	}

	h = hash(name);
	if ((exp = (struct file_list *) calloc(sizeof (struct file_list),
	    sizeof (char))) == NULL) {
		(void) fprintf(stderr, gettext(
		    "tar: out of memory, exclude/include table(entry)\n"));
		exit(1);
	}

	if ((exp->name = strdup(name)) == NULL) {
		(void) fprintf(stderr, gettext(
		    "tar: out of memory, exclude/include table(file name)\n"));
		exit(1);
	}

	exp->next = table[h];
	table[h] = exp;
}


/*
 * See if a file name or any of the file's parent directories is in the
 * specified table, if the file name has any trailing '/'s then delete
 * them before searching the table
 */

static int
is_in_table(struct file_list *table[], char *str)
{
	char	name[MAXNAM];
	unsigned int	h;
	struct	file_list	*exp;
	char	*ptr;

	(void) strcpy(name, str);
	while (name[strlen(name) - 1] == '/') {
		name[strlen(name) - 1] = NULL;
	}

	/*
	 * check for the file name in the passed list
	 */
	h = hash(name);
	exp = table[h];
	while (exp != NULL) {
		if (strcmp(name, exp->name) == 0) {
			return (1);
		}
		exp = exp->next;
	}

	/*
	 * check for any parent directories in the file list
	 */
	while (ptr = strrchr(name, '/')) {
		*ptr = NULL;
		h = hash(name);
		exp = table[h];
		while (exp != NULL) {
			if (strcmp(name, exp->name) == 0) {
				return (1);
			}
			exp = exp->next;
		}
	}

	return (0);
}


/*
 * Compute a hash from a string.
 */

static unsigned int
hash(char *str)
{
	char	*cp;
	unsigned int	h;

	h = 0;
	for (cp = str; *cp; cp++) {
		h += *cp;
	}
	return (h % TABLE_SIZE);
}


static	char *
getmem(size)
{
	char *p = calloc((unsigned) size, sizeof (char));

	if (p == NULL && freemem) {
		(void) fprintf(stderr, gettext(
		    "tar: out of memory, link and directory modtime "
		    "info lost\n"));
		freemem = 0;
		if (eflag)
			done(1);
	}
	return (p);
}


/*
 * vperror() --variable argument perror.
 * Takes 3 args: exit_status, formats, args.  If exit status is 0, then
 * the eflag (exit on error) is checked -- if it is non-zero, tar exits
 * with the value of whatever "errno" is set to.  If exit_status is not
 * zero, then it exits with that error status. If eflag and exit_status
 * are both zero, the routine returns to where it was called.
 */

static void
vperror(int exit_status, register char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	(void) fputs("tar: ", stderr);
	(void) vfprintf(stderr, fmt, ap);
	(void) fprintf(stderr, ": %s\n", strerror(errno));
	va_end(ap);
	if (exit_status)
		done(exit_status);
	else if (eflag)
		done(errno);
}


static void
fatal(char *format, ...)
{
	va_list	ap;

	va_start(ap, format);
	(void) fprintf(stderr, "tar: ");
	(void) vfprintf(stderr, format, ap);
	(void) fprintf(stderr, "\n");
	va_end(ap);
	done(1);
}


/*
 * Check to make sure that argument is a char * ptr.
 * Actually, we just check to see that it is non-null.
 * If it is null, print out the message and call usage(), bailing out.
 */

static void
assert_string(char *s, char *msg)
{
	if (s == NULL) {
		(void) fprintf(stderr, msg);
		usage();
	}
}


static void
mterr(char *operation, int i, int exitcode)
{
	fprintf(stderr, gettext(
	"tar: %s error: "), operation);
	if (i < 0)
		perror("");
	else
		fprintf(stderr, gettext("unexpected EOF\n"));
	done(exitcode);
}

static int
wantit(char *argv[], char **namep)
{
	register char **cp;
	int gotit;		/* true if we've found a match */

	getdir();
	check_prefix(namep);	/* sets *namep to point at the proper */
				/* name  */
	if (endtape()) {
		if (Bflag) {
			/*
			 * Logically at EOT - consume any extra blocks
			 * so that write to our stdin won't fail and
			 * emit an error message; otherwise something
			 * like "dd if=foo.tar | (cd bar; tar xvf -)"
			 * will produce a bogus error message from "dd".
			 */

			while (read(mt, tbuf, TBLOCK*nblock) > 0) {
				/* empty body */
			}
		}
		return (-1);
	}

	gotit = 0;

	if ((Iflag && is_in_table(include_tbl, *namep)) ||
	    (! Iflag && *argv == NULL)) {
		gotit = 1;
	} else {
		for (cp = argv; *cp; cp++) {
			if (prefix(*cp, *namep)) {
				gotit = 1;
				break;
			}
		}
	}

	if (! gotit) {
		passtape();
		return (0);
	}

	if (Xflag && is_in_table(exclude_tbl, *namep)) {
		if (vflag) {
			fprintf(stderr, gettext("%s excluded\n"),
			    *namep);
		}
		passtape();
		return (0);
	}

	return (1);
}


/*
 *  Return through *namep a pointer to the proper fullname (i.e  "<name> |
 *  <prefix>/<name>"), as represented in the header entry dblock.dbuf.
 */

static void
check_prefix(char **namep)
{
	static char fullname[MAXNAM];
	size_t k;

	if (dblock.dbuf.prefix[0] != '\0') {
		for (k = 0; k < PRESIZ && dblock.dbuf.prefix[k]; k++) {
			/* empty body */
		}

		fullname[0] = '\0';
		strncat(fullname, dblock.dbuf.prefix, k);
		strcat(fullname, "/");
		strncat(fullname, dblock.dbuf.name, NAMSIZ);
		*namep = fullname;
	} else {
		*namep = dblock.dbuf.name;
	}
}


static wchar_t
yesnoresponse(void)
{
	register wchar_t c;

	c = getwchar();
	if (c != '\n')
		while (getwchar() != '\n');
	else c = 0;
	return (c);
}


/*
 * Return true if the object indicated by the file descriptor and type
 * is a tape device, false otherwise
 */

static int
istape(int fd, int type)
{
	int result = 0;

	if (type & S_IFCHR) {
		struct mtget mtg;

		if (ioctl(fd, MTIOCGET, &mtg) != -1) {
			result = 1;
		}
	}

	return (result);
}

#include <utmp.h>

struct	utmp utmp;

#define	NMAX	(sizeof (utmp.ut_name))

typedef struct cachenode {	/* this struct must be zeroed before using */
	struct cachenode *next;	/* next in hash chain */
	int val;		/* the uid or gid of this entry */
	int namehash;		/* name's hash signature */
	char name[NMAX+1];	/* the string that val maps to */
} cachenode_t;

#define	HASHSIZE	256

static cachenode_t *names[HASHSIZE];
static cachenode_t *groups[HASHSIZE];
static cachenode_t *uids[HASHSIZE];
static cachenode_t *gids[HASHSIZE];

static int
hash_byname(char *name)
{
	int i, c, h = 0;

	for (i = 0; i < NMAX; i++) {
		c = name[i];
		if (c == '\0')
			break;
		h = (h << 4) + h + c;
	}
	return (h);
}

static cachenode_t *
hash_lookup_byval(cachenode_t *table[], int val)
{
	int h = val;
	cachenode_t *c;

	for (c = table[h & (HASHSIZE - 1)]; c != NULL; c = c->next) {
		if (c->val == val)
			return (c);
	}
	return (NULL);
}

static cachenode_t *
hash_lookup_byname(cachenode_t *table[], char *name)
{
	int h = hash_byname(name);
	cachenode_t *c;

	for (c = table[h & (HASHSIZE - 1)]; c != NULL; c = c->next) {
		if (c->namehash == h && strcmp(c->name, name) == 0)
			return (c);
	}
	return (NULL);
}

static cachenode_t *
hash_insert(cachenode_t *table[], char *name, int value)
{
	cachenode_t *c;
	int signature;

	c = calloc(1, sizeof (cachenode_t));
	if (c == NULL) {
		perror("malloc");
		exit(1);
	}
	if (name != NULL) {
		strncpy(c->name, name, NMAX);
		c->namehash = hash_byname(name);
	}
	c->val = value;
	if (table == uids || table == gids)
		signature = c->val;
	else
		signature = c->namehash;
	c->next = table[signature & (HASHSIZE - 1)];
	table[signature & (HASHSIZE - 1)] = c;
	return (c);
}

static char *
getname(uid_t uid)
{
	cachenode_t *c;

	if ((c = hash_lookup_byval(uids, uid)) == NULL) {
		struct passwd *pwent = getpwuid(uid);
		c = hash_insert(uids, pwent ? pwent->pw_name : NULL, uid);
	}
	return (c->name);
}

static char *
getgroup(gid_t gid)
{
	cachenode_t *c;

	if ((c = hash_lookup_byval(gids, gid)) == NULL) {
		struct group *grent = getgrgid(gid);
		c = hash_insert(gids, grent ? grent->gr_name : NULL, gid);
	}
	return (c->name);
}

static uid_t
getuidbyname(char *name)
{
	cachenode_t *c;

	if ((c = hash_lookup_byname(names, name)) == NULL) {
		struct passwd *pwent = getpwnam(name);
		c = hash_insert(names, name, pwent ? (int)pwent->pw_uid : -1);
	}
	return ((uid_t)c->val);
}

static gid_t
getgidbyname(char *group)
{
	cachenode_t *c;

	if ((c = hash_lookup_byname(groups, group)) == NULL) {
		struct group *grent = getgrnam(group);
		c = hash_insert(groups, group, grent ? (int)grent->gr_gid : -1);
	}
	return ((gid_t)c->val);
}

static void
build_dblock(
	const char	*name,
	const char	*linkname,
	const char	*magic,
	const char	*version,
	const char	typeflag,
	const uid_t	uid,
	const gid_t	gid,
	const dev_t	device,
	const char	*prefix)
{
	dblock.dbuf.typeflag = typeflag;
	(void) sprintf(dblock.dbuf.name, "%s", name);
	(void) sprintf(dblock.dbuf.linkname, "%s", linkname);
	(void) sprintf(dblock.dbuf.magic, "%s", magic);
	(void) sprintf(dblock.dbuf.version, "%2s", version);
	(void) sprintf(dblock.dbuf.uname, "%s", getname(uid));
	(void) sprintf(dblock.dbuf.gname, "%s", getgroup(gid));
	(void) sprintf(dblock.dbuf.devmajor, "%07o",
	    major(device));
	(void) sprintf(dblock.dbuf.devminor, "%07o",
	    minor(device));
	(void) sprintf(dblock.dbuf.prefix, "%s", prefix);
	(void) sprintf(dblock.dbuf.chksum, "%07o", checksum());
	dblock.dbuf.typeflag = typeflag;
}


/*
 *  makeDir - ensure that a directory with the pathname denoted by name
 *            exists, and return 1 on success, and 0 on failure (e.g.,
 *	      read-only file system, exists but not-a-directory).
 */

static int
makeDir(register char *name)
{
	struct stat buf;

	if (access(name, 0) < 0) {  /* name doesn't exist */
		if (mkdir(name, 0777) < 0) {
			vperror(0, name);
			return (0);
		}

		resugname(name, 0);
	} else {		   /* name exists */
		if (stat(name, &buf) < 0) {
			vperror(0, name);
			return (0);
		}

		return ((buf.st_mode & S_IFMT) == S_IFDIR);
	}

	return (1);
}


/*
 * Save this directory and its mtime on the stack, popping and setting
 * the mtimes of any stacked dirs which aren't parents of this one.
 * A null name causes the entire stack to be unwound and set.
 *
 * Since all the elements of the directory "stack" share a common
 * prefix, we can make do with one string.  We keep only the current
 * directory path, with an associated array of mtime's, one for each
 * '/' in the path.  A negative mtime means no mtime.  The mtime's are
 * offset by one (first index 1, not 0) because calling this with a null
 * name causes mtime[0] to be set.
 *
 * This stack algorithm is not guaranteed to work for tapes created
 * with the 'r' option, but the vast majority of tapes with
 * directories are not.  This avoids saving every directory record on
 * the tape and setting all the times at the end.
 *
 * (This was borrowed from the 4.1.3 source, and adapted to the 5.x
 *  environment)
 */

#define	NTIM	(MAXNAM/2+1)		/* a/b/c/d/... */

static void
doDirTimes(char *name, time_t modTime)
{
	static char dirstack[MAXNAM];
	static time_t modtimes[NTIM];

	register char *p = dirstack;
	register char *q = name;
	register int ndir = 0;
	char *savp;
	int savndir;

	if (q) {
		/*
		 * Find common prefix
		 */

		while (*p == *q && *p) {
			if (*p++ == '/')
			    ++ndir;
			q++;
		}
	}

	savp = p;
	savndir = ndir;

	while (*p) {
		/*
		 * Not a child: unwind the stack, setting the times.
		 * The order we do this doesn't matter, so we go "forward."
		 */

		if (*p++ == '/')
		    if (modtimes[++ndir] >= 0) {
			    *--p = '\0'; /* zap the slash */
			    setPathTimes(dirstack, modtimes[ndir]);
			    *p++ = '/';
		    }
	}

	p = savp;
	ndir = savndir;

	/*
	 *  Push this one on the "stack"
	 */

	if (q) {
		while (*p = *q++) {	/* append the rest of the new dir */
			if (*p++ == '/')
				modtimes[++ndir] = -1;
		}
	}

	modtimes[ndir] = modTime;	/* overwrite the last one */
}


/*
 *  setPathTimes - set the modification time for given path.  Return 1 if
 *                 successful and 0 if not successful.
 */

static int
setPathTimes(char *path, time_t modTime)
{
	struct utimbuf ubuf;

	ubuf.actime   = time((time_t *) 0);
	ubuf.modtime  = modTime;

	if (utime(path, &ubuf) < 0) {
		vperror(0, "can't set time on %s", path);
		return (0);
	}

	return (1);
}


/*
 * If hflag is set then delete the symbolic link's target.
 * If !hflag then delete the target.
 */

static void
delete_target(char *namep)
{
	struct	stat	xtractbuf;
	char buf[FILENAME_MAX];
	int n;

	if (rmdir(namep) < 0) {
		if (errno == ENOTDIR && !hflag) {
			(void) unlink(namep);
		} else if (errno == ENOTDIR && hflag) {
			if (!lstat(namep, &xtractbuf)) {
				if ((xtractbuf.st_mode & S_IFMT) != S_IFLNK) {
					(void) unlink(namep);
				} else if ((n = readlink(namep, buf,
					    FILENAME_MAX)) != -1) {
					buf[n] = (char) NULL;
					(void) rmdir(buf);
					if (errno == ENOTDIR)
						(void) unlink(buf);
				} else {
					(void) unlink(namep);
				}
			} else {
				(void) unlink(namep);
			}
		}
	}
}


/*
 * ACL changes:
 *	putfile():
 *		Get acl info after stat. Write out ancillary file
 *		before the normal file, i.e. directory, regular, FIFO,
 *		link, special. If acl count is less than 4, no need to
 *		create ancillary file. (i.e. standard permission is in
 *		use.
 *	doxtract():
 *		Process ancillary file. Read it in and set acl info.
 *		watch out for -o option.
 *	-t option to display table
 */

/*
 * New functions for ACLs and other security attributes
 */

/*
 * The function appends the new security attribute info to the end of
 * existing secinfo.
 */
int
append_secattr(
	char	 **secinfo,	/* existing security info */
	int	 *secinfo_len,	/* length of existing security info */
	int	 size,		/* new attribute size: unit depends on type */
	char	*attrp,		/* new attribute data pointer */
	char	 attr_type)	/* new attribute type */
{
	char	*new_secinfo;
	char	*attrtext;
	int	newattrsize;
	int	oldsize;

	/* no need to add */
	if (attrp == NULL)
		return (0);

	switch (attr_type) {
	case UFSD_ACL:
		attrtext = acltotext((aclent_t *)attrp, size);
		if (attrtext == NULL) {
			fprintf(stderr, "acltotext failed\n");
			return (-1);
		}
		/* header: type + size = 8 */
		newattrsize = 8 + strlen(attrtext) + 1;
		attr = (struct sec_attr *) malloc(newattrsize);
		if (attr == NULL) {
			fprintf(stderr, "can't allocate memory\n");
			return (-1);
		}
		attr->attr_type = UFSD_ACL;
		sprintf(attr->attr_len,  "%06o", size); /* acl entry count */
		(void) strcpy((char *) &attr->attr_info[0], attrtext);
		free(attrtext);
		break;

	/* SunFed's case goes here */

	default:
		fprintf(stderr, "unrecognized attribute type\n");
		return (-1);
	}

	/* old security info + new attr header(8) + new attr */
	oldsize = *secinfo_len;
	*secinfo_len += newattrsize;
	new_secinfo = (char *) malloc(*secinfo_len);
	if (new_secinfo == NULL) {
		fprintf(stderr, "can't allocate memory\n");
		*secinfo_len -= newattrsize;
		return (-1);
	}

	memcpy(new_secinfo, *secinfo, oldsize);
	memcpy(new_secinfo + oldsize, attr, newattrsize);

	free(*secinfo);
	*secinfo = new_secinfo;
	return (0);
}

/*
 * write_ancillary(): write out an ancillary file.
 *      The file has the same header as normal file except the type and size
 *      fields. The type is 'A' and size is the sum of all attributes
 *	in bytes.
 *	The body contains a list of attribute type, size and info. Currently,
 *	there is only ACL info.  This file is put before the normal file.
 */
void
write_ancillary(union hblock *dblockp, char *secinfo, int len)
{
	long    blocks;
	int	savflag;
	int	savsize;

	/* Just tranditional permissions or no security attribute info */
	if (len == 0)
		return;

	/* save flag and size */
	savflag = (dblockp->dbuf).typeflag;
	(void) sscanf(dblockp->dbuf.size, "%12lo", &savsize);

	/* special flag for ancillary file */
	dblockp->dbuf.typeflag = 'A';

	/* for pre-2.5 versions of tar, need to make sure */
	/* the ACL file is readable			  */
	(void) sprintf(dblock.dbuf.mode, "%07o",
		(stbuf.st_mode & MODEMASK) | 0000200);
	(void) sprintf(dblockp->dbuf.size, "%011lo", len);
	(void) sprintf(dblockp->dbuf.chksum, "%07o", checksum());

	/* write out the header */
	writetape((char *)dblockp);

	/* write out security info */
	blocks = TBLOCKS(len);
	writetbuf((char *)secinfo, blocks);

	/* restore mode, flag and size */
	(void) sprintf(dblock.dbuf.mode, "%07o", stbuf.st_mode & MODEMASK);
	dblockp->dbuf.typeflag = savflag;
	(void) sprintf(dblockp->dbuf.size, "%011lo", savsize);
}
