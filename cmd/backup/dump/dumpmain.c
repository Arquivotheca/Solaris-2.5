/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#pragma ident	"@(#)dumpmain.c	1.92	94/11/28 SMI"

#include "dump.h"
#include <config.h>
#include <rmt.h>
#include <sys/mtio.h>
#include <sys/mman.h>
#ifndef USG
#include <sys/label.h>
#include <sys/audit.h>
#endif

int	notify = 0;		/* notify operator flag */
int	blockswritten = 0;	/* number of blocks written on current tape */
int	tapeno = 0;		/* current tape number */
daddr_t	filenum = 0;		/* current file number on tape */
int	density = 0;		/* density in bytes/0.1" */
int	tenthsperirg;		/* inter-record-gap in 0.1"'s */
int	ntrec = 0;		/* # tape blocks in each tape record */
int	cartridge = 0;		/* assume non-cartridge tape */
int	tracks;			/* # tracks on a cartridge tape */
int	diskette = 0;		/* assume not dumping to a diskette */
int	printsize = 0;		/* just print estimated size and exit */
#ifdef DEBUG
int	xflag;			/* debugging switch */
#endif

char	*myname;

#ifdef __STDC__
static char *mb(long long);
static void nextstate(int);
#else
static char *mb();
static void nextstate();
#endif

extern	jmp_buf checkpoint_buf;	/* context for return from checkpoint */

void	audit_args();

