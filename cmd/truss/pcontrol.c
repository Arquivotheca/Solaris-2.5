/*
 * Copyright (c) 1992 Sun Microsystems, Inc.  All Rights Reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)pcontrol.c	1.17	94/01/17 SMI"	/* SVr4.0 1.3	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include "pcontrol.h"
#include "ramdata.h"
#include "proto.h"

/* Process Management */

/* This module is carefully coded to contain only read-only data */
/* All read/write data is defined in ramdata.c (see also ramdata.h) */


/*
 * Function prototypes for static routines in this module.
 */
#if	defined(__STDC__)

static	void	prdump( process_t * );
static	void	deadcheck( process_t * );
static	int	execute( process_t * , int );
static	int	checksyscall( process_t * );

#else	/* defined(__STDC__) */

static	void	prdump();
static	void	deadcheck();
static	int	execute();
static	int	checksyscall();

#endif	/* defined(__STDC__) */


#if i386
CONST char * CONST regname[NREGS] = {
	"gs", "fs", "es", "ds", "edi", "esi", "ebp", "esp", "ebx",
	"edx", "ecx", "eax", "trapno", "err", "eip", "cs", "efl",
	"uesp", "ss"
};
#endif /* i386 */

#if u3b2 || u3b15
CONST char * CONST regname[NREGS] = {
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",
	"fp", "ap", "ps", "sp", "pcbp", "isp", "pc"
};
#endif

#if u3b
CONST char * CONST regname[NREGS] = {
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",
	"ap", "fp", "sp", "r12", "r13", "r14", "epsw", "pc", "ps"
};
#endif

#if mc68k
CONST char * CONST regname[NREGS] = {
	"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	"a0", "a1", "a2", "a3", "a4", "a5", "fp", "sp",
	"pc", "ps"
};
#endif

#if sparc
CONST char * CONST regname[NREGS] = {
	"g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
	"o0", "o1", "o2", "o3", "o4", "o5", "o6", "o7",
	"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
	"i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7",
	"psr", "pc", "npc", "y", 
};
#endif

int
Pcreate(P, args)	/* create new controlled process */
register process_t *P;	/* program table entry */
char **args;		/* argument array, including the command name */
{
	register int i;
	register int upid;
	register int fd;
	char procname[100];

	upid = fork();

	if (upid == 0) {		/* child process */
		(void) pause();		/* wait for PRSABORT from parent */

		/* if running setuid or setgid, reset credentials to normal */
		if ((i = getgid()) != getegid())
			(void) setgid(i);
		if ((i = getuid()) != geteuid())
			(void) setuid(i);

		(void) execvp(*args, args);	/* execute the command */
		_exit(127);
	}

	if (upid == -1) {		/* failure */
		perror("Pcreate fork()");
		return -1;
	}

	/* initialize the process structure */
	(void) memset((char *)P, 0, sizeof(*P));

	P->cntrl = TRUE;
	P->child = TRUE;
	P->state = PS_RUN;
	P->upid  = upid;

	/* open the /proc/upid file */
	(void) sprintf(procname, "%s/%d", procdir, upid);

	/* exclusive open prevents others from interfering */
	if ((fd = open(procname, (O_RDWR|O_EXCL))) < 0) {
		perror("Pcreate open()");
		(void) kill(upid, SIGKILL);
		return -1;
	}

	/* make sure it's not one of 0, 1, or 2 */
	/* this allows truss to work when spawned by init(1m) */
	if (0 <= fd && fd <= 2) {
		int dfd = fcntl(fd, F_DUPFD, 3);

		(void) close(fd);
		if (dfd < 0) {
			perror("Pcreate fcntl()");
			(void) kill(upid, SIGKILL);
			return -1;
		}
		fd = dfd;
	}
	/* mark it close-on-exec so any created process doesn't inherit it */
	(void) fcntl(fd, F_SETFD, 1);

	/* mark it run-on-last-close so it runs even if we die on a signal */
	if (Ioctl(fd, PIOCSRLC, 0) == -1)
		perror("Pcreate PIOCSRLC");

	P->pfd = fd;

	(void) Pstop(P);	/* stop the controlled process */
	for (;;) {		/* wait for process to sleep in pause() */
		if (P->state == PS_STOP
		 && P->why.pr_why == PR_REQUESTED
		 && (P->why.pr_flags & PR_ASLEEP)
		 && Pgetsysnum(P) == SYS_pause)
			break;

		if (P->state != PS_STOP		/* interrupt or process died */
		 || Psetrun(P, 0, PRSTOP) != 0) {	/* can't restart */
			int sig = SIGKILL;

			(void) Ioctl(fd, PIOCKILL, (int)&sig);	/* kill !  */
			(void) close(fd);
			(void) kill(upid, sig);			/* kill !! */
			P->state = PS_DEAD;
			P->pfd = 0;
			return -1;
		}
		(void) Pwait(P);
		/* dumpwhy(P, 0); */
	}

	(void) Psysentry(P, SYS_exit, 1);	/* catch these sys calls */
	(void) Psysentry(P, SYS_exec, 1);
	(void) Psysentry(P, SYS_execve, 1);

	/* kick it off the pause() */
	if (Psetrun(P, 0, PRSABORT) == -1) {
		int sig = SIGKILL;

		perror("Pcreate PIOCRUN");
		(void) Ioctl(fd, PIOCKILL, (int)&sig);
		(void) kill(upid, sig);
		(void) close(fd);
		P->state = PS_DEAD;
		P->pfd = 0;
		return -1;
	}

	(void) Pwait(P);	/* wait for exec() or exit() */

	return 0;
}

/* This is for the !?!*%! call to sleep() in execvp() */
unsigned int
sleep(n)
unsigned n;
{
	if (n) {
		(void) sighold(SIGALRM);
		(void) alarm(n);
		(void) sigpause(SIGALRM);
		(void) alarm(0);
		(void) sigrelse(SIGALRM);
		timeout = FALSE;
	}
	return 0;
}

