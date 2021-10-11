/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988,1989,1991  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */

#ident	"@(#)mkfs.c	1.34	94/06/21 SMI"	/* SVr4.0 1.9 */

/*
 * make file system for cylinder-group style file systems
 *
 * usage:
 *
 *    mkfs [-F FSType] [-V] [-G] [-M dirname] [-m] [options]
 *	[-o specific_options]  special size
 *	[nsect ntrack bsize fsize cpg	minfree	rps nbpi opt apc rotdelay
 *	  2     3      4     5     6	7	8   9	 10  11  12
 *	nrpos maxcontig]
 *	13    14
 *
 *  where specific_options are:
 *	N - no create
 *	nsect - The number of sectors per track
 *	ntrack - The number of tracks per cylinder
 *	bsize - block size
 *	fragsize - fragment size
 *	cgsize - The number of disk cylinders per cylinder group.
 * 	free - minimum free space
 *	rps - rotational speed (rev/sec).
 *	nbpi - number of data bytes per allocated inode
 *	opt - optimization (space, time)
 *	apc - number of alternates
 *	gap - gap size
 *	nrpos - number of rotational positions
 */

/*
 * The following constants set the defaults used for the number
 * of sectors/track (fs_nsect), and number of tracks/cyl (fs_ntrak).
 *   
 *			NSECT		NTRAK
 *	72MB CDC	18		9
 *	30MB CDC	18		5
 *	720KB Diskette	9		2
 */
#define sun		1

#ifdef sun
#define	DFLNSECT	32
#define	DFLNTRAK	16
#else
#define	DFLNSECT	18
#define	DFLNTRAK	9
#endif	/* sun */

/*
 * The following two constants set the default block and fragment sizes.
 * Both constants must be a power of 2 and meet the following constraints:
 *	MINBSIZE <= DESBLKSIZE <= MAXBSIZE
 *	DEV_BSIZE <= DESFRAGSIZE <= DESBLKSIZE
 *	DESBLKSIZE / DESFRAGSIZE <= 8
 */
#define	DESBLKSIZE	8192
#define	DESFRAGSIZE	1024

/*
 * Cylinder groups may have up to MAXCPG cylinders. The actual
 * number used depends upon how much information can be stored
 * on a single cylinder. The default is to use 16 cylinders
 * per group.  This is the largest value acceptable under SunOs 4.1
 */
#define	MAXCPG		16	/* for 4.2 FFS compatibility	*/

#define	DESCPG		16	/* desired fs_cpg */

/*
 * MINFREE gives the minimum acceptable percentage of file system
 * blocks which may be free. If the freelist drops below this level
 * only the superuser may continue to allocate blocks. This may
 * be set to 0 if no reserve of free blocks is deemed necessary,
 * however throughput drops by fifty percent if the file system
 * is run at between 90% and 100% full; thus the default value of
 * fs_minfree is 10%. With 10% free space, fragmentation is not a
 * problem, so we choose to optimize for time.
 */
#define	MINFREE		10
#define	DEFAULTOPT	FS_OPTTIME

/*
 * ROTDELAY gives the minimum number of milliseconds to initiate
 * another disk transfer on the same cylinder. It is used in
 * determining the rotationally optimal layout for disk blocks
 * within a file; the default of fs_rotdelay is 4ms.  On a drive with
 * track readahead buffering this should be zero.
 */
#define	ROTDELAY	0

/*
 * MAXTRAX is the maximum data in bytes (56K) we can transfer by the 
 * most disk drivers.  maxcontig is determined by MAXTRAX/bsize.
 */
#define	MAXTRAX		57344

/*
 * MAXCONTIG sets the default for the maximum number of blocks
 * that may be allocated sequentially. Since UNIX drivers are
 * not capable of scheduling multi-block transfers, this defaults
 * to 1 (ie no contiguous blocks are allocated). UFS + negates this.
 * MAXTRAX / 8k = 7.
 */
#define	MAXCONTIG	7

/*
 * MAXBLKPG determines the maximum number of data blocks which are
 * placed in a single cylinder group. The default is one indirect
 * block worth of data blocks.
 */
#define	MAXBLKPG(bsize)	((bsize) / sizeof(daddr_t))

/*
 * Each file system has a number of inodes statically allocated.
 * We allocate one inode slot per NBPI bytes, expecting this
 * to be far more than we will ever need.
 */
#define	NBPI		2048	/* Number Bytes Per Inode */

/*
 * Disks are assumed to rotate at 60HZ, unless otherwise specified.
 */
#define	DEFHZ		60

/*
 * Cylinder group related limits.
 *
 * For each cylinder we keep track of the availability of blocks at different
 * rotational positions, so that we can lay out the data to be picked
 * up with minimum rotational latency.  NRPOS is the number of rotational
 * positions which we distinguish.  With NRPOS 8 the resolution of our
 * summary information is 2ms for a typical 3600 rpm drive.
 */
#define	NRPOS		8       /* number distinct rotational positions */
 
#ifndef	STANDALONE
#include	<stdio.h>
#include	<sys/mnttab.h>
#endif

#include	<sys/param.h>
#include	<time.h>
#include	<sys/types.h>
#include	<sys/sysmacros.h>
#include	<sys/vnode.h>
#include	<sys/fs/ufs_fsdir.h>
#include	<sys/fs/ufs_inode.h>
#include	<sys/fs/ufs_fs.h>
#include	<sys/mntent.h>
#include	<sys/filio.h>

#define bcopy(f,t,n)    memcpy(t,f,n)
#define bzero(s,n)	memset(s,0,n)
#define bcmp(s,d,n)	memcmp(s,d,n)

#define index(s,r)	strchr(s,r)
#define rindex(s,r)	strrchr(s,r)

#include	<sys/stat.h>
#include	<sys/ustat.h>
#include	<locale.h>
#include	<fcntl.h>

#ifdef sun
#include	<sys/dkio.h>
#endif

extern offset_t	llseek();
extern char	*getfullblkname();
extern long	lrand48();

extern int	optind;
extern char	*optarg;


/*
 * The size of a cylinder group is calculated by CGSIZE. The maximum size
 * is limited by the fact that cylinder groups are at most one block.
 * Its size is derived from the size of the maps maintained in the 
 * cylinder group and the (struct cg) size.
 */
#define	CGSIZE(fs) \
    /* base cg		*/ (sizeof(struct cg) + \
    /* blktot size	*/ (fs)->fs_cpg * sizeof(long) + \
    /* blks size	*/ (fs)->fs_cpg * (fs)->fs_nrpos * sizeof(short) + \
    /* inode map	*/ howmany((fs)->fs_ipg, NBBY) + \
    /* block map	*/ howmany((fs)->fs_cpg * (fs)->fs_spc / NSPF(fs), NBBY))

/*
 * We limit the size of the inode map to be no more than a
 * third of the cylinder group space, since we must leave at
 * least an equal amount of space for the block map.
 *
 * N.B.: MAXIpG must be a multiple of INOPB(fs).
 */
#define MAXIpG(fs)	roundup((fs)->fs_bsize * NBBY / 3, INOPB(fs))

#define UMASK		0755
#define MAXINOPB	(MAXBSIZE / sizeof(struct dinode))
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)
#define MB		(1024*1024)

/*
 * Used to set the inode generation number. Since both inodes and dinodes
 * are dealt with, we really need a pointer to an icommon here.
 */
#define IRANDOMIZE(icp)	(icp)->ic_gen = lrand48();

/*
 * Forward declarations
 */
static void initcg(int cylno);
static void fsinit();
static int makedir(register struct direct *protodir, int entries);
static daddr_t alloc(int size, int mode);
static void iput(register struct inode *ip);
static void rdfs(daddr_t bno, int size, char *bf);
static void wtfs(daddr_t bno, int size, char *bf);
static int isblock(struct fs *fs, unsigned char *cp, int h);
static void clrblock(struct fs *fs, unsigned char *cp, int h);
static void setblock(struct fs *fs, unsigned char *cp, int h);
static void usage ();
static void dump_fscmd (char *fsys, int fsi);
static unsigned int number(long big);
static int match(char *s);


/*
 * variables set up by front end.
 */
int	Nflag;			/* run mkfs without writing file system */
int	fssize;			/* file system size	*/
int	bsize;			/* block size		*/
int	fsize;			/* fragment size	*/
int	cpg;			/* cylinders/cylinder group	*/
int	rps;			/* revolutions/second of drive	*/
int	rotdelay = -1;		/* rotational delay between blocks */
int	sectorsize = DEV_BSIZE;	/* bytes/sector from param.h	*/
int	cpg_flag;		/* cylinders/cylinder group specified */
int	maxcontig = MAXCONTIG;	/* max contiguous blocks to allocate */
int	bbsize	= BBSIZE;	/* boot block size	*/
int	sbsize	= SBSIZE;	/* superblock size	*/

union {
	struct fs fs;
	char pad[SBSIZE];
} fsun;
#define	sblock	fsun.fs

struct	csum *fscs;

union {
	struct cg cg;
	char pad[MAXBSIZE];
} cgun;

#define	acg	cgun.cg
/*
 * Size of screen in cols in which to fit output
 */
#define	WIDTH	80

struct dinode zino[MAXBSIZE / sizeof(struct dinode)];

char	*fsys;
time_t	mkfstime;
int	fsi;
int	fso;
int	Nflag = 0;	/* do not write to disk or execute the function */
int	mflag = 0;	/* return the command line used to create this FS */
daddr_t	alloc();

/* The BIG parameter is machine dependent.  It should be a long integer */
/* constant that can be used by the number parser to check the validity */
/* of numeric parameters.  On 16-bit machines, it should probably be    */
/* the maximum unsigned integer, 0177777L.  On 32-bit machines where    */
/* longs are the same size as ints, the maximum signed integer is more  */
/* appropriate.  This value is 017777777777L.			   */

#define BIG	017777777777L /*  == ( 2 ** 31) -1 = 0x7fff ffff	*/

unsigned int	number();
char		zerobuf[BBSIZE];

/* default values for mkfs */
int	nsect	 = DFLNSECT;	/* fs_nsect */
int	ntrack	 = DFLNTRAK;	/* fs_ntrak */
int	bsize	 = DESBLKSIZE; 	/* fs_bsize */
int	fragsize = DESFRAGSIZE; /* fs_fsize */
int	minfree	 = MINFREE; 	/* fs_minfree */
int	rps	 = DEFHZ; 
int	nbpi	 = NBPI;
int	nbpi_flag = 0;		/* set for default value override */
char	opt	 = 't';		/* fs_optim */
int	apc	 = 0;		
int	apc_flag = 0;
char	*string;
/*
 * logging support
 */
int	ismdd;
int	islog;
int	islogok;

/*
 * growfs globals and forward references
 */
int		grow;
int		ismounted;
int		bdevismounted;
char		*directory;
long		grow_fssize;
long		grow_fs_size;
long		grow_fs_ncg;
daddr_t		grow_fs_csaddr;
long		grow_fs_cssize;
int		grow_fs_clean;
struct csum	*grow_fscs;
daddr_t		grow_sifrag;
int		test;
int		testforce;
daddr_t		testfrags;
int		inlockexit;

