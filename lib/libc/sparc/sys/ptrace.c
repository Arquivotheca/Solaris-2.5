/*
 * ptrace(2) interface built on top of proc(4).
 */

#ident	"@(#)ptrace.c	1.18	92/09/22 SMI"

#ifdef __STDC__
	#pragma weak ptrace = _ptrace
#endif

#include "synonyms.h"
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/psw.h>
#include <sys/user.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

static mutex_t pt_lock = DEFAULTMUTEX;

#define	TRUE	1
#define	FALSE	0

/*
 * All my children...
 */
typedef struct cstatus {
	struct cstatus * next;		/* linked list		*/
	pid_t		pid;		/* process-id		*/
	short		pfd;		/* /proc filedescriptor	*/
	short		flags;		/* see below		*/
	prstatus_t	pstatus;	/* from /proc		*/
	user_t		user;		/* manufactured u-block	*/
} cstatus_t;

/* flags */
#define	CS_SETREGS	0x01		/* set registers on run */
#define	CS_PSARGS	0x02		/* u_psargs[] has been fetched */
#define	CS_SIGNAL	0x04		/* u_signal[] has been fetched */

#define	NULLCP	((cstatus_t *)0)

static cstatus_t * childp = NULLCP;

/* fake u-block offsets */
#define	UP		((user_t *)NULL)
#define	U_REG		((int)(&UP->u_reg[0]))
#define	U_AR0		((int)(&UP->u_ar0))
#define	U_PSARGS	((int)(&UP->u_psargs[0]))
#define	U_SIGNAL	((int)(&UP->u_signal[0]))
#define	U_CODE		((int)(&UP->u_code))
#define	U_ADDR		((int)(&UP->u_addr))
#define	U_END		((int)sizeof(user_t))
#define	REGADDR		0xffff0000	/* arbitrary kernel address for u_ar0 */

#if defined(__STDC__)
	/* external routines defined in this module */
	extern	int	ptrace(int, pid_t, int, int);
	/* static routines defined in this module */
	static	cstatus_t *	FindProc(pid_t);
	static	void	CheckAllProcs();
	static	int	OpenProc(pid_t, int);
	static	cstatus_t *	GrabProc(pid_t);
	static	int	Reopen(cstatus_t *);
	static	void	ReleaseProc(cstatus_t *);
	static	int	ProcUpdate(cstatus_t *);
	static	void	MakeUser(cstatus_t *);
	static	void	GetPsargs(cstatus_t *);
	static	void	GetSignal(cstatus_t *);
#else
	/* external routines defined in this module */
	extern	int	ptrace();
	/* static routines defined in this module */
	static	cstatus_t *	FindProc();
	static	void	CheckAllProcs();
	static	int	OpenProc();
	static	cstatus_t *	GrabProc();
	static	int	Reopen();
	static	void	ReleaseProc();
	static	int	ProcUpdate();
	static	void	MakeUser();
	static	void	GetPsargs();
	static	void	GetSignal();
#endif

#if PTRACE_DEBUG
/* for debugging */
static char *
map(request)
int request;
{
	static char name[20];

	switch (request) {
	case 0:	return "PTRACE_TRACEME";
	case 1:	return "PTRACE_PEEKTEXT";
	case 2:	return "PTRACE_PEEKDATA";
	case 3:	return "PTRACE_PEEKUSER";
	case 4:	return "PTRACE_POKETEXT";
	case 5:	return "PTRACE_POKEDATA";
	case 6:	return "PTRACE_POKEUSER";
	case 7:	return "PTRACE_CONT";
	case 8:	return "PTRACE_KILL";
	case 9:	return "PTRACE_SINGLESTEP";
	}
	(void)sprintf(name, "%d", request);
	return name;
}
#endif

