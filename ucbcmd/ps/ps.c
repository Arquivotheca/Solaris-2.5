/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)ps.c	1.17	95/07/21 SMI"	/* SVr4.0 1.4	*/

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice

Notice of copyright on this source code product does not indicate
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
		  All rights reserved.
********************************************************************/

/*
 * ps -- print things about processes.
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mkdev.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/param.h>
#include <curses.h>
#include <libelf.h>
#include <sys/elf.h>
#include <locale.h>
#include <wctype.h>
#include <stdarg.h>

#define	NTTYS	2	/* max ttys that can be specified with the -t option  */
			/* only one tty can be specified with SunOS ps */
#define	SIZ	30	/* max processes that can be specified with -p and -g */
#define	ARGSIZ	30	/* size of buffer holding args for -t, -p, -u options */

#define	FSTYPE_MAX	8

#ifndef	MAXLOGIN
#define	MAXLOGIN 8	/* max number of chars in login that will be printed */
#endif

struct psent {
	struct prpsinfo *psinfo;
	char *psargs;
	int found;
};

/* Structure for storing user info */
struct udata {
	uid_t	uid;		/* numeric user id */
	char	name[MAXLOGIN];	/* login name, may not be null terminated */
};

int	tplen, maxlen, twidth;
char	hdr[81];
struct  winsize win;

struct udata uid_tbl[SIZ];      /* table to store selected uid's */
int	nut = 0;		/* counter for uid_tbl */

int	retcode = 1;
int	lflg;	/* long format */
int	uflg;	/* user-oriented output */
int	aflg;	/* Display all processes */
int	eflg;	/* Display environment as well as arguments */
int	gflg;	/* Display process group leaders */
int	tflg;   /* Processes running on specific terminals */
int	rflg;	/* Running processes only flag */
int	Sflg;	/* Accumulated time plus all reaped children */
int	xflg;	/* Include processes with no controlling tty */
int	cflg;	/* Display command name */
int	vflg;	/* Virtual memory-oriented output */
int	nflg;	/* Numerical output */
int	pflg;	/* Specific process id passed as argument */
int	Uflg;	/* Update private database, ups_data */
int	errflg;
int	isatty();
char	*gettty();
char	*ttyname();
char	argbuf[ARGSIZ];
char	*parg;
char	*p1;			/* points to successive option arguments */
uid_t	my_uid;
static char	stdbuf[BUFSIZ];

int	ndev;			/* number of devices */
int	maxdev;			/* number of devl structures allocated */

#define	DNSIZE	14
struct devl {			/* device list	 */
	char	dname[DNSIZE];	/* device name	 */
	dev_t	dev;		/* device number */
} *devl;

static	int	nmajor;		/* number of remembered major device numbers */
static	int	maxmajor;	/* number of major device numbers allocated */
static	int	*majordev;	/* array of remembered major device numbers */

/*
 * struct for the symbolic wait channel info
 */

int	nchans;		/* total # of wait channels */

#define	WNAMESIZ	12
#define	WSNAMESIZ	8
#define	WTSIZ		95

struct wchan {
	char		wc_name[WNAMESIZ+1];	/* symbolic name */
	Elf32_Addr	wc_addr;		/* addr in kmem */
} *wchanhd;				/* an array sorted by wc_addr */

#define	NWCINDEX	10		/* the size of the index array */

Elf32_Addr	wchan_index[NWCINDEX];	/* used to speed searches */

/*
 * names listed here get mapped
 */
struct wchan_map {
	char	*map_from;
	char	*map_to;
} wchan_map_list[] = {
	{ "u", "pause" },
	{ NULL, NULL },
};

char	*tty[NTTYS];	/* for t option */
int	ntty = 0;
pid_t	pidsave;
int	npid = 0;
int	ngrpid = 0;

char	*procdir = "/proc";	/* standard /proc directory */
void	usage();		/* print usage message and quit */
void	getdev();		/* reconstruct /tmp/ps_data */
static	int	getdevcalled = 0;	/* if == 1, getdev() has been called */

extern char	*sys_errlist[];

int pscompare(const void *, const void *);

