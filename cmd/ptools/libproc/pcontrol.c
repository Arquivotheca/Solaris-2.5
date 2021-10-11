#ident	"@(#)pcontrol.c	1.2	95/06/26 SMI"

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

/* Process Management */

/* This module is carefully coded to contain only read-only data */
/* All read/write data is defined in ramdata.c (see also ramdata.h) */

/*
 * Function prototypes for static routines in this module.
 */
#if	defined(__STDC__)

static	void	deadcheck( process_t * );
static	int	execute( process_t * , int );
static	int	checksyscall( process_t * );

#else	/* defined(__STDC__) */

static	void	deadcheck();
static	int	execute();
static	int	checksyscall();

#endif	/* defined(__STDC__) */

static int
dupfd(fd, dfd)
	register int fd;
	register int dfd;
{
	/*
	 * Make sure fd not one of 0, 1, or 2.
	 * This allows the program to work when spawned by init(1m).
	 * Also, if dfd is non-zero, dup the fd to be dfd.
	 */
	if (dfd > 0 || (0 <= fd && fd <= 2)) {
		if (dfd <= 0)
			dfd = 3;
		dfd = fcntl(fd, F_DUPFD, dfd);
		(void) close(fd);
		fd = dfd;
	}
	/*
	 * Mark it close-on-exec so any created process doesn't inherit it.
	 */
	if (fd >= 0)
		(void) fcntl(fd, F_SETFD, 1);
	return (fd);
}

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
	P->pfd = -1;
	P->lwpfd = -1;

	/* open the /proc/upid file */
	(void) sprintf(procname, "%s/%d", procdir, upid);

	/* exclusive open prevents others from interfering */
	if ((fd = open(procname, (O_RDWR|O_EXCL))) < 0
	 || (fd = dupfd(fd, 0)) < 0) {
		perror("Pcreate open()");
		(void) kill(upid, SIGKILL);
		return -1;
	}
	P->pfd = fd;

	/* mark it run-on-last-close so it runs even if we die on a signal */
	if (Ioctl(fd, PIOCSRLC, 0) == -1)
		perror("Pcreate PIOCSRLC");

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
			P->pfd = -1;
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
		P->pfd = -1;
		return -1;
	}

	(void) Pwait(P);	/* wait for exec() or exit() */

	return 0;
}