void		lockexit();
void		randomgeneration();
void		checksummarysize();
void		checksblock();
void		growinit();
void		checkdev();
void		checkmount();
struct dinode	*gdinode();
int		csfraginrange();
struct csfrag	*findcsfrag();
void		checkindirect();
void		addcsfrag();
void		delcsfrag();
void		checkdirect();
void		findcsfragino();
void		fixindirect();
void		fixdirect();
void		fixcsfragino();
void		extendsummaryinfo();
int		notenoughspace();
void		unalloccsfragino();
void		unalloccsfragfree();
void		findcsfragfree();
void		copycsfragino();
void		rdcg();
void		wtcg();
void		flcg();
void		allocfrags();
void		alloccsfragino();
void		alloccsfragfree();
void		freefrags();
int		findfreerange();
void		resetallocinfo();
void		extendcg();
void		ulockfs();
void		wlockfs();
void		clockfs();
void		wtsb();

main(argc, argv)
	int argc;
	char *argv[];
{
	register long i, mincpc, mincpg, inospercg;
	long cylno, rpos, blk, j, warn = 0;
	long used, mincpgcnt, bpcg;
	long mapcramped, inodecramped;
	long postblsize, rotblsize, totalsbsize;
	FILE *mnttab;
	struct mnttab mntp;
	char *special;
	struct stat statarea;
	struct ustat ustatarea;
	struct dk_cinfo dkcinfo;
	char pbuf[sizeof(unsigned int) * 3 + 1];
	int width, plen;
	unsigned int num;
#ifdef sun
	int spc_flag = 0;
#endif	
	int	c;
	int	tmpmaxcontig	= -1;
	int	tmpnrpos	= NRPOS;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt (argc, argv, "F:bmo:VGM:T:t:")) != EOF) {
		switch (c) {

		case 'F':
			string = optarg;
			if (strcmp(string, "ufs") != 0)
				usage();
			break;

		case 'm':	/* return command line used to create this FS */
			mflag++;
			break;

		case 'o':
			/*
			 * ufs specific options.
			 */
			string = optarg;
			while (*string != '\0') {
				if (match("nsect=")) 
					nsect=number(BIG);
				else if (match("ntrack=")) 
					ntrack=number(BIG);
				else if (match("bsize=")) {
					bsize=number(BIG);
					if (tmpmaxcontig < 0)
						maxcontig = MAXTRAX / bsize;
				}
				else if (match("fragsize=")) 
					fragsize=number(BIG);
				else if (match("cgsize=")) {
					cpg_flag = 1;
					cpg=number(BIG);
				}
				else if (match("free=")) 
					minfree=number(BIG);
				else if (match("maxcontig="))
					tmpmaxcontig=maxcontig=number(BIG);
				else if (match("nrpos=")) 
					tmpnrpos=number(BIG);
				else if (match("rps=")) 
					rps=number(BIG);
				else if (match("nbpi=")) {
					nbpi_flag = 1;
					nbpi=number(BIG);
				}
				else if (match("opt=")) 
					opt = *string++;
				else if (match("apc=")) {
					apc=number(BIG);
					apc_flag = 1;
				}
				else if (match("gap=")) 
					rotdelay=number(BIG);
				else if (match("N")) 
					Nflag++;
				else if (*string == '\0') break;
				else {
					fprintf(stdout, gettext("illegal option: %s\n"),
						string);
					usage();
				}
			    if (*string == ',') string++;
			    if (*string == ' ') string++;
			}
			break;

		case 'V':
			{
				char	*opt_text;
				int	opt_count;

				(void) fprintf (stdout, gettext("mkfs -F ufs "));
				for (opt_count = 1; opt_count < argc ; opt_count++) {
					opt_text = argv[opt_count];
					if (opt_text)
					   (void)fprintf(stdout," %s ",opt_text);
				}
				(void) fprintf (stdout, "\n");
			}
			break;

		case 'N':	/* Set do no writes flag */
		case 'n':
			Nflag++;
			break;

		case 'b':	/* do nothing for this */
			break;

		case 'M':	/* grow the mounted file system */
			directory = optarg;
			/* FALL THROUGH */

		case 'G':	/* grow the file system */
			grow = 1;
			break;

		case 'T':	/* For testing */
			testforce = 1;
			/* FALL THROUGH */

		case 't':
			test = 1;
			string = optarg;
			testfrags = number(BIG);
			break;

		case '?':
			usage ();
			break;
		}
	}
	time(&mkfstime);
	if (optind > (argc - 1)) {
		usage ();
	}
	argc -= optind;
	argv = &argv[optind];
	fsys = argv[0];
	fsi = open(fsys, 0);
	if(fsi < 0) {
		printf(gettext("%s: cannot open\n"), fsys);
		lockexit(32);
	}
	if (mflag) {
		dump_fscmd (fsys, fsi);
		lockexit(0);
	}	
	if (argc < 2) {
		usage();
	}
	/*
	 * get the controller info
	 */
	ismdd = 0;
	islog = 0;
	islogok = 0;
	if (ioctl(fsi, DKIOCINFO, &dkcinfo) == 0)
		/*
		 * if it is an MDD (disksuite) device
		 */
		if (dkcinfo.dki_ctype == DKC_MD) {
			ismdd++;
			/*
			 * check the logging device
			 */
			if (ioctl(fsi, _FIOISLOG, NULL) == 0) {
				islog++;
				if (ioctl(fsi, _FIOISLOGOK, NULL) == 0)
					islogok++;
			}
		}
	fssize = atoi(argv[1]);
	if (!Nflag) {
		special = getfullblkname(fsys);
		if (grow)
			checkdev(fsys, special);

		/*
		 * If we found the block device name, 
		 * then check the mount table.
		 * if mounted, and growing write lock the file system
		 * 
		 */
		if ((special != NULL) && (*special != '\0')) {
			mnttab = fopen(MNTTAB, "r");
			while ((getmntent(mnttab, &mntp)) == NULL) {
				if (grow) {
					checkmount(&mntp, special);
					continue;
				}
				if (strcmp(special, mntp.mnt_special) == 0) {
					printf(gettext(
					    "%s is mounted, can't mkfs\n"),
					    special);
					exit(32);
				}
			}
			fclose(mnttab);
		}
		if ((bdevismounted) && (ismounted == 0)) {
			printf(gettext("can't check mount point; "));
			printf(gettext(
			  "%s is mounted but not in mnttab(4)\n"),
			  special);
			lockexit(32);
		}
		if (directory) {
			if (ismounted == 0) {
				printf(gettext("%s is not mounted\n"),
					special);
				lockexit(32);
			}
			wlockfs();
		}
		fso = (grow) ? open(fsys, O_WRONLY) : creat(fsys, 0666);
		if(fso < 0) {
			printf(gettext("%s: cannot create\n"), fsys);
			lockexit(32);
		}
		if (!grow && stat(fsys, &statarea) < 0) {
			fprintf(stderr, gettext("%s: %s: cannot stat\n"),
				argv[0], fsys);
			exit(32);
		}
		if (!grow && ((statarea.st_mode & S_IFMT) != S_IFBLK) &&
			((statarea.st_mode & S_IFMT) != S_IFCHR)) {
			printf(gettext("%s is not special device:%x, can't mkfs\n"),
				fsys, statarea.st_mode);
			exit(32);
		}
		if (!grow && ustat(statarea.st_rdev, &ustatarea) >= 0) {
			printf(gettext("%s is mounted, can't mkfs\n"), fsys);
			exit(32);
		}
#if 0
	} else {
/* Is this for checking a vfstab entry??? */
		static char protos[60];
		char fsbuf[100];
 
		printf(gettext("file sys size: "));
		gets(protos);
		fssize = atoi(protos);
		do {
			printf(gettext("file system: "));
			gets(fsbuf);
			fso = open(fsbuf, 1);
			fsi = open(fsbuf, 0);
		} while (fso < 0 || fsi < 0);
#endif
	}


	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 */
	if (argc < 2) {
		exit (0);
	}
	fssize = atoi(argv[1]);
	if (fssize <= 0)
		printf(gettext("preposterous size %d. sectors\n"), fssize), lockexit(32);

	/*
	 * seed random # generator (for ic_generation)
	 */
	(void)srand48((long)(time((time_t *)NULL) + getpid()));

	if (grow) {
		growinit();
		goto grow00;
	}
	/*
	 * verify device size
	 */
	wtfs(fssize - 1, sectorsize, (char *)&sblock);

	/*
	 * make the fs unmountable
	 */
	rdfs(SBOFF / sectorsize, sbsize, (char *)&sblock);
	sblock.fs_magic = -1;
	sblock.fs_clean = FSBAD;
	sblock.fs_state = FSOKAY - sblock.fs_time;
	wtfs(SBOFF / sectorsize, sbsize, (char *)&sblock);
	bzero(&sblock, sbsize);

	/*
	 * collect and verify the sector and track info
	 */
	if (argc > 2)
		nsect = atoi(argv[2]);
	sblock.fs_nsect = nsect;
	if (argc > 3)
		ntrack = atoi(argv[3]);
	sblock.fs_ntrak = ntrack;
	if (sblock.fs_ntrak <= 0 || sblock.fs_ntrak > fssize)
		printf(gettext("preposterous ntrack %d\n"), sblock.fs_ntrak), lockexit(32);

	if (sblock.fs_nsect <= 0 || sblock.fs_nsect > fssize)
		printf(gettext("preposterous nsect %d\n"), sblock.fs_nsect), lockexit(32);
	/*
	 * Validate specified/determined spc
	 * and calculate minimum cylinders per group.
	 */

	sblock.fs_spc = sblock.fs_ntrak * sblock.fs_nsect;

	if (argc > 11) {
		sblock.fs_spc -= atoi(argv[11]);
grow00:
	if (sblock.fs_spc != sblock.fs_ntrak * sblock.fs_nsect)
		spc_flag = 1;
		apc_flag = 0;
	}
	if (apc_flag) {
		sblock.fs_spc -= apc;
		spc_flag = 1;
	}
	if (grow)
		goto grow10;

	/* Now check for rotational delay argument */
	if (argc > 12) 
		/* if specified, use it */
		rotdelay = atoi(argv[12]);

	if (rotdelay <= -1)	/* default by newfs and mkfs */
		rotdelay = ROTDELAY;

	if (argc > 13)
		sblock.fs_nrpos = atoi(argv[13]);
	else
		sblock.fs_nrpos = tmpnrpos;

	if (sblock.fs_nrpos <= 0)
		printf(gettext("preposterous nrpos %d\n"), sblock.fs_nrpos), lockexit(32);

	/*
	 * collect and verify the block and fragment sizes
	 */
	if (argc > 4) {
		bsize = atoi(argv[4]);
		if (tmpmaxcontig < 0)
			maxcontig = MAXTRAX / bsize;
	}
	sblock.fs_bsize = bsize;;

	if (argc > 5)
		fragsize = atoi(argv[5]);
	sblock.fs_fsize = fragsize;

	/*
	 * maxcontig
	 */
	if (argc > 14) {
		tmpmaxcontig = atoi(argv[14]);
		if (tmpmaxcontig > 0)
			maxcontig = tmpmaxcontig;
	}

	if (!POWEROF2(sblock.fs_bsize)) {
		printf(gettext("block size must be a power of 2, not %d\n"),
		    sblock.fs_bsize);
		lockexit(32);
	}
	if (!POWEROF2(sblock.fs_fsize)) {
		printf(gettext("fragment size must be a power of 2, not %d\n"),
		    sblock.fs_fsize);
		lockexit(32);
	}
	if (sblock.fs_fsize < sectorsize) {
		printf(gettext("fragment size %d is too small, minimum is %d\n"),
		    sblock.fs_fsize, sectorsize);
		lockexit(32);
	}
	if (sblock.fs_bsize < MINBSIZE) {
		printf(gettext("block size %d is too small, minimum is %d\n"),
		    sblock.fs_bsize, MINBSIZE);
		lockexit(32);
	}
	if (sblock.fs_bsize > MAXBSIZE) {
		printf(gettext("block size %d is too big, maximum is %d\n"),
		    sblock.fs_bsize, MAXBSIZE);
		lockexit(32);
	}		
	if (sblock.fs_bsize < sblock.fs_fsize) {
		printf(gettext("block size (%d) cannot be smaller than fragment size (%d)\n"),
		    sblock.fs_bsize, sblock.fs_fsize);
		lockexit(32);
	}
	if (argc > 6) {
		cpg = atoi(argv[6]);
		if (cpg <= 0)
			printf(gettext("%s: bad cylinders/group"), *argv),
								lockexit(32);
		cpg_flag++;
	} else if (cpg_flag == 0) { /* If not explicity set, use default */
		cpg = DESCPG;
	}

	if (argc > 7) {
		sblock.fs_minfree = atoi(argv[7]);
		if (sblock.fs_minfree < 0 || sblock.fs_minfree > 99) {
			printf(gettext("%s: bogus minfree reset to %d%%\n"), argv[7],
				MINFREE);
			sblock.fs_minfree = MINFREE;
		}
	} else
		sblock.fs_minfree = minfree;

	if (argc > 8)
		rps = atoi(argv[8]);
	if (rps <= 0 || rps > 1000)
		printf(gettext("preposterous rotations per second %d\n"), rps), lockexit(32);
