#ident	"@(#)pcontrol.h	1.2	95/06/26 SMI"

/* include file for process management */

/* requires:
 *	<stdio.h>
 *	<signal.h>
 *	<sys/types.h>
 *	<sys/fault.h>
 *	<sys/syscall.h>
 */
#include <sys/procfs.h>

#if i386
#	include <sys/regset.h>
#	include <sys/psw.h>	/* for PS_C definition */
#	define	R_R0	EAX
#	define	R_R1	EDX
#	define	R_PC	EIP
#	define	R_PS	EFL
#	define	R_SP	UESP
#endif


#if sparc
#	include <sys/psw.h>	/* for PSR_C definition */
#	define	NGREG	NPRGREG
	typedef	prgreg_t	greg_t;
	typedef	prgregset_t	gregset_t;
	typedef	prfpregset_t	fpregset_t;
#endif

#define	CONST	const
#define	VOID	void

#define	TRUE	1
#define	FALSE	0

/* definition of the process (program) table */
typedef struct {
	char	cntrl;		/* if TRUE then controlled process */
	char	child;		/* TRUE :: process created by fork() */
	char	state;		/* state of the process, see flags below */
	char	sig;		/* if dead, signal which caused it */
	char	rc;		/* exit code if process terminated normally */
	char	jsig;		/* non-zero: process was job-stopped */
	int	pfd;		/* /proc/<upid> filedescriptor */
	int	lwpfd;		/* specific lwp filedescriptor */
	pid_t	upid;		/* UNIX process ID */
	prstatus_t why;		/* from /proc -- status values when stopped */
	prpsinfo_t psinfo;	/* from /proc -- ps info on grab */
	long	sysaddr;	/* address of most recent syscall instruction */
	sigset_t sigmask;	/* signals which stop the process */
	fltset_t faultmask;	/* faults which stop the process */
	sysset_t sysentry;	/* system calls which stop process on entry */
	sysset_t sysexit;	/* system calls which stop process on exit */
	char	setsig;		/* set signal mask before continuing */
	char	sethold;	/* set signal hold mask before continuing */
	char	setfault;	/* set fault mask before continuing */
	char	setentry;	/* set sysentry mask before continuing */
	char	setexit;	/* set sysexit mask before continuing */
	char	setregs;	/* set registers before continuing */
} process_t;

/* shorthand for register array */
#define	REG	why.pr_reg

/* state values */
#define	PS_NULL	0	/* no process in this table entry */
#define	PS_RUN	1	/* process running */
#define	PS_STEP	2	/* process running single stepped */
#define	PS_STOP	3	/* process stopped */
#define	PS_LOST	4	/* process lost to control (EAGAIN) */
#define	PS_DEAD	5	/* process terminated */

/* Error returns from Pgrab() */
#define	G_NOPROC	(-1)	/* No such process */
#define	G_ZOMB		(-2)	/* Zombie process */
#define	G_PERM		(-3)	/* No permission */
#define	G_BUSY		(-4)	/* Another process has control */
#define	G_SYS		(-5)	/* System process */
#define	G_SELF		(-6)	/* Process is self */
#define	G_STRANGE	(-7)	/* Unanticipated error, perror() was called */
#define	G_INTR		(-8)	/* Interrupt received while grabbing */

/* machine-specific stuff */

#if u3b
typedef	unsigned short	syscall_t;	/* holds a syscall instruction */
#define	SYSCALL	0xd900	/* value of syscall (OST) instruction */
#define	FRMSZ	13	/* frame size, in words */
#endif

#if u3b2 || u3b15
typedef	unsigned short	syscall_t;	/* holds a syscall instruction */
#define	SYSCALL	0x3061	/* value of syscall (GATE) instruction */
#define	FRMSZ	9	/* frame size, in words */
#define	ERRBIT	0x40000	/* bit in psw indicating syscall error */
#endif

#if mc68k
typedef	unsigned short	syscall_t;	/* holds a syscall instruction */
#define	SYSCALL	0x4e40	/* value of syscall (trap 0) instruction */
#define	ERRBIT	0x1	/* bit in psw indicating syscall error */
#endif

