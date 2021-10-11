/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#ident	"@(#)ptrace.c	1.13	95/08/29 SMI"

/*
 * ptrace(2)/wait(2) interface built on top of proc(4).
 * proc_wait(pid, wait_loc), defined here, must be used
 * instead of wait(wait_loc).
 */

#include "adb.h"
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/auxv.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/uio.h>
#include <sys/regset.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/psw.h>
#include <sys/ptrace.h>


/* bit in processor status word indicating syscall failure */
#if sparc
#define	ERRBIT	PSR_C
#elif i386
#define	ERRBIT	PS_C
#endif

#define	TRUE	1
#define	FALSE	0

/* external routines defined in this module */
extern	int	ptrace(int, int, int, int, caddr_t);
extern	int	proc_wait(pid_t, int *);
extern	auxv_t	*FetchAuxv(void);
extern	char *	map(int);
/* static routines defined in this module */
static	int	Dupfd(int, int);
static	void	MakeProcName(char *, pid_t);
static	int	OpenProc(pid_t);
static	void	CloseProc(void);
static	int	OpenLwp(pid_t, id_t);
static	void	CloseLwp(void);
static	int	GrabProc(pid_t);
static	int	FirstOpen(pid_t);

static	pid_t	process_id = 0;		/* pid of process under control */
static	char	stopped = FALSE;
static	char	setregs = FALSE;
static	int	pfd = -1;		/* /proc/<pid> */
static	id_t	lwp_id = 0;
static	int	lwpfd = -1;