grow10:
	if (argc > 9)
		nbpi = atoi(argv[9]);
	if (nbpi < DEV_BSIZE) {
		printf(gettext("%d: bogus nbpi reset to 2048\n"), nbpi);
		nbpi = 2048;
	}
	if (nbpi < sblock.fs_fsize) {
		printf(gettext("warning: wasteful data byte allocation / inode:\n"));
		printf(gettext("%d smaller than allocatable fragment size: %d\n"), 
		    nbpi, sblock.fs_fsize);
	}
	if (grow)
		goto grow20;

	if (argc > 10)
		opt = *argv[10];

	if (opt == 's')
		sblock.fs_optim = FS_OPTSPACE;
	else if (opt == 't')
		sblock.fs_optim = FS_OPTTIME;
	else {
		printf(gettext("%c: bogus optimization preference %s\n"),
			opt, "reset to time");
		sblock.fs_optim = FS_OPTTIME;
	}	

	sblock.fs_bmask = ~(sblock.fs_bsize - 1);
	sblock.fs_fmask = ~(sblock.fs_fsize - 1);
	/*
	 * Planning now for future expansion.
	 */
#if defined(mc68000) || defined(sparc)
		sblock.fs_qbmask.val[0] = 0;
		sblock.fs_qbmask.val[1] = ~sblock.fs_bmask;
		sblock.fs_qfmask.val[0] = 0;
		sblock.fs_qfmask.val[1] = ~sblock.fs_fmask;
#endif 
#if defined(vax) || defined(i386)
		sblock.fs_qbmask.val[0] = ~sblock.fs_bmask;
		sblock.fs_qbmask.val[1] = 0;
		sblock.fs_qfmask.val[0] = ~sblock.fs_fmask;
		sblock.fs_qfmask.val[1] = 0;
#endif 
	for (sblock.fs_bshift = 0, i = sblock.fs_bsize; i > 1; i >>= 1)
		sblock.fs_bshift++;
	for (sblock.fs_fshift = 0, i = sblock.fs_fsize; i > 1; i >>= 1)
		sblock.fs_fshift++;
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	for (sblock.fs_fragshift = 0, i = sblock.fs_frag; i > 1; i >>= 1)
		sblock.fs_fragshift++;
	if (sblock.fs_frag > MAXFRAG) {
		printf(gettext("fragment size %d is too small, minimum with block size %d is %d\n"),
		    sblock.fs_fsize, sblock.fs_bsize,
		    sblock.fs_bsize / MAXFRAG);
		lockexit(32);
	}
	sblock.fs_nindir = sblock.fs_bsize / sizeof(daddr_t);
	sblock.fs_inopb = sblock.fs_bsize / sizeof(struct dinode);
	sblock.fs_nspf = sblock.fs_fsize / sectorsize;
	for (sblock.fs_fsbtodb = 0, i = NSPF(&sblock); i > 1; i >>= 1)
		sblock.fs_fsbtodb++;
	sblock.fs_sblkno =
	    roundup(howmany(bbsize + sbsize, sblock.fs_fsize), sblock.fs_frag);
	sblock.fs_cblkno = (daddr_t)(sblock.fs_sblkno +
	    roundup(howmany(sbsize, sblock.fs_fsize), sblock.fs_frag));
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;
	sblock.fs_cgoffset = roundup(
	    howmany(sblock.fs_nsect, NSPF(&sblock)), sblock.fs_frag);
	for (sblock.fs_cgmask = 0xffffffff, i = sblock.fs_ntrak; i > 1; i >>= 1)
		sblock.fs_cgmask <<= 1;
	if (!POWEROF2(sblock.fs_ntrak))
		sblock.fs_cgmask <<= 1;
	/*
	 * Validate specified/determined spc
	 * and calculate minimum cylinders per group.
	 */

	for (sblock.fs_cpc = NSPB(&sblock), i = sblock.fs_spc;
	     sblock.fs_cpc > 1 && (i & 1) == 0;
	     sblock.fs_cpc >>= 1, i >>= 1)
		/* void */;
	mincpc = sblock.fs_cpc;
	bpcg = sblock.fs_spc * sectorsize;
	inospercg = roundup(bpcg / sizeof(struct dinode), INOPB(&sblock));
	if (inospercg > MAXIpG(&sblock))
		inospercg = MAXIpG(&sblock);
	used = (sblock.fs_iblkno + inospercg / INOPF(&sblock)) * NSPF(&sblock);
	mincpgcnt = howmany(sblock.fs_cgoffset * (~sblock.fs_cgmask) + used,
	    sblock.fs_spc);
	mincpg = roundup(mincpgcnt, mincpc);
	/*
	 * Insure that cylinder group with mincpg has enough space
	 * for block maps
	 */
	sblock.fs_cpg = mincpg;
	sblock.fs_ipg = inospercg;
	mapcramped = 0;
	while (CGSIZE(&sblock) > sblock.fs_bsize) {
		mapcramped = 1;
		if (sblock.fs_bsize < MAXBSIZE) {
			sblock.fs_bsize <<= 1;
			if ((i & 1) == 0) {
				i >>= 1;
			} else {
				sblock.fs_cpc <<= 1;
				mincpc <<= 1;
				mincpg = roundup(mincpgcnt, mincpc);
				sblock.fs_cpg = mincpg;
			}
			sblock.fs_frag <<= 1;
			sblock.fs_fragshift += 1;
			if (sblock.fs_frag <= MAXFRAG)
				continue;
		}
		if (sblock.fs_fsize == sblock.fs_bsize) {
			printf(gettext("There is no block size that \
can support this disk\n"));
			lockexit(32);
		}
		sblock.fs_frag >>= 1;
		sblock.fs_fragshift -= 1;
		sblock.fs_fsize <<= 1;
		sblock.fs_nspf <<= 1;
	}
	/*
	 * Insure that cylinder group with mincpg has enough space for inodes
	 */
	inodecramped = 0;
	used *= sectorsize;
	inospercg = roundup((mincpg * bpcg - used) / nbpi, INOPB(&sblock));
	sblock.fs_ipg = inospercg;
	while (inospercg > MAXIpG(&sblock)) {
		inodecramped = 1;
		if (mincpc == 1 || sblock.fs_frag == 1 ||
		    sblock.fs_bsize == MINBSIZE)
			break;
		printf(gettext("With a block size of %d %s %d\n"), sblock.fs_bsize,
		    gettext("minimum bytes per inode is"),
		    (mincpg * bpcg - used) / MAXIpG(&sblock) + 1);
		sblock.fs_bsize >>= 1;
		sblock.fs_frag >>= 1;
		sblock.fs_fragshift -= 1;
		mincpc >>= 1;
		sblock.fs_cpg = roundup(mincpgcnt, mincpc);
		if (CGSIZE(&sblock) > sblock.fs_bsize) {
			sblock.fs_bsize <<= 1;
			break;
		}
		mincpg = sblock.fs_cpg;
		inospercg =
		    roundup((mincpg * bpcg - used) / nbpi, INOPB(&sblock));
		sblock.fs_ipg = inospercg;
	}
	if (inodecramped) {
		if (inospercg > MAXIpG(&sblock)) {
			printf(gettext("Minimum bytes per inode is %d\n"),
			    (mincpg * bpcg - used) / MAXIpG(&sblock) + 1);
		} else if (!mapcramped) {
			printf(gettext(
"With %d bytes per inode, minimum cylinders per group is %d\n"), 
nbpi, mincpg);
		}
	}
	if (mapcramped) {
		printf(gettext(
"With %d sectors per cylinder, minimum cylinders per group is %d\n"), 
sblock.fs_spc, mincpg);
	}
	if (inodecramped || mapcramped) {
		if (sblock.fs_bsize != bsize)
			printf(gettext("%s to be changed from %d to %d\n"),
			    gettext("This requires the block size"),
			    bsize, sblock.fs_bsize);
		if (sblock.fs_fsize != fsize)
			printf(gettext("\t%s to be changed from %d to %d\n"),
			    gettext("and the fragment size"),
			    fsize, sblock.fs_fsize);
		lockexit(32);
	}
	/* 
	 * Calculate the number of cylinders per group
	 */
	sblock.fs_cpg = cpg;
	if (sblock.fs_cpg % mincpc != 0) {
		printf(gettext("%s groups must have a multiple of %d cylinders\n"),
			cpg_flag ? gettext("Cylinder") : gettext("Warning: cylinder"), mincpc);
		sblock.fs_cpg = roundup(sblock.fs_cpg, mincpc);
		if (!cpg_flag)
			cpg = sblock.fs_cpg;
	}
	/*
	 * Must insure there is enough space for inodes
	 */
	sblock.fs_ipg = roundup((sblock.fs_cpg * bpcg - used) / nbpi,
		INOPB(&sblock));
	while (sblock.fs_ipg > MAXIpG(&sblock)) {
		inodecramped = 1;
		sblock.fs_cpg -= mincpc;
		sblock.fs_ipg = roundup((sblock.fs_cpg * bpcg - used) / nbpi,
			INOPB(&sblock));
	}
	/*
	 * Must insure there is enough space to hold block map
	 */
	while (CGSIZE(&sblock) > sblock.fs_bsize) {
		mapcramped = 1;
		sblock.fs_cpg -= mincpc;
		sblock.fs_ipg = roundup((sblock.fs_cpg * bpcg - used) / nbpi,
			INOPB(&sblock));
	}
	sblock.fs_fpg = (sblock.fs_cpg * sblock.fs_spc) / NSPF(&sblock);
	if ((sblock.fs_cpg * sblock.fs_spc) % NSPB(&sblock) != 0) {
		printf(gettext("newfs: panic (fs_cpg * fs_spc) %% NSPF != 0"));
		lockexit(32);
	}
	if (sblock.fs_cpg < mincpg) {
		printf(gettext("cylinder groups must have at least %d cylinders\n"),
			mincpg);
		lockexit(32);
	} else if (sblock.fs_cpg != cpg && cpg_flag &&
		!mapcramped && !inodecramped) {
			lockexit(32);
	}
	sblock.fs_cgsize = fragroundup(&sblock, CGSIZE(&sblock));