main(argc, argv)
	int	argc;
	char	*argv[];
{
	char		*arg;
	int		bflag = 0, i, error = 0;
	double		fetapes = 0.0;
	register	struct	mnttab	*dt;
	char		buf[MAXBSIZE];
	char		msgbuf[3000], *msgp;
	char		*defseq = "cmd-line";
	char		kbsbuf[BUFSIZ];

	host = NULL;

	if (myname = strrchr(argv[0], '/'))
		myname++;
	else
		myname = argv[0];

	/*
	 * initiate syslog() functionality.  if we ever start catching
	 * SIGCHLD, or LOG_NOWAIT into the second argument, and make
	 * the SIGCHLD handler such that it can handle syslog() children.
	 */
	openlog(myname, LOG_CONS, LOG_DAEMON);

	if (strcmp("hsmdump", myname) == 0) {
		metamucil_mode = METAMUCIL;	/* online mode is assumed */
		tape = TAPE;
	} else {
		online = 0;
		metamucil_mode = NOT_METAMUCIL;
		tape = DEFTAPE;
	}

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif  /* TEXT_DOMAIN */
	(void) textdomain(TEXT_DOMAIN);
#ifndef USG
	/*
	 * Put out an audit message to reflect the parameters passed
	 */
	audit_args(AU_ADMIN, argc, argv);
#endif
	(void) setreuid(-1, getuid());
	(void) gethostname(spcl.c_host, NAMELEN);
	dumppid = getpid();

	tsize = 0;	/* no default size, detect EOT dynamically */

	if (metamucil_mode == METAMUCIL) {
		error = readconfig((char *)0, msg);
	} else {
		createdefaultfs(NOT_METAMUCIL);
	}

	disk = NULL;
	increm = NINCREM;
	/*CONSTANTCONDITION*/
	if (TP_BSIZE / DEV_BSIZE == 0 || TP_BSIZE % DEV_BSIZE != 0) {
		msg(gettext("TP_BSIZE must be a multiple of DEV_BSIZE\n"));
		dumpabort();
	}
	incno = '9';
	uflag = 0;
	arg = "u";
	if (argc > 1) {
		argv++;
		argc--;
		arg = *argv;
		if (*arg == '-')
			argc++;
	}
	while (*arg)
	switch (*arg++) {		/* BE CAUTIOUS OF FALLTHROUGHS */
	case 'w':
		lastdump('w');		/* tell us only what has to be done */
		exit(0);
		break;

	case 'W':			/* what to do */
		lastdump('W');		/* tell state of what has been done */
		exit(0);		/* do nothing else */
		break;

	case 'f':			/* output file */
		if (argc > 1) {
			argv++;
			argc--;
			tape = *argv;
		}
		if (strcmp(tape, "-") == 0 && verify) {
			msg(gettext(
			"Cannot verify when dumping to standard out.\n"));
			dumpabort();
		}
		break;

	case 'd':			/* density, in bits per inch */
		if (argc > 1) {
			argv++;
			argc--;
			density = atoi(*argv) / 10;
			if (density <= 0) {
				msg(gettext(
				    "Density must be a positive integer\n"));
				dumpabort();
			}
		}
		break;

	case 's':			/* tape size, feet */
		if (argc > 1) {
			argv++;
			argc--;
			tsize = atol(*argv);
			if (tsize < 0) {
				msg(gettext(
			    "Tape size must be a non-negative integer\n"));
				dumpabort();
			}
		}
		break;

	case 't':			/* tracks */
		if (argc > 1) {
			argv++;
			argc--;
			tracks = atol(*argv);
		}
		break;

	case 'b':			/* blocks per tape write */
		if (argc > 1) {
			argv++;
			argc--;
			bflag++;
			ntrec = atol(*argv);
			if (ntrec <= 0 || (ntrec&1) || ntrec > (MAXNTREC*2)) {
				msg(gettext(
		    "Block size must be a positive, even integer <= %d\n"),
				    MAXNTREC*2);
				dumpabort();
			}
			ntrec /= 2;
		}
		break;

	case 'c':			/* Tape is cart. not 9-track */
	case 'C':			/* 'C' to be consistent with 'D' */
		cartridge++;
		break;

	case '0':			/* dump level */
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		incno = arg[-1];
		break;

	case 'x':			/* perform true incremental dump */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `x'\n"));
			dumpabort();
		}
		trueinc++;
		incno = '9';
		break;

	case 'I':			/* alternate dumpdates file */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `I'\n"));
			dumpabort();
		}
		if (argc > 1) {
			argv++;
			argc--;
			increm = *argv;
		}
		break;

	case 'u':			/* update /etc/dumpdates */
		uflag++;
		break;

	case 'U':			/* update specified database */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `U'\n"));
			dumpabort();
		}
		database++;
		if (argc > 1) {
			argv++;
			argc--;
			if (setdbserver(*argv) < 0) {
				msg(gettext(
				    "Unknown database server host `%s'\n"),
				    *argv);
				dumpabort();
			}
		}
		break;

	case 'n':			/* notify operators */
		notify++;
		break;

	case 'a':			/* create archive file */
		archive = 1;
		if (argc > 1) {
			argv++;
			argc--;
			archivefile = *argv;
		}
		break;

	case 'v':
		verify++;
		doingverify++;
		if (strcmp(tape, "-") == 0) {
			msg(gettext(
			"Cannot verify when dumping to standard out.\n"));
			dumpabort();
		}
		break;

	case 'D':
		diskette++;
		if (metamucil_mode == METAMUCIL)
			offline++;
		break;

	case 'l':
		autoload++;
		if (metamucil_mode == NOT_METAMUCIL)
			break;
		/* FALLTHROUGH */ /* if METAMUCIL */

	case 'o':
		offline++;
		break;

	case 'M':			/* exception mail list */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `M'\n"));
			dumpabort();
		}
		if (argc > 1) {
			register char *cp;
			argv++;
			argc--;
			helplist = *argv;
			cp = strchr(helplist, ',');
			while (cp) {
				*cp = ' ';
				cp = strchr(cp, ',');
			}
		}
		if (!helplist || !helplist[0]) {
			msg(gettext("Bad mail recipient list\n"));
			dumpabort();
		}
		break;

	case 'm':			/* list of mail recipients */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `m'\n"));
			dumpabort();
		}
		if (argc > 1) {
			argv++;
			argc--;
			(void) setmail(*argv);
		}
		break;

	case 'F':			/* force off-line mode */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `F'\n"));
			dumpabort();
		}
		online = 0;
		break;

	case 'L':			/* file containing volume labels */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `L'\n"));
			dumpabort();
		}
		if (argc > 1) {
			argv++;
			argc--;
			labelfile = *argv;
		}
		readlfile();		/* get tape labels to use */
		break;

	case 'N':			/* list of tape names (in lieu of L) */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `N'\n"));
			dumpabort();
		}
		if (argc > 1) {
			argv++;
			argc--;
			buildlfile(*argv);	/* build fake file */
		}
		break;

	case 'O':			/* specify operator daemon host */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `O'\n"));
			dumpabort();
		}
		if (argc > 1) {
			argv++;
			argc--;
			if (setopserver(*argv) < 0) {
				msg(gettext(
				    "Unknown operator daemon host `%s'\n"),
				    *argv);
				dumpabort();
			}
		}
		break;

	case 'P':			/* move to... */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `P'\n"));
			dumpabort();
		}
		doposition++;
		/* FALLTHROUGH */

	case 'p':			/* file position on 1st vol */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `p'\n"));
			dumpabort();
		}
		if (argc > 1) {
			argv++;
			argc--;
			filenum = atoi(*argv);
			if (filenum <= 0) {
				msg(gettext(
			    "File position must be a positive integer\n"));
				dumpabort();
			}
		}
		pflag++;
		break;

	case 'R':			/* database update recovery mode */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `R'\n"));
			dumpabort();
		}
		recover++;
		break;

	case 'S':
		printsize++;
		break;

	case 'V':
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `V'\n"));
			dumpabort();
		}
		verifylabels++;
		break;

	case 'X':			/* no dump */
		if (metamucil_mode == NOT_METAMUCIL) {
			msg(gettext("Bad option `X'\n"));
			dumpabort();
		}
		nodump++;
		break;

