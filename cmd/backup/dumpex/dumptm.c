#ident	"@(#)dumptm.c 1.41 93/10/13"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "tapelib.h"
#include <config.h>
#include <lfile.h>
#include <rmt.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/vnode.h>
#ifdef USG
#include <sys/fs/ufs_inode.h>
#else
#include <ufs/inode.h>
#endif
#include <protocols/dumprestore.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

int	debug;

#define	DATEFMT		" %c "

static int	Cflag;		/* configuration file specified */
static char	*Cfile = NULL;	/* configuration file name */

static int	Lflag;		/* library file specified */
static char	*Lfile;		/* library file name */

static int	lflag;		/* list flag */
static int	llflag;		/* secret super-list flag */

static int	nflag;		/* next n unallocated tapes */
static int	nnumber;	/* how many unallocated tapes */

static int	sflag;		/* scratch tapes */
static int	pflag;		/* purge tapes */

static int	Wflag;		/* w and don't ask questions */
static int	wflag;		/* write scratch tape */
static char	*wnumber;	/* [W: name &] w:number to scratch & write */

static int	mflag;		/* m -> make a new file */
static char	*mfilename;	/* name of new file to make */

static int	eflag;		/* tell me errored & expired tapes */

static int	dflag;		/* set expiration dates */
static char	*darg;		/* -d argument as spec'd */

static int	cflag;		/* set expire cycles */
static int	ccycle;		/* which cycle to set */

static int	uflag;		/* find undefined tapes */
static int	ucount;		/* find this many of them */

static int	rflag;		/* specify range for -s, -l, -d, -c */
static char	*rarg;		/* -r argument as spec'd */

static int	Rflag;		/* Reboot mode -- clean old reserved tapes */

static int	Dflag;		/* specify device for -w, -W */
static char	*Darg;		/* -D argument as spec'd */

#ifdef __STDC__
static void usage(void);
static void writescratchlabel(char *, char *);
static struct range_f *newrange(int);
static struct range_f *range(char *);
static void list(struct range_f *);
static void findfree(int, int);
static void finderror(void);
static int etccheck(char *);
static void reboot_mode(void);
static void opendie(char *, int);
/*
 * From libdump
 */
extern time_t getreldate(char *, struct timeb *);
#else
static void usage();
static void writescratchlabel();
static struct range_f *newrange();
static struct range_f *range();
static void list();
static void findfree();
static void finderror();
static int etccheck();
static void reboot_mode();
static void opendie();
#endif
extern char *optarg;
extern int opterr, optind;

static void
usage(void)
{
	(void) fprintf(stderr, "%s\n", progname);
	(void) fprintf(stderr, gettext(
		"  [%s configfile]\tConfiguration file\n"), "-C");
	(void) fprintf(stderr, gettext(
		"  [%s tapelibfile]\tTape library file\n"), "-L");
	(void) fprintf(stderr, gettext(
		"  [%s]\t\t\tScratch range (from %s) of tapes\n"), "-s", "-r");
	(void) fprintf(stderr, gettext(
		"  [%s]\t\t\tPurge (delete) range (from %s) of tapes\n"),
		"-p", "-r");
	(void) fprintf(stderr, gettext(
	    "  [%s]\t\t\tList range (from %s) of tapes; default: %s\n"),
		"-l", "-r", gettext("all"));
	(void) fprintf(stderr, gettext(
	"  [%s nnnnn]\t\tScratch tape nnnnn then write label on %s device\n"),
		"-w", "-D");
	(void) fprintf(stderr, gettext(
	    "  [%s anylabel]\t\tWrite anylabel on %s device\n"), "-W", "-D");
	(void) fprintf(stderr, gettext(
	    "  [%s n]\t\tDisplay and reserve next n unallocated tapes\n"),
		"-n");
	(void) fprintf(stderr, gettext(
	    "  [%s n]\t\tReport the next n tapes with no status\n"), "-u");
	(void) fprintf(stderr, gettext(
	    "  [%s]\t\t\tDisplay errored but expired tapes\n"), "-e");
	(void) fprintf(stderr, gettext(
	    "  [%s tapelibfile]\tMake new tapelibfile\n"), "-m");
	(void) fprintf(stderr, gettext(
	"  [%s date]\t\tSet expiration dates in range to specified date\n"),
		"-d");
	(void) fprintf(stderr, gettext(
	    "  [%s cycle]\t\tSet expire cycles in range to cyclenumber\n"),
		"-c");
	(void) fprintf(stderr, gettext(
	    "  [%s range]\t\tSpecify range for %s\n"), "-r", "-s, -l, -d, -c");
	(void) fprintf(stderr, gettext(
	    "  [%s device]\t\tSpecify device for %s\n"), "-D", "-w, -W");
	(void) fprintf(stderr, gettext(
	    "  [%s]\t\t\tReboot mode; re-scratch stale Tmp_reserved tapes\n"),
	    "-R");
	exit(1);
}