main(argc, argv)
	int	argc;
	char	**argv;
{
	struct prpsinfo info;	/* process information structure from /proc */
	char *psargs = NULL;	/* pointer to buffer for -w and -ww options */
	struct psent *psent;
	int entsize;
	int nent;

	register char	**ttyp = tty;
	char	*name;
	char	*tmp;
	char	*p;
	int	c;
	pid_t	pid;		/* pid:  process id */
	pid_t	ppid;		/* ppid: parent process id */
	int	i, found;
	extern char	*optarg;
	extern int	optind;

	unsigned	size;

	DIR *dirp;
	struct dirent *dentp;
	char	pname[100];
	int	pdlen;

	setlocale(LC_ALL, "");

	my_uid = getuid();

	if (ioctl(1, TIOCGWINSZ, &win) == -1)
		twidth = 80;
	else
		twidth = (win.ws_col == 0 ? 80 : win.ws_col);

	/* add the '-' for BSD compatibility */
	if (argc > 1) {
		if (argv[1][0] != '-' && !isdigit (argv[1][0])) {
			tmp = (char*) malloc (strlen(argv[1]) + 2);
			(void) sprintf(tmp, "%s%s", "-", argv[1]);
			argv[1] = tmp;
		}
	}

	setbuf(stdout, stdbuf);
	while ((c = getopt(argc, argv, "lcaengrSt:xuvwU")) != EOF)
		switch (c) {
		case 'g':
			gflg++;	/* include process group leaders */
			break;
		case 'c':	/* display internal command name */
			cflg++;
			break;
		case 'r':	/* restrict output to running processes */
			rflg++;
			break;
		case 'S':	/* display time by process and all reaped children */
			Sflg++;
			break;
		case 'x':	/* process w/o controlling tty */
			xflg++;
			break;
		case 'l':	/* long listing */
			lflg++;
			uflg = vflg = 0;
			break;
		case 'u':	/* user-oriented output */
			uflg++;
			lflg = vflg = 0;
			break;
		case 'U': 	/* update private database ups_data */
			Uflg++;
			break;
		case 'w':	/* increase display width */
			if (twidth < 132)
				twidth = 132;
			else	/* second w option */
				twidth = NCARGS;
			break;
		case 'v':	/* display virtual memory format */
			vflg++;
			lflg = uflg = 0;
			break;
		case 'a':
			/*
			 * display all processes except process group
			 * leaders and processes w/o controlling tty
			 */
			aflg++;
			gflg++;
			break;
		case 'e':
			/* Display environment along with aguments. */
			eflg++;
			break;
		case 'n':	/* Display numerical output */
			nflg++;
			break;
		case 't':	/* restrict output to named terminal */
#define	TSZ	30
			tflg++;
			gflg++;
			xflg=0;

			p1 = optarg;
			do {	/* only loop through once (NTTYS = 2)*/
				parg = argbuf;
				if (ntty >= NTTYS-1)
					break;
				getarg();
				if ((p = malloc(TSZ)) == NULL) {
					fprintf(stderr, "ps: no memory\n");
					exit(1);
				}
				size = TSZ;
				if (isdigit(*parg)) {
					(void) strcpy(p, "tty");
					size -= 3;
				}

				if (parg && *parg == '?')
					xflg++;

				(void) strncat(p, parg, (int)size);
				*ttyp++ = p;
				ntty++;
			} while (*p1);
			break;
		default:			/* error on ? */
			errflg++;
			break;
		}

	if (errflg)
		usage();

	if ( optind + 1 < argc ) { /* more than one additional argument */
		fprintf(stderr,"ps: too many arguments\n");
		usage();
	}

	if ( optind < argc ) { /* user specified a specific proc id */
		pflg++;
		p1 = argv[optind];
		parg = argbuf;
		getarg();
		if (!num(parg)) {
			fprintf(stderr,
			  "ps: %s is an invalid non-numeric argument for a process id\n", parg);
			usage();
		}
		pidsave = (pid_t)atol(parg);
		aflg = rflg = xflg = 0;
		gflg++;
	}

	if (tflg)
		*ttyp = 0;

	if (Uflg) {	/* update psfile */

		/* allow update only if permissions for real uid allow it */
		setuid(my_uid);

		getdev();
		getwchan();
		wrdata();
		exit(0);
	}

	if (!readata()) {	/* get data from psfile */
		getdev();
		getwchan();
		wrdata();
	}
	uconv();

	/* allocate an initial guess for the number of processes */
	entsize = 1024;
	psent = (struct psent *)malloc(entsize * sizeof(struct psent));
	if (psent == NULL) {
		fprintf(stderr, "ps: no memory\n");
		exit(1);
	}
	nent = 0;	/* no active entries yet */

	if (lflg ) {
		sprintf(hdr," F   UID   PID  PPID CP PRI NI   SZ  RSS    WCHAN S TT        TIME COMMAND");
	} else if (uflg) {
		if (nflg)
			sprintf(hdr,"   UID   PID %%CPU %%MEM   SZ  RSS TT       S    START  TIME COMMAND");
		else
			sprintf(hdr,"USER       PID %%CPU %%MEM   SZ  RSS TT       S    START  TIME COMMAND");
	} else if (vflg) {
		sprintf(hdr,"   PID TT       S  TIME SIZE  RSS %%CPU %%MEM COMMAND");
	} else
		sprintf(hdr,"   PID TT       S  TIME COMMAND");
	twidth = twidth - strlen(hdr) + 6;
	printf("%s\n",hdr);

	if (twidth > PRARGSZ && (psargs = malloc(twidth)) == NULL) {
		fprintf(stderr, "ps: no memory\n");
		exit(1);
	}

	/*
	 * Determine which processes to print info about by searching
	 * the /proc directory and looking at each process.
	 */
	if ((dirp = opendir(procdir)) == NULL) {
		(void) fprintf(stderr, "ps: cannot open PROC directory %s\n",
		    procdir);
		exit(1);
	}

	(void) strcpy(pname, procdir);
	pdlen = strlen(pname);
	pname[pdlen++] = '/';

	/* for each active process --- */
	while (dentp = readdir(dirp)) {
		int	procfd;		/* filedescriptor for /proc/nnnnn */

		if (dentp->d_name[0] == '.')		/* skip . and .. */
			continue;
		(void) strcpy(pname + pdlen, dentp->d_name);
retry:
		if ((procfd = open(pname, O_RDONLY)) == -1)
			continue;

		/*
		 * Get the info structure for the process
		 */
		if (ioctl(procfd, PIOCPSINFO, (char *) &info) == -1) {
			int	saverr = errno;

			(void) close(procfd);
			if (saverr == EAGAIN)
				goto retry;
			if (saverr != ENOENT)
				(void) fprintf(stderr,
				  "ps: PIOCPSINFO on %s: %s\n",
				  pname, sys_errlist[saverr]);
			continue;
		}

		found = 0;
		if (info.pr_state == 0)		/* can't happen? */
			goto closeit;
		pid  = info.pr_pid;
		ppid = info.pr_ppid;

		/* Display only process from command line */
		if (pflg) { 	/* pid in arg list */
			/*if (search(pidlist, npid, pid))*/
			if (pidsave == pid ) {
				found++;
			}
			else
				goto closeit;
		}

		/*
		 * Omit "uninteresting" processes unless 'g' option.
		 */
		if ((ppid == 1) && !(gflg))
			goto closeit;

		/*
		 * Omit non-running processes for 'r' option
		 */
		if (rflg && !(info.pr_sname =='O' || info.pr_sname =='R'))
			goto closeit;

		if (!found && !tflg && !aflg && info.pr_euid != my_uid)
			goto closeit;

		/*
		 * Read the args for the -w and -ww cases
		 */
		if ((psargs != NULL && preadargs(procfd, &info, psargs) == -1)
		 || (eflg && preadenvs(procfd, &info, psargs) == -1)) {
			int	saverr = errno;

			(void) close(procfd);
			if (saverr == EAGAIN)
				goto retry;
			if (saverr != ENOENT)
				(void) fprintf(stderr,
				  "ps: read() on %s: %s\n",
				  pname, sys_errlist[saverr]);
			continue;
		}

		if (nent >= entsize) {
			entsize *= 2;
			psent = (struct psent *)realloc((char *)psent,
				entsize * sizeof(struct psent));
			if (psent == NULL) {
				fprintf(stderr, "ps: no memory\n");
				exit(1);
			}
		}
		if ((psent[nent].psinfo = malloc(sizeof(struct prpsinfo)))
		    == NULL) {
			fprintf(stderr, "ps: no memory\n");
			exit(1);
		}
		*psent[nent].psinfo = info;
		if (psargs == NULL)
			psent[nent].psargs = NULL;
		else {
			if ((psent[nent].psargs = malloc(strlen(psargs)+1))
			    == NULL) {
				fprintf(stderr, "ps: no memory\n");
				exit(1);
			}
			(void) strcpy(psent[nent].psargs, psargs);
		}
		psent[nent].found = found;
		nent++;
closeit:
		(void) close(procfd);
	}

	(void) closedir(dirp);

	qsort((char *)psent, nent, sizeof(psent[0]), pscompare);

	for (i = 0; i < nent; i++) {
		struct psent *pp = &psent[i];
		if (prcom(pp->found, pp->psinfo, pp->psargs)) {
			printf("\n");
			retcode = 0;
		}
	}
	exit(retcode);
	/* NOTREACHED */
}