#if 0
static void
fddump(fd)	/* debugging code -- dump fstat(fd) */
int fd;
{
	struct stat statb;
	CONST char * s;

	(void) fprintf(stderr, "fd = %d", fd);
	if (fstat(fd, &statb) == -1)
		goto out;
	switch (statb.st_mode & S_IFMT) {
	case S_IFDIR: s="S_IFDIR"; break;
	case S_IFCHR: s="S_IFCHR"; break;
	case S_IFBLK: s="S_IFBLK"; break;
	case S_IFREG: s="S_IFREG"; break;
	case S_IFIFO: s="S_IFIFO"; break;
	default:      s="???"; break;
	}
	(void) fprintf(stderr, "  %s  mode = 0%o  dev = 0x%.4X  ino = %d",
		s,
		statb.st_mode & ~S_IFMT,
		statb.st_dev,
		statb.st_ino);
	(void) fprintf(stderr, "  uid = %d,  gid = %d,  size = %ld\n",
		statb.st_uid,
		statb.st_gid,
		statb.st_size);

out:
	(void) fputc('\n', stderr);
}
#endif

int
Pgrab(P, upid, force)		/* grab existing process */
register process_t *P;		/* program table entry */
register pid_t upid;		/* UNIX process ID */
int force;			/* if TRUE, grab regardless */
{
	register int fd = -1;
	int nmappings;
	int ruid;
	struct prcred prcred;
	char procname[100];

again:	/* Come back here if we lose it in the Window of Vulnerability */
	if (fd >= 0) {
		(void) close(fd);
		fd = -1;
	}

	(void) memset((char *)P, 0, sizeof(*P));

	if (upid <= 0)
		return -1;

	/* generate the /proc/upid filename */
	(void) sprintf(procname, "%s/%ld", procdir, upid);

	/* Request exclusive open to avoid grabbing someone else's	*/
	/* process and to prevent others from interfering afterwards.	*/
	/* If this fails and the 'force' flag is set, attempt to	*/
	/* open non-exclusively (effective only for the super-user).	*/
	if ((fd = open(procname, (O_RDWR|O_EXCL))) < 0
	 && (fd = (force? open(procname, O_RDWR) : -1)) < 0) {
		if (errno == EBUSY && !force)
			return 1;
		if (debugflag)
			perror("Pgrab open()");
		return -1;
	}

	/* ---------------------------------------------------- */
	/* We are now in the Window of Vulnerability (WoV).	*/
	/* The process may exec() a setuid/setgid or unreadable	*/
	/* object file between the open() and the PIOCSTOP.	*/
	/* We will get EAGAIN in this case and must start over.	*/
	/* ---------------------------------------------------- */

	/* If the process is a system process, we can't control it	*/
	/* even if we are super-user.  The number of mappings is the	*/
	/* way to determine a system process.				*/
	if (Ioctl(fd, PIOCNMAP, (int)&nmappings) == -1) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno != ENOENT)	/* Don't complain about zombies */
			perror("Pgrab PIOCNMAP");
		nmappings = 0;
	}
	/* there must be at least text, data, and stack */
	if (nmappings < 3) {
		(void) close(fd);
		return -1;
	}

	/* Verify process credentials in case we are running setuid root.   */
	/* We only verify that our real uid matches the process's real uid. */
	/* This means that the user really did create the process, even     */
	/* if using a different group id (via newgrp(1) for example).       */
	if (Ioctl(fd, PIOCCRED, (int)&prcred) == -1) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno != ENOENT)	/* Don't complain about zombies */
			perror("Pgrab PIOCCRED");
		(void) close(fd);
		return -1;
	}
	if ((ruid = getuid()) != 0	/* super-user allowed anything */
	 && ruid != prcred.pr_ruid) {	/* credentials check failed */
		(void) close(fd);
		return -1;
	}

	/* make sure it's not one of 0, 1, or 2 */
	/* this allows truss to work when spawned by init(1m) */
	if (0 <= fd && fd <= 2) {
		int dfd = fcntl(fd, F_DUPFD, 3);

		(void) close(fd);
		if (dfd < 0) {
			perror("Pgrab fcntl()");
			return -1;
		}
		fd = dfd;
	}
	/* mark it close-on-exec so any created process doesn't inherit it */
	(void) fcntl(fd, F_SETFD, 1);

	/* mark it run-on-last-close so it runs even if we die from SIGKILL */
	if (Ioctl(fd, PIOCSRLC, 0) == -1) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno != ENOENT)	/* Don't complain about zombies */
			perror("Pgrab PIOCSRLC");
	}

	P->cntrl = TRUE;
	P->child = FALSE;
	P->state = PS_RUN;
	P->upid  = upid;
	P->pfd   = fd;

	/* before stopping the process, make sure it's not ourself */
	if (upid == getpid()) {
		/* write a magic number, read it through /proc file */
		/* and see if the results match. */
		long magic1 = 0;
		long magic2 = 2;

		errno = 0;

		if (Pread(P, (long)&magic1, (char *)&magic2, sizeof(magic2))
		    == sizeof(magic2)
		 && magic2 == 0
		 && (magic1 = 0xfeedbeef)
		 && Pread(P, (long)&magic1, (char *)&magic2, sizeof(magic2))
		    == sizeof(magic2)
		 && magic2 == 0xfeedbeef) {
			(void) close(fd);
			(void) fprintf(stderr,
			"Pgrab(): process attempted to grab itself\n");
			return -1;
		}
	}
	
	/* Stop the process, get its status and its signal/syscall masks. */
	if (Pstatus(P, PIOCSTOP, 2) != 0) {
		if (P->state == PS_LOST)	/* WoV */
			goto again;
		if ((errno != EINTR && errno != ERESTART)
		 || (P->state != PS_STOP && !(P->why.pr_flags&PR_DSTOP))) {
			if (P->state != PS_RUN) {
				if (errno != ENOENT)
					perror("Pgrab PIOCSTOP");
#if 0
				fddump(fd);
#endif
			}
			(void) close(fd);
			return -1;
		}
	}

	/* Process is or will be stopped, these will "certainly" not fail */
	if (Ioctl(fd, PIOCGTRACE, (int)&P->sigmask) == -1)
		perror("Pgrab PIOCGTRACE");
	if (Ioctl(fd, PIOCGFAULT, (int)&P->faultmask) == -1)
		perror("Pgrab PIOCGFAULT");
	if (Ioctl(fd, PIOCGENTRY, (int)&P->sysentry) == -1)
		perror("Pgrab PIOCGENTRY");
	if (Ioctl(fd, PIOCGEXIT,  (int)&P->sysexit)  == -1)
		perror("Pgrab PIOCGEXIT");

	return 0;
}