int
ptrace(request, pid, addr, data)
	register int request;
	register pid_t pid;
	register int addr;
	int data;
{
	register prstatus_t * ps;
	register cstatus_t * cp;
	register int i;
	register unsigned xaddr;
	siginfo_t siginfo;

	mutex_lock(&pt_lock);

#if PTRACE_DEBUG
	fprintf(stderr, " ptrace(%s, 0x%X, 0x%X, 0x%X)\n",
		map(request), pid, addr, data);
#endif

	if (request == 0) {	/* PTRACE_TRACEME, executed by traced process */
		/*
		 * Set stop-on-all-signals and nothing else.
		 * Set ptrace-compatible flag.
		 * Turn off inherit-on-fork flag (grandchildren run away).
		 */
		sigset_t signals;
		fltset_t faults;
		sysset_t syscalls;
		long rflags;
		long sflags;
		register int fd;	/* /proc filedescriptor */

		prfillset(&signals);
		premptyset(&faults);
		premptyset(&syscalls);
		rflags = PR_RLC|PR_FORK;
		sflags = PR_PCOMPAT;

		if ((fd = OpenProc(getpid(), O_RDWR)) < 0
		 || ioctl(fd, PIOCSTRACE, (int)&signals) != 0
		 || ioctl(fd, PIOCSFAULT, (int)&faults) != 0
		 || ioctl(fd, PIOCSENTRY, (int)&syscalls) != 0
		 || ioctl(fd, PIOCSEXIT, (int)&syscalls) != 0
		 || ioctl(fd, PIOCRESET, &rflags) != 0
		 || ioctl(fd, PIOCSET, &sflags) != 0
		 || close(fd) != 0)
			exit(255);

		mutex_unlock(&pt_lock);
		return 0;
	}

again:
	errno = 0;

	/* find the cstatus structure corresponding to pid */
	if ((cp = GrabProc(pid)) == NULLCP)
		goto esrch;

	ps = &cp->pstatus;
	if (!(ps->pr_flags & PR_ISTOP)) {
		if (ProcUpdate(cp) != 0) {
			ReleaseProc(cp);
			goto esrch;
		}
		if (!(ps->pr_flags & PR_ISTOP))
			goto esrch;
	}

	/*
	 * Process the request.
	 */
	errno = 0;
	switch (request) {
	case 1:		/* PTRACE_PEEKTEXT */
	case 2:		/* PTRACE_PEEKDATA */
		if (addr & 03)
			goto eio;
		if (lseek(cp->pfd, (long)addr, 0) == (long)addr
		 && read(cp->pfd, (char *)&data, sizeof(data)) == sizeof(data)) {
			mutex_unlock(&pt_lock);
			return data;
		}
		goto eio;

	case 3:		/* PTRACE_PEEKUSER */
		if (addr & 03)
			goto eio;
		xaddr = addr;
		if (xaddr >= REGADDR && xaddr < REGADDR+sizeof(gregset_t))
			xaddr -= REGADDR-U_REG;
		if (xaddr >= U_PSARGS && xaddr < U_PSARGS+sizeof(UP->u_psargs))
			GetPsargs(cp);
		if (xaddr >= U_SIGNAL && xaddr < U_SIGNAL+sizeof(UP->u_signal))
			GetSignal(cp);
		if (xaddr >= 0 && xaddr < U_END) {
			mutex_unlock(&pt_lock);
			return *((int *)((caddr_t)(&cp->user) + xaddr));
		}
		goto eio;

	case 4:		/* PTRACE_POKETEXT */
	case 5:		/* PTRACE_POKEDATA */
		if (addr & 03)
			goto eio;
		xaddr = addr;
		if (xaddr >= (unsigned)cp->user.u_reg[REG_SP]
		 && xaddr < (unsigned)cp->user.u_reg[REG_SP]+16*sizeof(int))
			cp->flags |= CS_SETREGS;
		if (lseek(cp->pfd, (long)addr, 0) == (long)addr
		 && write(cp->pfd, (char *)&data, sizeof(data)) == sizeof(data)) {
			mutex_unlock(&pt_lock);
			return data;
		}
		goto eio;

	case 6:		/* PTRACE_POKEUSER */
		if (addr & 03)
			goto eio;
		xaddr = addr;
		if (xaddr >= REGADDR && xaddr < REGADDR+sizeof(gregset_t))
			xaddr -= REGADDR-U_REG;
		if (xaddr >= U_REG && xaddr < U_REG+sizeof(gregset_t)) {
			register int rx = (xaddr-U_REG)/sizeof(greg_t);
			if (rx == REG_PS)
				data = (cp->user.u_reg[REG_PS] &
				    ~PSL_USERMASK) | (data & PSL_USERMASK);
			else if (rx == REG_SP || rx == REG_PC || rx == REG_nPC)
				data &= ~03;
			cp->user.u_reg[rx] = data;
			cp->flags |= CS_SETREGS;
			mutex_unlock(&pt_lock);
			return data;
		}
		goto eio;

	case 7:		/* PTRACE_CONT */
	case 9:		/* PTRACE_SINGLESTEP */
	    {
		prrun_t prrun;		/* must be initialized to zero */

		(void) memset((char *)&prrun, 0, sizeof(prrun));

		if (request == 9)	/* PTRACE_SINGLESTEP */
			prrun.pr_flags |= PRSTEP;
		if (addr != 1		/* new virtual address */
		 && (addr & ~03) != cp->user.u_reg[REG_PC]) {
			prrun.pr_vaddr = (caddr_t)(addr & ~03);
			prrun.pr_flags |= PRSVADDR;
		}
		if (data == 0)		/* make data the current signal */
			prrun.pr_flags |= PRCSIG;
		else if (data != ps->pr_cursig) {
			(void) memset((char *)&siginfo, 0, sizeof(siginfo));
			siginfo.si_signo = data;
			if (ioctl(cp->pfd, PIOCSSIG, (int)&siginfo) != 0)
				goto eio;
		}
		if (cp->flags & CS_SETREGS) {
			ps->pr_reg[R_PSR] = cp->user.u_reg[REG_PSR];
			ps->pr_reg[R_PC]  = cp->user.u_reg[REG_PC];
			ps->pr_reg[R_nPC] = cp->user.u_reg[REG_nPC];
			ps->pr_reg[R_Y]   = cp->user.u_reg[REG_Y];
			ps->pr_reg[R_G1]  = cp->user.u_reg[REG_G1];
			ps->pr_reg[R_G2]  = cp->user.u_reg[REG_G2];
			ps->pr_reg[R_G3]  = cp->user.u_reg[REG_G3];
			ps->pr_reg[R_G4]  = cp->user.u_reg[REG_G4];
			ps->pr_reg[R_G5]  = cp->user.u_reg[REG_G5];
			ps->pr_reg[R_G6]  = cp->user.u_reg[REG_G6];
			ps->pr_reg[R_G7]  = cp->user.u_reg[REG_G7];
			ps->pr_reg[R_O0]  = cp->user.u_reg[REG_O0];
			ps->pr_reg[R_O1]  = cp->user.u_reg[REG_O1];
			ps->pr_reg[R_O2]  = cp->user.u_reg[REG_O2];
			ps->pr_reg[R_O3]  = cp->user.u_reg[REG_O3];
			ps->pr_reg[R_O4]  = cp->user.u_reg[REG_O4];
			ps->pr_reg[R_O5]  = cp->user.u_reg[REG_O5];
			ps->pr_reg[R_O6]  = cp->user.u_reg[REG_O6];
			ps->pr_reg[R_O7]  = cp->user.u_reg[REG_O7];
			(void)lseek(cp->pfd, (long)cp->user.u_reg[REG_SP], 0);
			(void)read(cp->pfd, (char *)&ps->pr_reg[R_L0],
				16*sizeof(int));
			if (ioctl(cp->pfd, PIOCSREG, (int)ps->pr_reg) != 0) {
				if (errno == ENOENT) {
					/* current signal must have killed it */
					ReleaseProc(cp);
					mutex_unlock(&pt_lock);
					return data;
				}
				goto tryagain;
			}
		}
		if (ioctl(cp->pfd,PIOCRUN,prrun.pr_flags?(int)&prrun:0) != 0) {
			if (errno == ENOENT) {
				/* current signal must have killed it */
				ReleaseProc(cp);
				mutex_unlock(&pt_lock);
				return data;
			}
			goto tryagain;
		}
		(void) memset((char *)ps, 0, sizeof(prstatus_t));
		cp->flags = 0;
		mutex_unlock(&pt_lock);
		return data;
	    }

	case 8:		/* PTRACE_KILL */
		/* overkill? */
		(void) memset((char *)&siginfo, 0, sizeof(siginfo));
		siginfo.si_signo = SIGKILL;
		(void) ioctl(cp->pfd, PIOCSSIG, (int)&siginfo);
		(void) kill(pid, SIGKILL);
		ReleaseProc(cp);
		mutex_unlock(&pt_lock);
		return 0;

	default:
		goto eio;
	}

tryagain:
	if (Reopen(cp))
		goto again;
	ReleaseProc(cp);
eio:
	errno = EIO;
	mutex_unlock(&pt_lock);
	return -1;
esrch:
	errno = ESRCH;
	mutex_unlock(&pt_lock);
	return -1;
}