checklp(num)
int num;
{
	char lpfilename[MAXNAMELEN], lpbuf[BUFSIZ];
	int lpnum = -1;
	FILE *lpfile;

	if (Cfile == NULL)
		return;

	(void) sprintf(lpfilename, "%s.lp", Cfile);
	if ((lpfile = fopen(lpfilename, "r")) == NULL)
		return;

	if (fgets(lpbuf, sizeof (lpbuf), lpfile))
		lpnum = atoi(lpbuf + IDCOLUMN);
	(void) fclose(lpfile);
	if (num == lpnum)
		(void) unlink(lpfilename);
}

main(argc, argv)
	char	*argv[];
{
	int	i, c;
	struct stat statbuf;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) == -1)
		die(gettext("Cannot get host name\n"));

	if (argc <= 1)
		usage();
	opterr = 0;
	while ((c = getopt(argc, argv, "C:L:ln:Nspm:W:w:ec:u:d:r:RD:Z")) !=
	    -1) {
		switch (c) {
		    case 'C':
			    Cfile = optarg;
			    Cflag++;
			    break;
		    case 'L':
			    Lfile = optarg;
			    Lflag++;
			    break;
		    case 'l':
			    if (lflag)
				llflag++;
			    lflag = 1;
			    break;
		    case 'n':
			    nnumber = atoi(optarg);
			    nflag++;
			    break;
		    case 'N':
			    (void) fprintf(stderr, gettext(
				"The %s option is no longer supported\n"),
				"-N");
			    (void) fprintf(stderr, gettext(
				"Use the \"%s\" command instead.\n"),
				"dumpex -n");
			    exit(1);
			    /*NOTREACHED*/
		    case 's':
			    sflag++;
			    break;
		    case 'p':
			    pflag++;
			    break;
		    case 'm':
			    mflag++;
			    mfilename = optarg;
			    break;
		    case 'W':
			    Wflag++;
			    wnumber = optarg;
			    break;
		    case 'w':
			    wflag++;
			    wnumber = optarg;
			    break;
		    case 'e':
			    eflag++;
			    break;
		    case 'c':
			    cflag++;
			    ccycle = atoi(optarg);
			    break;
		    case 'u':
			    uflag++;
			    ucount = atoi(optarg);
			    break;
		    case 'd':
			    dflag++;
			    darg = optarg;
			    break;
		    case 'r':
			    rflag++;
			    rarg = optarg;
			    break;
		    case 'R':
			    Rflag++;
			    break;
		    case 'D':
			    Dflag++;
			    Darg = optarg;
			    break;
		    case 'Z':	/* needed d and D, sorry */
			    debug++;
			    break;
		    case '?':
			usage();
			die(gettext("Cannot parse switch `%s'\n"), argv[0]);
		}
	}

	checkroot(0);
	(void) sprintf(confdir, "%s/dumps", gethsmpath(etcdir));

	if (Rflag) {
		/* If -R specified, all other args are (silently) ignored */
		reboot_mode();
		exit(0);
	}

	if (mflag) {
		char	filepath[MAXPATHLEN];
		struct stat statbuf;
		int	fid;
		if (Lflag || Cflag)
			die(gettext("Specify only one of -L, -C, and -m\n"));
		if (sflag || pflag || lflag || wflag || Wflag || nflag ||
		    eflag || dflag || cflag || rflag || uflag)
			die(gettext("Use only -D with -m\n"));
		if (index(mfilename, '/') != NULL)
			die(gettext(
		    "No slashes allowed in configuration file names\n"));
		(void) sprintf(filepath, "%s/%s", confdir, mfilename);
		if (stat(filepath, &statbuf) != -1)
			die(gettext("File `%s' already exists\n"),
				mfilename);
		fid = creat(filepath, 0660);
		if (fid == -1)
			die(gettext("Cannot create `%s'\n"), filepath);
		if (write(fid, tapelibfilesecurity, strlen(tapelibfilesecurity))
		    != strlen(tapelibfilesecurity))
			die(gettext("Write to new tape file failed\n"));
		if (close(fid) == -1)
			die(gettext("%s failed (%d)\n"), "close", 22);
		(void) printf("%s: %s: created\n", progname, filepath);
		exit(0);
	}
	if (Lflag && Cflag)
		die(gettext("Specify only one of -L and -C\n"));

	if (stat(confdir, &statbuf) < 0 && mkdir(confdir, 0700) < 0)
		die(gettext("Cannot create %s directory"), confdir);
	if (chdir(confdir) == -1)
		die(gettext("Cannot chdir to %s\n"), confdir);
	if (sflag + pflag + lflag + Wflag + wflag + nflag + eflag + uflag +
	    dflag + cflag != 1)
		die(gettext(
	    "Specify exactly one of flags c, d, e, l, n, N, s, p, u, w, W\n"));

	if ((nflag || eflag) && Cflag == 0)
		(void) fprintf(stderr, gettext(
"%s: Warning: the n, N, and e options should use -C so %s\n\
knows the current master cycle number\n"), progname, progname);
	if (Cflag) {
		openconfig(Cfile);
		readit();
		(void) fclose(infid);
		Cflag = 0;
		Lflag = 1;
		Lfile = cf_tapelib;
	}
	if (Wflag) {		/* Just write a label */
		if (Lflag || Cflag)
			(void) fprintf(stderr, gettext(
			    "%s: Ignoring specified configuration/library\n"),
			    progname);
		if (Darg == 0)
			die(gettext("-W requires a -D device\n"));
		writescratchlabel(wnumber, Darg);
		exit(0);
	}
	if (Lfile == NULL)
		die(gettext(
		    "Must specify a configuration (-C) or library (-L)\n"));
	if (!etccheck(Lfile))
		die(gettext("Library `%s' does not exist\n"), Lfile);
	tl_open(Lfile, (Cflag) ? Cfile : NULL);

	/*
	 * figure out if we will be incrementing mastercycle before we start
	 * for real, and if so, go ahead and do it so we don't screw up and
	 * reserve based on the wrong mastercycle:
	 */

	for (i = 1; i < MAXDUMPSETS; i++) {
		struct devcycle_f *d;
		if (cf_tapeset[i] == NULL)
			continue;
		for (d = cf_tapeset[i]->ts_devlist; d; d = d->dc_next)
			if (d->dc_filesys[0] == '-' || d->dc_filesys[0] == '*')
				goto dontincr;
	}
	cf_mastercycle++;