int
Preopen(P, force)	/* reopen the /proc file (after PS_LOST) */
register process_t *P;
int force;			/* if TRUE, grab regardless */
{
	register int fd;
	char procname[100];

	/* reopen the /proc/upid file */
	(void) sprintf(procname, "%s/%ld", procdir, P->upid);
	if ((fd = open(procname, (O_RDWR|O_EXCL))) < 0
	 && (fd = (force? open(procname, O_RDWR) : -1)) < 0) {
		if (debugflag)
			perror("Preopen open()");
		return -1;
	}

	if (P->pfd == 0		/* close the old filedescriptor */
	 || close(P->pfd) != 0)
		P->pfd = 3;

	/* make the new filedescriptor the same as the old */
	if (fd != P->pfd) {
		int dfd = fcntl(fd, F_DUPFD, P->pfd);

		P->pfd = 0;
		(void) close(fd);
		if (dfd < 0) {
			perror("Preopen fcntl()");
			return -1;
		}
		fd = dfd;
	}
	/* mark it close-on-exec so any created process doesn't inherit it */
	(void) fcntl(fd, F_SETFD, 1);

	/* set run-on-last-close so it runs even if we die from SIGKILL */
	if (Ioctl(fd, PIOCSRLC, 0) == -1)
		perror("Preopen PIOCSRLC");

	P->state = PS_RUN;
	P->pfd   = fd;
	
	/* process should be stopped on exec (REQUESTED) */
	/* or else should be stopped on exit from exec() (SYSEXIT) */
	if (Pwait(P) == 0
	 && P->state == PS_STOP
	 && (P->why.pr_why == PR_REQUESTED
	  || (P->why.pr_why == PR_SYSEXIT
	   && (P->why.pr_what == SYS_exec || P->why.pr_what == SYS_execve)))) {
		/* fake up stop-on-exit-from-execve */
		if (P->why.pr_why == PR_REQUESTED) {
			P->why.pr_why = PR_SYSEXIT;
			P->why.pr_what = SYS_execve;
		}
	}
	else {
		(void) fprintf(stderr,
		"Preopen: expected REQUESTED or SYSEXIT(SYS_execve) stop\n");
	}

	return 0;
}

int
Prelease(P)		/* release process to run freely */
register process_t *P;
{
	register int fd = P->pfd;
	long flags;

	if (fd == 0)
		return -1;

	if (debugflag)
		(void) fprintf(stderr, "Prelease: releasing pid # %ld\n",
			P->upid);

	/* attempt to stop it if we have to reset its registers */
	if (P->sethold || P->setregs) {
		register int count;
		for (count = 10;
		     count > 0 && (P->state == PS_RUN || P->state == PS_STEP);
		     count--) {
			(void) Pstop(P);
		}
	}

	/* if we lost control, all we can do is close the file */
	if (P->state == PS_STOP) {
		if (P->sethold
		 && Ioctl(fd, PIOCSHOLD, (int)&P->why.pr_sighold) != 0)
			perror("Prelease PIOCSHOLD");
		if (P->setregs
		 && Ioctl(fd, PIOCSREG, (int)&P->REG[0]) != 0)
			perror("Prelease PIOCSREG");
	}

	flags = PR_FORK;
	(void) Ioctl(fd, PIOCRESET, (int)&flags);
	flags = PR_RLC;
	(void) Ioctl(fd, PIOCSET, (int)&flags);
	(void) close(fd);	/* this sets the process running */

	/* zap the process structure */
	(void) memset((char *)P, 0, sizeof(*P));

	return 0;
}

int
Phang(P)		/* release process but leave it stopped */
register process_t *P;
{
	register int fd = P->pfd;
	register int count;
	long flags;
	sysset_t sysset;
	sigset_t sigset;
	fltset_t fltset;

	if (fd == 0)
		return -1;

	if (debugflag)
		(void) fprintf(stderr, "Phang: releasing pid # %ld\n",
			P->upid);

	/* attempt to stop it */
	for (count = 10;
	     count > 0 && (P->state == PS_RUN || P->state == PS_STEP);
	     count--)
		(void) Pstop(P);

	/* if we lost control, all we can do is close the file */
	if (P->state == PS_STOP) {
		if (P->sethold
		 && Ioctl(fd, PIOCSHOLD, (int)&P->why.pr_sighold) != 0)
			perror("Phang PIOCSHOLD");

		if (P->setregs
		 && Ioctl(fd, PIOCSREG, (int)&P->REG[0]) != 0)
			perror("Phang PIOCSREG");

		/* no run-on-last-close or kill-on-last-close*/
		flags = PR_RLC|PR_KLC| /* PR_ASYNC| */ PR_PCOMPAT;
		if (Ioctl(fd, PIOCRESET, (int)&flags) != 0)
			perror("Phang PIOCRESET");

		/* inherit-on-fork */
		flags = PR_FORK;
		if (Ioctl(fd, PIOCSET, (int)&flags) != 0)
			perror("Phang PIOCSET");

		/* no signal tracing */
		premptyset(&sigset);
		if (ioctl(fd, PIOCSTRACE, (int)&sigset) != 0)
			perror("Phang PIOCSTRACE");

		/* no fault tracing */
		premptyset(&fltset);
		if (ioctl(fd, PIOCSFAULT, (int)&fltset) != 0)
			perror("Phang PIOCSFAULT");

		/* no syscall tracing */
		premptyset(&sysset);
		if (ioctl(fd, PIOCSENTRY, (int)&sysset) != 0)
			perror("Phang PIOCSENTRY");

		premptyset(&sysset);
		if (ioctl(fd, PIOCSEXIT, (int)&sysset) != 0)
			perror("Phang PIOCSENTRY");
	}

	(void) close(fd);

	/* zap the process structure */
	(void) memset((char *)P, 0, sizeof(*P));

	return 0;
}

/* debugging */
static void
prdump(P)
process_t *P;
{
	long bits = *((long *)&P->why.pr_sigpend);

	if (P->why.pr_cursig)
		(void) fprintf(stderr, "  p_cursig  = %d", P->why.pr_cursig);
	if (bits)
		(void) fprintf(stderr, "  p_sigpend = 0x%.8lX", bits);
	(void) fputc('\n', stderr);
}

#if 0
/* debugging */
static void
dumpregs(P)
register process_t *P;
{
	register int i;

	for (i = 0; i < NREGS; i++) {
		(void) fprintf(stderr, "  %%%s = 0x%.8X",
			regname[i], P->REG[i]);
		if ((i+1)%4 == 0)
			(void) fprintf(stderr, "\n");
	}
	if (i%4 != 0)
		(void) fprintf(stderr, "\n");
}
#endif