grow20:
	/*
	 * Now have size for file system and nsect and ntrak.
	 * Determine number of cylinders and blocks in the file system.
	 */
	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_ncyl = fssize * NSPF(&sblock) / sblock.fs_spc;
	if (fssize * NSPF(&sblock) > sblock.fs_ncyl * sblock.fs_spc) {
		sblock.fs_ncyl++;
		warn = 1;
	}
	if (sblock.fs_ncyl < 1) {
		printf(gettext("file systems must have at least one cylinder\n"));
		lockexit(32);
	}
	if (grow)
		goto grow30;
	/*
	 * Determine feasability/values of rotational layout tables.
	 *
	 * The size of the rotational layout tables is limited by the
	 * size of the superblock, SBSIZE. The amount of space available
	 * for tables is calculated as (SBSIZE - sizeof (struct fs)).
	 * The size of these tables is inversely proportional to the block
	 * size of the file system. The size increases if sectors per track
	 * are not powers of two, because more cylinders must be described
	 * by the tables before the rotational pattern repeats (fs_cpc).
	 */
	sblock.fs_postblformat = FS_DYNAMICPOSTBLFMT;
	sblock.fs_sbsize = fragroundup(&sblock, sizeof(struct fs));
	sblock.fs_npsect = sblock.fs_nsect;
	sblock.fs_interleave = 1;
	if (sblock.fs_ntrak == 1) {
		sblock.fs_cpc = 0;
		goto next;
	}
	postblsize = sblock.fs_nrpos * sblock.fs_cpc * sizeof(short);
	rotblsize = sblock.fs_cpc * sblock.fs_spc / NSPB(&sblock);
	totalsbsize = sizeof(struct fs) + rotblsize;

/* do static allocation if nrpos == 8 and fs_cpc == 16  */
	if (sblock.fs_nrpos == 8 && sblock.fs_cpc <= 16) {
		/* use old static table space */
		sblock.fs_postbloff = (char *)(&sblock.fs_opostbl[0][0]) -
		    (char *)(&sblock.fs_link);
		sblock.fs_rotbloff = &sblock.fs_space[0] -
		    (u_char *)(&sblock.fs_link);
	} else {
		/* use 4.3 dynamic table space */
		sblock.fs_postbloff = &sblock.fs_space[0] -
		    (u_char *)(&sblock.fs_link);
		sblock.fs_rotbloff = sblock.fs_postbloff + postblsize;
		totalsbsize += postblsize;
	}
	if (totalsbsize > SBSIZE ||
	    sblock.fs_nsect > (1 << NBBY) * NSPB(&sblock)) {
		printf("%s %s %d %s %d.%s",
		    gettext(
"Warning: insufficient space in super block for\n\
rotational layout tables with nsect\
and ntrak\
\nFile system performance may be impaired.\n"), 
sblock.fs_nsect, sblock.fs_ntrak);
		sblock.fs_cpc = 0;
		goto next;
	}
	sblock.fs_sbsize = fragroundup(&sblock, totalsbsize);


	/*
	 * calculate the available blocks for each rotational position
	 */
	for (cylno = 0; cylno < sblock.fs_cpc; cylno++)
		for (rpos = 0; rpos < sblock.fs_nrpos; rpos++)
			fs_postbl(&sblock, cylno)[rpos] = -1;
	for (i = (rotblsize - 1) * sblock.fs_frag;
	     i >= 0; i -= sblock.fs_frag) {
		cylno = cbtocylno(&sblock, i);
		rpos = cbtorpos(&sblock, i);
		blk = fragstoblks(&sblock, i);
		if (fs_postbl(&sblock, cylno)[rpos] == -1)
			fs_rotbl(&sblock)[blk] = 0;
		else
			fs_rotbl(&sblock)[blk] =
			    fs_postbl(&sblock, cylno)[rpos] - blk;
		fs_postbl(&sblock, cylno)[rpos] = blk;
	}
next:
grow30:
	/*
	 * Compute/validate number of cylinder groups.
	 */
	sblock.fs_ncg = sblock.fs_ncyl / sblock.fs_cpg;
	if (sblock.fs_ncyl % sblock.fs_cpg)
		sblock.fs_ncg++;
	sblock.fs_dblkno = sblock.fs_iblkno + sblock.fs_ipg / INOPF(&sblock);
	i = MIN(~sblock.fs_cgmask, sblock.fs_ncg - 1);
	if (cgdmin(&sblock, i) - cgbase(&sblock, i) >= sblock.fs_fpg) {
		printf(gettext("inode blocks/cyl group (%d) >= data blocks (%d)\n"),
		    cgdmin(&sblock, i) - cgbase(&sblock, i) / sblock.fs_frag,
		    sblock.fs_fpg / sblock.fs_frag);
		printf(gettext("number of cylinders per cylinder group (%d) %s.\n"),
		    sblock.fs_cpg, "must be increased");
		lockexit(32);
	}
	j = sblock.fs_ncg - 1;
	if ((i = fssize - j * sblock.fs_fpg) < sblock.fs_fpg &&
	    cgdmin(&sblock, j) - cgbase(&sblock, j) > i) {
		printf(gettext("Warning: inode blocks/cyl group (%d) >= data blocks (%d) in last\n"),
		    (cgdmin(&sblock, j) - cgbase(&sblock, j)) / sblock.fs_frag,
		    i / sblock.fs_frag);
		printf(gettext("    cylinder group. This implies %d sector(s) cannot be allocated.\n"),
		    i * NSPF(&sblock));
		sblock.fs_ncg--;
		sblock.fs_ncyl -= sblock.fs_ncyl % sblock.fs_cpg;
		sblock.fs_size = fssize = sblock.fs_ncyl * sblock.fs_spc /
		    NSPF(&sblock);
		warn = 0;
	}
	if (warn && !spc_flag) {
		printf(gettext("Warning: %d sector(s) in last cylinder unallocated\n"),
		    sblock.fs_spc -
		    (fssize * NSPF(&sblock) - (sblock.fs_ncyl - 1)
		    * sblock.fs_spc));
	}
	/*
	 * fill in remaining fields of the super block
	 */
	sblock.fs_csaddr = cgdmin(&sblock, 0);
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));
	i = sblock.fs_bsize / sizeof(struct csum);
	sblock.fs_csmask = ~(i - 1);
	for (sblock.fs_csshift = 0; i > 1; i >>= 1)
		sblock.fs_csshift++;
	fscs = (struct csum *)calloc(1, sblock.fs_cssize);
	checksummarysize();
	if (grow) {
		bcopy((caddr_t)grow_fscs, (caddr_t)fscs, (int)grow_fs_cssize);
		extendsummaryinfo();
		goto grow40;
	}
	sblock.fs_magic = FS_MAGIC;
	sblock.fs_rotdelay = rotdelay;
	sblock.fs_maxcontig = maxcontig;
	sblock.fs_maxbpg = MAXBLKPG(sblock.fs_bsize);

	sblock.fs_rps = rps;
	sblock.fs_cgrotor = 0;
	sblock.fs_cstotal.cs_ndir = 0;
	sblock.fs_cstotal.cs_nbfree = 0;
	sblock.fs_cstotal.cs_nifree = 0;
	sblock.fs_cstotal.cs_nffree = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
	sblock.fs_time = mkfstime;
	sblock.fs_state = FSOKAY - sblock.fs_time;
	sblock.fs_clean = FSCLEAN;
grow40:
	
	/*
	 * Dump out summary information about file system.
	 */
	printf(gettext("%s:\t%d sectors in %d cylinders of %d tracks, %d sectors\n"),
	    fsys, sblock.fs_size * NSPF(&sblock), sblock.fs_ncyl,
	    sblock.fs_ntrak, sblock.fs_nsect);
	printf(gettext("\t%.1fMB in %d cyl groups (%d c/g, %.2fMB/g, %d i/g)\n"),
	    (float)sblock.fs_size * sblock.fs_fsize / MB, sblock.fs_ncg,
	    sblock.fs_cpg, (float)sblock.fs_fpg * sblock.fs_fsize / MB,
	    sblock.fs_ipg);
	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	printf(gettext("super-block backups (for fsck -F ufs -o b=#) at:\n"));
	for (width = cylno = 0; cylno < sblock.fs_ncg; cylno++) {
		if ((grow == 0) || (cylno >= grow_fs_ncg))
			initcg(cylno);
		num = fsbtodb(&sblock, cgsblock(&sblock, cylno));
		sprintf(pbuf, " %u,", num);
		plen = strlen(pbuf);
		if ((width + plen) > (WIDTH - 1)) {
			width = plen;
			printf("\n");
		} else {
			width += plen;
		}
		printf("%s", pbuf);
	}
	printf("\n");
	if (Nflag)
		lockexit(0);
	if (grow)
		goto grow50;

	/*
	 * Now construct the initial file system,
	 * then write out the super-block.
	 */
	fsinit();
grow50:
	/*
	 * write the superblock
	 */
	wtsb();

	/*
	 * extend the last cylinder group in the original file system
	 */
	if (grow) {
		extendcg(grow_fs_ncg-1);
		wtsb();
	}

	/* 
	 * Write out the duplicate super blocks
	 */
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
		wtfs(fsbtodb(&sblock, cgsblock(&sblock, cylno)),
		    sbsize, (char *)&sblock);

	/*
	 * set clean flag
	 */
	if (grow)
		sblock.fs_clean = grow_fs_clean;
	else
		sblock.fs_clean = FSCLEAN;
	sblock.fs_time = mkfstime;
	sblock.fs_state = FSOKAY - sblock.fs_time;
	wtfs(SBOFF / sectorsize, sbsize, (char *)&sblock);

	if (islog && !islogok)
		(void) ioctl(fso, _FIOLOGRESET, NULL);

	fsync(fso);
	close(fsi);
	close(fso);

#ifndef STANDALONE
	lockexit(0);
#endif
}

/*
 * Initialize a cylinder group.
 */
