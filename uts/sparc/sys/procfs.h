/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_PROCFS_H
#define	_SYS_PROCFS_H

#pragma ident	"@(#)procfs.h	1.30	95/06/14 SMI"	/* from SVr4.0 1.24 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/fault.h>
#include <sys/syscall.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ioctl codes and system call interfaces for /proc.
 */

#define	PIOC		('q'<<8)
#define	PIOCSTATUS	(PIOC|1)	/* get process status */
#define	PIOCSTOP	(PIOC|2)	/* post STOP request and... */
#define	PIOCWSTOP	(PIOC|3)	/* wait for process to STOP */
#define	PIOCRUN		(PIOC|4)	/* make process runnable */
#define	PIOCGTRACE	(PIOC|5)	/* get traced signal set */
#define	PIOCSTRACE	(PIOC|6)	/* set traced signal set */
#define	PIOCSSIG	(PIOC|7)	/* set current signal */
#define	PIOCKILL	(PIOC|8)	/* send signal */
#define	PIOCUNKILL	(PIOC|9)	/* delete a signal */
#define	PIOCGHOLD	(PIOC|10)	/* get held signal set */
#define	PIOCSHOLD	(PIOC|11)	/* set held signal set */
#define	PIOCMAXSIG	(PIOC|12)	/* get max signal number */
#define	PIOCACTION	(PIOC|13)	/* get signal action structs */
#define	PIOCGFAULT	(PIOC|14)	/* get traced fault set */
#define	PIOCSFAULT	(PIOC|15)	/* set traced fault set */
#define	PIOCCFAULT	(PIOC|16)	/* clear current fault */
#define	PIOCGENTRY	(PIOC|17)	/* get syscall entry set */
#define	PIOCSENTRY	(PIOC|18)	/* set syscall entry set */
#define	PIOCGEXIT	(PIOC|19)	/* get syscall exit set */
#define	PIOCSEXIT	(PIOC|20)	/* set syscall exit set */

/*
 * These four are obsolete (replaced by PIOCSET/PIOCRESET).
 */
#define	PIOCSFORK	(PIOC|21)	/* set inherit-on-fork flag */
#define	PIOCRFORK	(PIOC|22)	/* reset inherit-on-fork flag */
#define	PIOCSRLC	(PIOC|23)	/* set run-on-last-close flag */
#define	PIOCRRLC	(PIOC|24)	/* reset run-on-last-close flag */

#define	PIOCGREG	(PIOC|25)	/* get general registers */
#define	PIOCSREG	(PIOC|26)	/* set general registers */
#define	PIOCGFPREG	(PIOC|27)	/* get floating-point registers */
#define	PIOCSFPREG	(PIOC|28)	/* set floating-point registers */
#define	PIOCNICE	(PIOC|29)	/* set nice priority */
#define	PIOCPSINFO	(PIOC|30)	/* get ps(1) information */
#define	PIOCNMAP	(PIOC|31)	/* get number of memory mappings */
#define	PIOCMAP		(PIOC|32)	/* get memory map information */
#define	PIOCOPENM	(PIOC|33)	/* open mapped object for reading */
#define	PIOCCRED	(PIOC|34)	/* get process credentials */
#define	PIOCGROUPS	(PIOC|35)	/* get supplementary groups */
#define	PIOCGETPR	(PIOC|36)	/* read struct proc */
#define	PIOCGETU	(PIOC|37)	/* read user area */

/*
 * These are new with SunOS5.0.
 */
#define	PIOCSET		(PIOC|38)	/* set process flags */
#define	PIOCRESET	(PIOC|39)	/* reset process flags */
/* Watchpoints not yet implemented in SunOS5.0 ---> */
#define	PIOCNWATCH	(PIOC|40)	/* get max number of watched areas */
#define	PIOCGWATCH	(PIOC|41)	/* get watched memory areas */
#define	PIOCSWATCH	(PIOC|42)	/* set/reset watched memory areas */
/* <--- Watchpoints not yet implemented in SunOS5.0 */
#define	PIOCUSAGE	(PIOC|43)	/* get resource usage */
#define	PIOCOPENPD	(PIOC|44)	/* get page data file descriptor */