int
Pgrab(P, upid, force)		/* grab existing process */
register process_t *P;		/* program table entry */
register pid_t upid;		/* UNIX process ID */
int force;			/* if TRUE, grab regardless */
{
	register int pfd = -1;
	int err;
	int rc;
	prstatus_t prstat;
	int ruid;
	struct prcred prcred;
	char procname[100];

again:	/* Come back here if we lose it in the Window of Vulnerability */
	if (pfd >= 0) {
		(void) close(pfd);
		pfd = -1;
	}

	(void) memset((char *)P, 0, sizeof(*P));
	P->pfd = -1;
	P->lwpfd = -1;

	/* generate the /proc/upid filename */
	(void) sprintf(procname, "%s/%d", procdir, upid);

	/* Request exclusive open to avoid grabbing someone else's	*/
	/* process and to prevent others from interfering afterwards.	*/
	/* If this fails and the 'force' flag is set, attempt to	*/
	/* open non-exclusively (effective only for the super-user).	*/
	if (((pfd = open(procname, (O_RDWR|O_EXCL))) < 0
	  && (pfd = (force? open(procname, O_RDWR) : -1)) < 0)
	 || (pfd = dupfd(pfd, 0)) < 0) {
		switch (errno) {
		case EBUSY:
			return G_BUSY;
		case ENOENT:
			return G_NOPROC;
		case EACCES:
		case EPERM:
			return G_PERM;
		default:
			perror("Pgrab open()");
			return G_STRANGE;
		}
	}

	/* ---------------------------------------------------- */
	/* We are now in the Window of Vulnerability (WoV).	*/
	/* The process may exec() a setuid/setgid or unreadable	*/
	/* object file between the open() and the PIOCSTOP.	*/
	/* We will get EAGAIN in this case and must start over.	*/
	/* ---------------------------------------------------- */

	/* Verify process credentials in case we are running setuid root.   */
	/* We only verify that our real uid matches the process's real uid. */
	/* This means that the user really did create the process, even     */
	/* if using a different group id (via newgrp(1) for example).       */
	if (Ioctl(pfd, PIOCCRED, (int)&prcred) == -1) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno == ENOENT)	/* Don't complain about zombies */
			rc = G_NOPROC;
		else {
			perror("Pgrab PIOCCRED");
			rc = G_STRANGE;
		}
		(void) close(pfd);
		return rc;
	}
	if ((ruid = getuid()) != 0	/* super-user allowed anything */
	 && ruid != prcred.pr_ruid) {	/* credentials check failed */
		(void) close(pfd);
		errno = EACCES;
		return G_PERM;
	}

	/* get the ps information, even if it is a system process or ourself */
	if (Ioctl(pfd, PIOCPSINFO, (int)&P->psinfo) == -1) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno == ENOENT)	/* Don't complain about zombies */
			rc = G_NOPROC;
		else {
			perror("Pgrab PIOCPSINFO");
			rc = G_STRANGE;
		}
		(void) close(pfd);
		return rc;
	}

	/*
	 * Get the process's status.
	 */
	if (Ioctl(pfd, PIOCSTATUS, (int)&prstat) != 0) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno == ENOENT)	/* Don't complain about zombies */
			rc = G_ZOMB;
		else {
			perror("Pgrab PIOCSTATUS");
			rc = G_STRANGE;
		}
		(void) close(pfd);
		return rc;
	}

	/*
	 * If the process is a system process, we can't control it.
	 */
	if (prstat.pr_flags & PR_ISSYS) {
		(void) close(pfd);
		return G_SYS;
	}

	P->cntrl = TRUE;
	P->child = FALSE;
	P->state = PS_RUN;
	P->upid  = upid;
	P->pfd   = pfd;

	/* before stopping the process, make sure it's not ourself */
	if (upid == getpid()) {
		/*
		 * Verify that the process is really ourself:
		 * Set a magic number, read it through the
		 * /proc file and see if the results match.
		 */
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
			(void) close(pfd);
			(void) memset((char *)P, 0, sizeof(*P));
			P->pfd = -1;
			P->lwpfd = -1;
			return G_SELF;
		}
	}

	/*
	 * If the process is already stopped or has been directed
	 * to stop via /proc, do not set run-on-last-close.
	 */
	if (prstat.pr_flags & (PR_ISTOP|PR_DSTOP)) {
		(void) Pstatus(P, PIOCSTATUS, 0);
		goto out;
	}

	/*
	 * Mark the process run-on-last-close so
	 * it runs even if we die from SIGKILL.
	 */
	if (Ioctl(pfd, PIOCSRLC, 0) != 0) {
		if (errno == EAGAIN)	/* WoV */
			goto again;
		if (errno == ENOENT)	/* Don't complain about zombies */
			rc = G_ZOMB;
		else {
			perror("Pgrab PIOCSRLC");
			rc = G_STRANGE;
		}
		(void) close(pfd);
		(void) memset((char *)P, 0, sizeof(*P));
		P->pfd = -1;
		P->lwpfd = -1;
		return rc;
	}
	
	/* Stop the process, get its status and its signal/syscall masks. */
	if (Pstatus(P, PIOCSTOP, 2) != 0) {
		if (P->state == PS_LOST)	/* WoV */
			goto again;
		if ((errno != EINTR && errno != ERESTART)
		 || (P->state != PS_STOP && !(P->why.pr_flags & PR_DSTOP))) {
			if (P->state != PS_RUN && errno != ENOENT) {
				perror("Pgrab PIOCSTOP");
				rc = G_STRANGE;
			} else {
				rc = G_ZOMB;
			}
			(void) close(pfd);
			(void) memset((char *)P, 0, sizeof(*P));
			P->pfd = -1;
			P->lwpfd = -1;
			return rc;
		}
	}

out:
	/*
	 * Process should either be stopped via /proc or
	 * there should be an outstanding stop directive.
	 */
	if (P->state != PS_STOP
	 && (P->why.pr_flags & (PR_ISTOP|PR_DSTOP)) == 0) {
		(void) fprintf(stderr, "Pgrab: process is not stopped\n");
		(void) close(pfd);
		return G_STRANGE;
	}

	/* Process is or will be stopped, these will "certainly" not fail */
	rc = 0;
	if (Ioctl(pfd, PIOCGTRACE, (int)&P->sigmask) == -1) {
		perror("Pgrab PIOCGTRACE");
		rc = G_STRANGE;
	} else if (Ioctl(pfd, PIOCGFAULT, (int)&P->faultmask) == -1) {
		perror("Pgrab PIOCGFAULT");
		rc = G_STRANGE;
	} else if (Ioctl(pfd, PIOCGENTRY, (int)&P->sysentry) == -1) {
		perror("Pgrab PIOCGENTRY");
		rc = G_STRANGE;
	} else if (Ioctl(pfd, PIOCGEXIT,  (int)&P->sysexit)  == -1) {
		perror("Pgrab PIOCGEXIT");
		rc = G_STRANGE;
	}

	if (rc) {
		(void) close(pfd);
		(void) memset((char *)P, 0, sizeof(*P));
		P->pfd = -1;
		P->lwpfd = -1;
	}

	return rc;
}