dontincr:

	if ((sflag) || (pflag)) {
		struct range_f *r;
		struct tapedesc_f tapedesc;

		if (rflag == 0)
			die(gettext("-%c requires a -r range\n"),
			    (sflag) ? 's' : 'p');
		tl_lock();
		for (r = range(rarg); r; r = r->r_next) {
			tl_read(r->r_val, &tapedesc);
			if (sflag) { /* scratch */
			    tapedesc.t_status =
				(tapedesc.t_status & ~TL_STATMASK) |
				TL_SCRATCH;
			    tapedesc.t_status &= ~TL_ERRORED;
			    tl_write(r->r_val, &tapedesc);
			    (void) printf(
				gettext("scratch %05.5d\n"), r->r_val);
			} else { /* delete */
			    if ((tapedesc.t_status & TL_STATMASK) ==
				TL_NOSTATUS)
				    continue;
			    memset(&tapedesc, '\0', sizeof (tapedesc));
			    tl_write(r->r_val, &tapedesc);
			    (void) printf(gettext("purge %5.5d\n"), r->r_val);
			}
			checklp(r->r_val);
		}
		tl_unlock();
	} else if (uflag) {
		struct tapedesc_f tapedesc;
		int	i;
		if (ucount < 1 || ucount > 1000)
			die(gettext("-u count should be in range 1..1000\n"));
		for (i = 0; i <= 99999 && ucount; i++) {
			tl_read(i, &tapedesc);
			if (tapedesc.t_status == 0) {
				(void) printf("%05.5d\n", i);
				ucount--;
			}
		}
	} else if (dflag) {	/* set expiration dates */
		struct range_f *r;
		struct tapedesc_f tapedesc;
		int	date = getreldate(darg, NULL);
		if (rflag == 0)
			die(gettext("-d requires a -r range\n"));
		tl_lock();
		for (r = range(rarg); r; r = r->r_next) {
			tl_read(r->r_val, &tapedesc);
			tapedesc.t_expdate = date;
			tl_write(r->r_val, &tapedesc);
		}
		tl_unlock();
	} else if (cflag) {	/* set cycle number */
		struct range_f *r;
		struct tapedesc_f tapedesc;
		if (rflag == 0)
			die(gettext("-c requires a -r range\n"));
		tl_lock();
		for (r = range(rarg); r; r = r->r_next) {
			tl_read(r->r_val, &tapedesc);
			tapedesc.t_expcycle = ccycle;
			tl_write(r->r_val, &tapedesc);
		}
		tl_unlock();
	} else if (lflag) {
		if (rflag == 0)
			rarg = gettext("all");
		list(range(rarg));
	} else if (wflag) {
		int	n;
		char	label[MAXLINELEN];

		if (ncf_dumpdevs == 0 && Dflag == 0)
			die(gettext("-w and -W each require the -D option\n"));
		n = atoi(wnumber);
		if (n < 0 || n > MAXTAPENUM)
			die(gettext("Invalid tape number %d\n"), n);
		(void) sprintf(label, "%s%c%05.5d", Lfile, LF_LIBSEP, n);
		tl_markstatus(n, TL_SCRATCH);
		writescratchlabel(label, (Dflag) ? Darg : cf_dumpdevs[0]);
		tl_markstatus(n, TL_SCRATCH | TL_LABELED);
		checklp(n);
	} else if (nflag)
		findfree(nnumber, 23);
	else if (eflag)
		finderror();
	exit(0);