/*
 * Lightweight process interfaces.
 */
#define	PIOCLWPIDS	(PIOC|45)	/* get lwp identifiers */
#define	PIOCOPENLWP	(PIOC|46)	/* get lwp file descriptor */
#define	PIOCLSTATUS	(PIOC|47)	/* get status of all lwps */
#define	PIOCLUSAGE	(PIOC|48)	/* get resource usage of all lwps */

/*
 * SVR4 run-time loader interfaces.
 */
#define	PIOCNAUXV	(PIOC|49)	/* get number of aux vector entries */
#define	PIOCAUXV	(PIOC|50)	/* get aux vector (see sys/auxv.h) */

/*
 * extra register state interfaces
 */
#define	PIOCGXREGSIZE	(PIOC|51)	/* get extra register state size */
#define	PIOCGXREG	(PIOC|52)	/* get extra register state */
#define	PIOCSXREG	(PIOC|53)	/* set extra register state */

/*
 * SPARC-specific interfaces.
 */
#define	PIOCGWIN	(PIOC|101)	/* get gwindows_t (see sys/reg.h) */

/*
 * General register access (sparc).
 * Don't confuse definitions here with definitions in <sys/reg.h>.
 */

#define	NPRGREG	38
typedef	int		prgreg_t;
typedef	prgreg_t	prgregset_t[NPRGREG];

#define	R_G0	0
#define	R_G1	1
#define	R_G2	2
#define	R_G3	3
#define	R_G4	4
#define	R_G5	5
#define	R_G6	6
#define	R_G7	7
#define	R_O0	8
#define	R_O1	9
#define	R_O2	10
#define	R_O3	11
#define	R_O4	12
#define	R_O5	13
#define	R_O6	14
#define	R_O7	15
#define	R_L0	16
#define	R_L1	17
#define	R_L2	18
#define	R_L3	19
#define	R_L4	20
#define	R_L5	21
#define	R_L6	22
#define	R_L7	23
#define	R_I0	24
#define	R_I1	25
#define	R_I2	26
#define	R_I3	27
#define	R_I4	28
#define	R_I5	29
#define	R_I6	30
#define	R_I7	31
#define	R_PSR	32
#define	R_PC	33
#define	R_nPC	34
#define	R_Y	35
#define	R_WIM	36
#define	R_TBR	37

/*
 * The following defines are for portability.
 */
#define	R_PS	R_PSR
#define	R_SP	R_O6
#define	R_FP	R_I6
#define	R_R0	R_O0
#define	R_R1	R_O1

/*
 * Floating-point register access (sparc FPU).
 * See <sys/reg.h> for details of interpretation.
 */

typedef struct prfpregset {
	union {				/* FPU floating point regs */
		u_long	pr_regs[32];		/* 32 singles */
		double	pr_dregs[16];		/* 16 doubles */
	} pr_fr;
	void *	pr_filler;
	u_long	pr_fsr;			/* FPU status register */
	u_char	pr_qcnt;		/* # of entries in saved FQ */
	u_char	pr_q_entrysize;		/* # of bytes per FQ entry */
	u_char	pr_en;			/* flag signifying fpu in use */
	u_long	pr_q[64];		/* contains the FQ array */
} prfpregset_t;

/*
 * Extra register access
 */

#define	XR_G0		0
#define	XR_G1		1
#define	XR_G2		2
#define	XR_G3		3
#define	XR_G4		4
#define	XR_G5		5
#define	XR_G6		6
#define	XR_G7		7
#define	NPRXGREG	8

#define	XR_O0		0
#define	XR_O1		1
#define	XR_O2		2
#define	XR_O3		3
#define	XR_O4		4
#define	XR_O5		5
#define	XR_O6		6
#define	XR_O7		7
#define	NPRXOREG	8

