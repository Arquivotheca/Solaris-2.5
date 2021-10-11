
#ident	"@(#)newfs.c	1.16	94/06/28 SMI"	/* from UCB 5.2 9/11/85 */

/*
 * newfs: friendly front end to mkfs
 * copyright (c) 1991 by Sun Microsystems, Inc.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fs/ufs_fs.h>
#include <sys/vnode.h>
#include <sys/fs/ufs_inode.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>
#include <sys/mkdev.h>

static void getdiskbydev();
static int  yes();
static int  notrand();
static void usage();

#define	EPATH "PATH=/usr/sbin:/sbin:"

int	Nflag;			/* run mkfs without writing file system */
int	verbose;		/* show mkfs line before exec */
int	fssize;			/* file system size */
int	fsize;			/* fragment size */
int	bsize;			/* block size */
int	ntracks;		/* # tracks/cylinder */
int	nsectors;		/* # sectors/track */
int	cpg;			/* cylinders/cylinder group */
int	minfree = -1;		/* free space threshold */
int	opt;			/* optimization preference (space or time) */
int	rpm;			/* revolutions/minute of drive */
int	nrpos = 8;		/* # of distinguished rotational positions */
				/* 8 is the historical default */
int	density;		/* number of bytes per inode */
int	apc;			/* alternates per cylinder */
int 	rot = -1;		/* rotational delay (msecs) */
int 	maxcontig = -1;		/* maximum number of contig blocks */

int	grow;			/* grow the file system */
char	*directory;		/* grow a mounted file system */

char	device[MAXPATHLEN];
char	cmd[BUFSIZ];

void	exenv();
void	fatal();

struct fs	*read_sb();	/* (Attempt to) read superblock */

extern	char	*getenv();
extern	int	read_vtoc();
extern	char	*getfullrawname();