#ifdef lint
	return (0);
#endif
}

static void
writescratchlabel(label, device)
	char	*label, *device;
{
	union u_spcl tphead;
	struct mtop mtop;
	int	fid;
	/* tape name parsing variables: */
	char	*tapename;	/* copy of tapename we start with */
	char	*tapedev;	/* ultimate tapename we use */
	char	*rmt_machine;	/* remote machine */
	char	*colon;		/* scratch -- index of ':' */
	char	*at;		/* scratch -- index of '@' */
	int	tapelocal;	/* boolean: 1 -> tape is local */

	(void) bzero((char *) &tphead, sizeof (union u_spcl));
	tphead.s_spcl.c_type = TS_TAPE;
	tphead.s_spcl.c_magic = NFS_MAGIC;
	if ((int)strlen(label) > LBLSIZE)
		(void) fprintf(stderr, gettext(
		    "%s: Truncating label `%s' to %d characters: `%.*s'\n"),
			progname, label, LBLSIZE, LBLSIZE, label);
	(void) strncpy(tphead.s_spcl.c_label, label, LBLSIZE);

	tapename = strdup(device);	/* yep, a memory leak */
	colon = index(tapename, ':');
	tapelocal = 1;		/* unless overridden later */
	if (colon == NULL) {	/* tapename had ':' */
		tapedev = tapename;
	} else {
		*colon = '\0';	/* isolate machine or name@machine */
		tapelocal = 0;	/* unless overridden later */
		tapedev = colon + 1;
		at = index(tapename, '@');
		if (at != NULL) {
			rmt_machine = at + 1;
		} else {
			/* add on the remote dump user, if necessary */
			if (cf_rdevuser && *cf_rdevuser) {
				char *tn = checkalloc(strlen(cf_rdevuser) +
					strlen(tapename) + 2);
				(void) sprintf(tn, "%s@%s", cf_rdevuser,
					tapename);
				tapename = tn;
				rmt_machine = index(tapename, '@') + 1;
			} else {
				rmt_machine = tapename;
			}
		}
		if (strcmp(rmt_machine, hostname) == 0)	/* local! */
			tapelocal = 1;
		else {
			if (rmthost(tapename, HIGHDENSITYTREC) == 0)
				die(gettext(
					"Cannot connect to tape host `%s'\n"),
					tapename);
		}
	}
	if (tapelocal) {
		(void) printf(gettext("WRITING LABEL `%s' on %s\n"),
			label, device);
		fid = open(tapedev, O_WRONLY);
		if (fid == -1)
			opendie(tapedev, errno);
		mtop.mt_op = MTREW;
		mtop.mt_count = 1;
		if (ioctl(fid, MTIOCTOP, &mtop) == -1)
			(void) fprintf(stderr, gettext(
				"%s: Cannot rewind `%s'\n"), progname, tapedev);
		if (write(fid, (char *) &tphead,
		    sizeof (union u_spcl)) != sizeof (union u_spcl))
			die(gettext("write to tape failed\n"));
		if (ioctl(fid, MTIOCTOP, &mtop) == -1)
			(void) fprintf(stderr, gettext(
				"%s: Cannot rewind `%s'\n"), progname, tapedev);
		if (close(fid) == -1)
			die(gettext("%s failed (%d)\n"), "close", 1);
	} else {
		(void) printf(gettext("WRITING LABEL `%s' remotely on %s\n"),
			label, device);
		if (rmtopen(tapedev, O_WRONLY) < 0)
			opendie(tapedev, errno);
		if (rmtioctl(MTREW, 1) < 0)
			(void) fprintf(stderr, gettext(
				"%s: Cannot rewind `%s:%s'\n"),
				progname, rmt_machine, tapedev);
		if (rmtwrite((char *) &tphead,
		    sizeof (union u_spcl)) != sizeof (union u_spcl))
			die(gettext("write to tape failed\n"));
		if (rmtioctl(MTREW, 1) < 0)
			(void) fprintf(stderr, gettext(
				"%s: Cannot rewind `%s:%s'\n"),
				progname, rmt_machine, tapedev);
		rmtclose();
	}
}