#define	NPRXFILLER	8

#define	XR_TYPE_V8P	1		/* interpret union as pr_v8p */

typedef struct prxregset {
	u_long		pr_type;		/* how to interpret union */
	u_long		pr_align;		/* alignment for the union */
	union {
	    struct pr_v8p {
		union {				/* extra FP registers */
			u_long		pr_regs[32];
			double		pr_dregs[16];
			long double	pr_qregs[8];
		} pr_xfr;
		u_long		pr_xfsr;	/* upper 32bits, FP state reg */
		u_long		pr_fprs;	/* FP registers state */
		u_long		pr_xg[NPRXGREG]; /* upper 32bits, G registers */
		u_long		pr_xo[NPRXOREG]; /* upper 32bits, O registers */
		longlong_t	pr_tstate;	/* TSTATE register */
		u_long		pr_filler[NPRXFILLER];
	    } pr_v8p;
	} pr_un;
} prxregset_t;

/* Holds one sparc instruction */

typedef	u_long	instr_t;

/* Process/lwp status structure */

#define	PRCLSZ		8	/* maximum size of scheduling class name */
#define	PRSYSARGS	8	/* maximum number of syscall arguments */

typedef struct prstatus {
	long	pr_flags;	/* Flags (see below) */
	short	pr_why;		/* Reason for process stop (if stopped) */
	short	pr_what;	/* More detailed reason */
	siginfo_t pr_info;	/* Info associated with signal or fault */
	short	pr_cursig;	/* Current signal */
	u_short	pr_nlwp;	/* Number of lwps in the process */
	sigset_t pr_sigpend;	/* Set of signals pending to the process */
	sigset_t pr_sighold;	/* Set of signals held (blocked) by the lwp */
	struct	sigaltstack pr_altstack; /* Alternate signal stack info */
	struct	sigaction pr_action; /* Signal action for current signal */
	pid_t	pr_pid;		/* Process id */
	pid_t	pr_ppid;	/* Parent process id */
	pid_t	pr_pgrp;	/* Process group id */
	pid_t	pr_sid;		/* Session id */
	timestruc_t pr_utime;	/* Process user cpu time */
	timestruc_t pr_stime;	/* Process system cpu time */
	timestruc_t pr_cutime;	/* Sum of children's user times */
	timestruc_t pr_cstime;	/* Sum of children's system times */
	char	pr_clname[PRCLSZ]; /* Scheduling class name */
	short	pr_syscall;	/* System call number (if in syscall) */
	short	pr_nsysarg;	/* Number of arguments to this syscall */
	long	pr_sysarg[PRSYSARGS]; /* Arguments to this syscall */
	id_t	pr_who;		/* Specific lwp identifier */
	sigset_t pr_lwppend;	/* Set of signals pending to the lwp */
	struct ucontext *pr_oldcontext; /* Address of previous ucontext */
	caddr_t	pr_brkbase;	/* Address of the process heap */
	u_long	pr_brksize;	/* Size of the process heap, in bytes */
	caddr_t	pr_stkbase;	/* Address of the process stack */
	u_long	pr_stksize;	/* Size of the process stack, in bytes */
	short	pr_processor;	/* processor which last ran this LWP */
	short	pr_bind;	/* processor LWP bound to or PBIND_NONE */
	long	pr_instr;	/* Current instruction */
	prgregset_t pr_reg;	/* General registers */
} prstatus_t;

/* pr_flags */