main(argc, argv)
	char *argv[];
{
	char *cp, *special;
	struct stat st;
	int status;
	struct fs	*sbp;	/* Pointer to superblock (if present) */

	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		for (cp = &argv[0][1]; *cp; cp++)
			switch (*cp) {

			case 'v':
				verbose++;
				break;

			case 'N':
				Nflag++;
				break;

			case 'G':
				grow++;
				break;

			case  'M':
				if (argc < 1)
					fatal("-M: missing mountpoint");
				argc--, argv++;
				directory = *argv;
				grow++;
				goto next;

			case 's':
				if (argc < 1)
					fatal("-s: missing file system size");
				argc--, argv++;
				fssize = atoi(*argv);
				if (fssize < 0)
					fatal("%s: bad file system size",
						*argv);
				goto next;

			case 'C':
				if (argc < 1)
					fatal("-C: missing maxcontig");
				argc--, argv++;
				maxcontig = atoi(*argv);
				if (maxcontig < 0)
					fatal("%s: bad maxcontig", *argv);
				goto next;

			case 'd':
				if (argc < 1)
					fatal("-d: missing rotational delay");
				argc--, argv++;
				rot = atoi(*argv);
				if (rot < 0)
					fatal(\
"%s: bad rotational delay", *argv);
				goto next;

			case 't':
				if (argc < 1)
					fatal("-t: missing track total");
				argc--, argv++;
				ntracks = atoi(*argv);
				if (ntracks < 0)
					fatal("%s: bad total tracks", *argv);
				goto next;

			case 'o':
				if (argc < 1)
					fatal(\
"-o: missing optimization preference");
				argc--, argv++;
				if (strcmp(*argv, "space") == 0)
					opt = FS_OPTSPACE;
				else if (strcmp(*argv, "time") == 0)
					opt = FS_OPTTIME;
				else
					fatal(\
"%s: bad optimization preference %s", *argv,
					    "(options are `space' or `time')");
				goto next;

			case 'a':
				if (argc < 1)
					fatal(
"-a: missing # of alternates per cyl");
				argc--, argv++;
				apc = atoi(*argv);
				if (apc < 0)
					fatal("%s: bad altenates per cyl ",
						*argv);
				goto next;

			case 'b':
				if (argc < 1)
					fatal("-b: missing block size");
				argc--, argv++;
				bsize = atoi(*argv);
				if (bsize < 0 || bsize < MINBSIZE)
					fatal("%s: bad block size", *argv);
				goto next;

			case 'f':
				if (argc < 1)
					fatal("-f: missing frag size");
				argc--, argv++;
				fsize = atoi(*argv);
				if (fsize < 0)
					fatal("%s: bad frag size", *argv);
				goto next;

			case 'c':
				if (argc < 1)
					fatal("-c: missing cylinders/group");
				argc--, argv++;
				cpg = atoi(*argv);
				if (cpg < 0)
					fatal(\
"%s: bad cylinders/group", *argv);
				goto next;

			case 'm':
				if (argc < 1)
					fatal("-m: missing free space %%\n");
				argc--, argv++;
				minfree = atoi(*argv);
				if (minfree < 0 || minfree > 99)
					fatal("%s: bad free space %%\n",
						*argv);
				goto next;

			case 'n':
				if (argc < 1)
					fatal(\
"-n: missing # of rotational positions\n");
				argc--, argv++;
				nrpos = atoi(*argv);
				if (nrpos <= 0)
					fatal(\
"%s: bad # of rotational positrions\n",
					    *argv);
				goto next;

			case 'r':
				if (argc < 1)
					fatal(\
"-r: missing revs/minute\n");
				argc--, argv++;
				rpm = atoi(*argv);
				if (rpm < 0)
					fatal(\
"%s: bad revs/minute\n", *argv);
				goto next;

			case 'i':
				if (argc < 1)
					fatal(\
"-i: missing bytes per inode\n");
				argc--, argv++;
				density = atoi(*argv);
				if (density < 0)
					fatal("%s: bad bytes per inode\n",
						*argv);
				goto next;

			default:
				usage();
				fatal("-%c: unknown flag", *cp);
			}
next:
		argc--, argv++;
	}

	/* At this point, there should only be one argument left:	*/
	/* The raw-special-device itself. If not, print usage message.	*/
	if (argc != 1) {
		usage();
		exit(1);
	}

	special = getfullrawname(argv[0]);
	if (special == NULL) {
		fprintf(stderr, "newfs: malloc failed\n");
		exit(1);
	}

	if (*special == '\0') {
		if (strchr(argv[0], '/') != NULL) {
			if (stat(argv[0], &st) < 0) {
				fprintf(stderr, "newfs: "); perror(special);
				exit(2);
			}
			fatal("%s: not a raw disk device", argv[0]);
		}
		(void) sprintf(device, "/dev/rdsk/%s", argv[0]);
		if ((special = getfullrawname(device)) == NULL) {
			fprintf(stderr, "newfs: malloc failed\n");
			exit(1);
		}

		if (*special == '\0') {
			(void) sprintf(device, "/dev/%s", argv[0]);
			if ((special = getfullrawname(device)) == NULL) {
				fprintf(stderr, "newfs: malloc failed\n");
				exit(1);
			}
			if (*special == '\0')
				fatal("%s: not a raw disk device", argv[0]);
		}
	}

	/* note: getdiskbydev does not set apc */
	getdiskbydev(special);
	if (fssize < 0)
		fatal("%s: no default size", special);
	if (nsectors < 0)
		fatal("%s: no default #sectors/track", special);
	if (ntracks < 0)
		fatal("%s: no default #tracks", special);
	if (bsize < 0)
		fatal("%s: no default block size", special);
	if (fsize < 0)
		fatal("%s: no default frag size", special);
	if (rpm < 0)
		fatal("%s: no default revolutions/minute value", special);
	if (rpm < 60) {
		fprintf(stderr, "Warning: setting rpm to 60\n");
		rpm = 60;
	}
	/* XXX - following defaults are both here and in mkfs */
	if (density <= 0)
		density = 2048;
	if (cpg == 0)
		cpg = 16;
	if (minfree < 0)
		minfree = 10;
	if (minfree < 10 && opt != FS_OPTSPACE) {
		fprintf(stderr, "setting optimization for space ");
		fprintf(stderr, "with minfree less than 10%%\n");
		opt = FS_OPTSPACE;
	}
#ifdef i386	/* Bug 1170182 */
	if (ntracks > 32 && (ntracks % 16) != 0) {
		ntracks -= (ntracks % 16);
	}
#endif
	/*
	 * Confirmation
	 */
	if (isatty(fileno(stdin)) && !Nflag) {
		/*
		** If we can read a valid superblock, report the mount
		** point on which this filesystem was last mounted.
		*/
		if (((sbp = read_sb(special)) != 0) &&
			(*sbp->fs_fsmnt != '\0')) {
			printf("newfs: %s last mounted as %s\n",
						special, sbp->fs_fsmnt);
		}
		if (grow == 0)
			printf("newfs: construct a new file system %s: (y/n)? ",
				special);
		else
			printf("newfs: grow file system on %s: (y/n)? ",
				special);
		if (!yes())
			exit(0);
	}
	/*
	 * If alternates-per-cylinder is ever implemented:
	 * need to get apc from dp->d_apc if no -a switch???
	 */
	sprintf(cmd,
	"mkfs -F ufs %s%s%s%s%s%s %d %d %d %d %d %d %d %d %d %s %d %d %d %d",
	    (directory != NULL) ? "-M " : "",
	    (directory != NULL) ? directory : "",
	    (directory != NULL) ? " " : "",
	    grow ? "-G " : "",
	    Nflag ? "-o N " : "", special,
	    fssize, nsectors, ntracks, bsize, fsize, cpg, minfree, rpm/60,
	    density, opt == FS_OPTSPACE ? "s" : "t", apc, rot, nrpos,
	    maxcontig);
	if (verbose)
		printf("%s\n", cmd);
	exenv();
	if (status = system(cmd))
		exit(status >> 8);
	if (Nflag)
		exit(0);
	sprintf(cmd, "/usr/sbin/fsirand %s", special);
	if ((grow == 0) && (notrand(special)) && (status = system(cmd)))
		fprintf(stderr, "%s: failed, status = %d\n", cmd, status);

	return (0);
	/* NOTREACHED */
}