#ifdef DEBUG
	case 'z':
		xflag++;
		break;
#endif

	default:
		msg(gettext("Bad option `%c'\n"), arg[-1]);
		dumpabort();
	}
	if (argc > 1) {
		argv++;
		argc--;
		disk = *argv;
	}
	if (disk == NULL && recover == 0 && nodump == 0) {
		if (metamucil_mode == METAMUCIL)
			(void) fprintf(stderr, gettext(
			    "Usage: %s [ options [ arguments ] ] filesystem\n"),
				myname);
		else
			(void) fprintf(stderr, gettext(
	"Usage: %s [0123456789fustdWwnDCcbavloS [argument]] filesystem\n"),
				myname);
		Exit(X_ABORT);
	}
	if (database && !filenum) {
		msg(gettext(
			"Database update (U) requires file position (p,P)\n"));
		dumpabort();
	} else if (!filenum)
		filenum = 1;

	if (error < 0) {	/* error == 0 if NOT_METAMUCIL */
		msg(gettext("Cannot read configuration file `%s': %s\n"),
			myname, strerror(errno));
		dumpabort();
	} else if (error > 0) {
		msg(gettext("Configuration file has %d syntax %s.\n"),
		    error, error > 1 ? gettext("errors") : gettext("error"));
		if (nodump)
			dumpabort();
		dumpailing("dump.conf syntax");
		error = 0;	/* user wants to override */
	} else if (nodump) {
		if (metamucil_mode == METAMUCIL)
			msg(gettext("Configuration file syntax OK.\n"));
		Exit(X_FINOK);
	}
#ifdef DEBUG
	printconfig();
