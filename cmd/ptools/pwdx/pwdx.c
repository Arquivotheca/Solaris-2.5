#ident	"@(#)pwdx.c	1.2	95/06/26 SMI"

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
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <elf.h>

#include "pcontrol.h"
#include "ramdata.h"

#if u3b2
#undef	_MAP_NEW
#define	_MAP_NEW	0
#endif

#define	LIBCWD	"/usr/proc/lib/libcwd.so.1"

extern int getopt();
extern char * optarg;
extern int optind;

static void intr(int);
static void errmsg(CONST char *, CONST char *);
void abend(CONST char *, CONST char *);

static	int	cwd_self(void);
static	int	grabit(process_t *, pid_t);
char *	fetchstring();
static	pid_t	getproc();
static	int	issignalled();

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
	Elf32_Ehdr Ehdr;
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
		(void) fprintf(stderr, "usage:\t%s pid ...\n", command);
		(void) fprintf(stderr, "  (show process working directory)\n");
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

		PR = NULL;	/* for abend() */

		(void) fflush(stdout);	/* line-at-a-time */

		/* get the specified pid and its /proc directory */
		pid = getproc(*argv++, &pdir);

		gret = 0;
		if (pid < 0 || (gret = grabit(Pr, pid)) != 0) {
			switch (gret) {
			case 1:		/* system process */
				(void) printf("%d:\t/\t[system process]\n",
					pid);
				break;
			case 2:		/* attempt to grab self */
				if (cwd_self() != 0)
					retc++;
				break;
			default:
				retc++;
				break;
			}
			continue;
		}

		PR = Pr;	/* for abend() */

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
				retc++;
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

#if u3b2
		ap = Pr->REG[R_AP];
		sp = Pr->REG[R_SP];
		Pr->REG[R_SP] += 3*sizeof(int);
		(void) Pputareg(Pr, R_SP);
#elif i386
		sp = Pr->REG[R_SP];
		Pr->REG[R_SP] -= 2*sizeof(int);
		(void) Pputareg(Pr, R_SP);
#endif

		for (sys = 1; sys <= PRMAXSYS; sys++)	/* no tracing */
			(void) Psysexit(Pr, sys, FALSE);

		fd = propen(Pr, LIBCWD, O_RDONLY, 0);
		ptr1 = prmmap(Pr, (caddr_t)0, 4096, PROT_READ|PROT_EXEC,
			MAP_PRIVATE, fd, 0);
		(void) prclose(Pr, fd);

		fd = propen(Pr, "/dev/zero", O_RDONLY, 0);
		ptr2 = prmmap(Pr, (caddr_t)0, 8192, PROT_READ|PROT_WRITE,
			MAP_PRIVATE, fd, 0);
		(void) prclose(Pr, fd);

		/* get the ELF header for the entry point */
		(void) Pread(Pr, (long)ptr1, (char *)&Ehdr, sizeof(Ehdr));
		/* should do some checking here */
		cwd = (char * (*)())(ptr1 + (long)Ehdr.e_entry);

		/* arrange to call (*cwd)() */
#if u3b2
		(void) Pwrite(Pr, (long)(sp), (char *)&ptr2, 4);
		(void) Pwrite(Pr, (long)(sp+4), (char *)&ill, 4);
		(void) Pwrite(Pr, (long)(sp+8), (char *)&ap, 4);
		Pr->REG[R_PC] = (int)cwd;
		Pr->REG[R_AP] = sp;
#elif i386
		(void) Pwrite(Pr, (long)(sp-4), (char *)&ptr2, 4);
		(void) Pwrite(Pr, (long)(sp-8), (char *)&ill, 4);
		Pr->REG[R_PC] = (int)cwd;
#elif sparc
		Pr->REG[R_O0] = (int)ptr2;
		Pr->REG[R_O7] = (int)ill;
		Pr->REG[R_PC] = (int)cwd;
		Pr->REG[R_nPC] = (int)cwd + 4;
#else
		"unknown architecture"