static void
initcg(int cylno)
{
	daddr_t cbase, d, dlower, dupper, dmax;
	long i, j, s;
	register struct csum *cs;

	/*
	 * Determine block bounds for cylinder group.
	 * Allow space for super block summary information in first
	 * cylinder group.
	 */
	cbase = cgbase(&sblock, cylno);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dlower = cgsblock(&sblock, cylno) - cbase;
	dupper = cgdmin(&sblock, cylno) - cbase;
	if (cylno == 0)
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
	cs = fscs + cylno;
	acg.cg_time = mkfstime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cylno;
	if (cylno == sblock.fs_ncg - 1)
		acg.cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
	else
		acg.cg_ncyl = sblock.fs_cpg;
	acg.cg_niblk = sblock.fs_ipg;
	acg.cg_ndblk = dmax - cbase;
	acg.cg_cs.cs_ndir = 0;
	acg.cg_cs.cs_nffree = 0;
	acg.cg_cs.cs_nbfree = 0;
	acg.cg_cs.cs_nifree = 0;
	acg.cg_rotor = 0;
	acg.cg_frotor = 0;
	acg.cg_irotor = 0;
	acg.cg_btotoff = &acg.cg_space[0] - (u_char *)(&acg.cg_link);
	acg.cg_boff = acg.cg_btotoff + sblock.fs_cpg * sizeof(long);
	acg.cg_iusedoff = acg.cg_boff + 
		sblock.fs_cpg * sblock.fs_nrpos * sizeof(short);
	acg.cg_freeoff = acg.cg_iusedoff + howmany(sblock.fs_ipg, NBBY);
	acg.cg_nextfreeoff = acg.cg_freeoff +
		howmany(sblock.fs_cpg * sblock.fs_spc / NSPF(&sblock), NBBY);
	for (i = 0; i < sblock.fs_frag; i++) {
		acg.cg_frsum[i] = 0;
	}
	bzero((caddr_t)cg_inosused(&acg), acg.cg_freeoff - acg.cg_iusedoff);
	acg.cg_cs.cs_nifree += sblock.fs_ipg;
	if (cylno == 0)
		for (i = 0; i < UFSROOTINO; i++) {
			setbit(cg_inosused(&acg), i);
			acg.cg_cs.cs_nifree--;
		}
	for (i = 0; i < sblock.fs_ipg / INOPF(&sblock); i += sblock.fs_frag) {
		randomgeneration();
		wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno) + i),
		    sblock.fs_bsize, (char *)zino);
	}
	bzero((caddr_t)cg_blktot(&acg), acg.cg_boff - acg.cg_btotoff);
	bzero((caddr_t)cg_blks(&sblock, &acg, 0),
	    acg.cg_iusedoff - acg.cg_boff);
	bzero((caddr_t)cg_blksfree(&acg), acg.cg_nextfreeoff - acg.cg_freeoff);

	if (cylno > 0) {
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			setblock(&sblock, cg_blksfree(&acg), d/sblock.fs_frag);
			acg.cg_cs.cs_nbfree++;
			cg_blktot(&acg)[cbtocylno(&sblock, d)]++;
			cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
			    [cbtorpos(&sblock, d)]++;
		}
		sblock.fs_dsize += dlower;
	}
	sblock.fs_dsize += acg.cg_ndblk - dupper;
	if (i = dupper % sblock.fs_frag) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree(&acg), dupper);
			acg.cg_cs.cs_nffree++;
		}
	}
	for (d = dupper; d + sblock.fs_frag <= dmax - cbase; ) {
		setblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag);
		acg.cg_cs.cs_nbfree++;
		cg_blktot(&acg)[cbtocylno(&sblock, d)]++;
		cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
		    [cbtorpos(&sblock, d)]++;
		d += sblock.fs_frag;
	}
	if (d < dmax - cbase) {
		acg.cg_frsum[dmax - cbase - d]++;
		for (; d < dmax - cbase; d++) {
			setbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	sblock.fs_cstotal.cs_ndir += acg.cg_cs.cs_ndir;
	sblock.fs_cstotal.cs_nffree += acg.cg_cs.cs_nffree;
	sblock.fs_cstotal.cs_nbfree += acg.cg_cs.cs_nbfree;
	sblock.fs_cstotal.cs_nifree += acg.cg_cs.cs_nifree;
	*cs = acg.cg_cs;
	wtfs(fsbtodb(&sblock, cgtod(&sblock, cylno)),
		sblock.fs_bsize, (char *)&acg);
}

/*
 * initialize the file system
 */
struct inode node;

#define LOSTDIR
#ifdef LOSTDIR
#define PREDEFDIR 3
#else
#define PREDEFDIR 2
#endif

struct direct root_dir[] = {
	{ UFSROOTINO, sizeof(struct direct), 1, "." },
	{ UFSROOTINO, sizeof(struct direct), 2, ".." },
#ifdef LOSTDIR
	{ LOSTFOUNDINO, sizeof(struct direct), 10, "lost+found" },
#endif
};
#ifdef LOSTDIR
struct direct lost_found_dir[] = {
	{ LOSTFOUNDINO, sizeof(struct direct), 1, "." },
	{ UFSROOTINO, sizeof(struct direct), 2, ".." },
	{ 0, DIRBLKSIZ, 0, 0 },
};
#endif
char buf[MAXBSIZE];

static void
fsinit()
{
	int i;


	/*
	 * initialize the node
	 */
	node.i_atime = mkfstime;
	node.i_mtime = mkfstime;
	node.i_ctime = mkfstime;
#ifdef LOSTDIR
	/*
	 * create the lost+found directory
	 */
	(void)makedir(lost_found_dir, 2);
	for (i = DIRBLKSIZ; i < sblock.fs_bsize; i += DIRBLKSIZ) {
		bcopy(&lost_found_dir[2], &buf[i], DIRSIZ(&lost_found_dir[2]));
	}
	node.i_number = LOSTFOUNDINO;
	node.i_smode = node.i_mode = IFDIR | 0700;
	node.i_nlink = 2;
	node.i_size = sblock.fs_bsize;
	node.i_db[0] = alloc(node.i_size, node.i_mode);
	node.i_blocks = btodb(fragroundup(&sblock, node.i_size));
	IRANDOMIZE(&node.i_ic);
	wtfs(fsbtodb(&sblock, node.i_db[0]), node.i_size, buf);
	iput(&node);
#endif
	/*
	 * create the root directory
	 */
	node.i_number = UFSROOTINO;
	node.i_mode = node.i_smode = IFDIR | UMASK;
	node.i_nlink = PREDEFDIR;
	node.i_size = makedir(root_dir, PREDEFDIR);
	node.i_db[0] = alloc(sblock.fs_fsize, node.i_mode);
	node.i_blocks = btodb(fragroundup(&sblock, node.i_size));
	IRANDOMIZE(&node.i_ic);
	wtfs(fsbtodb(&sblock, node.i_db[0]), sblock.fs_fsize, buf);
	iput(&node);
}

/*
 * construct a set of directory entries in "buf".
 * return size of directory.
 */
static int
makedir(register struct direct *protodir, int entries)
{
	char *cp;
	int i, spcleft;

	spcleft = DIRBLKSIZ;
	for (cp = buf, i = 0; i < entries - 1; i++) {
		protodir[i].d_reclen = DIRSIZ(&protodir[i]);
		bcopy(&protodir[i], cp, protodir[i].d_reclen);
		cp += protodir[i].d_reclen;
		spcleft -= protodir[i].d_reclen;
	}
	protodir[i].d_reclen = spcleft;
	bcopy(&protodir[i], cp, DIRSIZ(&protodir[i]));
	return (DIRBLKSIZ);
}

/*
 * allocate a block or frag
 */
static daddr_t
alloc(int size, int mode)
{
	int i, frag;
	daddr_t d;

	rdfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	if (acg.cg_magic != CG_MAGIC) {
		printf(gettext("cg 0: bad magic number\n"));
		lockexit(32);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		printf(gettext("first cylinder group ran out of space\n"));
		lockexit(32);
	}
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag))
			goto goth;
	printf(gettext("internal error: can't find block in cyl 0\n"));
	lockexit(32);
goth:
	clrblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag);
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;
	fscs[0].cs_nbfree--;
	if (mode & IFDIR) {
		acg.cg_cs.cs_ndir++;
		sblock.fs_cstotal.cs_ndir++;
		fscs[0].cs_ndir++;
	}
	cg_blktot(&acg)[cbtocylno(&sblock, d)]--;
	cg_blks(&sblock, &acg, cbtocylno(&sblock, d))[cbtorpos(&sblock, d)]--;
	if (size != sblock.fs_bsize) {
		frag = howmany(size, sblock.fs_fsize);
		fscs[0].cs_nffree += sblock.fs_frag - frag;
		sblock.fs_cstotal.cs_nffree += sblock.fs_frag - frag;
		acg.cg_cs.cs_nffree += sblock.fs_frag - frag;
		acg.cg_frsum[sblock.fs_frag - frag]++;
		for (i = frag; i < sblock.fs_frag; i++)
			setbit(cg_blksfree(&acg), d + i);
	}
	wtfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	return (d);
}

/*
 * Allocate an inode on the disk
 */
static void
iput(register struct inode *ip)
{
	struct dinode buf[MAXINOPB];
	daddr_t d;
	int c;

	c = itog(&sblock, (int)ip->i_number);
	rdfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	if (acg.cg_magic != CG_MAGIC) {
		printf(gettext("cg 0: bad magic number\n"));
		lockexit(32);
	}
	acg.cg_cs.cs_nifree--;
	setbit(cg_inosused(&acg), ip->i_number);
	wtfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	sblock.fs_cstotal.cs_nifree--;
	fscs[0].cs_nifree--;
	if((int)ip->i_number >= sblock.fs_ipg * sblock.fs_ncg) {
		printf(gettext("fsinit: inode value out of range (%d).\n"),
		    ip->i_number);
		lockexit(32);
	}
	d = fsbtodb(&sblock, itod(&sblock, (int)ip->i_number));
	rdfs(d, sblock.fs_bsize, (char *) buf);
	buf[itoo(&sblock, (int)ip->i_number)].di_ic = ip->i_ic;
	wtfs(d, sblock.fs_bsize, (char *) buf);
}

/*
 * read a block from the file system
 */
static void
rdfs(daddr_t bno, int size, char *bf)
{
	int n;

	if (llseek(fsi, (offset_t)bno * sectorsize, 0) < 0) {
		printf(gettext("seek error: %ld\n"), bno);
		perror("rdfs");
		lockexit(32);
	}
	n = read(fsi, bf, size);
	if(n != size) {
		printf(gettext("read error: %ld\n"), bno);
		perror("rdfs");
		lockexit(32);
	}
}

/*
 * write a block to the file system
 */
static void
wtfs(daddr_t bno, int size, char *bf)
{
	int n;

	if (llseek(fso, (offset_t)bno * sectorsize, 0) < 0) {
		printf(gettext("seek error: %ld\n"), bno);
		perror("wtfs");
		lockexit(32);
	}
	if (Nflag)
		return;
	n = write(fso, bf, size);
	if(n != size) {
		printf(gettext("write error: %ld\n"), bno);
		perror("wtfs");
		lockexit(32);
	}
}

/*
 * check if a block is available
 */
static int
isblock(struct fs *fs, unsigned char *cp, int h)
{
	unsigned char mask;

	switch (fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
#ifdef STANDALONE
		printf("isblock bad fs_frag %d\n", fs->fs_frag);
#else
		fprintf(stderr, "isblock bad fs_frag %d\n", fs->fs_frag);
#endif
		return (0);
	}
}

/*
 * take a block out of the map
 */
static void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch ((fs)->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
#ifdef STANDALONE
		printf("clrblock bad fs_frag %d\n", fs->fs_frag);
#else
		fprintf(stderr, "clrblock bad fs_frag %d\n", fs->fs_frag);
#endif
		return;
	}
}

/*
 * put a block into the map
 */