int
ptrace(request, pid, addr, data, addr2)
register int request;
register int pid;
register int addr;
int data;
caddr_t addr2;
{
	register prstatus_t * ps = &Prstatus;
	extern int v9flag;
	static long size;
	union {
		sysset_t syscalls;
		siginfo_t siginfo;
	} arg;


	db_printf(5, "ptrace: request=%s, pid=%D, addr=%X, data=%X,\n\taddr2=%X",
	    map(request), pid, addr, data, addr2);

	if (request == PTRACE_TRACEME) {	/* executed by traced process */
		/*
		 * Set stop-on-exit-from-exec flags.
		 */
		char procname[32];	/* /proc/<pid> */
		register int fd;

		MakeProcName(procname, getpid());
		if ((fd = open(procname, O_RDWR, 0)) < 0)
			_exit(255);
		premptyset(&arg.syscalls);
		praddset(&arg.syscalls, SYS_exec);
		praddset(&arg.syscalls, SYS_execve);
		if (ioctl(fd, PIOCSENTRY, &arg) != 0)
			_exit(255);
		if (ioctl(fd, PIOCSEXIT, &arg) != 0)
			_exit(255);
		if (close(fd) != 0)
			_exit(255);

		return 0;
	}

	/*
	 * adb doesn't control more than one process at the same time.
	 */
	if (pid != process_id && process_id != 0) {
		errno = ESRCH;
		return -1;
	}

	if (request == PTRACE_ATTACH) {
		int retry = 2;

		while (GrabProc(pid) < 0) {
			CloseProc();
			if (--retry < 0 || errno != EAGAIN) {
				if (errno == ENOENT)
					errno = ESRCH;
				else if (errno == EBUSY)
					errno = EPERM;
				return -1;
			}
			(void) sleep(1);
		}
		if (SetTracingFlags() != 0) {
			CloseProc();
			return -1;
		}
		return 0;
	}

again:
	errno = 0;

	switch (request) {
	case PTRACE_PEEKUSER:
		break;

	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
#if !i386	/* no alignment restriction on x86 */
		if (addr & 03)
			goto eio;
#endif
		errno = 0;
		if (pread(pfd, (char *)&data, sizeof(data), (off_t)addr)
		    == sizeof(data))
			return data;
		goto tryagain;

	case PTRACE_POKEUSER:
		break;

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
#if !i386	/* no alignment restriction on x86 */
		if (addr & 03)
			goto eio;
#endif
#if sparc
		if ((caddr_t)ps->pr_reg[R_SP] < (caddr_t)addr+sizeof(data)
		 && (caddr_t)ps->pr_reg[R_SP]+16*sizeof(int) > (caddr_t)addr)
			setregs = TRUE;
#endif
		if (pwrite(pfd, (char *)&data, sizeof(data), (off_t)addr)
		    == sizeof(data))
			return 0;
		goto tryagain;

	case PTRACE_SYSCALL:
		break;

	case PTRACE_SINGLESTEP:
	case PTRACE_CONT:
	    {
		prrun_t prrun;

		CloseLwp();

		if (request == PTRACE_SINGLESTEP && ps->pr_who) {
			/*
			 * If stepping, some LWP must be stopped.
			 * Open that LWP, and step only that one.
			 */
			if (OpenLwp(pid, ps->pr_who) == -1)
				fprintf(stderr, "Unable to open LWP for step.\n");
		}
		if (addr != 1		/* new virtual address */
		 && addr != ps->pr_reg[R_PC]) {
			ps->pr_reg[R_PC] = (prgreg_t)addr;
#if sparc
			ps->pr_reg[R_nPC] = (prgreg_t)addr + 4;
#endif
			setregs = TRUE;
		}
		if (setregs) {
#if sparc
			(void) pread(pfd, (char *)&ps->pr_reg[R_L0],
				16*sizeof(int), (off_t)ps->pr_reg[R_SP]);
#endif
			if (ioctl(pfd, PIOCSREG, &ps->pr_reg[0]) != 0)
				goto tryagain;
			setregs = FALSE;
		}
		/* make data the current signal */
		if (data != 0 && data != ps->pr_cursig) {
			(void) memset((char *)&arg.siginfo, 0,
			    sizeof(arg.siginfo));
			arg.siginfo.si_signo = data;
			if (ioctl(pfd, PIOCSSIG, &arg) != 0)
				goto tryagain;
		}
		(void) memset((char *)&prrun, 0, sizeof(prrun));
		if (data == 0)
			prrun.pr_flags |= PRCSIG;
		if (request == PTRACE_SINGLESTEP)
			prrun.pr_flags |= PRSTEP;
		if (ioctl((lwpfd >= 0)? lwpfd : pfd, PIOCRUN, &prrun) != 0)
			goto tryagain;
		stopped = FALSE;
		return 0;
	    }

	case PTRACE_KILL:
		/* overkill? */
		data = SIGKILL;
		(void) memset((char *)&arg.siginfo, 0, sizeof(arg.siginfo));
		arg.siginfo.si_signo = data;
		(void) ioctl(pfd, PIOCSSIG, &arg);
		(void) kill(pid, SIGKILL);
		(void) ioctl(pfd, PIOCRUN, 0);
		/* Put a stake through the heart of this zombie. */
		(void) proc_wait(pid, &data);
		CloseProc();
		return 0;

	case PTRACE_DETACH:
		if (addr != 1		/* new virtual address */
		 && addr != ps->pr_reg[R_PC]) {
			ps->pr_reg[R_PC] = (prgreg_t)addr;
#if sparc
			ps->pr_reg[R_nPC] = (prgreg_t)addr + 4;
#endif
			setregs = TRUE;
		}
		if (setregs) {
#if sparc
			(void) pread(pfd, (char *)&ps->pr_reg[R_L0],
				16*sizeof(int), (off_t)ps->pr_reg[R_SP]);
#endif
			if (ioctl(pfd, PIOCSREG, &ps->pr_reg[0]) != 0)
				goto tryagain;
			setregs = FALSE;
		}
		if (data != ps->pr_cursig) {
			(void) memset((char *)&arg.siginfo, 0,
			    sizeof(arg.siginfo));
			arg.siginfo.si_signo = data;
			if (ioctl(pfd, PIOCSSIG, &arg) != 0)
				goto tryagain;
		}
		CloseProc();	/* this sets the process running */
		return 0;

	case PTRACE_GETREGS:
	    {
		/* Nothing to do; they really live in the global struct. */
		return 0;
	    }

	case PTRACE_SETREGS:
	    {
		/* Just record that there has been a change. */
		setregs = TRUE;
		return 0;
	    }

	case PTRACE_GETFPAREGS:
		break;

	case PTRACE_GETFPREGS:
	    {
		if (ioctl(pfd, PIOCGFPREG, (int)addr) != 0)
			goto tryagain;
		if (v9flag && (ioctl(pfd, PIOCGXREGSIZE, &size) == 0))
			ioctl(pfd, PIOCGXREG, (caddr_t)&xregs);
		return 0;
	    }

	case PTRACE_SETFPAREGS:
		break;

	case PTRACE_SETFPREGS:
	    {
		if (ioctl(pfd, PIOCSFPREG, (int)addr) != 0)
			goto tryagain;
		if (v9flag && (ioctl(pfd, PIOCGXREGSIZE, &size) == 0))
			ioctl(pfd, PIOCSXREG, (caddr_t)&xregs);
		return 0;
	    }

#if sparc
	case PTRACE_GETWINDOW:
	case PTRACE_SETWINDOW:
		break;
#endif

	case PTRACE_READDATA:
	case PTRACE_READTEXT:
		if (data <= 0)
			goto eio;
		errno = 0;
		if (pread(pfd, (char *)addr2, (unsigned)data, (off_t)addr)
		    == data)
			return 0;
		goto tryagain;

	case PTRACE_WRITEDATA:
	case PTRACE_WRITETEXT:
		if (data <= 0)
			goto eio;
#if sparc
		if ((caddr_t)ps->pr_reg[R_SP] < (caddr_t)addr2+(unsigned)data
		 && (caddr_t)ps->pr_reg[R_SP]+16*sizeof(int) > (caddr_t)addr2)
			setregs = TRUE;
#endif
		if (pwrite(pfd, (char *)addr2, (unsigned)data, (off_t)addr)
		    == data)
			return 0;
		goto tryagain;

	case PTRACE_DUMPCORE:
	case PTRACE_TRAPCODE:
		break;

#if i386
	case PTRACE_SETWRBKPT:
	case PTRACE_SETACBKPT:
	case PTRACE_CLRDR7:
	case PTRACE_SETBPP:
		break;
#endif
	}

	/* unimplemented request */
	db_printf(1, "ptrace: cannot handle %s!", map(request));
	errno = EIO;
	return -1;

tryagain:
	if (pfd == -1 || errno == ENOENT) {
		errno = ESRCH;
		return -1;
	}
	if (errno == EAGAIN && GrabProc(pid) >= 0)
		goto again;
eio:
	errno = EIO;
	return -1;
}