static void
usage()		/* print usage message and quit */
{
	static char usage1[] = "ps [ -aceglnrSuUvwx ] [ -t term ] [ num ]";

	fprintf(stderr, "usage: %s\n", usage1);
	exit(1);
}

/*
 * Read the process arguments from the process.
 * This allows >PRARGSZ characters of arguments to be displayed but,
 * unlike pr_psargs[], the process may have changed them.
 */
#define	NARG	100
int
preadargs(int pfd, struct prpsinfo *psinfo, char *psargs)
{
	long argvoff = (long)psinfo->pr_argv;
	int len;
	char *psa = psargs;
	int bsize = twidth;
	int narg = NARG;
	long argv[NARG];
	long argoff;
	long nextargoff;

	if (psinfo->pr_zomb || strcmp(psinfo->pr_clname, "SYS") == 0)
		goto out;

	(void) memset(psa, 0, bsize--);
	nextargoff = 0;
	errno = EIO;
	while (bsize > 0) {
		if (narg == NARG) {
			(void) memset((char *)argv, 0, sizeof(argv));
			if (pread(pfd, (char *)argv, sizeof(argv), argvoff)
			    <= 0) {
				if (errno == EIO)
					break;
				return -1;
			}
			narg = 0;
		}
		if ((argoff = argv[narg++]) == 0)
			break;
		if (argoff != nextargoff
		 && pread(pfd, psa, bsize, argoff) <= 0) {
			if (errno == EIO)
				break;
			return -1;
		}
		len = strlen(psa);
		psa += len;
		*psa++ = ' ';
		bsize -= len + 1;
		nextargoff = argoff + len + 1;
		argvoff += sizeof(char **);
	}
	while (psa > psargs && isspace(*(psa-1)))
		psa--;

out:
	*psa = '\0';
	if ((int)strlen(psinfo->pr_psargs) > (int)strlen(psargs))
		strcpy(psargs, psinfo->pr_psargs);

	return 0;
}

/*
 * Read environment variables from the process.
 * Append them to psargs if there is room.
 */
int
preadenvs(int pfd, struct prpsinfo *psinfo, char *psargs)
{
	long envpoff = (long)psinfo->pr_envp;
	int len;
	char *psa;
	char *psainit;
	int bsize;
	int nenv = NARG;
	long envp[NARG];
	long envoff;
	long nextenvoff;

	psainit = psa = (psargs != NULL)? psargs : psinfo->pr_psargs;
	len = strlen(psa);
	psa += len;
	bsize = twidth - len - 1;

	if (bsize <= 0 || psinfo->pr_zomb ||
	    strcmp(psinfo->pr_clname, "SYS") == 0)
		return 0;

	nextenvoff = 0;
	errno = EIO;
	while (bsize > 0) {
		if (nenv == NARG) {
			(void) memset((char *)envp, 0, sizeof(envp));
			if (pread(pfd, (char *)envp, sizeof(envp), envpoff)
			    <= 0) {
				if (errno == EIO)
					break;
				return -1;
			}
			nenv = 0;
		}
		if ((envoff = envp[nenv++]) == 0)
			break;
		if (envoff != nextenvoff
		 && pread(pfd, psa+1, bsize, envoff) <= 0) {
			if (errno == EIO)
				break;
			return -1;
		}
		*psa++ = ' ';
		len = strlen(psa);
		psa += len;
		bsize -= len + 1;
		nextenvoff = envoff + len + 1;
		envpoff += sizeof(char **);
	}
	while (psa > psainit && isspace(*(psa-1)))
		psa--;
	*psa = '\0';

	return 0;
}

/*
 * readata reads in the open devices (terminals) and stores
 * info in the devl structure.
 */
static char	psfile[] = "/tmp/ups_data";