#endif
	msginit();

	if (signal(SIGINT, interrupt) == SIG_IGN)
		(void) signal(SIGINT, SIG_IGN);

	if (recover) {
		doupdate((char *)0);
		Exit(X_FINOK);
	}
	if (strcmp(tape, "-") == 0) {
		if (database) {		/* must be METAMUCIL if database set */
			msg(gettext(
				"Cannot update database and dump to `-'\n"));
			Exit(X_ABORT);
		}
		pipeout++;
		tape = dumpdev = sdumpdev = gettext("standard output");
		(void) strcpy(spcl.c_label, "none");
	} else if (*tape == '+') {
		if (metamucil_mode == METAMUCIL) {
			if (setdevice(++tape) < 0) {
				msg(gettext(
				/*CSTYLED*/
				   "Device sequence `%s' not in config file\n"),
					tape);
				dumpabort();
			}
		}
		nextdevice();
		getlabel();
	} else {
		if (diskette)
			/* if not already set, set diskette to default */
			if (metamucil_mode == METAMUCIL) {
				if (strcmp(tape, TAPE) == 0)
					tape = DISKETTE;
			} else {
				if (strcmp(tape, DEFTAPE) == 0)
					tape = DISKETTE;
			}
		if (metamucil_mode == METAMUCIL) {
			if (makedevice(defseq, tape, sequence) < 0) {
				msg(gettext("Error in dump device list: %s\n"),
				    strerror(errno));
				dumpabort();
			}
			(void) setdevice(defseq);
			getdevinfo((dtype_t *)0, &ndevices, (int *)0);
			if (ndevices == 0) {
				msg(gettext(
				    "Error in dump device list: No devices\n"));
				dumpabort();
			}
		}
		nextdevice();
		getlabel();
	}
	if (cartridge && diskette) {
		error = 1;
		msg(gettext("Cannot select both cartridge and diskette\n"));
	}
	if (density && diskette) {
		error = 1;
		msg(gettext("Cannot select density of diskette\n"));
	}
	if (tracks && diskette) {
		error = 1;
		msg(gettext("Cannot select number of tracks of diskette\n"));
	}
	if (error)
		dumpabort();

	/*
	 * Determine how to default tape size and density
	 *
	 *		density				tape size
	 * 9-track	1600 bpi (160 bytes/.1")	2300 ft.
	 * 9-track	6250 bpi (625 bytes/.1")	2300 ft.
	 *
	 * Most Sun-2's came with 4 track (20MB) cartridge tape drives,
	 * while most other machines (Sun-3's and non-Sun's) come with
	 * 9 track (45MB) cartridge tape drives.  Some Sun-2's came with
	 * 9 track drives, but there is no way for the software to detect
	 * which drive type is installed.  Sigh...  We make the gross
	 * assumption that #ifdef mc68010 will test for a Sun-2.
	 *
	 * cartridge	8000 bpi (100 bytes/.1")	425 * tracks ft.
	 */
	if (density == 0)
		density = cartridge ? 100 : 625;
	if (tracks == 0)
		tracks = 9;
	if (!bflag) {
		if (cartridge)
			ntrec = CARTRIDGETREC;
		else if (diskette)
			ntrec = NTREC;
		else if (density >= 625)
			ntrec = HIGHDENSITYTREC;
		else
			ntrec = NTREC;
	}
	if (!diskette) {
		tsize *= 12L*10L;
		if (cartridge)
			tsize *= tracks;
	}
	rmtinit(msg, Exit);
	if (host) {
		char	*cp = strchr(host, '@');
		if (cp == (char *)0)
			cp = host;
		else
			cp++;
		(void) setreuid(-1, 0);
		if (rmthost(host, ntrec) == 0) {
			msg(gettext("Cannot connect to tape host `%s'\n"), cp);
			dumpabort();
		}
		(void) setreuid(-1, getuid());
		host = cp;
	}
	if (signal(SIGHUP, sigAbort) == SIG_IGN)
		(void) signal(SIGHUP, SIG_IGN);
	if (signal(SIGTRAP, sigAbort) == SIG_IGN)
		(void) signal(SIGTRAP, SIG_IGN);
	if (signal(SIGFPE, sigAbort) == SIG_IGN)
		(void) signal(SIGFPE, SIG_IGN);
	if (signal(SIGBUS, sigAbort) == SIG_IGN)
		(void) signal(SIGBUS, SIG_IGN);
	if (signal(SIGSEGV, sigAbort) == SIG_IGN)
		(void) signal(SIGSEGV, SIG_IGN);
	if (signal(SIGTERM, sigAbort) == SIG_IGN)
		(void) signal(SIGTERM, SIG_IGN);
	if (signal(SIGUSR1, sigAbort) == SIG_IGN)
		(void) signal(SIGUSR1, SIG_IGN);
	if (signal(SIGPIPE, sigAbort) == SIG_IGN)
		(void) signal(SIGPIPE, SIG_IGN);

	set_operators();	/* /etc/group snarfed */
	mnttabread();		/* /etc/fstab, /etc/mtab snarfed */

	/*
	 *	disk can be either the full special file name,
	 *	the suffix of the special file name,
	 *	the special name missing the leading '/',
	 *	the file system name with or without the leading '/'.
	 *	NB:  we attempt to avoid dumping the block device
	 *	(using rawname) because specfs and the vm system
	 *	are not necessarily in sync.
	 */
	dt = mnttabsearch(disk, 0);
	if (dt != 0) {
		filesystem = dt->mnt_mountp;
		disk = rawname(dt->mnt_special);
		(void) strncpy(spcl.c_dev, dt->mnt_special, NAMELEN);
		(void) strncpy(spcl.c_filesys, dt->mnt_mountp, NAMELEN);
	} else {
		(void) strncpy(spcl.c_dev, disk, NAMELEN);
#ifdef PARTIAL
		/* check for partial filesystem dump */
		partial_check();
		dt = mnttabsearch(disk, 1);
		if (dt != 0) {
			filesystem = dt->mnt_mountp;
			disk = rawname(dt->mnt_special);
			(void) strncpy(spcl.c_filesys,
			    "a partial file system", NAMELEN);
		}
		else
#endif /* PARTIAL */
		{
			(void) strncpy(spcl.c_filesys,
			    "an unlisted file system", NAMELEN);
			disk = rawname(disk);
		}
	}

	fi = open(disk, O_RDONLY);
	if (fi < 0) {
		msg(gettext("Cannot open dump device `%s': %s\n"),
			disk, strerror(errno));
		Exit(X_ABORT);
	}

	(void) sscanf(&incno, "%1ld", &spcl.c_level);
	getitime();		/* /etc/dumpdates snarfed */

	/*LINTED [buf is char array]*/
	sblock = (struct fs *)buf;
	sync();
	bread(SBLOCK, (char *)sblock, (long)SBSIZE);
	if (sblock->fs_magic != FS_MAGIC) {
		msg(gettext(
	    "Warning - super-block on device `%s' is corrupt - run fsck\n"));
		dumpabort();
	}

	setupmail();			/* initialize mail delivery */
	if (online) {
		if (setreuid(-1, 0) < 0)
			msg(gettext("Cannot become super-user: %s\n"),
				strerror(errno));
		setfsname(filesystem);
		if (!checkonline())	/* on-line or off-line mode? */
			online = 0;
		(void) setreuid(-1, getuid());
	}
	alloctape();			/* allocate tape buffers */

	if (database && !filesystem) {
		msg(gettext(
	"Cannot do database update without knowing target file system\n"));
		Exit(X_ABORT);
	}

	if (online) {
		/*
		 * In order to avoid holding a lock on the file
		 * system while waiting for the first tape to
		 * be mounted, we check for the appropriate mount
		 * before proceeding.
		 */
		(void) sprintf(msgbuf, gettext(
		    "Cannot open `%s'.  Do you want to retry the open?: %s "),
		    dumpdev, gettext("(\"yes\" or \"no\") "));
		if (!pipeout && verifylabels && (filenum == 1 || doposition)) {
			verifylabel();
			positiontape(msgbuf);
			doposition = 0;	/* since we've already done it */
		}
		setfsname(filesystem);
restart:
		if (strcmp(getfslocktype(), "all") == 0 ||
		    strcmp(getfslocktype(), "write") == 0) {
			(void) lockfs(filesystem, getfslocktype());
			/*
			 * Make sure the archive file is not
			 * on this file system.
			 */
			if (archive)
				archivefile = okwrite(archivefile, 1);
		} else
			if (strcmp(getfslocktype(), "none") != 0)
				(void) lockfs(filesystem, "rename");
		/*
		 * Re-read super-block (locking may
		 * have caused an update)
		 */
		bread(SBLOCK, (char *)sblock, (long)SBSIZE);
		if (sblock->fs_magic != FS_MAGIC) {	/* paranoia */
			msg(gettext(
				"bad super-block magic number, run fsck\n"));
			dumpabort();
		}
		if (!doingactive)
			allocino();
	}

	(void) time(&spcl.c_date);
	bcopy(&(spcl.c_shadow), c_shadow_save, sizeof (c_shadow_save));

	if (!printsize) {
		char *online_mode;

		if (metamucil_mode == METAMUCIL) {
			if (online)
				online_mode = gettext(" in on-line mode.\n");
			else
				online_mode = gettext(" in off-line mode.\n");
		} else {
			online_mode = ".\n";
		}
		if (trueinc)
			msg(gettext("Date of this true incremental dump: %s\n"),
				prdate(spcl.c_date, 1));
		else
			msg(gettext("Date of this level %c dump: %s\n"),
				incno, prdate(spcl.c_date, 1));
		msg(gettext("Date of last level %c dump: %s\n"),
			(u_char)lastincno, prdate(spcl.c_ddate, 1));
		msg(gettext("Dumping %s "), disk);
		if (filesystem != 0)
			msgtail("(%.*s:%s) ", NAMELEN, spcl.c_host,
				filesystem);
		msgtail(gettext("to %s%s"), sdumpdev, online_mode);
		(void) opermes(LOG_INFO, gettext(
		    "Level %c dump of %s (%s) to %s%s"), incno, disk,
			filesystem ? filesystem : gettext("unknown"),
			sdumpdev, online_mode);
	}

	esize = 0;
	msiz = roundup(howmany(sblock->fs_ipg * sblock->fs_ncg, NBBY),
		TP_BSIZE);
	if (!doingactive) {
		clrmap = (char *)calloc(msiz, sizeof (char));
		filmap = (char *)calloc(msiz, sizeof (char));
		dirmap = (char *)calloc(msiz, sizeof (char));
		nodmap = (char *)calloc(msiz, sizeof (char));
		shamap = (char *)calloc(msiz, sizeof (char));
		activemap = (char *)calloc(msiz, sizeof (char));
	} else {
		(void) bzero(clrmap, msiz);
		(void) bzero(filmap, msiz);
		(void) bzero(dirmap, msiz);
		(void) bzero(nodmap, msiz);
		(void) bzero(shamap, msiz);
		/* retain active map */
	}

	dumpstate = INIT;
	dumptoarchive = 1;
	dumptodatabase = 0;

	/*
	 * Read cylinder group inode-used bitmaps to avoid reading clear inodes.
	 */
	{
		register char *clrp = clrmap;
		struct cg *cgp =
		    (struct cg *)calloc((u_int)sblock->fs_cgsize, 1);

		for (i = 0; i < sblock->fs_ncg; i++) {
			bread(fsbtodb(sblock, cgtod(sblock, i)),
			    (char *)cgp, sblock->fs_cgsize);
			(void) bcopy(cg_inosused(cgp), clrp,
			    (int)sblock->fs_ipg / NBBY);
			clrp += sblock->fs_ipg / NBBY;
		}
		free((char *)cgp);
		for (i = 0; clrp > clrmap; i <<= NBBY) {
			i |= *--clrp & ((1<<NBBY) - 1);
			*clrp = i >> 1;
		}
	}

	if (!printsize) {
		msgp = gettext("Mapping (Pass I) [regular files]\n");
		msg(msgp);
		(void) opermes(LOG_INFO, msgp);
	}

	ino = 0;