/*
 * Utility for OpenProc()/OpenLwp().
 */
static int
Dupfd(fd, dfd)
	register int fd;
	register int dfd;
{
	/*
	 * Make sure fd not one of 0, 1, or 2 to avoid stdio interference.
	 * Also, if dfd is greater than 2, dup fd to be exactly dfd.
	 */
	if (dfd > 2 || (0 <= fd && fd <= 2)) {
		if (dfd > 2 && fd != dfd)
			(void) close(dfd);
		else
			dfd = 3;
		if (fd != dfd) {
			dfd = fcntl(fd, F_DUPFD, dfd);
			(void) close(fd);
			fd = dfd;
		}
	}
	/*
	 * Mark filedescriptor close-on-exec.
	 * Should also be close-on-return-from-fork-in-child.
	 */
	(void) fcntl(fd, F_SETFD, 1);
	return (fd);
}

/*
 * Construct the /proc file name:  "/proc/<pid>"
 * The name buffer passed by the caller must be large enough.
 */
static void
MakeProcName(procname, pid)
	register char *procname;
	register pid_t pid;
{
	register char * s;

	(void) strcpy(procname, "/proc/00000");
	s = procname + strlen(procname);
	while (pid) {
		*--s = pid%10 + '0';
		pid /= 10;
	}
}

/*
 * Open/reopen the /proc/<pid> file.
 */
static int
OpenProc(pid)
	pid_t pid;
{
	char procname[32];		/* /proc/<pid> */
	register int fd;

	MakeProcName(procname, pid);

	if ((fd = open(procname, O_RDWR|O_EXCL, 0)) < 0
	 || (pfd = Dupfd(fd, pfd)) < 0)
		goto err;

	process_id = pid;
	stopped = FALSE;
	setregs = FALSE;
	return 0;

err:
	CloseProc();
	return -1;
}

/*
 * Close the /proc/<pid> file.
 */
static void
CloseProc()
{
	CloseLwp();

	if (pfd > 0)
		(void) close(pfd);

	memset(&Prstatus, 0, sizeof(Prstatus));
	process_id = 0;
	stopped = FALSE;
	setregs = FALSE;
	pfd = -1;
}

/*
 * Open/reopen the lwp file.
 */