int
readata()
{
	int fd;

	ndev = nmajor = 0;
	if ((fd = open(psfile, O_RDONLY)) == -1)
		return (0);

	if (psread(fd, (char *) &ndev, sizeof (ndev)) == 0 ||
	    (devl = (struct devl *)malloc(ndev * sizeof (*devl))) == NULL)
		goto bad;
	maxdev = ndev;
	if (psread(fd, (char *)devl, ndev * sizeof (*devl)) == 0)
		goto bad;

	/* Read symbolic wait channel data. */
	if (psread(fd, (char *) &nchans, sizeof nchans) == 0 ||
	    (wchanhd = malloc(nchans * sizeof(struct wchan))) == NULL)
		goto bad;
	if (psread(fd, (char *)wchanhd, nchans * sizeof(struct wchan)) == 0 ||
	    psread(fd, (char *)wchan_index, NWCINDEX * sizeof(Elf32_Addr)) == 0)
		goto bad;

	if (psread(fd, (char *) &nmajor, sizeof (nmajor)) == 0 ||
	    (majordev = (int *)malloc(nmajor * sizeof (int))) == NULL)
		goto bad;
	maxmajor = nmajor;
	if (psread(fd, (char *)majordev, nmajor * sizeof (int)) == 0)
		goto bad;

	(void) close(fd);
	return (1);

bad:
	if (devl)
		free(devl);
	if (wchanhd)
		free(wchanhd);
	if (majordev)
		free(majordev);
	devl = NULL;
	wchanhd = NULL;
	majordev = NULL;
	maxdev = ndev = 0;
	nchans = 0;
	maxmajor = nmajor = 0;
	(void) close(fd);
	return (0);
}

/*
 * getdev() uses getdevdir() to pass pathnames under /dev to gdev()
 * along with a status buffer.
 */
void
getdev()
{
	void getdevdir();
	FILE *fp;
	char buf[256];
	int dev_seen = 0;

	if (getdevcalled)
		return;
	getdevcalled++;

	ndev = 0;
	if ((fp = fopen("/etc/ttysrch", "r")) == NULL) {
		getdevdir("/dev/term", 1);
		getdevdir("/dev/pts", 1);
		getdevdir("/dev/xt", 1);
		getdevdir("/dev", 0);
	} else {
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			char *dir = buf;
			char *cp;
			int len = strlen(dir) - 1;

			if (len <= 0)
				continue;
			*(dir + len) = '\0';
			while (len > 0 && isspace(*dir))
				len--, dir++;
			if (len == 0 || strncmp(dir, "/dev", 4) != 0)
				continue;
			cp = dir;
			while (len > 0 && !isspace(*cp))
				len--, cp++;
			*cp = '\0';
			if (strcmp(dir, "/dev") == 0) {
				dev_seen = 1;
				getdevdir(dir, 0);
			} else {
				getdevdir(dir, 1);
			}
		}
		(void) fclose(fp);
		if (!dev_seen)
			getdevdir("/dev", 0);
	}
}

/*
 * getdevdir() searches a directory and passes every
 * file it encounters to gdev().
 */
void
getdevdir(char *dir, int remember)
{
	DIR *dirp;
	struct dirent *dentp;
	struct stat statb;
	char pathname[128];
	char *filename;

	(void) strcpy(pathname, dir);
	filename = pathname + strlen(pathname);
	*filename++ = '/';
	(void) strcpy(filename, ".");
	if ((dirp = opendir(pathname)) != NULL) {
		while (dentp = readdir(dirp)) {
			(void) strcpy(filename, dentp->d_name);
			if (stat(pathname, &statb) == 0)
				gdev(pathname, &statb, remember);
		}
		(void) closedir(dirp);
	}
}

/*
 * gdev() puts device names and ID into the devl structure for character
 * special files in /dev.  The "/dev/" string is stripped from the name
 * and if the resulting pathname exceeds DNSIZE in length then the highest
 * level directory names are stripped until the pathname is DNSIZE or less.
 */
int
gdev(char *objptr, struct stat *statp, int remember)
{
	register int	i;
	int	leng, start;
	static struct devl ldevl[2];
	static int	lndev, consflg;

	if ((statp->st_mode & S_IFMT) == S_IFCHR) {
		/* Get more and be ready for syscon & systty. */
		while (ndev + lndev >= maxdev) {
			maxdev += 100;
			devl = (struct devl *)
			    realloc(devl, sizeof(struct devl ) * maxdev);
			if (devl == NULL) {
				fprintf(stderr,
				    "ps: not enough memory for %d devices\n",
				    maxdev);
				exit(1);
			}
		}
		/*
		 * Save systty & syscon entries if the console
		 * entry hasn't been seen.
		 */
		if (!consflg
		  && (strcmp("/dev/systty", objptr) == 0
		    || strcmp("/dev/syscon", objptr) == 0)) {
			(void) strncpy(ldevl[lndev].dname,
			  &objptr[5], DNSIZE);
			ldevl[lndev].dev = statp->st_rdev;
			lndev++;
			return 0;
		}

		leng = strlen(objptr);
		/* Strip off /dev/ */
		if (leng < DNSIZE + 4)
			(void) strcpy(devl[ndev].dname, &objptr[5]);
		else {
			start = leng - DNSIZE - 1;

			for (i = start; i < leng && (objptr[i] != '/');
			  i++)
				;
			if (i == leng )
				(void) strncpy(devl[ndev].dname,
				  &objptr[start], DNSIZE);
			else
				(void) strncpy(devl[ndev].dname,
				  &objptr[i+1], DNSIZE);
		}
		devl[ndev].dev = statp->st_rdev;
		ndev++;
		/*
		 * Put systty & syscon entries in devl when console
		 * is found.
		 */
		if (strcmp("/dev/console", objptr) == 0) {
			consflg++;
			for (i = 0; i < lndev; i++) {
				(void) strncpy(devl[ndev].dname,
				  ldevl[i].dname, DNSIZE);
				devl[ndev].dev = ldevl[i].dev;
				ndev++;
			}
			lndev = 0;
		}

		if (remember) {		/* remember the major device number */
			int i;
			int maj = major(statp->st_rdev);

			while (nmajor >= maxmajor) {
				maxmajor += 20;
				majordev = (int *)
				    realloc(majordev, sizeof (int) * maxmajor);
				if (majordev == NULL) {
					(void) fprintf(stderr,
				"ps: not enough memory for %d major numbers\n",
					    maxmajor);
					exit(1);
				}
			}
			for (i = 0; i < nmajor; i++)
				if (maj == majordev[i])
					break;
			if (i == nmajor) {	/* new entry */
				majordev[i] = maj;
				nmajor++;
			}
		}
	}
	return 0;
}