static struct range_f *
newrange(val)
	int	val;
{
	struct range_f *r;
	/*LINTED [alignment ok]*/
	r = (struct range_f *) checkalloc(sizeof (struct range_f));
	r->r_val = val;
	r->r_next = 0;
	return (r);
}

static struct range_f *
range(list)
	char	*list;
{
	int	i, j;
	struct range_f *first, *last, *r;
	int	start, finish;
	char	*p;
	first = last = newrange(0);
	if (strcasecmp(list, gettext("all")) == 0) {
		for (j = 0; j < tapeliblen; j++) {
			r = newrange(j);
			last->r_next = r;
			last = r;
		}
		r = first->r_next;
		free((char *) first);
		return (r);
	}
	for (p = list; *p; p++)
		if (index("0123456789-,", *p) == NULL)
			goto BAD;
	split(list, ",");
	if (nsplitfields == 0)
		goto BAD;
	for (i = 0; i < nsplitfields; i++) {
		if (splitfields[i][0] == '\0')
			goto BAD;
		if ((p = index(splitfields[i], '-')) != NULL) {
			if (rindex(splitfields[i], '-') != p)
				goto BAD;
			if (splitfields[i][0] == '-')
				goto BAD;
			if (*(p + 1) == '\0')
				goto BAD;
			finish = atoi(p + 1);
			start = atoi(splitfields[i]);	/* atoi stops at - */
			if (start < finish) {
				if ((sflag + wflag) == 0 && finish > tapeliblen)
					finish = tapeliblen;
				if (finish > MAXTAPENUM)
					finish = MAXTAPENUM;
				for (j = start; j <= finish; j++) {
					r = newrange(j);
					last->r_next = r;
					last = r;
				}
			} else {
				if ((sflag + wflag) == 0 && start > tapeliblen)
					start = tapeliblen;
				if (start > MAXTAPENUM)
					start = MAXTAPENUM;
				for (j = start; j >= finish; j--) {
					r = newrange(j);
					last->r_next = r;
					last = r;
				}
			}
		} else {
			r = newrange(atoi(splitfields[i]));
			last->r_next = r;
			last = r;
		}
	}
	r = first->r_next;
	free((char *) first);
	return (r);
BAD:
	die(gettext("Bad range: %s\n"), list);
	/* NOTREACHED */
}