void
exenv()
{
	char *epath;				/* executable file path */
	char *cpath;				/* current path */

	if ((cpath = getenv("PATH")) == NULL) {
		fprintf(stderr, "newfs: no entry in env\n");
	}
	if ((epath = malloc(strlen(EPATH)+strlen(cpath)+1)) == NULL) {
		fprintf(stderr, "newfs: malloc failed\n");
		exit(1);
	}
	strcpy(epath, EPATH);
	strcat(epath, cpath);
	if (putenv(epath) < 0) {
		fprintf(stderr, "newfs: putenv failed\n");
		exit(1);
	}
}

static int
yes()
{
	int	i, b;

	i = b = getchar();
	while (b != '\n' && b != '\0' && b != EOF)
		b = getchar();
	return (i == 'y');
}

/*VARARGS*/
void
fatal(fmt, arg1, arg2)
	char *fmt;
{

	fprintf(stderr, "newfs: ");
	fprintf(stderr, fmt, arg1, arg2);
	putc('\n', stderr);
	exit(10);
}

static void
getdiskbydev(disk)
	char *disk;
{
	int partno;
	struct dk_geom g;
	int fd;
	struct vtoc vtoc;

	if ((fd = open(disk, 0)) < 0) {
		perror(disk);
		exit(1);
	}
	if (ioctl(fd, DKIOCGGEOM, &g))
		fatal("%s: Unable to read Disk geometry", disk);
	partno = readvtoc(fd, disk, &vtoc);
	(void) close(fd);
	if (partno > (int) vtoc.v_nparts)
		fatal("%s: can't figure out file system partition", disk);
	if (fssize == 0)
		fssize = (int) vtoc.v_part[partno].p_size;
	if (nsectors == 0)
		nsectors = g.dkg_nsect;
	if (ntracks == 0)
		ntracks = g.dkg_nhead;
	if (bsize == 0)
		bsize = 8192;
	if (fsize == 0)
		fsize = 1024;
	if (rpm == 0)
		rpm = ((int) g.dkg_rpm <= 0) ? 3600: g.dkg_rpm;
}
/*
 * readvtoc()
 *
 * Read a partition map.
 */
static int
readvtoc(fd, name, vtoc)
	int		fd;	/* opened device */
	char		*name;	/* name of disk device */
	struct vtoc	*vtoc;
{
	int	retval;
	if ((retval = read_vtoc(fd, vtoc)) >= 0)
		return (retval);

	switch (retval) {
		case (VT_ERROR):
			fprintf(stderr, "newfs: ");
			perror(name);
			exit(10);
			/* NOTREACHED */
		case (VT_EIO):
			fatal("%s: I/O error accessing VTOC", name);
			/* NOTREACHED */
		case (VT_EINVAL):
			fatal("%s: Invalid field in VTOC", name);
		/* NOTREACHED */
	}
}

/*
 * read_sb(char * rawdev) - Attempt to read the superblock from a raw device
 *
 * Returns:
 *	0 :
 *		Could not read a valid superblock for a variety of reasons.
 *		Since 'newfs' handles any fatal conditions, we're not going
 *		to make any guesses as to why this is failing or what should
 *		be done about it.
 *
 *	struct fs *:
 *		A pointer to (what we think is) a valid superblock. The
 *		space for the superblock is static (inside the function)
 *		since we will only be reading the values from it.
 */