#define	PR_STOPPED	0x0001	/* lwp is stopped */
#define	PR_ISTOP	0x0002	/* lwp is stopped on an event of interest */
#define	PR_DSTOP	0x0004	/* lwp has a stop directive in effect */
#define	PR_ASLEEP	0x0008	/* lwp is sleeping in a system call */
#define	PR_FORK		0x0010	/* inherit-on-fork is in effect */
#define	PR_RLC		0x0020	/* run-on-last-close is in effect */
#define	PR_PTRACE	0x0040	/* obsolete, never set in SunOS5.0 */
#define	PR_PCINVAL	0x0080	/* contents of pr_instr undefined */
#define	PR_ISSYS	0x0100	/* system process */
#define	PR_STEP		0x0200	/* lwp has a single-step directive in effect */
#define	PR_KLC		0x0400	/* kill-on-last-close is in effect */
#define	PR_ASYNC	0x0800	/* asynchronous-stop is in effect */
#define	PR_PCOMPAT	0x1000	/* ptrace-compatibility mode is in effect */
#define	PR_MSACCT	0x2000	/* micro-state usage accounting is in effect */
#define	PR_BPTADJ	0x4000	/* breakpoint trap pc adjustment is in effect */
#define	PR_ASLWP	0x8000	/* this lwp is the aslwp */

/* Reasons for stopping */

#define	PR_REQUESTED	1
#define	PR_SIGNALLED	2
#define	PR_SYSENTRY	3
#define	PR_SYSEXIT	4
#define	PR_JOBCONTROL	5
#define	PR_FAULTED	6
#define	PR_SUSPENDED	7
#define	PR_CHECKPOINT	8

/* Information for the ps(1) command */

#define	PRFNSZ		16		/* max size of execed filename */
#define	PRARGSZ		80		/* Number of chars of arguments */

typedef struct prpsinfo {
	char	pr_state;	/* numeric process state (see pr_sname) */
	char	pr_sname;	/* printable character representing pr_state */
	char	pr_zomb;	/* !=0: process terminated but not waited for */
	char	pr_nice;	/* nice for cpu usage */
	u_long	pr_flag;	/* process flags */
	uid_t	pr_uid;		/* real user id */
	gid_t	pr_gid;		/* real group id */
	pid_t	pr_pid;		/* unique process id */
	pid_t	pr_ppid;	/* process id of parent */
	pid_t	pr_pgrp;	/* pid of process group leader */
	pid_t	pr_sid;		/* session id */
	caddr_t	pr_addr;	/* physical address of process */
	long	pr_size;	/* size of process image in pages */
	long	pr_rssize;	/* resident set size in pages */
	caddr_t	pr_wchan;	/* wait addr for sleeping process */
	timestruc_t pr_start;	/* process start time, sec+nsec since epoch */
	timestruc_t pr_time;	/* usr+sys cpu time for this process */
	long	pr_pri;		/* priority, high value is high priority */
	char	pr_oldpri;	/* pre-SVR4, low value is high priority */
	char	pr_cpu;		/* pre-SVR4, cpu usage for scheduling */
	o_dev_t	pr_ottydev;	/* short tty device number */
	dev_t	pr_lttydev;	/* controlling tty device (PRNODEV if none) */
	char	pr_clname[PRCLSZ];	/* scheduling class name */
	char	pr_fname[PRFNSZ];	/* last component of execed pathname */
	char	pr_psargs[PRARGSZ];	/* initial characters of arg list */
	short	pr_syscall;	/* system call number (if in syscall) */
	short	pr_fill;
	timestruc_t pr_ctime;	/* usr+sys cpu time for reaped children */
	u_long	pr_bysize;	/* size of process image in bytes */
	u_long	pr_byrssize;	/* resident set size in bytes */
	int	pr_argc;	/* initial argument count */
	char	**pr_argv;	/* initial argument vector */
	char	**pr_envp;	/* initial environment vector */
	int	pr_wstat;	/* if zombie, the wait() status */
			/* The following percent numbers are 16-bit binary */
			/* fractions [0 .. 1] with the binary point to the */
			/* right of the high-order bit (one == 0x8000) */
	u_short	pr_pctcpu;	/* % of recent cpu time, one or all lwps */
	u_short	pr_pctmem;	/* % of of system memory used by the process */
	uid_t	pr_euid;	/* effective user id */
	gid_t	pr_egid;	/* effective group id */
	id_t	pr_aslwpid;	/* lwp id of the aslwp; zero if no aslwp */
	long	pr_filler[7];	/* for future expansion */
} prpsinfo_t;