/*
 * Ensure that the lwp's signal mask and the
 * lwp registers are flushed to the lwp.
 */
static int
Psync(P)
register process_t *P;
{
	int fd = (P->lwpfd >= 0)? P->lwpfd : P->pfd;
	int rc = 0;

	if (P->sethold && Ioctl(fd, PIOCSHOLD, (int)&P->why.pr_sighold) != 0)
		rc = -1;
	if (P->setregs && Ioctl(fd, PIOCSREG, (int)&P->REG[0]) != 0)
		rc = -1;
	P->sethold  = FALSE;
	P->setregs  = FALSE;
	return rc;
}

int
Pchoose(P)		/* choose an lwp for further operations */
register process_t *P;
{
	int fd;
	id_t *lwpid;
	int nlwp;
	int i;

	Punchoose(P);

	nlwp = P->why.pr_nlwp;
	if (nlwp <= 0)
		nlwp = 1;
	lwpid = malloc((nlwp+1)*sizeof(id_t));
	if (Ioctl(P->pfd, PIOCLWPIDS, (int)lwpid) != 0) {
		free(lwpid);
		return -1;
	}

	/* search for an lwp --- */
	for (i = 0; i < nlwp; i++) {
		/* open the lwp file */
		if ((fd = Ioctl(P->pfd, PIOCOPENLWP, (int)&lwpid[i])) < 0
		 || (fd = dupfd(fd, 0)) < 0)
			continue;
		P->lwpfd = fd;

		/* get the lwp status */
		if (Pstatus(P, PIOCSTATUS, 0) != 0)
			break;

		/* avoid the aslwp, if possible */
		if ((P->why.pr_flags & PR_ASLWP) && P->why.pr_nlwp > 1) {
			Punchoose(P);
			continue;
		}

		/* we have the lwp we want */
		free(lwpid);
		return 0;
	}

	Punchoose(P);
	free(lwpid);
	return -1;
}

/* undo Pchoose() */
void
Punchoose(P)
register process_t *P;
{
	(void) Psync(P);

	if (P->lwpfd >= 0)
		(void) close(P->lwpfd);
	P->lwpfd = -1;

	/* refresh the process status */
	(void) Pstatus(P, PIOCSTATUS, 0);
}

int
Preopen(P, force)	/* reopen the /proc file (after PS_LOST) */
register process_t *P;
int force;			/* if TRUE, grab regardless */
{
	register int fd;
	char procname[100];

	if (P->lwpfd >= 0)
		(void) close(P->lwpfd);
	P->lwpfd = -1;

	/* reopen the /proc/upid file */
	(void) sprintf(procname, "%s/%ld", procdir, P->upid);
	if (((fd = open(procname, (O_RDWR|O_EXCL))) < 0
	  && (fd = (force? open(procname, O_RDWR) : -1)) < 0)
	 || close(P->pfd) < 0
	 || (fd = dupfd(fd, P->pfd)) < 0) {
		if (debugflag)
			perror("Preopen open()");
		(void) close(P->pfd);
		P->pfd = -1;
		return -1;
	}
	P->pfd = fd;

	/* set run-on-last-close so it runs even if we die from SIGKILL */
	if (Ioctl(fd, PIOCSRLC, 0) == -1)
		perror("Preopen PIOCSRLC");

	P->state = PS_RUN;
	
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
	int jsig = P->jsig;

	if (P->pfd < 0)
		return -1;

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
		/* we didn't lose it, set the registers */
		if (Psync(P) != 0)
			perror("Prelease PIOCSHOLD/PIOCSREG");
	}

	(void) Ioctl(P->pfd, PIOCRFORK, 0);
	(void) Ioctl(P->pfd, PIOCSRLC, 0);
	if (jsig)
		(void) Ioctl(P->pfd, PIOCKILL, (int)&jsig);

	/* closing the files sets the process running */
	if (P->lwpfd >= 0)
		(void) close(P->lwpfd);
	if (P->pfd >= 0)
		(void) close(P->pfd);

	/* zap the process structure */
	(void) memset((char *)P, 0, sizeof(*P));
	P->pfd = -1;
	P->lwpfd = -1;

	return 0;
}