#ifdef PARTIAL
	if (partial_mark(argc, argv)) {
#endif /* PARTIAL */
		if (!doingactive)
			pass(mark, clrmap);	/* mark updates esize */
		else
			pass(active_mark, clrmap);	/* updates esize */
#ifdef PARTIAL
	}
#endif /* PARTIAL */
	do {
		if (!printsize) {
			msgp = gettext("Mapping (Pass II) [directories]\n");
			msg(msgp);
			(void) opermes(LOG_INFO, msgp);
		}
		nadded = 0;
		ino = 0;
		pass(add, dirmap);
	} while (nadded);

	ino = 0; /* adjust estimated size for shadow inodes */
	pass(markshad, nodmap);
	ino = 0;
	pass(estshad, shamap);
	freeshad();

	bmapest(clrmap);
	bmapest(nodmap);

	if (diskette) {
		/* estimate number of floppies */
		if (tsize != 0)
			fetapes = (double)(esize + ntrec) / tsize;
	} else if (cartridge) {
		/*
		 * Estimate number of tapes, assuming streaming stops at
		 * the end of each block written, and not in mid-block.
		 * Assume no erroneous blocks; this can be compensated for
		 * with an artificially low tape size.
		 */
		tenthsperirg = 16;	/* actually 15.48, says Archive */
		if (tsize != 0)
			fetapes = ((double)esize /* blocks */
			    * TP_BSIZE		/* bytes/block */
			    * (1.0/density)	/* 0.1" / byte */
			    +
			    (double)esize	/* blocks */
			    * (1.0/ntrec)	/* streaming-stops per block */
			    * tenthsperirg)	/* 0.1" / streaming-stop */
			    * (1.0 / tsize);	/* tape / 0.1" */
	} else {
		/* Estimate number of tapes, for old fashioned 9-track tape */
#ifdef sun
		/* sun has long irg's */
		tenthsperirg = (density == 625) ? 6 : 12;
#else
		tenthsperirg = (density == 625) ? 5 : 8;
#endif
		if (tsize != 0)
			fetapes = ((double)esize /* blocks */
			    * TP_BSIZE		/* bytes / block */
			    * (1.0/density)	/* 0.1" / byte */
			    +
			    (double)esize	/* blocks */
			    * (1.0/ntrec)	/* IRG's / block */
			    * tenthsperirg)	/* 0.1" / IRG */
			    * (1.0 / tsize);	/* tape / 0.1" */
	}

	etapes = fetapes;	/* truncating assignment */
	etapes++;
	/* count the nodemap on each additional tape */
	for (i = 1; i < etapes; i++)
		bmapest(nodmap);
	esize += etapes + ntrec; /* headers + ntrec trailer blocks */

	/*
	 * If all we wanted was the size estimate,
	 * just print it out and exit.
	 */
	if (printsize) {
		(void) printf("%lld\n", esize * TP_BSIZE);
		sendmail();
		Exit(0);
	}

	if (tsize != 0) {
		if (diskette)
			msgp = gettext(
			    "Estimated %lld blocks (%s) on %3.2f diskettes.\n");
		else
			msgp = gettext(
			    "Estimated %lld blocks (%s) on %3.2f tapes.\n");

		msg(msgp,
		    esize*2, mb(esize), fetapes,
		    diskette ? "diskette" : "tape");
		(void) opermes(LOG_INFO, msgp,
		    esize*2, mb(esize), fetapes,
		    diskette ? "diskette" : "tape");
	} else {
		msgp = gettext("Estimated %lld blocks (%s).\n");
		msg(msgp, esize*2, mb(esize));
		(void) opermes(LOG_INFO, msgp, esize*2, mb(esize));
	}

	if (database) {
		/*
		 * The database tmp files are created
		 * after the mapping passes complete.
		 * This prevents the tmp files from being
		 * dumped should we be dumping the
		 * directory in which they reside.
		 * (The same is true for the archive file,
		 * if one is being created.)
		 */
		creatdbtmp(spcl.c_date);
	}

	dumpstate = CLRI;

	otape(1);			/* bitmap is the first to tape write */
	*telapsed = 0;
	(void) time(tstart_writing);

	{
		register char *np, *fp, *dp;
		np = nodmap;
		dp = dirmap;
		fp = filmap;
		for (i = 0; i < msiz; i++)
			*fp++ = *np++ ^ *dp++;
	}

	while (dumpstate != DONE) {
		/*
		 * When we receive EOT notification from
		 * the writer, the signal handler calls
		 * rollforward and then jumps here.
		 */
		(void) setjmp(checkpoint_buf);
		switch (dumpstate) {
		case INIT:
			/*
			 * We get here if a tape error occurred
			 * after releasing the name lock but before
			 * the volume containing the last of the
			 * dir info was completed.  We have to start
			 * all over in this case.
			 */
			{
				char *rmsg = gettext(
		"Warning - output error occurred after releasing name lock\n\
\tThe dump will restart\n");
				(void) opermes(LOG_WARNING, rmsg);
				msg(rmsg);
				goto restart;
			}
			/* NOTREACHED */
		case START:
		case CLRI:
			ino = UFSROOTINO;
			dumptoarchive = 1;
			dumptodatabase = 0;
			bitmap(clrmap, TS_CLRI);
			nextstate(BITS);
			/* FALLTHROUGH */
		case BITS:
			ino = UFSROOTINO;
			dumptoarchive = 1;
			dumptodatabase = 0;
			if (BIT(UFSROOTINO, nodmap))	/* empty dump check */
				bitmap(nodmap, TS_BITS);
			nextstate(DIRS);
			if (!doingverify) {
				msgp = gettext(
					"Dumping (Pass III) [directories]\n");
				msg(msgp);
				(void) opermes(LOG_INFO, msgp);
			}
			/* FALLTHROUGH */
		case DIRS:
			dumptoarchive = 1;
			dumptodatabase = 1;
			pass(dirdump, dirmap);
			if (online)
				(void) lockfs(filesystem, getfslocktype());
			nextstate(FILES);
			if (!doingverify) {
				msgp = gettext(
					"Dumping (Pass IV) [regular files]\n");
				msg(msgp);
				(void) opermes(LOG_INFO, msgp);
			}
			/* FALLTHROUGH */
		case FILES:
			dumptoarchive = 0;
			dumptodatabase = 0;
			pass(dump, filmap);
			flushcmds();
			dumpstate = END;	/* don't reset ino */
			/* FALLTHROUGH */
		case END:
			dumptoarchive = 1;
			dumptodatabase = 0;
			spcl.c_type = TS_END;
			for (i = 0; i < ntrec; i++) {
				spclrec();
			}
			flusht();
			break;
		case DONE:
			break;
		default:
			msg(gettext("Internal state error\n"));
			dumpabort();
		}
	}

	if ((! doingactive) && (! active))
		trewind();
	if (verify && !doingverify) {
		msgp = gettext("Finished writing last dump volume\n");
		msg(msgp);
		(void) opermes(LOG_INFO, msgp);
		Exit(X_VERIFY);
	}
	if (spcl.c_volume > 1)
		(void) sprintf(msgbuf,
		    gettext("%ld blocks (%s) on %ld volumes"),
			spcl.c_tapea*2, mb(spcl.c_tapea), spcl.c_volume);
	else
		(void) sprintf(msgbuf,
		    gettext("%ld blocks (%s) on 1 volume"),
			spcl.c_tapea*2, mb(spcl.c_tapea));
	if (timeclock(0) != 0) {
		(void) sprintf(kbsbuf,
		    gettext(" at %d KB/sec"),
		    spcl.c_tapea * 1000 / timeclock(0));
		(void) strcat(msgbuf, kbsbuf);
	}
	(void) strcat(msgbuf, "\n");
	msg(msgbuf);
	(void) opermes(LOG_INFO, msgbuf);
	(void) timeclock(-1);

	if (archive)
		msg(gettext("Archiving dump to `%s'\n"), archivefile);
	if (active && !verify) {
		nextstate(INIT);
		activepass();
		goto restart;
	}
	msgp = gettext("DUMP IS DONE\n");
	msg(msgp);
	broadcast(msgp);
	if (! doingactive)
		putitime();
	Exit(X_FINOK);