#if 0
/* debugging */
static void
dumpwhy(P, str)
register process_t *P;
char *str;
{
	if (str)
		(void) fprintf(stderr, "%s\n", str);
	(void) fprintf(stderr, "pr_flags  = 0x%.8X\n", P->why.pr_flags);
	(void) fprintf(stderr, "pr_why    = 0x%.8X\n", P->why.pr_why);
	(void) fprintf(stderr, "pr_what   = 0x%.8X\n", P->why.pr_what);
	(void) fprintf(stderr, "pr_pid    = 0x%.8X\n", P->why.pr_pid);
	(void) fprintf(stderr, "pr_ppid   = 0x%.8X\n", P->why.pr_ppid);
	(void) fprintf(stderr, "pr_pgrp   = 0x%.8X\n", P->why.pr_pgrp);
	(void) fprintf(stderr, "pr_cursig = 0x%.8X\n", P->why.pr_cursig);
	(void) fprintf(stderr, "pr_sigpend= 0x%.8X\n", P->why.pr_sigpend.bits[0]);
	(void) fprintf(stderr, "pr_sighold= 0x%.8X\n", P->why.pr_sighold.bits[0]);
	(void) fprintf(stderr, "pr_instr  = 0x%.8X\n", P->why.pr_instr);
	(void) fprintf(stderr, "pr_utime  = 0x%.8X\n", P->why.pr_utime);
	(void) fprintf(stderr, "pr_stime  = 0x%.8X\n", P->why.pr_stime);
	dumpregs(P);
}
#endif

int
Pwait(P)	/* wait for process to stop for any reason */
register process_t *P;
{
	return Pstatus(P, PIOCWSTOP, 0);
}

int
Pstop(P)	/* direct process to stop; wait for it to stop */
register process_t *P;
{
	return Pstatus(P, PIOCSTOP, 0);
}

int
Pstatus(P, request, sec) /* wait for specified process to stop or terminate */
register process_t *P;	/* program table entry */
register int request;	/* PIOCSTATUS, PIOCSTOP, PIOCWSTOP */
unsigned sec;		/* if non-zero, alarm timeout in seconds */
{
	register int status = 0;
	int err = 0;

	switch (P->state) {
	case PS_NULL:
	case PS_LOST:
	case PS_DEAD:
		return -1;
	case PS_STOP:
		if (request != PIOCSTATUS)
			return 0;
	}

	switch (request) {
	case PIOCSTATUS:
	case PIOCSTOP:
	case PIOCWSTOP:
		break;
	default:
		/* programming error */
		(void) fprintf(stderr, "Pstatus: illegal request\n");
		return -1;
	}

	timeout = FALSE;
	if (sec)
		(void) alarm(sec);
	if (Ioctl(P->pfd, request, (int)&P->why) != 0) {
		err = errno;
		if (sec)
			(void) alarm(0);
		if (request != PIOCSTATUS && (err == EINTR || err == ERESTART)
		 && Ioctl(P->pfd, PIOCSTATUS, (int)&P->why) != 0)
			err = errno;
	}
	else if (sec)
		(void) alarm(0);

	if (err) {
		switch (err) {
		case EINTR:		/* timeout or user typed DEL */
		case ERESTART:
			if (debugflag)
				(void) fprintf(stderr, "Pstatus: EINTR\n");
			break;
		case EAGAIN:		/* we lost control of the the process */
			if (debugflag)
				(void) fprintf(stderr, "Pstatus: EAGAIN\n");
			P->state = PS_LOST;
			break;
		default:		/* check for dead process */
			if (debugflag || err != ENOENT) {
				CONST char * errstr;

				switch (request) {
				case PIOCSTATUS:
					errstr = "Pstatus PIOCSTATUS"; break;
				case PIOCSTOP:
					errstr = "Pstatus PIOCSTOP"; break;
				case PIOCWSTOP:
					errstr = "Pstatus PIOCWSTOP"; break;
				default:
					errstr = "Pstatus PIOC???"; break;
				}
				perror(errstr);
			}
			deadcheck(P);
			break;
		}
		if (!timeout || (err != EINTR && err != ERESTART)) {
			errno = err;
			return -1;
		}
	}

	if (!(P->why.pr_flags&PR_STOPPED)) {
		if (request == PIOCSTATUS || timeout) {
			timeout = FALSE;
			return 0;
		}
		(void) fprintf(stderr, "Pstatus: process is not stopped\n");
		return -1;
	}

	P->state = PS_STOP;
	timeout = FALSE;

#if 0	/* debugging */
    {
	struct prstatus pstats;

	if (Ioctl(P->pfd, PIOCSTATUS, (int)&pstats) == -1)
		perror("Pstatus: PIOCSTATUS");
	else if (memcmp((char *)&P->why, (char *)&pstats, sizeof(pstats)) != 0)
		(void) fprintf(stderr,
			"Pstatus: PIOCWSTOP and PIOCSTATUS disagree!\n");
    }
#endif

#if 0	/* debugging */
    {
	GREGSET regs;		/* array of registers from process */
	register int i;

	/* read the process registers */
	if (Ioctl(P->pfd, PIOCGREG, (int)&regs[0]) == -1)
		perror("Pstatus: PIOCGREG failure");
	else for (i = 0; i < NREGS; i++) {
		if (debugflag && P->REG[i] != regs[i])
			(void) fprintf(stderr,
				"  %%%s: 0x%.8X 0x%.8X\n",
				regname[i], P->REG[i], regs[i]);
		P->REG[i] = regs[i];
	}
    }
#endif

	switch (P->why.pr_why) {
	case PR_REQUESTED:
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: REQUESTED");
			prdump(P);
		}
		break;
	case PR_SIGNALLED:
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: SIGNALLED %s",
				signame(P->why.pr_what));
			prdump(P);
		}
		break;
	case PR_FAULTED:
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: FAULTED %s",
				fltname(P->why.pr_what));
			prdump(P);
		}
#if 0 /* debugging */
		dumpregs(P);
#endif
		break;
	case PR_SYSENTRY:
		/* remember syscall address */
#if sparc
		P->sysaddr = P->REG[R_PC];
#else
		P->sysaddr = P->REG[R_PC]-sizeof(syscall_t);