/*
 * Have we seen this tty's major device before?
 * Used to determine if it is useful to rebuild ps_data file.
 */
static int
majorexists(dev_t dev)
{
	int i;
	int maj = major(dev);

	for (i = 0; i < nmajor; i++)
		if (maj == majordev[i])
			return (1);
	return (0);
}

wrdata()
{
	char	tmpname[16];
	char *	tfname;
	int	fd;

	(void) umask(02);
	(void) strcpy(tmpname, "/tmp/ps.XXXXXX");
	if ((tfname = mktemp(tmpname)) == NULL || *tfname == '\0') {
		fprintf(stderr, "ps: mktemp(\"/tmp/ps.XXXXXX\") failed, %s\n",
		    sys_errlist[errno]);
		fprintf(stderr, "ps: Please notify your System Administrator\n");
		return;
	}

	if ((fd = open(tfname, O_WRONLY|O_CREAT|O_EXCL, 0664)) < 0) {
		fprintf(stderr, "ps: open(\"%s\") for write failed, %s\n",
		    tfname, sys_errlist[errno]);
		fprintf(stderr, "ps: Please notify your System Administrator\n");
		return;
	}

	/*
	 * Make owner root, group sys.
	 */
	(void) fchown(fd, (uid_t)0, (gid_t)3);

	/* write /dev data */
	pswrite(fd, (char *)&ndev, sizeof(ndev));
	pswrite(fd, (char *)devl, ndev * sizeof(*devl));

	/* write symbolic wait channel data */
	pswrite(fd, (char *)&nchans, sizeof nchans);
	pswrite(fd, (char *)wchanhd, nchans * sizeof(struct wchan));
	pswrite(fd, (char *)wchan_index, NWCINDEX * sizeof(Elf32_Addr));

	pswrite(fd, (char *)&nmajor, sizeof (nmajor));
	pswrite(fd, (char *)majordev, nmajor * sizeof (int));

	(void) close(fd);

	if (rename(tfname, psfile) != 0) {
		fprintf(stderr, "ps: rename(\"%s\",\"%s\") failed, %s\n",
		    tfname, psfile, sys_errlist[errno]);
		fprintf(stderr, "ps: Please notify your System Administrator\n");
		return;
	}
}

/*
 * getarg() finds the next argument in list and copies arg into argbuf.
 * p1 first pts to arg passed back from getopt routine.  p1 is then
 * bumped to next character that is not a comma or blank -- p1 NULL
 * indicates end of list.
 */

getarg()
{
	char	*parga;

	parga = argbuf;
	while (*p1 && *p1 != ',' && *p1 != ' ')
		*parga++ = *p1++;
	*parga = '\0';

	while (*p1 && (*p1 == ',' || *p1 == ' '))
		p1++;
}

/*
 * gettty returns the user's tty number or ? if none.
 */
char *
gettty(ip, psinfo)
	register int	*ip;	/* where the search left off last time */
	struct prpsinfo *psinfo;
{
	register int	i;

	if (psinfo->pr_ttydev != PRNODEV && *ip >= 0) {
		for (i = *ip; i < ndev; i++) {
			if (devl[i].dev == psinfo->pr_ttydev) {
				*ip = i + 1;
				return devl[i].dname;
			}
		}
	}
	*ip = -1;
	return (psinfo->pr_ttydev == PRNODEV? "?" : "??");
}

/*
 * Print percent from 16-bit binary fraction [0 .. 1]
 * Round up .01 to .1 to indicate some small percentage (the 0x7000 below).
 */
void
prtpct(u_short pct)
{
	u_long value = pct;	/* need 32 bits to compute with */

	value = ((value * 1000) + 0x7000) >> 15;	/* [0 .. 1000] */
	(void) printf("%3lu.%lu", value / 10, value % 10);
}

/*
 * Print info about the process.
 */