#if i386
typedef unsigned char syscall_t[7];	/* holds a syscall instruction */
#define	SYSCALL	0x9a	/* syscall (lcall) instruction */
#define	ERRBIT	PS_C	/* bit in psw indicating syscall error */
#endif

#if sparc
typedef	unsigned long	syscall_t;	/* holds a syscall instruction */
#define	OSYSCALL 0x91d02000	/* value of old syscall (ta 0) instruction */
#define	SYSCALL	0x91d02008	/* value of syscall (ta 8) instruction */
#define	ERRBIT	PSR_C		/* bit in psw indicating syscall error */
#endif

struct	argdes	{	/* argument descriptor for system call (Psyscall) */
	int	value;		/* value of argument given to system call */
	char *	object;		/* pointer to object in controlling process */
	char	type;		/* AT_BYVAL, AT_BYREF */
	char	inout;		/* AI_INPUT, AI_OUTPUT, AI_INOUT */
	short	len;		/* if AT_BYREF, length of object in bytes */
};

struct	sysret	{	/* return values from system call (Psyscall) */
	int	errno;		/* syscall error number */
	greg_t	r0;		/* %r0 from system call */
	greg_t	r1;		/* %r1 from system call */
};

/* values for type */
#define	AT_BYVAL	0
#define	AT_BYREF	1

/* values for inout */
#define	AI_INPUT	0
#define	AI_OUTPUT	1
#define	AI_INOUT	2

/* maximum number of syscall arguments */
#define	MAXARGS		8

/* maximum size in bytes of a BYREF argument */
#define	MAXARGL		(4*1024)


/* external data used by the package */
extern char * procdir;		/* "/proc" */

/*
 * Function prototypes for routines in the process control package.
 */
extern	int	Pcreate( process_t * , char ** );
extern	int	Pgrab( process_t * , pid_t , int );
extern	int	Pchoose( process_t * );
extern	void	Punchoose( process_t * );
extern	int	Preopen( process_t * , int );
extern	int	Prelease( process_t * );
extern	int	Phang( process_t * );
extern	int	Pwait( process_t * );
extern	int	Pstop( process_t * );
extern	int	Pstatus( process_t * , int , unsigned );
extern	int	Pgetsysnum( process_t * );
extern	int	Psetsysnum( process_t * , int );
extern	int	Pgetregs( process_t * );
extern	int	Pgetareg( process_t * , int );
extern	int	Pputregs( process_t * );
extern	int	Pputareg( process_t * , int );
extern	int	Psetrun( process_t * , int , int );
extern	int	Pstart( process_t * , int );
extern	int	Pterm( process_t * );
extern	int	Pread( process_t * , long , char * , int );
extern	int	Pwrite( process_t * , long , const char * , int );
extern	int	Psignal( process_t * , int , int );
extern	int	Pfault( process_t * , int , int );
extern	int	Psysentry( process_t * , int , int );
extern	int	Psysexit( process_t * , int , int );
extern	struct sysret	Psyscall( process_t * , int , int , struct argdes * );
extern	int	Ioctl( int , int , int );

/*
 * Function prototypes for system calls forced on the victim process.
 */
struct sigaction;
struct itimerval;
struct flock;
struct stat;
struct rlimit;

extern	int	propen( process_t *, char *, int, int );
extern	int	prclose( process_t *, int );
extern	caddr_t	prmmap( process_t *, caddr_t, size_t, int, int, int, off_t );
extern	int	prmunmap( process_t *, caddr_t, size_t );
extern	int	prsigaction( process_t *, int, struct sigaction *,
			struct sigaction *);
extern	int	prsetitimer( process_t *, int, struct itimerval *,
			struct itimerval *);
extern	int	prfcntl(process_t *, int, int, struct flock *);
extern	int	prfstat(process_t *, int, struct stat *);
extern	int	prgetrlimit(process_t *, int, struct rlimit *);

/*
 * Miscellaneous functions.
 */
extern	void	msleep(unsigned int);
extern	int	scantext(process_t *);
extern	char *	fltname(int);
extern	char *	signame(int);
extern	char *	sysname(int);
extern	int	isprocdir(const char *);