#ifdef lint
	return (0);
#endif
}

void
sigAbort(sig)
	int	sig;
{
	char	*sigtype;

	switch (sig) {
	case SIGHUP:
		sigtype = "SIGHUP";
		break;
	case SIGTRAP:
		sigtype = "SIGTRAP";
		break;
	case SIGFPE:
		sigtype = "SIGFPE";
		break;
	case SIGBUS:
		msg(gettext("%s  ABORTING!\n"), "SIGBUS()");
		(void) signal(SIGUSR2, SIG_DFL);
		if (metamucil_mode == METAMUCIL && lockpid == getpid())
			(void) lockfs(filesystem, "unlock");
		abort();
		/*NOTREACHED*/
	case SIGSEGV:
		msg(gettext("%s  ABORTING!\n"), "SIGSEGV()");
		(void) signal(SIGUSR2, SIG_DFL);
		if (metamucil_mode == METAMUCIL && lockpid == getpid())
			(void) lockfs(filesystem, "unlock");
		abort();
		/*NOTREACHED*/
	case SIGALRM:
		sigtype = "SIGALRM";
		break;
	case SIGTERM:
		sigtype = "SIGTERM";
		break;
	case SIGPIPE:
		msg(gettext("Broken pipe\n"));
		dumpabort();
		/*NOTREACHED*/
	default:
		sigtype = "SIGNAL";
		break;
	}
	msg(gettext("%s()  try rewriting\n"), sigtype);
	if (pipeout) {
		msg(gettext("Unknown signal, Cannot recover\n"));
		dumpabort();
	}
	msg(gettext("Rewriting attempted as response to unknown signal.\n"));
	(void) fflush(stderr);
	(void) fflush(stdout);
	close_rewind();
	Exit(X_REWRITE);
}