struct fs *
read_sb(fsdev)
	char *	fsdev;
{
	static struct fs	sblock;

	struct stat		statb;
	int			dskfd;
	char			*bufp = NULL;
	int			bufsz = 0;


	if (stat(fsdev, &statb) < 0)
	{
		return (0);
	}

	if ((dskfd = open(fsdev, O_RDONLY)) < 0)
	{
		return (0);
	}

	/*
	 * We need a buffer whose size is a multiple of DEV_BSIZE in order
	 * to read from a raw device (which we were probably passed).
	 */

	bufsz = ((sizeof (sblock) / DEV_BSIZE) + 1) * DEV_BSIZE;
	if ((bufp = (char *)malloc(bufsz)) == NULL)
	{
		close(dskfd);
		return (0);
	}

	if (llseek(dskfd, (offset_t)SBOFF, SEEK_SET) < 0)
	{
		close(dskfd);
		free(bufp);
		return (0);
	}


	if (read(dskfd, bufp, bufsz) < 0)
	{
		close(dskfd);
		free(bufp);
		return (0);
	}
	close(dskfd);	/* Done with the file */

	(void) memcpy(&sblock, bufp, sizeof (sblock));
	free(bufp);	/* Don't need this anymore */

	if (sblock.fs_magic != FS_MAGIC)
	{
		return (0);
	}

	if (sblock.fs_ncg < 1)
	{
		return (0);
	}

	if (sblock.fs_cpg < 1)
	{
		return (0);
	}

	if (sblock.fs_ncg * sblock.fs_cpg < sblock.fs_ncyl ||
		(sblock.fs_ncg - 1) * sblock.fs_cpg >= sblock.fs_ncyl)
	{
		return (0);
	}

	if (sblock.fs_sbsize < 0 || sblock.fs_sbsize > SBSIZE)
	{
		return (0);
	}

	return (&sblock);
}

/*
 * Read the UFS file system on the raw device SPECIAL.  If it does not
 * appear to be a UFS file system, return non-zero, indicating that
 * fsirand should be called (and it will spit out an error message).
 * If it is a UFS file system, take a look at the inodes in the first
 * cylinder group.  If they appear to be randomized (non-zero), return
 * zero, which will cause fsirand to not be called.  If the inode generation
 * counts are all zero, then we must call fsirand, so return non-zero.
 */

#define	RANDOMIZED	0
#define	NOT_RANDOMIZED	1

static int
notrand(special)
	char *special;
{
	char fsbuf[SBSIZE];
	struct dinode dibuf[MAXBSIZE/sizeof (struct dinode)];
	struct fs *fs;
	struct dinode *dip;
	offset_t seekaddr;
	int bno, inum, n;
	int fd;

	fs = (struct fs *) fsbuf;
	if ((fd = open(special, 0)) == -1)
		return (NOT_RANDOMIZED);
	if (llseek(fd, (offset_t)SBLOCK * DEV_BSIZE, 0) == -1 ||
	    (n = read(fd, (char *) fs, SBSIZE)) != SBSIZE ||
	    fs->fs_magic != FS_MAGIC) {
		(void) close(fd);
		return (NOT_RANDOMIZED);
	}

	/* looks like a UFS file system; read the first cylinder group */
	bsize = INOPB(fs) * sizeof (struct dinode);
	inum = 0;
	while (inum < fs->fs_ipg) {
		bno = itod(fs, inum);
		seekaddr = (offset_t)fsbtodb(fs, bno) * DEV_BSIZE;
		if (llseek(fd, seekaddr, 0) == -1 ||
		    (n = read(fd, (char *) dibuf, bsize)) != bsize) {
			(void) close(fd);
			return (NOT_RANDOMIZED);
		}
		for (dip = dibuf; dip < &dibuf[INOPB(fs)]; dip++) {
			if (dip->di_gen != 0) {
				(void) close(fd);
				return (RANDOMIZED);
			}
			inum++;
		}
	}
	(void) close(fd);
	return (NOT_RANDOMIZED);
}

static void
usage()
{
	fprintf(stderr, "usage: newfs [ -v ] [ mkfs-options ] %s\n",
		"raw-special-device");
	fprintf(stderr, "where mkfs-options are:\n");
	fprintf(stderr, "\t-N do not create file system, %s\n",
		"just print out parameters");
	fprintf(stderr, "\t-s file system size (sectors)\n");
	fprintf(stderr, "\t-b block size\n");
	fprintf(stderr, "\t-f frag size\n");
	fprintf(stderr, "\t-t tracks/cylinder\n");
	fprintf(stderr, "\t-c cylinders/group\n");
	fprintf(stderr, "\t-m minimum free space %%\n");
	fprintf(stderr, "\t-o optimization preference %s\n",
		"(`space' or `time')");
	fprintf(stderr, "\t-r revolutions/minute\n");
	fprintf(stderr, "\t-i number of bytes per inode\n");
	fprintf(stderr, "\t-a number of alternates per cylinder\n");
	fprintf(stderr, "\t-C maxcontig\n");
	fprintf(stderr, "\t-d rotational delay\n");
	fprintf(stderr, "\t-n number of rotational positions\n");
	fprintf(stderr, "\t-G grow file system\n");
	fprintf(stderr, "\t-M grow mounted file system\n");
}