#define	TMAX 3			/* max subscript for state messages */

static void
list(range)
	struct range_f *range;
{
	struct tapedesc_f t;
	struct range_f *r;
	static char **statmessages;

	if (statmessages == (char **)0) {
		/*LINTED [alignment ok]*/
		statmessages = (char **)checkalloc(sizeof (char *) * (TMAX+1));
		/* TL_* from structs.h */
		statmessages[0] = gettext("No_status");
		statmessages[1] = gettext("Scratch");
		statmessages[2] = gettext("Used");
		statmessages[3] = gettext("Tmp_reserved");
	}

	for (r = range; r; r = r->r_next) {
		tl_read(r->r_val, &t);
		if ((t.t_status & TL_STATMASK) == TL_NOSTATUS)
			continue;
		if ((t.t_status & TL_STATMASK) > TMAX)
			(void) printf("%05.5d  %-15.15s",
			    r->r_val, gettext("UNKNOWN<----"));
		else
			(void) printf("%05.5d  %-15.15s", r->r_val,
				statmessages[t.t_status & TL_STATMASK]);
		if (llflag == 0 && (t.t_status & TL_STATMASK) == TL_SCRATCH)
			/* spaces important */
			(void) printf(" %24.24s     ", " ");
		else if (llflag == 0 && t.t_expdate == -1)
			(void) printf(" %-24.24s     ",
			    gettext("    ---- NEVER ----"));
		else {
			struct tm *tm = localtime(&t.t_expdate);
			char buf[256];
			(void) strftime(buf, sizeof (buf),
				gettext(DATEFMT), tm);
			(void) printf("%s%4ld", buf, t.t_expcycle);
		}
		if (t.t_status & TL_ERRORED)
			(void) printf(gettext(" ERRORED"));
		if (t.t_status & TL_LABELED)
			(void) printf(gettext(" LABELED"));
		if (t.t_status & TL_OFFSITE)
			(void) printf(gettext(" OFFSITE"));
		(void) printf("\n");
	}
}

#define	HOURS(t) ((t) * MINUTESPERHOUR * SECONDSPERMINUTE)

static void
findfree(n, reservetime)
	int	n;
	int	reservetime;
{				/* reservetime==0 -> no reserve */
	time_t	tloc;
	int	readlen;	/* probably will get short read at end of DB */
	int	tapesread;	/* how many tapes in the short read */
	int	i;

	if (time(&tloc) == -1)
		die(gettext("Cannot determine current time\n"));
	if (reservetime) {
		reservetime = HOURS(reservetime);
		tl_lock();
	}
	for (cachestart = 0;
	    cachestart < tapeliblen; cachestart += NCACHEDTAPES) {
		if (lseek(tapelibfid,
		    (off_t) (cachestart * TBUFSZ + lentapelibfilesecurity),
		    0) == (off_t) - 1)
			die(gettext("Cannot seek in tape library\n"));
		if ((readlen = read(tapelibfid, (char *) tapecache,
		    NCACHEDTAPES * TBUFSZ)) == -1)
			die(gettext("Cannot read tape library\n"));
		tapesread = readlen / TBUFSZ;
		for (i = 0; i < tapesread; i++) {
			int	stat = tapecache[i].t_status & TL_STATMASK;

			if (stat == TL_NOSTATUS)
				continue;
			if (tapecache[i].t_status & TL_ERRORED ||
			    tapecache[i].t_status & TL_OFFSITE)
				continue;

			if (stat != TL_SCRATCH && (tapecache[i].t_expdate < 0 ||
			    tapecache[i].t_expdate > tloc ||
			    tapecache[i].t_expcycle >= cf_mastercycle))
				continue;

			(void) printf("%05.5d\n", i + cachestart);
			if (reservetime)
				tl_reserve(i + cachestart, reservetime);
			if (--n == 0) {
				if (reservetime)
					tl_unlock();
				return;
			}
		}
	}
	(void) printf(gettext("Unable to find %d more free tapes\n"), n);
	if (reservetime)
		tl_unlock();
}