#endif
		(void) Pputregs(Pr);
		(void) Pfault(Pr, FLTBOUNDS, TRUE);
		(void) Pfault(Pr, FLTILL, TRUE);

		if (Pstart(Pr, 0) == 0
		 && Pwait(Pr) == 0
		 && Pr->state == PS_STOP
		 && Pr->why.pr_why == PR_FAULTED) {
			char * dir;
			char * addr;
			if (Pr->why.pr_what != FLTBOUNDS)
				(void) printf("%d:\texpected FLTBOUNDS\n",
					Pr->upid);
			addr = (char *)Pr->REG[R_R0];
			(void) Ioctl(Pr->lwpfd, PIOCCFAULT, 0);
			if (addr == (char *)NULL
			 || (dir = fetchstring((long)addr)) == (char *)NULL)
				(void) printf("%d:\t???\n", Pr->upid);
			else
				(void) printf("%d:\t%s\n", Pr->upid, dir);
		}

		(void) Pfault(Pr, FLTILL, FALSE);
		(void) Pfault(Pr, FLTBOUNDS, FALSE);
		(void) prmunmap(Pr, ptr1, 4096);
		(void) prmunmap(Pr, ptr2, 8192);

		/* restore the registers */
		for (reg = 0; reg < NGREG; reg++)
			Pr->REG[reg] = savedreg[reg];
		(void) Pputregs(Pr);

		/* get back to the sleeping syscall */
		if (reissue) {
#if u3b2
			Pr->REG[R_PC] -= 2;
			(void) Pputareg(Pr, R_PC);
			(void) Psetsysnum(Pr, reissue);
#elif i386
			Pr->REG[R_PC] -= 7;
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
cwd_self()
{
	int fd;
	char * ptr1;
	char * ptr2;
	char * (*cwd)();
	int rv = 0;

	fd = open(LIBCWD, O_RDONLY, 0);
	ptr1 = mmap((caddr_t)0, 4096, PROT_READ|PROT_EXEC,
		MAP_PRIVATE, fd, 0);
	(void) close(fd);
	if (ptr1 == NULL) {
		perror(LIBCWD);
		rv = -1;
	}

	fd = open("/dev/zero", O_RDONLY, 0);
	ptr2 = mmap((caddr_t)0, 8192, PROT_READ|PROT_WRITE,
		MAP_PRIVATE, fd, 0);
	(void) close(fd);
	if (ptr2 == NULL) {
		perror("/dev/zero");
		rv = -1;
	}

	cwd = (char * (*)())(ptr1 + (long)((Elf32_Ehdr *)ptr1)->e_entry);

	/* arrange to call (*cwd)() */
	if (ptr1 != NULL && ptr2 != NULL) {
		char * dir = (*cwd)(ptr2);
		if (dir == (char *)NULL)
			(void) printf("%d:\t???\n", getpid());
		else
			(void) printf("%d:\t%s\n", getpid(), dir);
	}

	(void) munmap(ptr1, 4096);
	(void) munmap(ptr2, 8192);

	return rv;
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

	pid = strtol(name, &next, 10);
	if (isdigit(*name) && pid >= 0 && *next == '\0') {
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

char *
fetchstring(addr)
register long addr;
{
	register process_t *Pr = &Proc;
	register int nbyte;
	register leng = 0;
	char string[41];
	string[40] = '\0';

	if (str_bsize == 0) {	/* initial allocation of string buffer */
		str_buffer = malloc(str_bsize = 16);
		if (str_buffer == NULL)
			abend("cannot allocate string buffer", 0);
	}
	*str_buffer = '\0';

	for (nbyte = 40; nbyte == 40 && leng < 400; addr += 40) {
		if ((nbyte = Pread(Pr, addr, string, 40)) < 0)
			return leng? str_buffer : NULL;
		if (nbyte > 0
		 && (nbyte = strlen(string)) > 0) {
			while (leng+nbyte >= str_bsize) {
				str_buffer = realloc(str_buffer, str_bsize *= 2);
				if (str_buffer == NULL)
					abend("cannot reallocate string buffer", 0);
			}
			(void) strcpy(str_buffer+leng, string);
			leng += nbyte;
		}
	}

	return str_buffer;
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
		(void) fprintf(stderr, "%s: no process %d\n",
			command, pid);
	else
		(void) fprintf(stderr, "%s: cannot control process %d\n",
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

static void
errmsg(s,q)
register CONST char *s;
register CONST char *q;
{
	char msg[200];

	msg[0] = '\0';
	if (command) {
		(void) strcpy(msg, command);
		(void) strcat(msg, ": ");
	}
	if (s) (void) strcat(msg, s);
	if (q) (void) strcat(msg, q);
	(void) strcat(msg, "\n");
	(void) write(2, msg, (unsigned)strlen(msg));
}

void
abend(s,q)
register CONST char *s;
register CONST char *q;
{
	if (s || q)
		errmsg(s, q);
	if (PR)
		(void) Prelease(PR);
	exit(2);
}