static int
OpenLwp(pid, lwpid)
	pid_t pid;
	id_t lwpid;
{
	register int fd;

	if ((fd = ioctl(pfd, PIOCOPENLWP, &lwpid)) < 0
	 || (lwpfd = Dupfd(fd, lwpfd)) < 0)
		goto err;

	lwp_id = lwpid;
	return 0;

err:
	CloseLwp();
	return -1;
}

/*
 * Close the lwp file.
 */
static void
CloseLwp()
{
	if (lwpfd > 0)
		(void) close(lwpfd);

	lwp_id = 0;
	lwpfd = -1;
}

static int
SetTracingFlags()
{
	union {
		sigset_t signals;
		fltset_t faults;
		sysset_t syscalls;
	} arg;

	/*
	 * Process is stopped; these will "certainly" not fail.
	 */
	prfillset(&arg.signals);
	if (ioctl(pfd, PIOCSTRACE, &arg) != 0)
		return -1;
	premptyset(&arg.faults);
	if (ioctl(pfd, PIOCSFAULT, &arg) != 0)
		return -1;
	premptyset(&arg.syscalls);
	praddset(&arg.syscalls, SYS_exec);
	praddset(&arg.syscalls, SYS_execve);
	if (ioctl(pfd, PIOCSENTRY, &arg) != 0)
		return -1;
	if (ioctl(pfd, PIOCSEXIT, &arg) != 0)
		return -1;
	return 0;
}

/*
 * Take control of a child process.
 */
static int
GrabProc(pid)
	register pid_t pid;
{
	prstatus_t *ps = &Prstatus;
	long setflags = PR_RLC;
	long resetflags = PR_FORK;
	prrun_t prrun;

	if (pid <= 0)
		return (-1);

	while (OpenProc(pid) == 0) {
		errno = 0;
		if (ioctl(pfd, PIOCSTOP, ps) == 0
		 && ioctl(pfd, PIOCSET, &setflags) == 0
		 && ioctl(pfd, PIOCRESET, &resetflags) == 0) {
			while (ps->pr_why == PR_SYSENTRY ||
			    ps->pr_why == PR_SYSEXIT) {
				(void) memset((char *)&prrun, 0, sizeof(prrun));
				prrun.pr_flags = PRSTOP;
				if (ioctl(pfd, PIOCRUN, &prrun) != 0 ||
				    ioctl(pfd, PIOCWSTOP, ps) != 0) {
					CloseProc();
					return (-1);
				}
			}
			stopped = TRUE;
			return (0);
		}
		if (errno != EAGAIN)
			break;
	}

	CloseProc();
	return (-1);
}

/*
 * The first open() of the /proc file by the parent.
 */
static int
FirstOpen(pid)
pid_t pid;
{
	char procname[32];		/* /proc/<pid> */
	register int fd;

	MakeProcName(procname, pid);

	/*
	 * See if child has finished its ptrace(0,0,0,0)
	 * and has come to a stop.
	 */
	for (;;) {
		if ((fd = open(procname, O_RDWR, 0)) < 0
		 || (fd = Dupfd(fd, 0)) < 0) {
			errno = ESRCH;
			return -1;
		}
		if (ioctl(fd, PIOCWSTOP, 0) == 0)
			break;
		(void) close(fd);
		if (errno != EAGAIN) {
			if (errno == ENOENT)
				errno = ESRCH;
			else if (errno == EBUSY)
				errno = EPERM;
			return -1;
		}
	}
	(void) close(fd);

	/* now open the process for real */
	if (GrabProc(pid) < 0) {
		errno = ESRCH;
		return -1;
	}
	if (SetTracingFlags() != 0) {
		CloseProc();
		return -1;
	}
	return (0);
}

auxv_t *
FetchAuxv()
{
	static auxv_t nullauxv;
	static auxv_t *auxv = NULL;
	static int nauxv = 0;
	int n;

	if (ioctl(pfd, PIOCNAUXV, &n) != 0)
		return &nullauxv;
	if (n >= nauxv) {
		nauxv = n + 1;
		auxv = (auxv_t *)realloc(auxv, nauxv*sizeof(auxv_t));
	}
	(void) memset((char *)auxv, 0, nauxv*sizeof(auxv_t));
	(void) ioctl(pfd, PIOCAUXV, auxv);

	return auxv;
}

