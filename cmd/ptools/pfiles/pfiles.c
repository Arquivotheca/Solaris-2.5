#ident	"@(#)pfiles.c	1.2	95/06/26 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/mkdev.h>

#include "pcontrol.h"
#include "ramdata.h"

#if u3b2
#undef	_MAP_NEW
#define	_MAP_NEW	0
#endif

extern int getopt();
extern char * optarg;
extern int optind;

static	int	grabit(process_t *, pid_t);

static	void	intr();
static	pid_t	getproc();
static	int	issignalled();
static	void	dofcntl(process_t *, int, int);
static	void	show_files(process_t *);
static	void	show_fileflags(int);

main(argc, argv)
int argc;
char **argv;
{
	int retc = 0;
	int sys;
	int sig;
	int flt;
	int opt;
	int fd;
	caddr_t ptr1;
	caddr_t ptr2;
	char * (*cwd)();
	long ill = -12;
	int errflg = FALSE;
	register process_t *Pr = &Proc;

	if ((command = strrchr(argv[0], '/')) != NULL)
		command++;
	else
		command = argv[0];

#if 1
	/* allow all accesses for setuid version */
	(void) setuid((int)geteuid());
#endif

	/* options */
	while ((opt = getopt(argc, argv, "P:Fq")) != EOF) {
		switch (opt) {
		case 'P':		/* alternate /proc directory */
			procdir = optarg;
			break;
		case 'F':		/* force grabbing (no O_EXCL) */
			Fflag = TRUE;
			break;
		case 'q':		/* let QUIT give a core dump */
			qflag = TRUE;
			break;
		default:
			errflg = TRUE;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (errflg || argc <= 0) {
		(void) fprintf(stderr, "usage:\t%s pid ...\n",
			command);
		exit(2);
	}

	if (!isprocdir(procdir)) {
		(void) fprintf(stderr, "%s: %s is not a PROC directory\n",
			command, procdir);
		exit(2);
	}

	/* catch signals from terminal */
	if (sigset(SIGHUP, SIG_IGN) == SIG_DFL)
		(void) sigset(SIGHUP, intr);
	if (sigset(SIGINT, SIG_IGN) == SIG_DFL)
		(void) sigset(SIGINT, intr);
	if (sigset(SIGQUIT, SIG_IGN) == SIG_DFL)
		(void) sigset(SIGQUIT, intr);
	(void) sigset(SIGALRM, intr);	/* always catch these */
	(void) sigset(SIGTERM, intr);
	if (qflag)		/* ensure death on SIGQUIT */
		(void) sigset(SIGQUIT, SIG_DFL);

	while (--argc >= 0 && !interrupt) {
		int reissue = 0;
		prgregset_t savedreg;
		int reg;
		sigset_t holdmask;
		int ap, sp;
		pid_t pid;
		char *pdir;
		int gret;

		(void) fflush(stdout);	/* line-at-a-time */

		/* get the specified pid and its /proc directory */
		pid = getproc(*argv++, &pdir);

		gret = 0;
		if (pid < 0 || (gret = grabit(Pr, pid)) != 0) {
			switch (gret) {
			case 1:		/* system process */
				(void) printf("%d:\t%.70s\n", pid,
					Pr->psinfo.pr_psargs);
				(void) printf("  [system process]\n");
				break;
			case 2:		/* attempt to grab self */
				(void) printf("%d:\t%.70s\n", pid,
					Pr->psinfo.pr_psargs);
				show_files((process_t *)NULL);
				break;
			default:
				retc++;
				break;
			}
			continue;
		}

		if (scantext(Pr) != 0) {
			(void) fprintf(stderr, "%s: cannot find text in %d\n",
				command, pid);
			(void) Prelease(Pr);
			retc++;
			continue;
		}

		for (sys = 1; sys <= PRMAXSYS; sys++) {	/* trace syscall exit */
			(void) Psysentry(Pr, sys, FALSE);
			(void) Psysexit(Pr, sys, TRUE);
		}
		for (sig = 1; sig <= PRMAXSIG; sig++)	/* trace no signals */
			(void) Psignal(Pr, sig, FALSE);
		for (flt = 1; flt <= PRMAXFAULT; flt++)	/* trace no faults */
			(void) Pfault(Pr, flt, FALSE);

		/* avoid waiting forever */
		(void) alarm(3);
		while ((Pr->why.pr_why != PR_REQUESTED
		     && Pr->why.pr_why != PR_SYSEXIT)
		  || issignalled(Pr)) {
			if (interrupt
			 || Pstart(Pr, 0) != 0
			 || (msleep(20), Pstop(Pr)) != 0
			 || Pr->state != PS_STOP) {
				(void) alarm(0);
				timeout = FALSE;
				(void) fprintf(stderr,
					"%s: cannot control process %d\n",
					command, pid);
				goto out;
			}
		}
		(void) alarm(0);
		timeout = FALSE;

		/* choose one lwp for further operations */
		if (Pchoose(Pr) != 0) {
			(void) fprintf(stderr,
				"%s: cannot find an available lwp in %ld\n",
				command, pid);
			retc++;
			goto out;
		}

		reissue = 0;
		if (Pr->why.pr_why == PR_REQUESTED
		 && (Pr->why.pr_flags & PR_ASLEEP)) {	/* sleeping syscall */
			if ((reissue = Pgetsysnum(Pr)) <= 0)
				goto out;

			/* remember the registers before aborting the syscall */
			(void) Pgetregs(Pr);
			for (reg = 0; reg < NGREG; reg++)
				savedreg[reg] = Pr->REG[reg];

			/* move the process to SYSEXIT */
			(void) Psetrun(Pr, 0, PRSABORT);
			(void) Pwait(Pr);

			if (Pr->state != PS_STOP
			 || Pr->why.pr_why != PR_SYSEXIT) {
				(void) fprintf(stderr,
				"expected to be stopped on PR_SYSEXIT\n");
				goto out;
			}
		}
#if 0 /* debug */
		else if (Pr->why.pr_why == PR_REQUESTED) {
			(void) fprintf(stderr, "PR_REQUESTED, not PR_ASLEEP\n");
		}
#endif

		holdmask = Pr->why.pr_sighold;	/* remember old hold mask */
		prfillset(&Pr->why.pr_sighold);	/* hold all signals */
		Pr->sethold = TRUE;

		(void) Pgetregs(Pr);

		if ((sig = Pr->why.pr_cursig) != 0) {
			(void) fprintf(stderr,
				"unexpected cursig: %d\n", sig);
			(void) Ioctl(Pr->lwpfd, PIOCKILL, (int)&sig);
			(void) Ioctl(Pr->lwpfd, PIOCSSIG, 0);
			Pr->why.pr_cursig = 0;
		}

		if (!reissue) {
			/* remember the registers */
			for (reg = 0; reg < NGREG; reg++)
				savedreg[reg] = Pr->REG[reg];
		}

		for (sys = 1; sys <= PRMAXSYS; sys++)	/* no tracing */
			(void) Psysexit(Pr, sys, FALSE);

/* ------------- Insert code to be executed here ------------- */

		(void) printf("%d:\t%.70s\n", pid, Pr->psinfo.pr_psargs);
		show_files(Pr);

/* ------------- End of code to be executed here ------------- */

		/* restore the registers */
		for (reg = 0; reg < NGREG; reg++)
			Pr->REG[reg] = savedreg[reg];
		(void) Pputregs(Pr);

		/* get back to the sleeping syscall */
		if (reissue) {
#if u3b2 || i386
			Pr->REG[R_PC] -= sizeof(syscall_t);
			(void) Pputareg(Pr, R_PC);
			(void) Psetsysnum(Pr, reissue);
#endif
			(void) Psysentry(Pr, reissue, TRUE);
			if (Pstart(Pr, 0) != 0
			 || Pwait(Pr) != 0
			 || Pr->why.pr_why != PR_SYSENTRY
			 || Pr->why.pr_what != reissue)
				(void) fprintf(stderr,
					"cannot reissue sys call\n");
			(void) Psysentry(Pr, reissue, FALSE);

			/* restore the registers again */
			for (reg = 0; reg < NGREG; reg++)
				Pr->REG[reg] = savedreg[reg];
			(void) Pputregs(Pr);
		}

		Pr->why.pr_sighold = holdmask;	/* unblock pending signals */
		Pr->sethold = TRUE;

out:
		(void) Prelease(Pr);
	}

	if (interrupt)
		retc++;
	return retc;
}

static int
issignalled(Pr)
register process_t *Pr;
{
	return Pr->why.pr_cursig;
}

static pid_t		/* get process id and /proc directory */
getproc(path, pdirp)	/* return pid on success, -1 on failure */
register char * path;		/* number or /proc/nnn */
char ** pdirp;			/* points to /proc directory on success */
{
	register char * name;
	register pid_t pid;
	char *next;

	if ((name = strrchr(path, '/')) != NULL)	/* last component */
		*name++ = '\0';
	else {
		name = path;
		path = procdir;
	}

	if (strcmp(name, "-") == 0) {
		pid = getpid();
		next = name+1;
	} else {
		pid = strtol(name, &next, 10);
	}
	if ((isdigit(*name) || *name == '-') && pid >= 0 && *next == '\0') {
		if (strcmp(procdir, path) != 0
		 && !isprocdir(path)) {
			(void) fprintf(stderr,
				"%s: %s is not a PROC directory\n",
				command, path);
			pid = -1;
		}
	} else {
		(void) fprintf(stderr, "%s: invalid process id: %s\n",
			command, name);
		pid = -1;
	}

	if (pid >= 0)
		*pdirp = path;
	return pid;
}

static int
grabit(Pr, pid)		/* take control of an existing process */
register process_t *Pr;
pid_t pid;
{
	int gcode;

	/* avoid waiting forever */
	(void) alarm(2);

	/* don't force the takeover unless the -F option was specified */
	gcode = Pgrab(Pr, pid, Fflag);

	(void) alarm(0);
	timeout = FALSE;

	/* don't force the takeover unless the -F option was specified */
	switch (gcode) {
	case 0:
		break;
	case G_BUSY:
		(void) fprintf(stderr,
			"%s: someone else is tracing process %d\n",
			command, pid);
		return -1;
	case G_SYS:		/* system process */
		return 1;
	case G_SELF:		/* attempt to grab self */
		return 2;
	default:
		goto bad;
	}

	if (Pr->state == PS_STOP
	 && Pr->why.pr_why == PR_JOBCONTROL) {
		int sig = SIGCONT;

		Pr->jsig = Pr->why.pr_what;
		if (Ioctl(Pr->pfd, PIOCKILL, (int)&sig) == -1) {
			perror("grabit(): PIOCKILL");
			(void) Prelease(Pr);
			goto bad;
		}
		Pr->state = PS_RUN;
		(void) Pwait(Pr);
	}

	if (Pr->state != PS_STOP
	 || Pr->why.pr_why != PR_REQUESTED) {
#if 1
		(void) fprintf(stderr,
			"%s: expected REQUESTED stop, pid# %d (%d,%d)\n",
			command, pid, Pr->why.pr_why, Pr->why.pr_what);
#endif
		if (Pr->state != PS_STOP) {
			(void) Prelease(Pr);
			goto bad;
		}
	}

	return 0;

bad:
	if (gcode == G_NOPROC || gcode == G_ZOMB || errno == ENOENT)
		(void) fprintf(stderr, "%s: no such process: %d\n",
			command, pid);
	else
		(void) fprintf(stderr, "%s: cannot control process: %d\n",
			command, pid);
	return -1;
}

static void
intr(sig)
register int sig;
{
	if (sig == SIGALRM) {		/* reset alarm clock */
		timeout = TRUE;
		(void) alarm(1);
	} else {
		interrupt = TRUE;
	}
}

/* ------ begin specific code ------ */

static void
show_files(process_t *Pr)
{
	struct stat statb;
	struct rlimit rlim;
	register int fd;
	register int nfd = 64;
	char * s;

	if (prgetrlimit(Pr, RLIMIT_NOFILE, &rlim) == 0)
		nfd = rlim.rlim_cur;
	(void) printf("  Current rlimit: %d file descriptors\n", nfd);

	for (fd = 0; fd < nfd; fd++) {
		char unknown[12];
		dev_t rdev;

		if (prfstat(Pr, fd, &statb) == -1)
			continue;
		rdev = (dev_t)(-1);
		switch (statb.st_mode & S_IFMT) {
		case S_IFDIR: s="S_IFDIR"; break;
		case S_IFCHR: s="S_IFCHR"; rdev = statb.st_rdev; break;
		case S_IFBLK: s="S_IFBLK"; rdev = statb.st_rdev; break;
		case S_IFREG: s="S_IFREG"; break;
		case S_IFIFO: s="S_IFIFO"; break;
		default:
			s = unknown;
			(void) sprintf(s, "0x%.4lx ", statb.st_mode & S_IFMT);
			break;
		}
		printf("%4d: %s mode:0%.3o",
			fd,
			s,
			statb.st_mode & ~S_IFMT);
		if (major(statb.st_dev) != -1 && minor(statb.st_dev) != -1)
			printf(" dev:%d,%d",
				major(statb.st_dev),
				minor(statb.st_dev));
		else
			printf(" dev:0x%.8X", statb.st_dev);
		printf(" ino:%d uid:%d gid:%d",
			statb.st_ino,
			statb.st_uid,
			statb.st_gid);
		if (rdev == (dev_t)(-1))
			printf(" size:%ld\n", statb.st_size);
		else if (major(rdev) != -1 && minor(rdev) != -1)
			printf(" rdev:%d,%d\n", major(rdev), minor(rdev));
		else
			printf(" rdev:0x%.8X\n", rdev);

		dofcntl(Pr, fd,
			(statb.st_mode & (S_IFMT|S_ENFMT|S_IXGRP))
			== (S_IFREG|S_ENFMT));
	}
}

static void
dofcntl(Pr, fd, manditory)		/* examine open file with fcntl() */
process_t *Pr;
register int fd;
int manditory;
{
	struct flock flock;
	register int fileflags;
	register int fdflags;

	fileflags = prfcntl(Pr, fd, F_GETFL, 0);
	fdflags = prfcntl(Pr, fd, F_GETFD, 0);

	if (fileflags != -1 || fdflags != -1) {
		printf("      ");
		if (fileflags != -1)
			show_fileflags(fileflags);
		if (fdflags != -1 && (fdflags & FD_CLOEXEC))
			printf(" close-on-exec");
		fputc('\n', stdout);
	}

	flock.l_type = F_WRLCK;
	flock.l_whence = 0;
	flock.l_start = 0;
	flock.l_len = 0;
	flock.l_sysid = 0;
	flock.l_pid = 0;
	if (prfcntl(Pr, fd, F_GETLK, &flock) != -1) {
		if (flock.l_type != F_UNLCK && (flock.l_sysid || flock.l_pid)) {
			unsigned long sysid = flock.l_sysid;

			printf("      %s %s lock set by",
				manditory? "manditory" : "advisory",
				flock.l_type == F_RDLCK? "read" : "write");
			if (sysid & 0xff000000)
				printf(" system %d.%d.%d.%d",
					(sysid>>24)&0xff, (sysid>>16)&0xff,
					(sysid>>8)&0xff, (sysid)&0xff);
			else if (sysid)
				printf(" system 0x%X", sysid);
			if (flock.l_pid)
				printf(" process %d", flock.l_pid);
			fputc('\n', stdout);
		}
	}
}

#ifdef O_PRIV
#define ALL_O_FLAGS	O_ACCMODE | O_NDELAY | O_NONBLOCK | O_APPEND | \
			O_PRIV | O_SYNC | O_DSYNC | O_RSYNC | \
			O_CREAT | O_TRUNC | O_EXCL | O_NOCTTY
#else
#define ALL_O_FLAGS	O_ACCMODE | O_NDELAY | O_NONBLOCK | O_APPEND | \
			O_SYNC | O_DSYNC | O_RSYNC | \
			O_CREAT | O_TRUNC | O_EXCL | O_NOCTTY
#endif
static void
show_fileflags(flags)
register int flags;
{
	char buffer[128];
	register char * str = buffer;

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		(void) strcpy(str, "O_RDONLY");
		break;
	case O_WRONLY:
		(void) strcpy(str, "O_WRONLY");
		break;
	case O_RDWR:
		(void) strcpy(str, "O_RDWR");
		break;
	default:
		(void) sprintf(str, "0x%x", flags & O_ACCMODE);
		break;
	}

	if (flags & O_NDELAY)
		(void) strcat(str, "|O_NDELAY");
	if (flags & O_NONBLOCK)
		(void) strcat(str, "|O_NONBLOCK");
	if (flags & O_APPEND)
		(void) strcat(str, "|O_APPEND");
#ifdef O_PRIV
	if (flags & O_PRIV)
		(void) strcat(str, "|O_PRIV");
#endif
	if (flags & O_SYNC)
		(void) strcat(str, "|O_SYNC");
	if (flags & O_DSYNC)
		(void) strcat(str, "|O_DSYNC");
	if (flags & O_RSYNC)
		(void) strcat(str, "|O_RSYNC");
	if (flags & O_CREAT)
		(void) strcat(str, "|O_CREAT");
	if (flags & O_TRUNC)
		(void) strcat(str, "|O_TRUNC");
	if (flags & O_EXCL)
		(void) strcat(str, "|O_EXCL");
	if (flags & O_NOCTTY)
		(void) strcat(str, "|O_NOCTTY");
	if (flags & ~(ALL_O_FLAGS))
		(void) sprintf(str + strlen(str), "|0x%x",
			flags & ~(ALL_O_FLAGS));

	(void) printf("%s", str);
}