/*
 * Find the cstatus structure corresponding to pid.
 */
static cstatus_t *
FindProc(pid)
	register pid_t pid;
{
	register cstatus_t * cp;

	for (cp = childp; cp != NULLCP; cp = cp->next)
		if (cp->pid == pid)
			break;

	return cp;
}

/*
 * Check every proc for existence, release those that are gone.
 * Be careful about the linked list; ReleaseProc() changes it.
 */
static void
CheckAllProcs()
{
	register cstatus_t * cp = childp;

	while (cp != NULLCP) {
		register cstatus_t * next = cp->next;

		if (ProcUpdate(cp) != 0)
			ReleaseProc(cp);
		cp = next;
	}
}

/*
 * Open the /proc/<pid> file.
 */
static int
OpenProc(pid, oflags)
	register pid_t pid;
	register int oflags;
{
	char procname[24];		/* /proc/nnnnn */
	register char * s;
	register int fd;

	if (pid <= 0)
		return -1;

	(void) strcpy(procname, "/proc/00000");
	s = procname + strlen(procname);
	while (pid) {
		*--s = pid%10 + '0';
		pid /= 10;
	}
	fd = open(procname, oflags, 0);

	/*
	 * Make sure filedescriptor is not 0, 1, or 2
	 * to avoid interference with standard I/O.
	 */
	if (0 <= fd && fd <= 2) {
		register int dfd = fcntl(fd, F_DUPFD, 3);
		(void) close(fd);
		fd = dfd;
	}

	/*
	 * Mark filedescriptor close-on-exec.
	 * Should also be close-on-return-from-fork-in-child.
	 */
	if (fd >= 0)
		(void) fcntl(fd, F_SETFD, 1);

	return fd;
}