static int timeout = FALSE;

static void
sigalrm(int sig)
{
	timeout = TRUE;
}

int
lwp_wait(pid, stat_loc)
int pid;	
int *stat_loc;
{
	struct sigaction action;
	struct sigaction oaction;
	prstatus_t *ps = &Prstatus;
	prrun_t prrun;
	int sig;

startover:
	db_printf(2, "lwp_wait: waiting for lwp %D...", ps->pr_who);
	/* Wait for the lwp to stop. */
	timeout = FALSE;
	action.sa_flags = 0;
	action.sa_handler = sigalrm;
	(void) sigemptyset(&action.sa_mask);
	(void) sigaction(SIGALRM, &action, &oaction);
	(void) alarm(2);
	if (ioctl(lwpfd, PIOCWSTOP, ps) == 0)
		(void) alarm(0);
	else {
		(void) alarm(0);
		if (errno != EINTR)
			error("errno %d waiting for LWP to stop", errno);
		(void) ioctl(lwpfd, PIOCSTATUS, ps);
	}
	(void) sigaction(SIGALRM, &oaction, NULL);

	if (timeout || !(ps->pr_flags & PR_ISTOP)) {
		/* We got the timeout.  This is *probably*
		 * because the lwp is blocked.
		 */
		db_printf(2, "lwp_wait: lwp %D is not stopped", ps->pr_who);
		if (ioctl(lwpfd, PIOCSTOP, ps) != 0) {
			if (errno != EINTR)
				error("Unable to get pstatus for blocked lwp");
		}
		ps->pr_why = PR_SIGNALLED;
		ps->pr_what = SIGTRAP;
		db_printf(2, "lwp_wait: pr_flags = 0x%X in blocked lwp",
			ps->pr_flags);
		if (ps->pr_flags & PR_ASLEEP) {
			printf("lwp %d is asleep in syscall %d\n", ps->pr_who,
				ps->pr_syscall);
		} else {
			/* Hard to say why, but we need to say something. */
			printf(" lwp %d appears to be hanged\n", ps->pr_who);
		}
	}

	if (ps->pr_why == PR_SIGNALLED) {
		sig = ps->pr_what;
	}
	if (ps->pr_why == PR_SYSENTRY  && 
	    (ps->pr_what == SYS_exec || ps->pr_what == SYS_execve)) {
		(void) ioctl(lwpfd, PIOCRUN, 0);
		goto startover;
	}
	if (ps->pr_why == PR_SYSEXIT  && 
	    (ps->pr_what == SYS_exec || ps->pr_what == SYS_execve) &&
	    !(ps->pr_reg[R_PS] & ERRBIT)) {
		sig = SIGTRAP;
	}
	/* simulate normal return from wait(2) */
	if (stat_loc != 0)
		*stat_loc = (sig << 8) | WSTOPFLG;

	/* This little weirdness is supposed to move the state of this lwp
	 * from PR_SIGNALLED to PR_REQUESTED.  Leaving the lwp in the former
	 * state confuses /proc when next we run the whole process.  BUT
	 * /proc gets even more confused when a run doesn't have a
	 * complimentary stop, so throw in one of those, too!
	 */
	(void) memset((char *)&prrun, 0, sizeof(prrun));
	prrun.pr_flags = PRCSIG|PRSTOP;
	(void) ioctl(lwpfd, PIOCRUN, &prrun);
	(void) ioctl(lwpfd, PIOCWSTOP, 0);

	CloseLwp();		/* can't afford to hang on to an fd */
	stopped = TRUE;
	setregs = FALSE;
	return 0;
}