prcom(found, psinfo, psargs)
	int	found;
	struct prpsinfo *psinfo;
	char	*psargs;
{
	register char	*cp;
	register char	*tp;
	char	*psa;
	char	*ctime();
	long	tm;
	int	i, wcnt, length;
	wchar_t	wchar;
	register char	**ttyp, *str;
	char *getchan();

	/*
	 * If process is zombie, call print routine and return.
	 */
	if (psinfo->pr_zomb) {
		if (tflg && !found)
			return 0;
		else {
			przom(psinfo);
			return 1;
		}
	}

	/*
	 * Get current terminal.  If none ("?") and 'a' is set, don't print
	 * info.  If 't' is set, check if term is in list of desired terminals
	 * and print it if it is.
	 */
	i = 0;
	tp = gettty(&i, psinfo);
	if (*tp == '?' && psinfo->pr_ttydev != PRNODEV &&
	    !getdevcalled && majorexists(psinfo->pr_ttydev)) {
		getdev();
		/* getwchan(); */
		wrdata();
		i = 0;
		tp = gettty(&i, psinfo);
	}

	if (*tp == '?' && !found && !xflg )
		return 0;

	if ( !(*tp == '?' && aflg) && tflg && !found) {
		int match = 0;

		/*
		 * Look for same device under different names.
		 */
		while (i >= 0 && !match) {
			for (ttyp = tty; (str = *ttyp) != 0 && !match; ttyp++)
				if (strcmp(tp, str) == 0)
					match = 1;
			if (!match)
				tp = gettty(&i, psinfo);
		}
		if (!match)
			return 0;
	}

	if (lflg)
		printf("%2x", psinfo->pr_flag & 0377);
	if (uflg) {
		if (!nflg) {
			struct passwd *pwd;

			if ((pwd = getpwuid(psinfo->pr_euid)) != NULL)
				printf("%-8.8s", pwd->pw_name);	/* USER */
			else
				printf(" %7.7ld", psinfo->pr_euid); /* UID */
		} else {
			printf(" %5ld", psinfo->pr_euid);	/* UID */
		}
	} else if (lflg)
		printf(" %5ld", psinfo->pr_euid);  		/* UID */

	printf("%6ld", psinfo->pr_pid);				/* PID */
	if (lflg)
		printf("%6ld", psinfo->pr_ppid);		/* PPID */
	if (lflg)
		printf("%3d", psinfo->pr_cpu & 0377);		/* CP */
	if (uflg) {
		prtpct(psinfo->pr_pctcpu);			/* %CPU */
		prtpct(psinfo->pr_pctmem);			/* %MEM */
	}
	if (lflg) {
		printf("%4d", psinfo->pr_pri);			/* PRI */
		printf("%3d", psinfo->pr_nice);			/* NICE */
	}
	if (lflg || uflg) {
		printf("%5d", psinfo->pr_bysize/1024);		/* SZ */
		printf("%5d", psinfo->pr_byrssize/1024);	/* RSS */
	}
	if (lflg) {						/* WCHAN */
		if (psinfo->pr_wchan == 0)
			printf("%+9.8s", "");
		else if (nflg)
			printf("%+9.8x", (int) psinfo->pr_wchan & 0xffffffff);
		else
			printf("%+9.8s", getchan(psinfo->pr_wchan));
	}
	if ((tplen = (int) strlen(tp)) > 9)
		maxlen = twidth - tplen + 9;
	else
		maxlen = twidth;

	if (!lflg)
		printf(" %-8.14s", tp);				/* TTY */
	printf(" %c", psinfo->pr_sname);			/* STATE */
	if (lflg)
		printf(" %-8.14s", tp);				/* TTY */
	if (uflg)
		prtime(psinfo->pr_start);			/* START */

	/* time just for process */
	tm = psinfo->pr_time.tv_sec;
	if (Sflg) {	/* calculate time for process and all reaped children*/
		tm += psinfo->pr_ctime.tv_sec;
		if (psinfo->pr_time.tv_nsec + psinfo->pr_ctime.tv_nsec
		    >= 1000000000)
			tm += 1;
	}

	printf(" %2ld:%.2ld", tm / 60, tm % 60);		/* TIME */

	if (vflg) {
		printf("%5d", psinfo->pr_bysize/1024);		/* SZ */
		printf("%5d", psinfo->pr_byrssize/1024);	/* RSS */
		prtpct(psinfo->pr_pctcpu);			/* %CPU */
		prtpct(psinfo->pr_pctmem);			/* %MEM */
	}
	if (cflg) {						/* CMD */
		wcnt = namencnt(psinfo->pr_fname, 16, maxlen);
		printf(" %.*s", wcnt, psinfo->pr_fname);
		return 1;
	}
	/*
	 * PRARGSZ == length of cmd arg string.
	 */
	if (psargs == NULL) {
		psa = &psinfo->pr_psargs[0];
		i = PRARGSZ;
		tp = &psinfo->pr_psargs[PRARGSZ];
	} else {
		psa = psargs;
		i = strlen(psargs);
		tp = psa + i;
	}

	for (cp = psa; cp < tp; ) {
		if (*cp == 0)
			break;
		length = mbtowc(&wchar, cp, MB_LEN_MAX);
		if (length < 0 || !iswprint(wchar)) {
			printf(" [ %.16s ]", psinfo->pr_fname);
			return 1;
		}
		cp += length;
	}
	wcnt = namencnt(psa, i, maxlen);
#if 0
	printf(" %.*s", wcnt, psa);	/* dumps core on really long strings */
#else
	putchar(' ');
	fwrite(psa, 1, wcnt, stdout);
#endif
	return 1;
}

/*
 * Returns 1 if arg is found in array arr, of length num; 0 otherwise.
 */
int
search(arr, number, arg)
	pid_t arr[];
	register int number;
	register pid_t arg;
{
	register int i;

	for (i = 0; i < number; i++)
		if (arg == arr[i]) {
			return 1;
		}
	return 0;
}

uconv()
{
	struct passwd *pwd;
	int found, i, j;

	/*
	 * Ask NIS for oarg.
	 */
	for (i = 0; i < nut; i++) {
		/*
		 * If not found and oarg is numeric, ask for numeric id
		 */
		if ((pwd = getpwnam(uid_tbl[i].name)) == NULL
		  && uid_tbl[i].name[0] >= '0'
		  && uid_tbl[i].name[0] <= '9')
			pwd = getpwuid((uid_t)atol(uid_tbl[i].name));

		/*
		 * If found, enter found index into tbl array.
		 */
		if (pwd != NULL) {
			uid_tbl[i].uid = pwd->pw_uid;
			(void) strncpy(uid_tbl[i].name, pwd->pw_name,
			  MAXLOGIN);
		} else {
			fprintf(stderr,
			  "ps: unknown user %s\n", uid_tbl[i].name);
			for (j = i + 1; j < nut; j++) {
				(void) strncpy(uid_tbl[j-1].name,
				  uid_tbl[j].name, MAXLOGIN);
			}
			nut--;
			if (nut <= 0)
				exit(1);
			i--;
		}
	}
}

/*
 * Special read; unlinks psfile on read error.
 */