/*
 * Take control of a child process.
 */
static cstatus_t *
GrabProc(pid)
	register pid_t pid;
{
	register cstatus_t * cp;
	register int fd;
	register pid_t ppid;
	long sflags;
	prstatus_t prstatus;

	if ((cp = FindProc(pid)) != NULLCP)	/* already grabbed */
		return cp;

	CheckAllProcs();	/* clean up before grabbing new process */

	ppid = getpid();
	while ((fd = OpenProc(pid, O_RDWR|O_EXCL)) >= 0) {
		errno = 0;
		sflags = PR_RLC;

		if (ioctl(fd, PIOCSTATUS, (int)&prstatus) == 0
		 && prstatus.pr_ppid == ppid
		 && (prstatus.pr_flags & PR_PCOMPAT)
		 && ioctl(fd, PIOCSET, &sflags) == 0) {
			cp = (cstatus_t *)malloc(sizeof(cstatus_t));
			if (cp == NULLCP) {
				(void) close(fd);
				break;
			}
			(void) memset((char *)cp, 0, sizeof(cstatus_t));
			cp->next = childp;
			childp = cp;
			cp->pid = pid;
			cp->pfd = fd;
			cp->pstatus = prstatus;
			MakeUser(cp);
			break;
		}

		(void) close(fd);
		if (errno != EAGAIN)
			break;
	}

	return cp;
}