static void
finderror(void)
{
	int	readlen;	/* probably will get short read at end of DB */
	int	tapesread;	/* how many tapes in the short read */
	int	i;
	time_t	tloc;

	if (time(&tloc) == -1)
		die(gettext("Cannot determine current time\n"));

	for (cachestart = 0;
	    cachestart < tapeliblen; cachestart += NCACHEDTAPES) {
		if (lseek(tapelibfid,
		    (off_t) (cachestart * TBUFSZ + lentapelibfilesecurity),
		    0) == (off_t) - 1)
			die(gettext("Cannot seek in tape library\n"));
		if ((readlen = read(tapelibfid, (char *) tapecache,
		    NCACHEDTAPES * TBUFSZ)) == -1)
			die(gettext("Cannot read tape library\n"));
		tapesread = readlen / TBUFSZ;
		for (i = 0; i < tapesread; i++) {
			int	stat = tapecache[i].t_status & TL_STATMASK;

			if (stat == TL_NOSTATUS)
				continue;
			if (tapecache[i].t_status & TL_OFFSITE)
				continue;

			if ((tapecache[i].t_expdate == -1 ||
			    tapecache[i].t_expdate > tloc ||
			    tapecache[i].t_expcycle >= cf_mastercycle))
				continue;
			if (tapecache[i].t_status & TL_ERRORED)
				(void) printf("%05.5d\n", i + cachestart);
		}
	}
}

static int
etccheck(filename)
	char	*filename;
{
	char	filepath[MAXPATHLEN];
	struct stat statbuf;

	if (index(filename, '/') != NULL)
		die(gettext(
			"No slashes allowed in configuration file names\n"));
	(void) sprintf(filepath, "%s/%s", confdir, filename);
	return (stat(filepath, &statbuf) != -1);
}

#include <dirent.h>
/*
 * At reboot time, clean up any tape files that have reserved tapes
 * that would expire within the "default" amount of time.
 */
static void
reboot_mode()
{
	DIR *dirp;
	struct dirent *dp;
	int i, tlfd, seclen, tapesread, readlen;
	char checkline[MAXLINELEN];
	time_t	tloc;

	if (time(&tloc) == -1)
		return;
	tloc += RESERVETIME;		/* our fudge factor */

	if (chdir(confdir) == -1)	/* nothing to do */
		return;

	dirp = opendir(".");
	if (dirp == (DIR *) NULL)	/* still nothing to do? */
		return;

	seclen = (int) strlen(tapelibfilesecurity);
	while ((dp = readdir(dirp)) != (struct dirent *) NULL) {
		tlfd = open(dp->d_name, O_RDWR);
		if (tlfd == -1)
			continue;

		if (read(tlfd, checkline, seclen) != seclen ||
		    strncmp(checkline, tapelibfilesecurity, (size_t) seclen)) {
			(void) close(tlfd);
			continue;
		}

		/* else, we've got a valid tape library file */
		(void) close(tlfd);
		tl_open(dp->d_name, NULL);
		for (cachestart = 0;
		    cachestart < tapeliblen; cachestart += NCACHEDTAPES) {
			if (lseek(tapelibfid,
			    (off_t) (cachestart * TBUFSZ + seclen), 0) ==
			    (off_t) -1 ||
			    ((readlen = read(tapelibfid, (char *) tapecache,
				NCACHEDTAPES * TBUFSZ)) == -1))
				continue;
			tapesread = readlen / TBUFSZ;
			for (i = 0; i < tapesread; i++) {
				int stat = tapecache[i].t_status & TL_STATMASK;

				if (stat != TL_TMP_RESERVED)
					continue;

				if (tapecache[i].t_expdate < 0 ||
				    tapecache[i].t_expdate > tloc)
					continue;

				tl_markstatus(i + cachestart, TL_SCRATCH);
			}
		}
		tl_close();
	}
	(void) closedir(dirp);
}

void
opendie(char *dev, int err)
{
	char *genmsg = gettext("Cannot open `%s': %s\n");

	if (err == EIO)
		die(genmsg, dev, gettext("no tape loaded or drive offline"));
	else if (err == EACCES)
		die(genmsg, dev, gettext("write protected"));
	else
		die(genmsg, dev, strerror(err));
}