static void
setblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
#ifdef STANDALONE
		printf("setblock bad fs_frag %d\n", fs->fs_frag);
#else
		fprintf(stderr, "setblock bad fs_frag %d\n", fs->fs_frag);
#endif
		return;
	}
}

static void
usage ()
{
	(void) fprintf (stderr,
	  gettext("ufs usage: mkfs [-F FSType] [-V] [-m] [-o options] special size(sectors) \\ \n"));
	/*							 0     1   */
	(void) fprintf (stderr,
"[nsect ntrack bsize fragsize cpg free rps nbpi opt apc gap nrpos maxcontig]\n");
/*   2     3       4      5      6   7    8   9   10  11  12  13  14 */
	(void) fprintf (stderr, 
		gettext(" -m : dump fs cmd line used to make this partition\n\
 -V :print this command line and return\n\
 -o :ufs options: :nsect=%d,ntrack=%d,bsize=%d,fragsize=%d\n\
 -o :ufs options: :cgsize=%d,free=%d,rps=%d,nbpi=%d,opt=%c\n\
 -o :ufs options: :apc=%d,gap=%d,nrpos=%d,maxcontig=%d\n\
NOTE that all -o suboptions: must be separated only by commas so as to\n\
be parsed as a single argument\n"), 
nsect, ntrack, bsize, fragsize, cpg, sblock.fs_minfree, rps, nbpi, opt,
apc, (rotdelay == -1) ? 0 : rotdelay, sblock.fs_nrpos, maxcontig);
	lockexit(32);
}

/*ARGSUSED*/
static void
dump_fscmd (char *fsys, int fsi)
{
	int inos = MAX(NBPI, sblock.fs_fsize);
 
	bzero ((char *)&sblock, sizeof (sblock));
	rdfs (SBLOCK, SBSIZE, (char *)&sblock);
 
	if (sblock.fs_magic != FS_MAGIC)
	    (void) printf (gettext("[not currently a valid file system - bad superblock]\n"));
 
	(void) printf (gettext("mkfs -F ufs -o "), fsys);
	(void) printf ("nsect=%d,ntrack=%d,", sblock.fs_nsect, sblock.fs_ntrak);
	(void) printf ("bsize=%d,fragsize=%d,cgsize=%d,free=%d,",
	    sblock.fs_bsize, sblock.fs_fsize, sblock.fs_cpg, sblock.fs_minfree);
	(void) printf ("rps=%d,nbpi=%d,opt=%c,apc=%d,gap=%d,",
	    sblock.fs_rps, inos,(sblock.fs_optim == FS_OPTSPACE) ? 's' : 't',
	    (sblock.fs_ntrak * sblock.fs_nsect) - sblock.fs_spc, sblock.fs_rotdelay);
	(void) printf ("nrpos=%d,maxcontig=%d ",
			sblock.fs_nrpos, sblock.fs_maxcontig);
	(void) printf ("%s %d\n", fsys,
	    fsbtodb(&sblock, sblock.fs_size));
 
	bzero ((char *)&sblock, sizeof (sblock));
}

/* number ************************************************************* */
/*									*/
/* Convert a numeric arg to binary					*/
/*									*/
/* Arg:	 big - maximum valid input number				*/
/* Global arg:  string - pointer to command arg				*/
/*									*/
/* Valid forms: 123 | 123k | 123*123 | 123x123				*/
/*									*/
/* Return:	converted number					*/
/*									*/
/* ******************************************************************** */

static unsigned int
number(long big)
{
	register char *cs;
	long n;
	long cut = BIG / 10;    /* limit to avoid overflow */

	cs = string;
	n = 0;
	while ((*cs >= '0') && (*cs <= '9') && (n <= cut))
	{
		n = n*10 + *cs++ - '0';
	}
	for (;;)
	{
		switch (*cs++)
		{

		case 'k':
			n *= 1024;
			continue;
 
		case '*':
		case 'x':
			string = cs;
			n *= number(BIG);
 
		/* Fall into exit test, recursion has read rest of string */
		/* End of string, check for a valid number */
 
		case ',':
		case '\0':
			if ((n > big) || (n < 0))
			{
				fprintf(stderr,
				    gettext("mkfs: argument out of range: \"%lu\"\n"), n);
				lockexit(2);
			}
			cs--;
			string = cs;
			return(n);
 
		default:
			fprintf(stderr, gettext("mkfs: bad numeric arg: \"%s\"\n"), string);
			lockexit(2);

		}
	} /* never gets here */
}

/* match ************************************************************** */
/*									*/
/* Compare two text strings for equality				*/
/*									*/
/* Arg:	 s - pointer to string to match with a command arg		*/
/* Global arg:  string - pointer to command arg				*/
/*									*/
/* Return:	1 if match, 0 if no match				*/
/*		If match, also reset `string' to point to the text	*/
/*		that follows the matching text.				*/
/*									*/
/* ******************************************************************** */
 
static int
match(char *s)
{
	register char *cs;
 
	cs = string;
	while (*cs++ == *s)
	{
		if (*s++ == '\0')
		{
			goto true;
		}
	}
	if (*s != '\0')
	{
		return(0);
	}
 
true:
	cs--;
	string = cs;
	return(1);
}

/*
 * GROWFS ROUTINES
 */

/* ARGSUSED */
void
lockexit(exitstatus)
	int	exitstatus;
{
	/*
	 * flush the dirty cylinder group
	 */
	if (inlockexit == 0) {
		inlockexit = 1;
		flcg();
	}
	/*
	 * make sure the file system is unlocked before exiting
	 */
	if (inlockexit == 1) {
		inlockexit = 2;
		ulockfs();
	}

	exit(exitstatus);
}

void
randomgeneration()
{
	int		 i;
	struct dinode	*dp;

	/*
	 * always perform fsirand(1) function... newfs will notice that
	 * the inodes have been randomized and will not call fsirand itself
	 */
	for (i=0, dp=zino; i<sblock.fs_inopb; ++i, ++dp)
		IRANDOMIZE(&dp->di_ic);
}

void
checksummarysize()
{
	daddr_t	dmax;
	daddr_t	dmin;
	long	cg0frags;
	long	cg0blocks;
	long	maxncg;
	long	maxfrags;
	long	fs_size;

	/*
	 * compute the maximum summary info size
	 */
	dmin = cgdmin(&sblock, 0);
	dmax = cgbase(&sblock, 0) + sblock.fs_fpg;
	fs_size = (grow) ? grow_fs_size : sblock.fs_size;
	if (dmax > fs_size)
		dmax = fs_size;
	cg0frags  = dmax - dmin;
	cg0blocks = cg0frags / sblock.fs_frag;
	if (cg0blocks > MAXCSBUFS)
		cg0blocks = MAXCSBUFS;
	cg0frags = cg0blocks * sblock.fs_frag;
	maxncg   = cg0blocks * (sblock.fs_bsize / sizeof (struct csum));
	maxfrags = maxncg * sblock.fs_fpg;

	/*
	 * remember for later processing in extendsummaryinfo()
	 */
	if (test)
		grow_sifrag = dmin + (cg0blocks * sblock.fs_frag);
	if (testfrags == 0)
		testfrags = cg0frags;
	if (testforce)
		if (testfrags > cg0frags) {
			printf(gettext("Too many test frags (%d); try %d\n"),
				testfrags, cg0frags);
			lockexit(32);
		}

	/*
	 * if summary info is too large (too many cg's) tell the user and exit
	 */
	if (sblock.fs_size > maxfrags) {
		printf(gettext(
			"Too many cylinder groups with %u sectors; try %u\n"),
			fsbtodb(&sblock, sblock.fs_size),
			fsbtodb(&sblock, maxfrags));
		lockexit(32);
	}
}

void
checksblock()
{
	/*
	 * make sure this is a file system
	 */
	if (sblock.fs_magic != FS_MAGIC) {
		printf(gettext("Bad superblock; magic number wrong\n"));
		lockexit(32);
	}
	if (sblock.fs_ncg < 1) {
		printf(gettext("Bad superblock; ncg out of range\n"));
		lockexit(32);
	}
	if (sblock.fs_cpg < 1) {
		printf(gettext("Bad superblock; cpg out of range\n"));
		lockexit(32);
	}
	if (sblock.fs_ncg * sblock.fs_cpg < sblock.fs_ncyl ||
	    (sblock.fs_ncg - 1) * sblock.fs_cpg >= sblock.fs_ncyl) {
		printf(gettext("Bad superblock; ncyl out of range\n"));
		lockexit(32);
	}
	if (sblock.fs_sbsize <= 0 || sblock.fs_sbsize > SBSIZE) {
		printf(gettext(
			"Bad superblock; superblock size out of range\n"));
		lockexit(32);
	}
}

void
growinit()
{
	int	i;
	char	buf[DEV_BSIZE];

	/*
	 * Read and verify the superblock
	 */
	rdfs(SBOFF / sectorsize, sbsize, (char *)&sblock);
	checksblock();
	if (sblock.fs_postblformat != FS_DYNAMICPOSTBLFMT) {
		printf(gettext("old file system format; can't growfs"));
		lockexit(32);
	}

	/*
	 * can't shrink a file system
	 */
	grow_fssize = fsbtodb(&sblock, sblock.fs_size);
	if (fssize < grow_fssize) {
		printf(gettext("%d sectors < current size of %d sectors\n"),
			fssize, grow_fssize);
		lockexit(32);
	}
	/*
	 * can't growfs when logging device has errors
	 */
	if ((islog && !islogok) ||
	    ((FSOKAY == (sblock.fs_state + sblock.fs_time)) &&
	    (sblock.fs_clean == FSLOG && !islog))) {
		printf(gettext("logging device has errors; can't growfs"));
		lockexit(32);
	}

	/*
	 * make sure device is big enough
	 */
	rdfs((daddr_t)fssize - 1, DEV_BSIZE, buf);
	wtfs((daddr_t)fssize - 1, DEV_BSIZE, buf);

	/*
	 * read current summary information
	 */
	grow_fscs = (struct csum *)(malloc((unsigned)sblock.fs_cssize));
	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize) {
		rdfs(fsbtodb(&sblock,
			sblock.fs_csaddr + numfrags(&sblock, i)),
			(int)(sblock.fs_cssize - i < sblock.fs_bsize ?
			sblock.fs_cssize - i : sblock.fs_bsize),
			((caddr_t)grow_fscs) + i);
	}
	/*
	 * save some current size related fields from the superblock
	 */
	grow_fs_size	= sblock.fs_size;
	grow_fs_ncg	= sblock.fs_ncg;
	grow_fs_csaddr	= sblock.fs_csaddr;
	grow_fs_cssize	= sblock.fs_cssize;

	/*
	 * save and reset the clean flag
	 */
	if (FSOKAY == (sblock.fs_state + sblock.fs_time))
		grow_fs_clean = sblock.fs_clean;
	else
		grow_fs_clean = FSBAD;
	sblock.fs_clean = FSBAD;
	sblock.fs_state = FSOKAY - sblock.fs_time;
	wtfs(SBOFF / sectorsize, sbsize, (char *)&sblock);
}