int
psread(fd, bp, bs)
	int fd;
	char *bp;
	unsigned int bs;
{
	int rbs;

	if ((rbs = read(fd, bp, bs)) != bs) {
		fprintf(stderr,
		    "ps: psread() error on read, rbs=%d, bs=%d, %s\n",
		    rbs, bs, sys_errlist[errno]);
		(void) unlink(psfile);
		return 0;
	}
	return 1;
}

/*
 * Special write; unlinks psfile on write error.
 */
pswrite(fd, bp, bs)
int	fd;
char	*bp;
unsigned	bs;
{
	int	wbs;

	if ((wbs = write(fd, bp, bs)) != bs) {
		fprintf(stderr,
		    "ps: pswrite() error on write, wbs=%d, bs=%d, %s\n",
		    wbs, bs, sys_errlist[errno]);
		(void) unlink(psfile);
	}
}

/*
 * Print starting time of process unless process started more than 24 hours
 * ago, in which case the date is printed.
 */
prtime(st)
	timestruc_t st;
{
	char sttim[26];
	static time_t tim = 0L;
	time_t starttime;

	if (tim == 0L)
		tim = time((time_t *) 0);
	starttime = st.tv_sec;
	if (tim - starttime > 24*60*60) {
		cftime(sttim, "%b %d", &starttime);
		sttim[7] = '\0';
	} else {
		cftime(sttim, "%H:%M:%S", &starttime);
		sttim[8] = '\0';
	}
	printf("%9.9s", sttim);
}

przom(psinfo)
	struct prpsinfo *psinfo;
{
	long	tm;

	if (lflg)
		printf("%2x", psinfo->pr_flag & 0377);
	if (uflg) {
		struct passwd *pwd;

		if ((pwd = getpwuid(psinfo->pr_euid)) != NULL)
			printf("%-8.8s", pwd->pw_name);		/* USER */
		else
			printf(" %7.7ld", psinfo->pr_euid);	/* UID */
	} else if (lflg)
		printf(" %5ld", psinfo->pr_euid);		/* UID */

	printf("%6ld", psinfo->pr_pid);				/* PID */
	if (lflg)
		printf("%6ld", psinfo->pr_ppid);		/* PPID */
	if (lflg)
		printf("  0");					/* CP */
	if (uflg) {
		prtpct(0);					/* %CPU */
		prtpct(0);					/* %MEM */
	}
	if (lflg) {
			printf("%4d", psinfo->pr_pri);		/* PRI */
			printf("   ");				/* NICE */
	}
	if (lflg || uflg) {
			printf("    0");			/* SZ */
			printf("    0");			/* RSS */
	}
	if (lflg)
			printf("         ");			/* WCHAN */
	printf("          ");					/* TTY */
	printf("%c", psinfo->pr_sname);				/* STATE */
	if (uflg)
		printf("         ");				/* START */

	/* time just for process */
	tm = psinfo->pr_time.tv_sec;
	if (Sflg) {     /* calculate time for process and all reaped children*/
		tm += psinfo->pr_ctime.tv_sec;
		if (psinfo->pr_time.tv_nsec + psinfo->pr_ctime.tv_nsec
		    >= 1000000000)
			tm += 1;
	}
	printf(" %2ld:%.2ld", tm / 60, tm % 60);		/* TIME */

	if (vflg) {
		printf("    0");				/* SZ */
		printf("    0");				/* RSS */
		prtpct(0);					/* %CPU */
		prtpct(0);					/* %MEM */
	}
	printf(" %.*s", maxlen, " <defunct>");
}

/*
 * Returns true iff string is all numeric.
 */
int
num(s)
	register char	*s;
{
	register int c;

	if (s == NULL)
		return 0;
	c = *s;
	do {
		if (!isdigit(c))
			return 0;
	} while (c = *++s);
	return 1;
}

/*
 * Function to compute the number of printable bytes in a multibyte
 * command string ("internationalization").
 */
int
namencnt(cmd, eucsize, scrsize)
	register char *cmd;
	int eucsize;
	int scrsize;
{
	register int eucwcnt = 0, scrwcnt = 0;
	register int neucsz, nscrsz;
	wchar_t  wchar;

	while (*cmd != '\0') {
		if ((neucsz = mbtowc(&wchar, cmd, MB_LEN_MAX)) < 0)
			return 8; /* default to use for illegal chars */
		if ((nscrsz = scrwidth(wchar)) == 0)
			return 8;
		if (eucwcnt + neucsz > eucsize || scrwcnt + nscrsz > scrsize)
			break;
		eucwcnt += neucsz;
		scrwcnt += nscrsz;
		cmd += neucsz;
	}
	return eucwcnt;
}

char sym_source[]="/dev/ksyms";
		/* File from which the wait channels are built */