#endif
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: SYSENTRY %s",
				sysname(P->why.pr_what, -1));
			prdump(P);
		}
		break;
	case PR_SYSEXIT:
		/* remember syscall address */
		P->sysaddr = P->REG[R_PC]-sizeof(syscall_t);
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: SYSEXIT %s",
				sysname(P->why.pr_what, -1));
			prdump(P);
		}
		break;
	case PR_JOBCONTROL:
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: JOBCONTROL %s",
				signame(P->why.pr_what));
			prdump(P);
		}
		break;
	case PR_SUSPENDED:
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: SUSPENDED");
			prdump(P);
		}
		break;
	default:
		if (debugflag) {
			(void) fprintf(stderr, "Pstatus: why: Unknown");
			prdump(P);
		}
		status = -1;
		break;
	}

#if 0	/* debugging */
	if (debugflag)
		dumpwhy(P, 0);
#endif

	return status;
}

int
Pgetsysnum(P)		/* determine which syscall number we are at */
register process_t *P;
{
	register int syscall = -1;

#if u3b2 || u3b5
	if (Pgetareg(P,0)==0 && (P->REG[0]&0x7c)==4 && Pgetareg(P,1)==0)
		syscall = (P->REG[1] & 0x7ff8) >> 3;
#endif
#if mc68k
	if (Pgetareg(P,0) == 0)
		syscall = P->REG[0] & 0xffff;
#endif
#if sparc || i386
#    if defined(sparc) && defined(oldstuff)
	if (Pgetareg(P,R_G1) == 0) {
		syscall = P->REG[R_G1];
		if (syscall == SYS_syscall	/* indirect system call */
		 && Pgetareg(P,R_O0) == 0)
			syscall = P->REG[R_O0];
	}
#    else
	syscall = P->why.pr_syscall;
#    endif
#endif /* sparc || i386 */

	return syscall;
}

int
Psetsysnum(P, syscall)	/* we are at a syscall trap, prepare to issue syscall */
register process_t *P;
register int syscall;
{
#if defined(i386)
	P->REG[R_R0] = syscall;
	if (Pputareg(P,R_R0))
		syscall = -1;
#endif /* i386 */
#if u3b2 || u3b5
	P->REG[0] = 4;
	P->REG[1] = syscall<<3;
	if (Pputareg(P,0) || Pputareg(P,1))
		syscall = -1;
#endif
#if mc68k
	P->REG[0] = syscall;
	if (Pputareg(P,0))
		syscall = -1;
#endif
#if sparc
	P->REG[R_G1] = syscall;
	if (Pputareg(P,R_G1))
		syscall = -1;
#endif
	return syscall;
}

static void
deadcheck(P)
register process_t *P;
{
	if (P->pfd == 0)
		P->state = PS_DEAD;
	else {
		while (Ioctl(P->pfd, PIOCSTATUS, (int)&P->why) != 0) {
			switch (errno) {
			default:
				/* process is dead */
				if (P->pfd != 0)
					(void) close(P->pfd);
				P->pfd = 0;
				P->state = PS_DEAD;
				break;
			case EINTR:
			case ERESTART:
				continue;
			case EAGAIN:
				P->state = PS_LOST;
				break;
			}
			break;
		}
	}
}

int
Pgetregs(P)	/* get values of registers from stopped process */
register process_t *P;
{
	if (P->state != PS_STOP)
		return -1;
	return 0;		/* registers are always available */
}

int
Pgetareg(P, reg)	/* get the value of one register from stopped process */
register process_t *P;
register int reg;		/* register number */
{
	if (reg < 0 || reg >= NREGS) {
		(void) fprintf(stderr,
			"Pgetareg(): invalid register number, %d\n", reg);
		return -1;
	}
	if (P->state != PS_STOP)
		return -1;
	return 0;		/* registers are always available */
}

int
Pputregs(P)	/* put values of registers into stopped process */
register process_t *P;
{
	if (P->state != PS_STOP)
		return -1;
	P->setregs = TRUE;	/* set registers before continuing */
	return 0;
}

int
Pputareg(P, reg)	/* put value of one register into stopped process */
register process_t *P;
register int reg;		/* register number */
{
	if (reg < 0 || reg >= NREGS) {
		(void) fprintf(stderr,
			"Pputareg(): invalid register number, %d\n", reg);
		return -1;
	}
	if (P->state != PS_STOP)
		return -1;
	P->setregs = TRUE;	/* set registers before continuing */
	return 0;
}

int
Psetrun(P, sig, flags)
register process_t *P;
int sig;		/* signal to pass to process */
register int flags;	/* flags: PRCSIG|PRSTEP|PRSABORT|PRSTOP */
{
	register int request;		/* for setting signal */
	register int why = P->why.pr_why;
	siginfo_t info;
	struct prrun prrun;

	if (sig < 0 || sig > PRMAXSIG
	 || P->state != PS_STOP)
		return -1;

	if (sig) {
		if (flags & PRCSIG)
			request = PIOCKILL;
		else {
			switch (why) {
			case PR_REQUESTED:
			case PR_SIGNALLED:
				request = PIOCSSIG;
				break;
			default:
				request = PIOCKILL;
				break;
			}
		}
	}

	/* must be initialized to zero */
	(void) memset((char *)&prrun, 0, sizeof(prrun));
	(void) memset((char *)&info, 0, sizeof(info));
	info.si_signo = sig;

	prrun.pr_flags = flags & ~(PRSTRACE|PRSHOLD|PRSFAULT|PRSVADDR);

	if (P->setsig) {
		prrun.pr_flags |= PRSTRACE;
		prrun.pr_trace = P->sigmask;
	}
	if (P->sethold) {
		prrun.pr_flags |= PRSHOLD;
		prrun.pr_sighold = P->why.pr_sighold;
	}
	if (P->setfault) {
		prrun.pr_flags |= PRSFAULT;
		prrun.pr_fault = P->faultmask;
	}
	while ((P->setentry && Ioctl(P->pfd, PIOCSENTRY, (int)&P->sysentry) == -1)
	 || (P->setexit  && Ioctl(P->pfd, PIOCSEXIT,  (int)&P->sysexit)  == -1)
	 || (P->setregs  && Ioctl(P->pfd, PIOCSREG,  (int)&P->REG[0])  == -1)
	 || (sig && Ioctl(P->pfd, request, (int)&info) == -1)) {
		if (errno != EBUSY || Ioctl(P->pfd, PIOCWSTOP, 0) != 0)
			goto bad;
	}
	P->setentry = FALSE;
	P->setexit  = FALSE;
	P->setregs  = FALSE;

	if (Ioctl(P->pfd, PIOCRUN, prrun.pr_flags? (int)&prrun : 0) == -1) {
		if ((why != PR_SIGNALLED && why != PR_JOBCONTROL)
		 || errno != EBUSY)
			goto bad;
		goto out;	/* ptrace()ed or jobcontrol stop -- back off */
	}

	P->setsig   = FALSE;
	P->sethold  = FALSE;
	P->setfault = FALSE;
out:
	P->state    = (flags&PRSTEP)? PS_STEP : PS_RUN;
	return 0;

bad:
	if (errno == ENOENT)
		goto out;
	perror("Psetrun");
	return -1;
}