#if !defined(_STYPES)
#define	pr_ttydev	pr_lttydev
#else
#define	pr_ttydev	pr_ottydev
#endif

#define	PRNODEV	(dev_t)(-1)	/* non-existent device */

/* Optional actions to take when process continues */

typedef struct prrun {
	long	pr_flags;	/* Flags */
	sigset_t pr_trace;	/* Set of signals to be traced */
	sigset_t pr_sighold;	/* Set of signals to be held */
	fltset_t pr_fault;	/* Set of faults to be traced */
	caddr_t	pr_vaddr;	/* Virtual address at which to resume */
	long	pr_filler[8];	/* Filler area for future expansion */
} prrun_t;

#define	PRCSIG		0x001	/* Clear current signal */
#define	PRCFAULT	0x002	/* Clear current fault */
#define	PRSTRACE	0x004	/* Use traced-signal set in pr_trace */
#define	PRSHOLD		0x008	/* Use held-signal set in pr_sighold */
#define	PRSFAULT	0x010	/* Use traced-fault set in pr_fault */
#define	PRSVADDR	0x020	/* Resume at virtual address in pr_vaddr */
#define	PRSTEP		0x040	/* Direct the lwp to single-step */
#define	PRSABORT	0x080	/* Abort syscall */
#define	PRSTOP		0x100	/* Set directed stop request */

/* Memory-management interface */

typedef struct prmap {
	caddr_t		pr_vaddr;	/* Virtual address */
	u_long		pr_size;	/* Size of mapping in bytes */
	off_t		pr_off;		/* Offset into mapped object, if any */
	u_long		pr_mflags;	/* Protection and attribute flags */
	u_long		pr_pagesize;	/* pagesize (bytes) for this mapping */
	long		pr_filler[3];	/* Filler for future expansion */
} prmap_t;

/* Protection and attribute flags */

#define	MA_READ		0x04	/* Readable by the traced process */
#define	MA_WRITE	0x02	/* Writable by the traced process */
#define	MA_EXEC		0x01	/* Executable by the traced process */
#define	MA_SHARED	0x08	/* Changes are shared by mapped object */
/*
 * These are obsolete and unreliable.
 * They are included here only for historical compatibility.
 */
#define	MA_BREAK	0x10	/* Grown by brk(2) */
#define	MA_STACK	0x20	/* Grown automatically on stack faults */

/* Process credentials */

typedef struct prcred {
	uid_t	pr_euid;	/* Effective user id */
	uid_t	pr_ruid;	/* Real user id */
	uid_t	pr_suid;	/* Saved user id (from exec) */
	gid_t	pr_egid;	/* Effective group id */
	gid_t	pr_rgid;	/* Real group id */
	gid_t	pr_sgid;	/* Saved group id (from exec) */
	u_int	pr_ngroups;	/* Number of supplementary groups */
} prcred_t;

/* Resource usage */