getwchan()
{
	struct stat	buf;
	int             fd, count, found;
	int             tmp, i;
	Elf            *elf_file;
	Elf32_Ehdr     *elf_head_p;
	Elf_Cmd         cmd;
	Elf_Kind        file_type;
	Elf32_Shdr     *p_shdr;
	Elf32_Sym      *sym_data;
	Elf_Scn        *scn;
	size_t          sym_size;
	void           *get_scndata();
	int             wchancomp();

	if (stat(sym_source, &buf) == -1) {
		fprintf(stderr, "ps: ");
		perror(sym_source);
		exit(1);
	}
	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "ps: Libelf is out of date\n");
		return 0;
	}
	if ((fd = open((sym_source), O_RDONLY)) == -1) {
		fprintf(stderr, "ps: Cannot read %s\n", sym_source);
		return 0;
	}
	cmd = ELF_C_READ;
	if ((elf_file = elf_begin(fd, cmd, (Elf *) 0)) == NULL) {
		fprintf(stderr, "ps: %s: %s\n", sym_source, elf_errmsg(-1));
		return 0;
	}
	file_type = elf_kind(elf_file);
	if (file_type != ELF_K_ELF) {
		fprintf(stderr, "ps: %s: invalid file type\n", sym_source);
		return 0;
	}
	if ((elf_head_p = elf32_getehdr(elf_file)) == NULL) {
		fprintf(stderr, "ps: %s: %s\n", sym_source, elf_errmsg(-1));
		return 0;
	}
	scn = 0;

	found = 0;
	while ((scn = elf_nextscn(elf_file, scn)) != 0) {	/* find symbol table */
		if ((p_shdr = elf32_getshdr(scn)) == 0) {
			fprintf(stderr, "ps: %s: %s\n", sym_source, elf_errmsg(-1));
			return 0;
		}
		if (p_shdr->sh_type == SHT_SYMTAB) {
			found = 1;
			break;
		}
	}
	if (!found) {
		fprintf(stderr, "ps: %s: could not get symbol table\n", sym_source);
		return 0;
	}
	/* get symbol table data */
	sym_data = NULL;
	sym_size = 0;
	if ((sym_data = (Elf32_Sym *) get_scndata(scn, &sym_size)) == NULL) {
		fprintf(stderr, "ps: %s: No symbol table data\n", sym_source);
		return 0;
	}
	count = (sym_size / sizeof(Elf32_Sym)) - 1;
	sym_data++;		/* first member holds the number of symbols */

	/* fill wchan info */
	while (count > 0) {

		char           *sym_name = (char *) 0;
		Elf32_Addr      sym_addr;

		sym_addr = sym_data->st_value;
		sym_name = (char *) elf_strptr(elf_file, p_shdr->sh_link, sym_data->st_name);
		if (sym_addr != (Elf32_Addr) 0
		    && sym_name != NULL
		    && *sym_name != '\0')
			addchan(sym_name, sym_addr);
		sym_data++;
		count--;
	}

	qsort(wchanhd, nchans, sizeof(struct wchan), wchancomp);
	for (i = 0; i < NWCINDEX; i++) {	/* speed up searches */
		tmp = i * nchans;
		wchan_index[i] = wchanhd[tmp / NWCINDEX].wc_addr;
	}
	return 1;
}

/*
 * Get the section descriptor and set the size of the data returned. Data is
 * byte-order converted.
 */
void *
get_scndata(fd_scn, size)
	Elf_Scn        *fd_scn;
	size_t         *size;
{
	Elf_Data       *p_data;

	p_data = 0;
	if ((p_data = elf_getdata(fd_scn, p_data)) == 0 || p_data->d_size == 0)
		return NULL;

	*size = p_data->d_size;
	return (p_data->d_buf);
}

/*
 * Add the given channel to the channel list.
 */
addchan(name, addr)
	char		*name;
	Elf32_Addr      addr;
{
	static int      left = 0;
	register struct wchan *wp;
	register char **p;
	register struct wchan_map *mp;

	/* wchan mappings */
	for (mp = wchan_map_list; mp->map_from; mp++) {
		if (*(mp->map_from) != *name)	/* quick check */
			continue;
		if (strncmp(name, mp->map_from, WNAMESIZ) == 0)
			name = mp->map_to;
	}

	if (left == 0) {	/* no space left - reallocate old or allocate
				 * new */
		if (wchanhd) {
			left = 100;
			wchanhd = (struct wchan *) realloc(wchanhd, (nchans + left) * sizeof(struct wchan));
		} else {
			left = 600;
			wchanhd = (struct wchan *) malloc(left * sizeof(struct wchan));
		}
		if (wchanhd == NULL) {
			fprintf(stderr, "ps: out of memory allocating wait channels\n");
			nflg++;
			return;
		}
	}
	left--;
	wp = &wchanhd[nchans++];
	strncpy(wp->wc_name, name, WNAMESIZ);
	wp->wc_name[WNAMESIZ] = '\0';
	wp->wc_addr = addr;
}

/*
 * Returns the symbolic wait channel corresponding to chan
 */
char *
getchan(chan)
	register Elf32_Addr chan;
{
	register	i, iend;
	register char  *prevsym;
	register struct wchan *wp;

	prevsym = "???";	/* nothing to begin with */
	if (chan) {
		for (i = 0; i < NWCINDEX; i++)
			if ((unsigned) chan < (unsigned) wchan_index[i])
				break;
		iend = i--;

		if (i < 0)	/* can't be found */
			return prevsym;
		iend *= nchans;
		iend /= NWCINDEX;
		i *= nchans;
		i /= NWCINDEX;
		wp = &wchanhd[i];
		for (; i < iend; i++, wp++) {
			if ((unsigned) wp->wc_addr > (unsigned) chan)
				break;
			prevsym = wp->wc_name;
		}
	}
	/*
	 * Many values are getting mapped to "_end", which
	 * doesn't make sense.  When we use kvm_nlist, we will
	 * probably get better information.
	 */
	if (strcmp(prevsym, "_end") == 0) {		/* XXX */
		prevsym = "???";
	}
	return prevsym;
}

/*
 * used in sorting the wait channel array
 */
int
wchancomp(w1, w2)
	struct wchan   *w1, *w2;
{
	register unsigned c1, c2;

	c1 = (unsigned) w1->wc_addr;
	c2 = (unsigned) w2->wc_addr;
	if (c1 > c2)
		return 1;
	else if (c1 == c2)
		return 0;
	else
		return -1;
}

int
pscompare(const void *v1, const void *v2)
{
	const struct psent *p1 = v1;
	const struct psent *p2 = v2;
	register int i;

	if (uflg)
		i = p2->psinfo->pr_pctcpu - p1->psinfo->pr_pctcpu;
	else if (vflg)
		i = p2->psinfo->pr_byrssize - p1->psinfo->pr_byrssize;
	else
		i = p1->psinfo->pr_ttydev - p2->psinfo->pr_ttydev;
	if (i == 0)
		i = p1->psinfo->pr_pid - p2->psinfo->pr_pid;
	return (i);
}