int
Pstart(P, sig)
register process_t *P;
int sig;		/* signal to pass to process */
{
	return Psetrun(P, sig, 0);
}

int
Pterm(P)
register process_t *P;
{
	int sig = SIGKILL;

	if (debugflag)
		(void) fprintf(stderr,
			"Pterm: terminating pid # %ld\n", P->upid);
	if (P->state == PS_STOP)
		(void) Pstart(P, SIGKILL);
	(void) Ioctl(P->pfd, PIOCKILL, (int)&sig);	/* make sure */
	(void) kill((int)P->upid, SIGKILL);		/* make double sure */

	if (P->pfd != 0)
		(void) close(P->pfd);

	/* zap the process structure */
	(void) memset((char *)P, 0, sizeof(*P));

	return 0;
}

int
Pread(P, address, buf, nbyte)
register process_t *P;
long address;		/* address in process */
char *buf;		/* caller's buffer */
int nbyte;		/* number of bytes to read */
{
	if (nbyte <= 0)
		return 0;
	return pread(P->pfd, buf, (unsigned)nbyte, (off_t)address);
}

int
Pwrite(P, address, buf, nbyte)
register process_t *P;
long address;		/* address in process */
CONST char *buf;	/* caller's buffer */
int nbyte;		/* number of bytes to write */
{
	if (nbyte <= 0)
		return 0;
	return pwrite(P->pfd, buf, (unsigned)nbyte, (off_t)address);
}

int
Psignal(P, which, stop)		/* action on specified signal */
register process_t *P;		/* program table exit */
register int which;		/* signal number */
register int stop;		/* if TRUE, stop process; else let it go */
{
	register int oldval;

	if (which <= 0 || which > PRMAXSIG || (which == SIGKILL && stop))
		return -1;

	oldval = prismember(&P->sigmask, which)? TRUE : FALSE;

	if (stop) {	/* stop process on receipt of signal */
		if (!oldval) {
			praddset(&P->sigmask, which);
			P->setsig = TRUE;
		}
	}
	else {		/* let process continue on receipt of signal */
		if (oldval) {
			prdelset(&P->sigmask, which);
			P->setsig = TRUE;
		}
	}

	return oldval;
}

int
Pfault(P, which, stop)		/* action on specified fault */
register process_t *P;		/* program table exit */
register int which;		/* fault number */
register int stop;		/* if TRUE, stop process; else let it go */
{
	register int oldval;

	if (which <= 0 || which > PRMAXFAULT)
		return -1;

	oldval = prismember(&P->faultmask, which)? TRUE : FALSE;

	if (stop) {	/* stop process on receipt of fault */
		if (!oldval) {
			praddset(&P->faultmask, which);
			P->setfault = TRUE;
		}
	}
	else {		/* let process continue on receipt of fault */
		if (oldval) {
			prdelset(&P->faultmask, which);
			P->setfault = TRUE;
		}
	}

	return oldval;
}

int
Psysentry(P, which, stop)	/* action on specified system call entry */
register process_t *P;		/* program table entry */
register int which;		/* system call number */
register int stop;		/* if TRUE, stop process; else let it go */
{
	register int oldval;

	if (which <= 0 || which > PRMAXSYS)
		return -1;

	oldval = prismember(&P->sysentry, which)? TRUE : FALSE;

	if (stop) {	/* stop process on sys call */
		if (!oldval) {
			praddset(&P->sysentry, which);
			P->setentry = TRUE;
		}
	}
	else {		/* don't stop process on sys call */
		if (oldval) {
			prdelset(&P->sysentry, which);
			P->setentry = TRUE;
		}
	}

	return oldval;
}

int
Psysexit(P, which, stop)	/* action on specified system call exit */
register process_t *P;		/* program table exit */
register int which;		/* system call number */
register int stop;		/* if TRUE, stop process; else let it go */
{
	register int oldval;

	if (which <= 0 || which > PRMAXSYS)
		return -1;

	oldval = prismember(&P->sysexit, which)? TRUE : FALSE;

	if (stop) {	/* stop process on sys call exit */
		if (!oldval) {
			praddset(&P->sysexit, which);
			P->setexit = TRUE;
		}
	}
	else {		/* don't stop process on sys call exit */
		if (oldval) {
			prdelset(&P->sysexit, which);
			P->setexit = TRUE;
		}
	}

	return oldval;
}


static int
execute(P, sysindex)	/* execute the syscall instruction */
register process_t *P;	/* process control structure */
int sysindex;		/* system call index */
{
	sigset_t hold;		/* mask of held signals */
	int sentry;		/* old value of stop-on-syscall-entry */

	/* move current signal back to pending */
	if (P->why.pr_cursig) {
		int sig = P->why.pr_cursig;
		(void) Ioctl(P->pfd, PIOCSSIG, 0);
		(void) Ioctl(P->pfd, PIOCKILL, (int)&sig);
		P->why.pr_cursig = 0;
	}

	sentry = Psysentry(P, sysindex, TRUE);	/* set stop-on-syscall-entry */
	hold = P->why.pr_sighold;	/* remember signal hold mask */
	prfillset(&P->why.pr_sighold);	/* hold all signals */
	P->sethold = TRUE;

	if (Psetrun(P, 0, PRCSIG) == -1)
		goto bad;
	while (P->state == PS_RUN)
		(void) Pwait(P);

	if (P->state != PS_STOP)
		goto bad;
	P->why.pr_sighold = hold;		/* restore hold mask */
	P->sethold = TRUE;
	(void) Psysentry(P, sysindex, sentry);	/* restore sysentry stop */
	if (P->why.pr_why  == PR_SYSENTRY
	 && P->why.pr_what == sysindex)
		return 0;
bad:
	return -1;
}