char *
rawname(cp)
	char *cp;
{
	struct stat st;
	char *dp;
	extern char *getfullrawname();

	if (stat(cp, &st) < 0 ||
	    (st.st_mode & S_IFMT) != S_IFBLK)
		return (cp);

	dp = getfullrawname(cp);
	if (dp == 0)
		return (0);
	if (*dp == '\0') {
		free(dp);
		return (0);
	}

	if (stat(dp, &st) < 0 ||
	    (st.st_mode & S_IFMT) != S_IFCHR)
		return (cp);

	return (dp);
}

static char *
mb(blks)
	long long	blks;
{
	static char buf[16];

	if (blks < 1024)
		(void) sprintf(buf, "%lldKB", blks);
	else
		(void) sprintf(buf, "%.2fMB", ((double)blks) / 1024.);
	return (buf);
}

#ifdef signal
void (*nsignal(sig, act))(int)
	int	sig;
	void	(*act)(int);
{
	struct sigaction sa, osa;

	sa.sa_handler = act;
	(void) sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(sig, &sa, &osa) < 0)
		return ((void (*)(int))-1);
	return (osa.sa_handler);
}
#endif

static void
nextstate(state)
	int	state;
{
	dumpstate = state;
	ino = 0;
	pos = 0;
	leftover = 0;
	if (online)
		resetino(ino);
}