void
checkdev(rdev, bdev)
	char	*rdev;
	char	*bdev;
{
	struct stat	statarea;
	struct ustat	ustatarea;

	if(stat(bdev, &statarea) < 0) {
		fprintf(stderr, gettext("can't check mount point; "));
		fprintf(stderr, gettext("can't stat %s\n"), bdev);
		lockexit(32);
	}
	if ((statarea.st_mode & S_IFMT) != S_IFBLK) {
		printf(gettext("can't check mount point; "));
		printf(gettext("%s is not a block device\n"), bdev);
		lockexit(32);
	}
	if (ustat(statarea.st_rdev, &ustatarea) >= 0)
		bdevismounted = 1;

	if(stat(rdev, &statarea) < 0) {
		fprintf(stderr, gettext("can't stat %s\n"), rdev);
		lockexit(32);
	}
	if ((statarea.st_mode & S_IFMT) != S_IFCHR) {
		printf(gettext("%s is not a character device\n"), rdev);
		lockexit(32);
	}
}

void
checkmount(mntp, bdevname)
	struct mnttab	*mntp;
	char		*bdevname;
{
	struct stat	statdir;
	struct stat	statdev;

	if (strcmp(bdevname, mntp->mnt_special) == 0) {
		if (stat(mntp->mnt_mountp, &statdir) == -1) {
			printf(gettext("can't stat %s\n"), mntp->mnt_mountp);
			lockexit(32);
		}
		if (stat(mntp->mnt_special, &statdev) == -1) {
			printf(gettext("can't stat %s\n"), mntp->mnt_special);
			lockexit(32);
		}
		if (statdir.st_dev != statdev.st_rdev) {
			printf(gettext(
				"%s is not mounted on %s; mnttab(4) wrong\n"),
				mntp->mnt_special, mntp->mnt_mountp);
			lockexit(32);
		}
		ismounted = 1;
		if (directory) {
			if (strcmp(mntp->mnt_mountp, directory) != 0) {
				printf(gettext("%s is mounted on %s, not %s\n"),
					bdevname, mntp->mnt_mountp, directory);
				lockexit(32);
			}
		} else {
			if (grow)
				printf(gettext(
					"%s is mounted on %s; can't growfs\n"),
					bdevname, mntp->mnt_mountp);
			else
				printf(gettext("%s is mounted, can't mkfs\n"),
					bdevname);
			lockexit(32);
		}
	}
}

struct dinode	*dibuf		= 0;
daddr_t		 difrag		= 0;
struct dinode *
gdinode(ino)
	ino_t	ino;
{
	/*
	 * read the block of inodes containing inode number ino
	 */
	if (dibuf == 0)
		dibuf = (struct dinode *)malloc((unsigned)sblock.fs_bsize);
	if (itod(&sblock, ino) != difrag) {
		difrag = itod(&sblock, ino);
		rdfs(fsbtodb(&sblock, difrag), (int)sblock.fs_bsize,
			(char *)dibuf);
	}
	return (dibuf + (ino % INOPB(&sblock)));
}

/*
 * structure that manages the frags we need for extended summary info
 *	These frags can be:
 *		free
 *		data  block
 *		alloc block
 */
struct csfrag {
	struct csfrag	*next;		/* next entry */
	daddr_t		 ofrag;		/* old frag */
	daddr_t		 nfrag;		/* new frag */
	long		 cylno;		/* cylno of nfrag */
	long		 frags;		/* number of frags */
	long		 size;		/* size in bytes */
	ino_t		 ino;		/* inode number */
	long		 fixed;		/* Boolean - Already fixed? */
};
struct csfrag	*csfrag;		/* state unknown */
struct csfrag	*csfragino;		/* frags belonging to an inode */
struct csfrag	*csfragfree;		/* frags that are free */

daddr_t maxcsfrag	= 0;		/* maximum in range */
daddr_t mincsfrag	= 0x7fffffff;	/* minimum in range */

int
csfraginrange(frag)
	daddr_t	frag;
{
	return ((frag >= mincsfrag) && (frag <= maxcsfrag));
}

struct csfrag *
findcsfrag(frag, cfap)
	daddr_t		frag;
	struct csfrag	**cfap;
{
	struct csfrag	*cfp;

	if (!csfraginrange(frag))
		return (NULL);

	for(cfp=*cfap; cfp; cfp = cfp->next)
		if (cfp->ofrag == frag)
			return (cfp);
	return (NULL);
}

void
checkindirect(ino, fragsp, frag, level)
	ino_t	ino;
	daddr_t	*fragsp;
	daddr_t	 frag;
	int	 level;
{
	int			i;
	int			ne	= sblock.fs_bsize / sizeof (daddr_t);
	daddr_t			fsb[MAXBSIZE / sizeof (daddr_t)];

	if (frag == 0)
		return;

	rdfs(fsbtodb(&sblock, frag), (int)sblock.fs_bsize, (char*)fsb);

	checkdirect(ino, fragsp, fsb, sblock.fs_bsize / sizeof (daddr_t));

	if (level)
		for (i=0; i<ne && *fragsp; ++i)
			checkindirect(ino, fragsp, fsb[i], level-1);
}

void
addcsfrag(ino, frag, cfap)
	ino_t		ino;
	daddr_t		frag;
	struct csfrag	**cfap;
{
	struct csfrag	*cfp;

	/*
	 * establish a range for faster checking in csfraginrange()
	 */
	if (frag > maxcsfrag)
		maxcsfrag = frag;
	if (frag < mincsfrag)
		mincsfrag = frag;

	/*
	 * if this frag belongs to an inode and is not the start of a block
	 *	then see if it is part of a frag range for this inode
	 */
	if (ino && (frag % sblock.fs_frag))
		for (cfp = *cfap; cfp; cfp = cfp->next) {
			if (ino != cfp->ino)
				continue;
			if (frag != cfp->ofrag + cfp->frags)
				continue;
			cfp->frags++;
			cfp->size += sblock.fs_fsize;
			return;
		}
	/*
	 * allocate a csfrag entry and link on specified anchor
	 */
	cfp = (struct csfrag *)calloc(1, sizeof (struct csfrag));
	cfp->ino	= ino;
	cfp->ofrag	= frag;
	cfp->frags	= 1;
	cfp->size	= sblock.fs_fsize;
	cfp->next	= *cfap;
	*cfap		= cfp;
}

void
delcsfrag(frag, cfap)
	daddr_t		frag;
	struct csfrag	**cfap;
{
	struct csfrag	*cfp;
	struct csfrag	**cfpp;

	/*
	 * free up entry whose beginning frag matches
	 */
	for (cfpp = cfap; *cfpp; cfpp = &(*cfpp)->next) {
		if (frag == (*cfpp)->ofrag) {
			cfp = *cfpp;
			*cfpp = (*cfpp)->next;
			free((char*)cfp);
			return;
		}
	}
}

void
checkdirect(ino, fragsp, db, ne)
	ino_t	ino;
	daddr_t	*fragsp;
	daddr_t	*db;
	int	 ne;
{
	int	 i;
	int	 j;
	int	 found;
	daddr_t	 frag;

	/*
	 * scan for allocation within the new summary info range
	 */
	for (i=0; i<ne && *fragsp; ++i) {
		if (frag = *db++) {
			found = 0;
			for (j=0; j<sblock.fs_frag && *fragsp; ++j) {
				if (found || (found = csfraginrange(frag))) {
					addcsfrag(ino, frag, &csfragino);
					delcsfrag(frag, &csfrag);
				}
				++frag;
				--(*fragsp);
			}
		}
	}
}

void
findcsfragino()
{
	int		 i;
	int		 j;
	daddr_t		 frags;
	struct dinode	*dp;

	/*
	 * scan all old inodes looking for allocations in the new
	 * summary info range.  Move the affected frag from the
	 * generic csfrag list onto the `owned-by-inode' list csfragino.
	 */
	for (i=UFSROOTINO; i<grow_fs_ncg*sblock.fs_ipg && csfrag; ++i) {
		dp = gdinode((ino_t)i);
		switch (dp->di_mode & IFMT) {
			case IFLNK 	:
			case IFDIR 	:
			case IFREG 	: break;
			default		: continue;
		}

		frags   = dbtofsb(&sblock, dp->di_blocks);

		checkdirect((ino_t)i, &frags, &dp->di_db[0], NDADDR+NIADDR);
		for (j=0; j<NIADDR && frags; ++j)
			checkindirect((ino_t)i, &frags, dp->di_ib[j], j);
	}
}

void
fixindirect(frag, level)
	daddr_t		frag;
	int		level;
{
	int			 i;
	int			 ne	= sblock.fs_bsize / sizeof (daddr_t);
	daddr_t			fsb[MAXBSIZE / sizeof (daddr_t)];

	if (frag == 0)
		return;

	rdfs(fsbtodb(&sblock, frag), (int)sblock.fs_bsize, (char*)fsb);

	fixdirect((caddr_t)fsb, frag, fsb, ne);

	if (level)
		for (i=0; i<ne; ++i)
			fixindirect(fsb[i], level-1);
}

void
fixdirect(bp, frag, db, ne)
	caddr_t		 bp;
	daddr_t		 frag;
	daddr_t		*db;
	int		 ne;
{
	int	 i;
	struct csfrag	*cfp;

	for (i=0; i<ne; ++i, ++db) {
		if (*db == 0)
			continue;
		if ((cfp = findcsfrag(*db, &csfragino)) == NULL)
			continue;
		*db = cfp->nfrag;
		cfp->fixed = 1;
		wtfs(fsbtodb(&sblock, frag), (int)sblock.fs_bsize, bp);
	}
}

void
fixcsfragino()
{
	int		 i;
	struct dinode	*dp;
	struct csfrag	*cfp;

	for (cfp=csfragino; cfp; cfp = cfp->next) {
		if (cfp->fixed)
			continue;
		dp = gdinode((ino_t)cfp->ino);
		fixdirect((caddr_t)dibuf, difrag, dp->di_db, NDADDR+NIADDR);
		for (i=0; i<NIADDR; ++i)
			fixindirect(dp->di_ib[i], i);
	}
}

void
extendsummaryinfo()
{
	int		i;
	int		localtest	= test;
	long		frags;
	daddr_t		oldfrag;
	daddr_t		newfrag;

	/*
	 * if no-write (-N), don't bother
	 */
	if (Nflag)
		return;

again:
	flcg();
	/*
	 * summary info did not change size -- do nothing unless in test mode
	 */
	if (grow_fs_cssize == sblock.fs_cssize)
		if (!localtest)
			return;

	/*
	 * build list of frags needed for additional summary information
	 */
	oldfrag = howmany(grow_fs_cssize, sblock.fs_fsize) + grow_fs_csaddr;
	newfrag = howmany(sblock.fs_cssize, sblock.fs_fsize) + grow_fs_csaddr;
	for (i=oldfrag, frags=0; i<newfrag; ++i, ++frags)
		addcsfrag((ino_t)0, (daddr_t)i, &csfrag);
	sblock.fs_dsize -= (newfrag - oldfrag);

	/*
	 * In test mode, we move more data than necessary from
	 * cylinder group 0.  The lookup/allocate/move code can be
	 * better stressed without having to create HUGE file systems.
	 */
	if (localtest)
		for (i=newfrag; i<grow_sifrag; ++i) {
			if (frags >= testfrags)
				break;
			frags++;
			addcsfrag((ino_t)0, (daddr_t)i, &csfrag);
		}

	/*
	 * move frags to free or inode lists, depending on owner
	 */
	findcsfragfree();
	findcsfragino();

	/*
	 * if not all frags can be located, file system must be inconsistent
	 */
	if (csfrag) {
		printf(gettext(
			"File system may be inconsistent; see fsck(1)\n"));
		lockexit(32);
	}

	/*
	 * allocate the free frags
	 */
	alloccsfragfree();
	/*
	 * allocate extra space for inode frags
	 */
	alloccsfragino();

	/*
	 * not enough space
	 */
	if (notenoughspace()) {
		unalloccsfragfree();
		unalloccsfragino();
		if (localtest && !testforce) {
			localtest = 0;
			goto again;
		}
		printf(gettext("Not enough free space\n"));
		lockexit(32);
	}

	/*
	 * copy the data from old frags to new frags
	 */
	copycsfragino();

	/*
	 * fix the inodes to point to the new frags
	 */
	fixcsfragino();

	/*
	 * We may have moved more frags than we needed.  Free them.
	 */
	rdcg((long)0);
	for (i=newfrag; i<=maxcsfrag; ++i)
		setbit(cg_blksfree(&acg), i-cgbase(&sblock, 0));
	wtcg();

	flcg();
}