typedef struct prusage {
	id_t		pr_lwpid;	/* lwp id.  0: process or defunct */
	u_long		pr_count;	/* number of contributing lwps */
	timestruc_t	pr_tstamp;	/* current time stamp */
	timestruc_t	pr_create;	/* process/lwp creation time stamp */
	timestruc_t	pr_term;	/* process/lwp termination time stamp */
	timestruc_t	pr_rtime;	/* total lwp real (elapsed) time */
	timestruc_t	pr_utime;	/* user level CPU time */
	timestruc_t	pr_stime;	/* system call CPU time */
	timestruc_t	pr_ttime;	/* other system trap CPU time */
	timestruc_t	pr_tftime;	/* text page fault sleep time */
	timestruc_t	pr_dftime;	/* data page fault sleep time */
	timestruc_t	pr_kftime;	/* kernel page fault sleep time */
	timestruc_t	pr_ltime;	/* user lock wait sleep time */
	timestruc_t	pr_slptime;	/* all other sleep time */
	timestruc_t	pr_wtime;	/* wait-cpu (latency) time */
	timestruc_t	pr_stoptime;	/* stopped time */
	timestruc_t	filltime[6];	/* filler for future expansion */
	u_long		pr_minf;	/* minor page faults */
	u_long		pr_majf;	/* major page faults */
	u_long		pr_nswap;	/* swaps */
	u_long		pr_inblk;	/* input blocks */
	u_long		pr_oublk;	/* output blocks */
	u_long		pr_msnd;	/* messages sent */
	u_long		pr_mrcv;	/* messages received */
	u_long		pr_sigs;	/* signals received */
	u_long		pr_vctx;	/* voluntary context switches */
	u_long		pr_ictx;	/* involuntary context switches */
	u_long		pr_sysc;	/* system calls */
	u_long		pr_ioch;	/* chars read and written */
	u_long		filler[10];	/* filler for future expansion */
} prusage_t;

/* Page data */

/* page data file header */
typedef struct prpageheader {
	timestruc_t	pr_tstamp;	/* real time stamp */
	u_long		pr_nmap;	/* number of address space mappings */
	u_long		pr_npage;	/* total number of pages */
} prpageheader_t;

/* page data mapping header */
typedef struct prasmap {
	caddr_t		pr_vaddr;	/* virtual address */
	u_long		pr_npage;	/* number of pages in mapping */
	off_t		pr_off;		/* offset into mapped object, if any */
	u_long		pr_mflags;	/* protection and attribute flags */
	u_long		pr_pagesize;	/* pagesize (bytes) for this mapping */
	long		pr_filler[3];	/* filler for future expansion */
} prasmap_t;

/*
 * npage bytes (rounded up to a sizeof (long)-byte boundary) follow
 * each mapping header, containing zero or more of these flags.
 */
#define	PG_REFERENCED	0x02		/* page referenced since last read */
#define	PG_MODIFIED	0x01		/* page modified since last read */
#define	PG_HWMAPPED	0x04		/* page is present and mapped */

/*
 * Macros for manipulating sets of flags.
 * sp must be a pointer to one of sigset_t, fltset_t, or sysset_t.
 * flag must be a member of the enumeration corresponding to *sp.
 */

/* turn on all flags in set */
#define	prfillset(sp) \
	{ register int _i_ = sizeof (*(sp))/sizeof (u_long); \
		while (_i_) ((u_long*)(sp))[--_i_] = (u_long)0xFFFFFFFF; }

/* turn off all flags in set */
#define	premptyset(sp) \
	{ register int _i_ = sizeof (*(sp))/sizeof (u_long); \
		while (_i_) ((u_long*)(sp))[--_i_] = (u_long)0; }

/* turn on specified flag in set */
#define	praddset(sp, flag) \
	((void)(((unsigned)((flag)-1) < 32*sizeof (*(sp))/sizeof (u_long)) ? \
	(((u_long*)(sp))[((flag)-1)/32] |= (1UL<<(((flag)-1)%32))) : 0))

/* turn off specified flag in set */
#define	prdelset(sp, flag) \
	((void)(((unsigned)((flag)-1) < 32*sizeof (*(sp))/sizeof (u_long)) ? \
	    (((u_long*)(sp))[((flag)-1)/32] &= ~(1UL<<(((flag)-1)%32))) : 0))

/* query: != 0 iff flag is turned on in set */
#define	prismember(sp, flag) \
	(((unsigned)((flag)-1) < 32*sizeof (*(sp))/sizeof (u_long)) && \
	    (((u_long*)(sp))[((flag)-1)/32] & (1UL<<(((flag)-1)%32))))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PROCFS_H */