/* worst-case alignment for objects on the stack */
#if i386	/* stack grows down, non-aligned */
#define	ALIGN(sp)	(sp)
#define	ARGOFF	1
#endif
#if u3b2	/* stack grows up, word-aligned */
#define	ALIGN(sp)	(((sp) + sizeof(int) - 1) & ~(sizeof(int) - 1))
#define	ARGOFF	0
#endif
#if sparc	/* stack grows down, doubleword-aligned */
#define	ALIGN(sp)	((sp) & ~(2*sizeof(int) - 1))
#define	ARGOFF	0
#endif

struct sysret		/* perform system call in controlled process */
Psyscall(P, sysindex, nargs, argp)
register process_t *P;	/* process control structure */
int sysindex;		/* system call index */
register int nargs;	/* number of arguments to system call */
struct argdes *argp;	/* argument descriptor array */
{
	register struct argdes *adp;	/* pointer to argument descriptor */
	struct sysret rval;		/* return value */
	register int i;			/* general index value */
	register int Perr = 0;		/* local error number */
	int sexit;			/* old value of stop-on-syscall-exit */
	GREG sp;			/* adjusted stack pointer */
	GREG ap;			/* adjusted argument pointer */
	GREGSET savedreg;		/* remembered registers */
	int arglist[MAXARGS+2];		/* syscall arglist */
	int why = P->why.pr_why;	/* reason for stopping */
	int what = P->why.pr_what;	/* detailed reason (syscall, signal) */

	/* block (hold) all signals for the duration. */
	sigset_t block, unblock;

	(void) sigfillset(&block);
	(void) sigemptyset(&unblock);
	(void) sigprocmask(SIG_BLOCK, &block, &unblock);

	rval.errno = 0;		/* initialize return value */
	rval.r0 = 0;
	rval.r1 = 0;

	premptyset(&psigs);	/* no saved signals yet */

	if (sysindex <= 0 || sysindex > PRMAXSYS	/* programming error */
	 || nargs < 0 || nargs > MAXARGS)
		goto bad1;

	if (P->state != PS_STOP			/* check state of process */
	 || (P->why.pr_flags & PR_ASLEEP)
	 || Pgetregs(P) != 0)
		goto bad2;

	for (i = 0; i < NREGS; i++)		/* remember registers */
		savedreg[i] = P->REG[i];

	if (checksyscall(P))			/* bad text ? */
		goto bad3;


	/* validate arguments and compute the stack frame parameters --- */

	sp = savedreg[R_SP];	/* begin with the current stack pointer */
	sp = ALIGN(sp);
	for (i=0, adp=argp; i<nargs; i++, adp++) {	/* for each argument */
		rval.r0 = i;		/* in case of error */
		switch (adp->type) {
		default:			/* programming error */
			goto bad4;
		case AT_BYVAL:			/* simple argument */
			break;
		case AT_BYREF:			/* must allocate space */
			switch (adp->inout) {
			case AI_INPUT:
			case AI_OUTPUT:
			case AI_INOUT:
				if (adp->object == NULL)
					goto bad5;	/* programming error */
				break;
			default:		/* programming error */
				goto bad6;
			}
			/* allocate stack space for BYREF argument */
			if (adp->len <= 0 || adp->len > MAXARGL)
				goto bad7;	/* programming error */
#if u3b2		/* upward stack growth */
			adp->value = sp;	/* stack address for object */
			sp = ALIGN(sp + adp->len);
#elif sparc || i386	/* downward stack growth */
			sp = ALIGN(sp - adp->len);
			adp->value = sp;	/* stack address for object */
#endif
			break;
		}
	}
	rval.r0 = 0;			/* in case of error */
#if u3b2
	ap = sp;			/* address of arg list */
	sp += sizeof(int)*(nargs+2);	/* space for arg list + CALL parms */
#endif
#if i386
	ap = sp;			/* address of arg list */
	sp -= sizeof(int)*(nargs+2);	/* space for arg list + CALL parms */
#endif
#if sparc
	sp -= (nargs>6)? sizeof(int)*(16+1+nargs) : sizeof(int)*(16+1+6);
	sp = ALIGN(sp);
	ap = sp+(16+1)*sizeof(int);	/* address of arg dump area */
#endif

	/* point of no return */

	/* special treatment of stopped-on-syscall-entry */
	/* move the process to the stopped-on-syscall-exit state */
	if (why == PR_SYSENTRY) {
		/* arrange to reissue sys call */
#if !sparc
		savedreg[R_PC] -= sizeof(syscall_t);
#endif

		sexit = Psysexit(P, what, TRUE);  /* catch this syscall exit */

		if (Psetrun(P, 0, PRSABORT) != 0	/* abort sys call */
		 || Pwait(P) != 0
		 || P->state != PS_STOP
		 || P->why.pr_why != PR_SYSEXIT
		 || P->why.pr_what != what
		 || Pgetareg(P, R_PS) != 0
		 || Pgetareg(P, 0) != 0
		 || (P->REG[R_PS] & ERRBIT) == 0
		 || (P->REG[R_R0] != EINTR && P->REG[R_R0] != ERESTART)) {
			(void) fprintf(stderr,
				"Psyscall(): cannot abort sys call\n");
			(void) Psysexit(P, what, sexit);
			goto bad9;
		}

		(void) Psysexit(P, what, sexit);/* restore previous exit trap */
	}


	/* perform the system call entry, adjusting %sp */
	/* this moves the process to the stopped-on-syscall-entry state */
	/* just before the arguments to the sys call are fetched */

	(void) Psetsysnum(P, sysindex);
	P->REG[R_SP] = sp;
	P->REG[R_PC] = P->sysaddr;	/* address of syscall */
#if sparc
	P->REG[R_nPC] = P->sysaddr+sizeof(syscall_t);
#elif u3b2
	P->REG[R_AP] = ap;
#endif
	(void) Pputregs(P);

	if (execute(P, sysindex) != 0	/* execute the syscall instruction */
#if sparc
	 || P->REG[R_PC] != P->sysaddr
	 || P->REG[R_nPC] != P->sysaddr+sizeof(syscall_t)
#else
	 || P->REG[R_PC] != P->sysaddr+sizeof(syscall_t)
#endif
	)
		goto bad10;