/*
 * Attempt recovery from security violation.
 * EAGAIN indicates process is stopped on exit from exec() and is
 * waiting for us either to reopen the /proc file (if we have
 * permission) or to close the file and let the process run.
 * Return TRUE if we successfully reopened the /proc file, else FALSE.
 */
static int
Reopen(cp)
	register cstatus_t * cp;
{
	register int pid = cp->pid;
	register int fd;

	while (errno == EAGAIN && (fd = OpenProc(pid, O_RDWR|O_EXCL)) >= 0) {
		(void) close(cp->pfd);
		cp->pfd = fd;
		if (ioctl(fd, PIOCSTATUS, (int)&cp->pstatus) == 0) {
			MakeUser(cp);
			return TRUE;
		}
	}

	/* lost control; kill it */
	(void) kill(cp->pid, SIGKILL);
	return FALSE;
}

/*
 * Close the /proc/<pid> file, if open.
 * Deallocate the memory used by the cstatus_t structure.
 */
static void
ReleaseProc(cp)
	register cstatus_t * cp;
{
	if (cp->pfd > 0)
		(void) close(cp->pfd);

	if (childp == cp)
		childp = cp->next;
	else {
		register cstatus_t * pcp;

		for (pcp = childp; pcp != NULLCP; pcp = pcp->next) {
			if (pcp->next == cp) {
				pcp->next = cp->next;
				break;
			}
		}
	}

	free((char *)cp);
}

/*
 * Update process information from /proc.
 * Return 0 on success, -1 on failure.
 */
static int
ProcUpdate(cp)
	register cstatus_t * cp;
{
	register prstatus_t * ps = &cp->pstatus;

	if (cp->flags & CS_SETREGS) {
		ps->pr_reg[R_PSR] = cp->user.u_reg[REG_PSR];
		ps->pr_reg[R_PC]  = cp->user.u_reg[REG_PC];
		ps->pr_reg[R_nPC] = cp->user.u_reg[REG_nPC];
		ps->pr_reg[R_Y]   = cp->user.u_reg[REG_Y];
		ps->pr_reg[R_G1]  = cp->user.u_reg[REG_G1];
		ps->pr_reg[R_G2]  = cp->user.u_reg[REG_G2];
		ps->pr_reg[R_G3]  = cp->user.u_reg[REG_G3];
		ps->pr_reg[R_G4]  = cp->user.u_reg[REG_G4];
		ps->pr_reg[R_G5]  = cp->user.u_reg[REG_G5];
		ps->pr_reg[R_G6]  = cp->user.u_reg[REG_G6];
		ps->pr_reg[R_G7]  = cp->user.u_reg[REG_G7];
		ps->pr_reg[R_O0]  = cp->user.u_reg[REG_O0];
		ps->pr_reg[R_O1]  = cp->user.u_reg[REG_O1];
		ps->pr_reg[R_O2]  = cp->user.u_reg[REG_O2];
		ps->pr_reg[R_O3]  = cp->user.u_reg[REG_O3];
		ps->pr_reg[R_O4]  = cp->user.u_reg[REG_O4];
		ps->pr_reg[R_O5]  = cp->user.u_reg[REG_O5];
		ps->pr_reg[R_O6]  = cp->user.u_reg[REG_O6];
		ps->pr_reg[R_O7]  = cp->user.u_reg[REG_O7];
		(void)lseek(cp->pfd, (long)cp->user.u_reg[REG_SP], 0);
		(void)read(cp->pfd, (char *)&ps->pr_reg[R_L0],
			16*sizeof(int));
		(void) ioctl(cp->pfd, PIOCSREG, (int)ps->pr_reg);
		cp->flags &= ~CS_SETREGS;
	}

	while (ioctl(cp->pfd, PIOCSTATUS, (int)ps) != 0) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN) {
			/* attempt to regain control */
			if (Reopen(cp))
				break;
		}
		return -1;
	}

	if (ps->pr_flags & PR_ISTOP)
		MakeUser(cp);
	else
		(void) memset((char *)ps, 0, sizeof(prstatus_t));

	return 0;
}