int
proc_wait(pid, stat_loc)
register pid_t pid;
int * stat_loc;
{
	register prstatus_t * ps = &Prstatus;
	register int fd;
	register int sig;
	int status;

	/* lwpfd != -1 iff we're single stepping.  In that case, we
	 * are running the lwp and want to wait for that instead of
	 * for the process.
	 */
	if (lwpfd != -1 && lwp_wait(pid, stat_loc) == 0)
		return pid;

	/*
	 * If this is the first time for this pid,
	 * open the /proc/pid file.  If all else fails,
	 * just give back whatever waitpid() has to offer.
	 */
	if ((process_id == 0 && FirstOpen(pid) < 0)
	 || process_id != pid)
		return waitpid(pid, stat_loc, WUNTRACED);

	db_printf(2, "proc_wait: waiting for process %D", pid);

	for (;;) {	/* loop on unsuccessful exec()s */

		int had_eagain = 0;

		while (ioctl(pfd, PIOCWSTOP, ps) != 0) {
			if (errno == EINTR) {
				(void) ioctl(pfd, PIOCSTOP, 0);
			} else if (errno == EAGAIN) {
				if (had_eagain == 0)
					had_eagain = 1;
				else {
					CloseProc();
					return -1;
				}
				if (GrabProc(pid) < 0)
					return -1;
			} else {
				CloseProc();
				/*
				 * If we are not the process's parent,
				 * waitpid() will fail.  Manufacture
				 * a normal exit code in this case.
				 */
				if (waitpid(pid, stat_loc, WUNTRACED) != pid
				 && stat_loc != 0)
					*stat_loc = 0;
				return pid;
			}
		}
		if (ps->pr_why == PR_SIGNALLED) {
			sig = ps->pr_what;
			break;
		}
		if (ps->pr_why == PR_REQUESTED) {	/* ATTACH */
			sig = SIGTRAP;
			break;
		}
		if (ps->pr_why == PR_SYSENTRY
		 && (ps->pr_what == SYS_exec || ps->pr_what == SYS_execve)) {
			had_eagain = 0;
		} else if (ps->pr_why == PR_SYSEXIT
		 && (ps->pr_what == SYS_exec || ps->pr_what == SYS_execve)
		 && !(ps->pr_reg[R_PS] & ERRBIT)) {
			sig = SIGTRAP;
			break;
		}
		(void) ioctl(pfd, PIOCRUN, 0);
	}

	stopped = TRUE;
	setregs = FALSE;

	/* simulate normal return from wait(2) */
	status = (sig << 8) | WSTOPFLG;
	db_printf(2, "proc_wait: returning status 0x%04x for process %D",
		status, pid);
	if (stat_loc != 0)
		*stat_loc = status;
	return pid;
}

void
enumerate_lwps(pid)
int pid;
{
	id_t *lwpid_list;
	int fd;
	int i;

	/* Special-case the mundane since it's so much simpler. */
	if (Prstatus.pr_nlwp == 1) {
		printf("lwpid %d is the only lwp in process %d.\n",
			Prstatus.pr_who, pid);
		return;
	}
	/* Get space for the lwpid list. */
	if ((lwpid_list = (id_t *) malloc((int) (Prstatus.pr_nlwp + 1) *
		sizeof(id_t))) == NULL)
		error("No memory for lwpid list");

	if (ioctl(pfd, PIOCLWPIDS, lwpid_list) == -1)
		error("Can't read proc's lwpid list");

	printf("lwpids ");
	for (i = 0; i < (int) Prstatus.pr_nlwp; i++) {
		if (i == 0)
			printf("%d", lwpid_list[0]);
		else if (i != Prstatus.pr_nlwp - 1)
			printf(", %d", lwpid_list[i]);
		else
			printf(" and %d", lwpid_list[i]);
	}
	free((char *) lwpid_list);
	printf(" are in process %d\n", pid);
	return;
	
}

void
set_lwp(lwpid, pid)
id_t lwpid;
pid_t pid;
{
	int lfd;
	int i;
	prstatus_t *ps = &Prstatus;
	extern addr_t usernpc;

	if (lwpid == (int) ps->pr_who)
		return;		/* We've all ready got this one. */

	/* Stop the process.  Too much can go wrong otherwise. */
	if (ioctl(pfd, PIOCSTOP, 0) != 0)
		error("Unable to stop process");

	if ((lfd = ioctl(pfd, PIOCOPENLWP, &lwpid)) < 0) {
		printf("lwpid %d was not found in proc %d\n", lwpid, pid);
		return;
	}

	i = ioctl(lfd, PIOCSTATUS, ps);
	(void) close(lfd);
	if (i == -1)
		error("Can't get lwp's status");

	/* Pick up this lwp's registers. */
	(void) core_to_regs();
	userpc = (addr_t)readreg(Reg_PC);
#if sparc
	usernpc = (addr_t)readreg(Reg_NPC);
#endif
	printf("lwp %d: ", ps->pr_who);
	(void) print_dis(Reg_PC);
	return;
}