	/* stopped at syscall entry; copy arguments to stack frame */
	for (i=0, adp=argp; i<nargs; i++, adp++) {	/* for each argument */
		rval.r0 = i;		/* in case of error */
		if (adp->type != AT_BYVAL
		 && adp->inout != AI_OUTPUT) {
			/* copy input byref parameter to process */
			if (Pwrite(P, (long)adp->value, adp->object, adp->len)
			    != adp->len)
				goto bad17;
		}
		arglist[ARGOFF+i] = adp->value;
#if sparc
		if (i < 6) {
			P->REG[R_O0+i] = adp->value;
			(void) Pputareg(P, R_O0+i);
		}
#endif
	}
	rval.r0 = 0;			/* in case of error */
#if i386
	arglist[0] = savedreg[R_PC];		/* CALL parameters */
	if (Pwrite(P, (long)sp, (char *)&arglist[0], (int)sizeof(int)*(nargs+1))
	    != sizeof(int)*(nargs+1))
		goto bad18;
#endif
#if u3b2
	arglist[nargs] = savedreg[R_PC];	/* CALL parameters */
	arglist[nargs+1] = savedreg[R_AP];
	if (Pwrite(P, (long)ap, (char *)&arglist[0], (int)sizeof(int)*(nargs+2))
	    != sizeof(int)*(nargs+2))
		goto bad18;
#endif
#if sparc
	if (nargs > 6
	 && Pwrite(P, (long)ap, (char *)&arglist[0], (int)sizeof(int)*nargs)
	    != sizeof(int)*nargs)
		goto bad18;
#endif

	/* complete the system call */
	/* this moves the process to the stopped-on-syscall-exit state */

	sexit = Psysexit(P, sysindex, TRUE);	/* catch this syscall exit */
	do {		/* allow process to receive signals in sys call */
		if (Psetrun(P, 0, 0) == -1)
			goto bad21;
		while (P->state == PS_RUN)
			(void) Pwait(P);
	} while (P->state == PS_STOP && P->why.pr_why == PR_SIGNALLED);
	(void) Psysexit(P, sysindex, sexit);	/* restore original setting */

	if (P->state != PS_STOP
	 || P->why.pr_why  != PR_SYSEXIT)
		goto bad22;
	if (P->why.pr_what != sysindex)
		goto bad23;
#if sparc
	if (P->REG[R_PC] != P->sysaddr+sizeof(syscall_t)
	 || P->REG[R_nPC] != P->sysaddr+2*sizeof(syscall_t))
#else
	if (P->REG[R_PC] != P->sysaddr+sizeof(syscall_t))
#endif
		goto bad24;


	/* fetch output arguments back from process */
#if u3b2
	if (Pread(P, (long)ap, (char *)&arglist[0], (int)sizeof(int)*(nargs+2))
	    != sizeof(int)*(nargs+2))
		goto bad25;
#endif
#if i386
	if (Pread(P, (long)sp, (char *)&arglist[0], (int)sizeof(int)*(nargs+1))
	    != sizeof(int)*(nargs+1))
		goto bad25;
#endif
	for (i=0, adp=argp; i<nargs; i++, adp++) {	/* for each argument */
		rval.r0 = i;		/* in case of error */
		if (adp->type != AT_BYVAL
		 && adp->inout != AI_INPUT) {
			/* copy output byref parameter from process */
			if (Pread(P, (long)adp->value, adp->object, adp->len)
			    != adp->len)
				goto bad26;
		}
		adp->value = arglist[ARGOFF+i];
	}


	/* get the return values from the syscall */

	if (P->REG[R_PS] & ERRBIT) {	/* error */
		rval.errno = P->REG[R_R0];
		rval.r0 = -1;
	}
	else {				/* normal return */
		rval.r0 = P->REG[R_R0];
		rval.r1 = P->REG[R_R1];
	}

	goto good;

bad26:	Perr++;
bad25:	Perr++;
bad24:	Perr++;
bad23:	Perr++;
bad22:	Perr++;
bad21:	Perr++;
	Perr++;
	Perr++;
bad18:	Perr++;
bad17:	Perr++;
	Perr++;
	Perr++;
	Perr++;
	Perr++;
	Perr++;
	Perr++;
bad10:	Perr++;
bad9:	Perr++;
	Perr += 8;
	rval.errno = -Perr;	/* local errors are negative */

good:
	/* restore process to its previous state (almost) */

	for (i = 0; i < NREGS; i++)	/* restore remembered registers */
		P->REG[i] = savedreg[i];
	(void) Pputregs(P);

	if (why == PR_SYSENTRY		/* special treatment */
	 && execute(P, what) != 0) {	/* get back to the syscall */
		(void) fprintf(stderr,
			"Psyscall(): cannot reissue sys call\n");
		if (Perr == 0)
			rval.errno = -27;
	}

	P->why.pr_why = why;
	P->why.pr_what = what;

	goto out;

bad8:	Perr++;
bad7:	Perr++;
bad6:	Perr++;
bad5:	Perr++;
bad4:	Perr++;
bad3:	Perr++;
bad2:	Perr++;
bad1:	Perr++;
	rval.errno = -Perr;	/* local errors are negative */

out:
	/* unblock (release) all signals before returning */
	(void) sigprocmask(SIG_SETMASK, &unblock, (sigset_t *)NULL);

	return rval;
}

static int
checksyscall(P)		/* check syscall instruction in process */
process_t *P;
{
	/* this should always succeed--we always have a good syscall address */
	syscall_t instr;		/* holds one syscall instruction */

	return(
#if i386
	   (Pread(P, P->sysaddr, (char *)instr, sizeof(instr)) == sizeof(instr)
	    && instr[0] == SYSCALL)?
#else
	   (Pread(P, P->sysaddr, (char *)&instr, sizeof(instr)) == sizeof(instr)
	    && instr == SYSCALL)?
#endif
	    0 : -1 );
}

int
Ioctl(fd, request, arg)		/* deal with RFS congestion */
int fd;
int request;
int arg;
{
	register int rc;
	char str[40];

	for(;;) {
		if ((rc = ioctl(fd, request, arg)) != -1
		 || errno != ENOMEM)
			return rc;

		if (debugflag) {
			(void) sprintf(str, "\t *** Ioctl(0x%X): ENOMEM\n",
				request);
			(void) write(2, str, (unsigned)strlen(str));
		}
	}
}

/* test for empty set */
/* support routine used by isemptyset() macro */
int
is_empty(sp, n)
register CONST long * sp;	/* pointer to set (array of longs) */
register unsigned n;		/* number of longs in set */
{
	if (n) {
		do {
			if (*sp++)
				return FALSE;
		} while (--n);
	}

	return TRUE;
}