int
Phang(P)		/* release process but leave it stopped */
register process_t *P;
{
	register int count;
	long flags;
	sysset_t sysset;
	sigset_t sigset;
	fltset_t fltset;

	if (P->pfd < 0)
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
		/* we didn't lose it; do more */
		if (Psync(P) != 0)
			perror("Phang PIOCSHOLD/PIOCSREG");

		/* no run-on-last-close or kill-on-last-close*/
		flags = PR_RLC|PR_KLC| /* PR_ASYNC| */ PR_PCOMPAT;
		if (Ioctl(P->pfd, PIOCRESET, (int)&flags) != 0)
			perror("Phang PIOCRESET");

		/* inherit-on-fork */
		flags = PR_FORK;
		if (Ioctl(P->pfd, PIOCSET, (int)&flags) != 0)
			perror("Phang PIOCSET");

		/* no signal tracing */
		premptyset(&sigset);
		if (ioctl(P->pfd, PIOCSTRACE, (int)&sigset) != 0)
			perror("Phang PIOCSTRACE");

		/* no fault tracing */
		premptyset(&fltset);
		if (ioctl(P->pfd, PIOCSFAULT, (int)&fltset) != 0)
			perror("Phang PIOCSFAULT");

		/* no syscall tracing */
		premptyset(&sysset);
		if (ioctl(P->pfd, PIOCSENTRY, (int)&sysset) != 0)
			perror("Phang PIOCSENTRY");

		premptyset(&sysset);
		if (ioctl(P->pfd, PIOCSEXIT, (int)&sysset) != 0)
			perror("Phang PIOCSENTRY");
	}

	if (P->lwpfd >= 0)
		(void) close(P->lwpfd);
	if (P->pfd >= 0)
		(void) close(P->pfd);

	/* zap the process structure */
	(void) memset((char *)P, 0, sizeof(*P));
	P->pfd = -1;
	P->lwpfd = -1;

	return 0;
}

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
	int pfd = (P->lwpfd >= 0)? P->lwpfd : P->pfd;

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
	if (Ioctl(pfd, request, (int)&P->why) != 0) {
		err = errno;
		if (sec)
			(void) alarm(0);
		if (request != PIOCSTATUS && (err == EINTR || err == ERESTART)
		 && Ioctl(pfd, PIOCSTATUS, (int)&P->why) != 0)
			err = errno;
	}
	else if (sec)
		(void) alarm(0);

	if (err) {
		switch (err) {
		case EINTR:		/* timeout or user typed DEL */
		case ERESTART:
			break;
		case EAGAIN:		/* we lost control of the the process */
			P->state = PS_LOST;
			break;
		default:		/* check for dead process */
			if (err != ENOENT) {
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

	switch (P->why.pr_why) {
	case PR_REQUESTED:
	case PR_SIGNALLED:
	case PR_FAULTED:
	case PR_JOBCONTROL:
#ifdef PR_SUSPENDED
	case PR_SUSPENDED:
#endif
		break;
	case PR_SYSENTRY:
		/* remember syscall address */
#if sparc
#if nono
		P->sysaddr = P->REG[R_PC];
#endif
		break;
#endif
	case PR_SYSEXIT:
#if nono
		P->sysaddr = P->REG[R_PC]-sizeof(syscall_t);
#endif
		break;
	default:
		status = -1;
		break;
	}

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
	syscall = P->why.pr_syscall;
#endif

	return syscall;
}

int
Psetsysnum(P, syscall)	/* we are at a syscall trap, prepare to issue syscall */
register process_t *P;
register int syscall;
{
#if i386
	P->REG[R_R0] = syscall;
	if (Pputareg(P,R_R0))
		syscall = -1;
#endif
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
	int pfd = (P->lwpfd >= 0)? P->lwpfd : P->pfd;

	if (pfd < 0)
		P->state = PS_DEAD;
	else {
		while (Ioctl(pfd, PIOCSTATUS, (int)&P->why) != 0) {
			switch (errno) {
			default:
				/* process or lwp is dead */
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
	if (reg < 0 || reg >= NGREG) {
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
	if (reg < 0 || reg >= NGREG) {
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
register int flags;	/* flags: PRCSIG|PRCFAULT|PRSTEP|PRSABORT|PRSTOP */
{
	register int request;		/* for setting signal */
	register int why = P->why.pr_why;
	int pfd = (P->lwpfd >= 0)? P->lwpfd : P->pfd;
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
	while ((P->setentry && Ioctl(pfd, PIOCSENTRY, (int)&P->sysentry) == -1)
	 || (P->setexit  && Ioctl(pfd, PIOCSEXIT,  (int)&P->sysexit)  == -1)
	 || (P->setregs  && Ioctl(pfd, PIOCSREG,  (int)&P->REG[0])  == -1)
	 || (sig && Ioctl(pfd, request, (int)&info) == -1)) {
		if (errno != EBUSY || Ioctl(pfd, PIOCWSTOP, 0) != 0)
			goto bad;
	}
	P->setentry = FALSE;
	P->setexit  = FALSE;
	P->setregs  = FALSE;

	if (Ioctl(pfd, PIOCRUN, prrun.pr_flags? (int)&prrun : 0) == -1) {
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

	if (P->state == PS_STOP)
		(void) Pstart(P, SIGKILL);
	(void) Ioctl(P->pfd, PIOCKILL, (int)&sig);	/* make sure */
	(void) kill((int)P->upid, SIGKILL);		/* make double sure */

	if (P->lwpfd >= 0)
		(void) close(P->lwpfd);
	if (P->pfd >= 0)
		(void) close(P->pfd);

	/* zap the process structure */
	(void) memset((char *)P, 0, sizeof(*P));
	P->pfd = -1;
	P->lwpfd = -1;

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
	int pfd = (P->lwpfd >= 0)? P->lwpfd : P->pfd;
	sigset_t hold;		/* mask of held signals */
	int sentry;		/* old value of stop-on-syscall-entry */
	sysset_t entryset;

	/* move current signal back to pending */
	if (P->why.pr_cursig) {
		int sig = P->why.pr_cursig;
		(void) Ioctl(pfd, PIOCSSIG, 0);
		(void) Ioctl(pfd, PIOCKILL, (int)&sig);
		P->why.pr_cursig = 0;
	}

#if 1
	entryset = P->sysentry;
	prfillset(&P->sysentry);
	P->setentry = TRUE;
#else
	sentry = Psysentry(P, sysindex, TRUE);	/* set stop-on-syscall-entry */
#endif
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
#if 1
	P->sysentry = entryset;
	P->setentry = TRUE;
#else
	(void) Psysentry(P, sysindex, sentry);	/* restore sysentry stop */
#endif
	if (P->why.pr_why  == PR_SYSENTRY
	 && P->why.pr_what == sysindex)
		return 0;
bad:
	fprintf(stderr, "execute(): expected %d/%d, got %d/%d\n",
		PR_SYSENTRY, sysindex, P->why.pr_why, P->why.pr_what);
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
	greg_t sp;			/* adjusted stack pointer */
	greg_t ap;			/* adjusted argument pointer */
	gregset_t savedreg;		/* remembered registers */
	int arglist[MAXARGS+2];		/* syscall arglist */
	int why;			/* reason for stopping */
	int what;			/* detailed reason (syscall, signal) */
	sigset_t block, unblock;
	int waschosen = FALSE;

	/* block (hold) all signals for the duration. */
	(void) sigfillset(&block);
	(void) sigemptyset(&unblock);
	(void) sigprocmask(SIG_BLOCK, &block, &unblock);

	rval.errno = 0;		/* initialize return value */
	rval.r0 = 0;
	rval.r1 = 0;

	/* if necessary, choose an lwp from the process to do all the work */
	if (P->lwpfd < 0) {
		if (Pchoose(P) != 0)
			goto bad8;
		waschosen = TRUE;
	}

	why = P->why.pr_why;
	what = P->why.pr_what;

	if (sysindex <= 0 || sysindex > PRMAXSYS	/* programming error */
	 || nargs < 0 || nargs > MAXARGS)
		goto bad1;

	if (P->state != PS_STOP			/* check state of process */
	 || (P->why.pr_flags & PR_ASLEEP)
	 || Pgetregs(P) != 0)
		goto bad2;

	for (i = 0; i < NGREG; i++)		/* remember registers */
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
			Pputareg(P, R_O0+i);
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

	for (i = 0; i < NGREG; i++)	/* restore remembered registers */
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
	/* if we chose an lwp for the operation, unchoose it now */
	if (waschosen)
		Punchoose(P);

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
	   (Pread(P,P->sysaddr,(char *)&instr,sizeof(instr)) == sizeof(instr)
#if i386
	    && instr[0] == SYSCALL
#else
	    && instr == (syscall_t)SYSCALL
#endif
	    )? 0 : -1 );
}

int
Ioctl(fd, request, arg)		/* deal with RFS congestion */
int fd;
int request;
int arg;
{
	register int rc;

	for(;;) {
		if ((rc = ioctl(fd, request, arg)) != -1
		 || errno != ENOMEM)
			return rc;
	}
}