/*
 * Manufacture the contents of the fake u-block.
 */
static void
MakeUser(cp)
	register cstatus_t * cp;
{
	register prstatus_t * ps = &cp->pstatus;

	cp->user.u_reg[REG_PSR] = ps->pr_reg[R_PSR];
	cp->user.u_reg[REG_PC]  = ps->pr_reg[R_PC];
	cp->user.u_reg[REG_nPC] = ps->pr_reg[R_nPC];
	cp->user.u_reg[REG_Y]   = ps->pr_reg[R_Y];
	cp->user.u_reg[REG_G1]  = ps->pr_reg[R_G1];
	cp->user.u_reg[REG_G2]  = ps->pr_reg[R_G2];
	cp->user.u_reg[REG_G3]  = ps->pr_reg[R_G3];
	cp->user.u_reg[REG_G4]  = ps->pr_reg[R_G4];
	cp->user.u_reg[REG_G5]  = ps->pr_reg[R_G5];
	cp->user.u_reg[REG_G6]  = ps->pr_reg[R_G6];
	cp->user.u_reg[REG_G7]  = ps->pr_reg[R_G7];
	cp->user.u_reg[REG_O0]  = ps->pr_reg[R_O0];
	cp->user.u_reg[REG_O1]  = ps->pr_reg[R_O1];
	cp->user.u_reg[REG_O2]  = ps->pr_reg[R_O2];
	cp->user.u_reg[REG_O3]  = ps->pr_reg[R_O3];
	cp->user.u_reg[REG_O4]  = ps->pr_reg[R_O4];
	cp->user.u_reg[REG_O5]  = ps->pr_reg[R_O5];
	cp->user.u_reg[REG_O6]  = ps->pr_reg[R_O6];
	cp->user.u_reg[REG_O7]  = ps->pr_reg[R_O7];
	cp->user.u_ar0 = (greg_t *)REGADDR;
	cp->user.u_code = ps->pr_info.si_code;
	cp->user.u_addr = ps->pr_info.si_addr;
	cp->flags &= ~(CS_PSARGS|CS_SIGNAL);
}

/*
 * Fetch the contents of u_psargs[].
 */
static void
GetPsargs(cp)
	register cstatus_t * cp;
{
	prpsinfo_t psinfo;

	(void) ioctl(cp->pfd, PIOCPSINFO, (int)&psinfo);
	(void) memcpy(cp->user.u_psargs, psinfo.pr_psargs, PSARGSZ);
	cp->flags |= CS_PSARGS;
}

/*
 * Fetch the contents of u_signal[].
 */
static void
GetSignal(cp)
	register cstatus_t * cp;
{
	struct sigaction action[MAXSIG];
	register int i;

	(void) ioctl(cp->pfd, PIOCACTION, (int)&action[0]);
	for (i = 0; i < MAXSIG; i++)
		cp->user.u_signal[i] = action[i].sa_handler;
	cp->flags |= CS_SIGNAL;
}