/*
 * timeclock() function, for keeping track of how much time we've spent
 * writing to the tape device.  it always returns the amount of time
 * already spent, in milliseconds.  if you pass it a positive, then that's
 * telling it that we're writing, so the time counts.  if you pass it a
 * zero, then that's telling it we're not writing; perhaps we're waiting
 * for user input.
 *
 * a state of -1 resets everything.
 */
unsigned long
timeclock(state)
	int state;
{
	static int *currentState = NULL;
	static struct timeval *clockstart;
	static unsigned long *emilli;

	struct timeval current[1];
	int fd;

#ifdef DEBUG
	fprintf(stderr, "pid=%d timeclock ", getpid());
	if (state == -1)
		fprintf(stderr, "cleared\n");
	else if (state > 0)
		fprintf(stderr, "ticking\n");
	else
		fprintf(stderr, "paused\n");
#endif /* DEBUG */

	/* if we haven't setup the shared memory, init */
	if (currentState == (int *) NULL) {
		if ((fd = open("/dev/zero", O_RDWR)) < 0) {
			msg(gettext("Cannot open `%s': %s\n"),
				"/dev/zero", strerror(errno));
			dumpabort();
		}
		currentState = (int *)mmap((char *) 0, getpagesize(),
			PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t) 0);
		if (currentState == (int *)-1) {
			msg(gettext(
				"Cannot memory map monitor variables: %s\n"),
				strerror(errno));
			dumpabort();
		}
		(void) close(fd);

		clockstart = (struct timeval *)(currentState + 1);
		emilli = (unsigned long *)(clockstart + 1);
		memset(currentState, '\0', getpagesize());
	}

	if (state == -1) {
		bzero(clockstart, sizeof (*clockstart));
		*currentState = *emilli = 0;
		return (0);
	}

	(void) gettimeofday(current, NULL);

	if (*currentState != 0) {
		current->tv_usec += 1000000;
		current->tv_sec--;

		*emilli += (current->tv_sec - clockstart->tv_sec) * 1000;
		*emilli += (current->tv_usec - clockstart->tv_usec) / 1000;
	}

	if (state != 0)
		bcopy(current, clockstart, sizeof (current));

	*currentState = state;

	return (*emilli);
}