int
notenoughspace()
{
	struct csfrag	*cfp;

	for (cfp=csfragino; cfp; cfp = cfp->next)
		if (cfp->nfrag == 0)
			return (1);
	return (0);
}

void
unalloccsfragino()
{
	struct csfrag	*cfp;

	while (cfp = csfragino) {
		if (cfp->nfrag)
			freefrags(cfp->nfrag, cfp->frags, cfp->cylno);
		delcsfrag(cfp->ofrag, &csfragino);
	}
}

void
unalloccsfragfree()
{
	struct csfrag	*cfp;

	while (cfp = csfragfree) {
		freefrags(cfp->ofrag, cfp->frags, cfp->cylno);
		delcsfrag(cfp->ofrag, &csfragfree);
	}
}

void
findcsfragfree()
{
	struct csfrag	*cfp;
	struct csfrag	*cfpnext;

	/*
	 * move free frags onto the free-frag list
	 */
	rdcg((long)0);
	for (cfp = csfrag; cfp; cfp = cfpnext) {
		cfpnext = cfp->next;
		if (isset(cg_blksfree(&acg), cfp->ofrag - cgbase(&sblock, 0))) {
			addcsfrag(cfp->ino, cfp->ofrag, &csfragfree);
			delcsfrag(cfp->ofrag, &csfrag);
		}
	}
}

void
copycsfragino()
{
	struct csfrag	*cfp;
	char		buf[MAXBSIZE];

	/*
	 * copy data from old frags to newly allocated frags
	 */
	for (cfp=csfragino; cfp; cfp = cfp->next) {
		rdfs(fsbtodb(&sblock, cfp->ofrag), (int)cfp->size, buf);
		wtfs(fsbtodb(&sblock, cfp->nfrag), (int)cfp->size, buf);
	}
}

long	curcylno	= -1;
int	cylnodirty	= 0;
void
rdcg(cylno)
	long	cylno;
{
	if (cylno != curcylno) {
		flcg();
		curcylno = cylno;
		rdfs(fsbtodb(&sblock, cgtod(&sblock, curcylno)),
			(int)sblock.fs_cgsize, (char *)&acg);
	}
}

void
flcg()
{
	if (cylnodirty) {
		resetallocinfo();
		wtfs(fsbtodb(&sblock, cgtod(&sblock, curcylno)),
			(int)sblock.fs_cgsize, (char*)&acg);
		cylnodirty = 0;
	}
	curcylno = -1;
}

void
wtcg()
{
	cylnodirty = 1;
}

void
allocfrags(frags, fragp, cylnop)
	long	frags;
	daddr_t	*fragp;
	long	*cylnop;
{
	int	 i;
	int	 j;
	long	 bits;
	long	 bit;

	/*
	 * Allocate a free-frag range in an old cylinder group
	 */
	for (i=0, *fragp=0; i<grow_fs_ncg; ++i) {
		if (((fscs+i)->cs_nffree < frags) && ((fscs+i)->cs_nbfree == 0))
			continue;
		rdcg((long)i);
		bit = bits = 0;
		while (findfreerange(&bit, &bits)) {
			if (frags <= bits)  {
				for (j=0; j<frags; ++j)
					clrbit(cg_blksfree(&acg), bit+j);
				wtcg();
				*cylnop = i;
				*fragp  = bit + cgbase(&sblock, i);
				return;
			}
			bit += bits;
		}
	}
}

void
alloccsfragino()
{
	struct csfrag	*cfp;

	/*
	 * allocate space for inode frag ranges
	 */
	for (cfp=csfragino; cfp; cfp=cfp->next) {
		allocfrags(cfp->frags, &cfp->nfrag, &cfp->cylno);
		if (cfp->nfrag == 0)
			break;
	}
}

void
alloccsfragfree()
{
	struct csfrag	*cfp;

	/*
	 * allocate the free frags needed for extended summary info
	 */
	rdcg((long)0);

	for (cfp=csfragfree; cfp; cfp=cfp->next)
		clrbit(cg_blksfree(&acg), cfp->ofrag - cgbase(&sblock, 0));

	wtcg();
}

void
freefrags(frag, frags, cylno)
	daddr_t	frag;
	long	frags;
	long	cylno;
{
	int	i;

	/*
	 * free frags
	 */
	rdcg(cylno);
	for (i=0; i<frags; ++i) {
		setbit(cg_blksfree(&acg), (frag+i) - cgbase(&sblock, cylno));
	}
	wtcg();
}

int
findfreerange(bitp, bitsp)
	long	*bitp;
	long	*bitsp;
{
	long	 bit;

	/*
	 * find a range of free bits in a cylinder group bit map
	 */
	for (bit = *bitp, *bitsp = 0; bit < acg.cg_ndblk; ++bit)
		if (isset(cg_blksfree(&acg), bit))
			break;

	if (bit >= acg.cg_ndblk)
		return (0);

	*bitp  = bit;
	*bitsp = 1;
	for (++bit; bit < acg.cg_ndblk; ++bit, ++(*bitsp)) {
		if ((bit % sblock.fs_frag) == 0)
			break;
		if (isclr(cg_blksfree(&acg), bit))
			break;
	}
	return (1);
}

void
resetallocinfo()
{
	long	cno;
	long	bit;
	long	bits;

	/*
	 * Compute the free blocks/frags info and update the appropriate
	 * inmemory superblock, summary info, and cylinder group fields
	 */
	sblock.fs_cstotal.cs_nffree -= acg.cg_cs.cs_nffree;
	sblock.fs_cstotal.cs_nbfree -= acg.cg_cs.cs_nbfree;

	acg.cg_cs.cs_nffree = 0;
	acg.cg_cs.cs_nbfree = 0;

	bzero((caddr_t)acg.cg_frsum, sizeof(acg.cg_frsum));
	bzero((caddr_t)cg_blktot(&acg), (int)(acg.cg_iusedoff-acg.cg_btotoff));

	bit = bits = 0;
	while (findfreerange(&bit, &bits)) {
		if (bits == sblock.fs_frag) { 
			acg.cg_cs.cs_nbfree++;
			cno = cbtocylno(&sblock, bit);
			cg_blktot(&acg)[cno]++;
			cg_blks(&sblock, &acg, cno)[cbtorpos(&sblock, bit)]++;
		} else {
			acg.cg_cs.cs_nffree += bits;
			acg.cg_frsum[bits]++;
		}
		bit += bits;
	}

	*(fscs + acg.cg_cgx) = acg.cg_cs;

	sblock.fs_cstotal.cs_nffree += acg.cg_cs.cs_nffree;
	sblock.fs_cstotal.cs_nbfree += acg.cg_cs.cs_nbfree;
}

void
extendcg(cylno)
	long	cylno;
{
	int	i;
	daddr_t	dupper;
	daddr_t	cbase;
	daddr_t	dmax;

	/*
	 * extend the cylinder group at the end of the old file system
	 * if it was partially allocated becase of lack of space
	 */
	flcg();
	rdcg(cylno);

	dupper = acg.cg_ndblk;
	if (cylno == sblock.fs_ncg - 1)
		acg.cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
	else
		acg.cg_ncyl = sblock.fs_cpg;
	cbase = cgbase(&sblock, cylno);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	acg.cg_ndblk = dmax - cbase;

	for (i=dupper; i<acg.cg_ndblk; ++i)
		setbit(cg_blksfree(&acg), i);

	sblock.fs_dsize += (acg.cg_ndblk - dupper);

	wtcg();
	flcg();
}

struct lockfs	lockfs;
int		lockfd;
int		islocked;
int		lockfskey;
char		lockfscomment[128];

void
ulockfs()
{
	/*
	 * if the file system was locked, unlock it before exiting
	 */
	if (islocked == 0)
		return;

	/*
	 * first, check if the lock held
	 */
	lockfs.lf_flags = LOCKFS_MOD;
	if (ioctl(lockfd, _FIOLFSS, &lockfs) == -1) {
		perror(directory);
		lockexit(32);
	}

	if (LOCKFS_IS_MOD(&lockfs)) {
		printf(gettext("FILE SYSTEM CHANGED DURING GROWFS!\n"));
		printf(gettext("   See lockfs(1), umount(1), and fsck(1)\n"));
		lockexit(32);
	}
	/*
	 * unlock the file system
	 */
	lockfs.lf_lock  = LOCKFS_ULOCK;
	lockfs.lf_flags = 0;
	lockfs.lf_key   = lockfskey;
	clockfs();
	if (ioctl(lockfd, _FIOLFS, &lockfs) == -1) {
		perror(directory);
		lockexit(32);
	}
}

void
wlockfs()
{

	/*
	 * if no-write (-N), don't bother
	 */
	if (Nflag)
		return;
	/*
	 * open the mountpoint, and write lock the file system
	 */
	if ((lockfd = open(directory, O_RDONLY)) == -1) {
		perror(directory);
		lockexit(32);
	}
	lockfs.lf_lock  = LOCKFS_WLOCK;
	lockfs.lf_flags = 0;
	lockfs.lf_key   = 0;
	clockfs();
	if (ioctl(lockfd, _FIOLFS, &lockfs) == -1) {
		perror(directory);
		lockexit(32);
	}
	islocked = 1;
	lockfskey = lockfs.lf_key;
}

void
clockfs()
{
	time_t	t;
	char	*ct;

	(void) time(&t);
	ct = ctime(&t);
	ct[strlen(ct)-1] = '\0';

	sprintf(lockfscomment, "%s -- mkfs pid %d", ct, getpid());
	lockfs.lf_comlen  = strlen(lockfscomment)+1;
	lockfs.lf_comment = lockfscomment;
}

void
wtsb()
{
	long	i;

	/*
	 * write summary information
	 */
	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize)
		wtfs(fsbtodb(&sblock, sblock.fs_csaddr + numfrags(&sblock, i)),
			(int)(sblock.fs_cssize - i < sblock.fs_bsize ?
			sblock.fs_cssize - i : sblock.fs_bsize),
			((char *)fscs) + i);

	/*
	 * write superblock
	 */
	sblock.fs_time = mkfstime;
	wtfs(SBOFF / sectorsize, sbsize, (char *)&sblock);
}
