/*
 * Copyright (c) 1989-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)param.c	2.120	95/09/11 SMI"	/* from SunOS */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/sysmacros.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/var.h>
#include <sys/callo.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_quota.h>
#include <sys/dedump.h>
#include <sys/dumphdr.h>
#include <sys/conf.h>
#include <sys/class.h>
#include <sys/ts.h>
#include <sys/rt.h>
#include <sys/exec.h>
#include <sys/exechdr.h>
#include <sys/buf.h>
#include <sys/resource.h>
#include <vm/seg.h>
#include <sys/vmparam.h>
#include <sys/utsname.h>
#include <sys/kmem.h>
#include <sys/stack.h>

#include <sys/map.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>

/*
 * System parameter formulae.
 *
 * This file is copied into each directory where we compile
 * the kernel; it should be modified there to suit local taste
 * if necessary.
 */

int	hz = HZ;
int	usec_per_tick = MICROSEC / HZ;	/* microseconds per clock tick */
int	nsec_per_tick = NANOSEC / HZ;	/* nanoseconds per clock tick */

/*
 * Tables of initialization functions, called from main().
 */

extern void binit(void);
extern void space_init(void);
extern void cred_init(void);
extern void dnlc_init(void);
extern void vfsinit(void);
extern void finit(void);
extern void strinit(void);
extern void flk_init(void);
#ifdef TRACE
extern void inittrace(void);
#endif /* TRACE */
#ifdef sparc
extern void init_clock_thread(void);
#endif
extern void softcall_init(void);
extern void sadinit(void);
extern void loginit(void);
extern void ttyinit(void);
extern void mp_strinit(void);

void	(*init_tbl[])(void) = {
#ifdef sparc
	init_clock_thread,
#endif
	binit,
	space_init,
	cred_init,
	dnlc_init,
	vfsinit,
	finit,
	strinit,
#ifdef TRACE
	inittrace,
#endif /* TRACE */
	softcall_init,
	sadinit,
	loginit,
	ttyinit,
	as_init,
	anon_init,
	segvn_init,
	flk_init,
	0
};


/*
 * Any per cpu resources should be initialized via
 * an entry in mp_init_tbl().
 */

void	(*mp_init_tbl[])(void) = {
	mp_strinit,
	0
};

int maxusers;		/* kitchen-sink knob for dynamic configuration */

/*
 * autoup -- used in struct var for dynamic config of the age a delayed-write
 * buffer must be in seconds before bdflush will write it out.
 */
int autoup = 30;

/*
 * bufhwm -- tuneable variable for struct var for v_bufhwm.
 * high water mark for buffer cache mem usage in units of K bytes.
 */
int bufhwm = 0;

/*
 * Process table.
 */
int max_nprocs;		/* set in param_init() */
int maxuprc;		/* set in param_init() */
int reserved_procs;
int nthread = 0;

/*
 * UFS tunables
 */
int ufs_iincr = 30;				/* */
int ufs_allocinode = 0;				/* */
int ufs_ninode;		/* set in param_init() */
int ncsize;		/* set in param_init() # of dnlc entries */

struct dquot *dquot, *dquotNDQUOT;		/* */
int ndquot;		/* set in param_init() */

#if defined(DEBUG)
	/* default: don't do anything */
int ufs_debug = UDBG_OFF;
#endif /* DEBUG */

/*
 * Exec switch table. This is used by the generic exec module
 * to switch out to the desired executable type, based on the
 * magic number. The currently supported types are ELF, a.out
 * (both NMAGIC and ZMAGIC), and interpreter (#!) files.
 */

short elfmagic = 0x7f45;
short intpmagic = 0x2321;
#ifdef sparc
short aout_nmagic = NMAGIC;
short aout_zmagic = ZMAGIC;
short aout_omagic = OMAGIC;
#endif
#ifdef i386
short coffmagic = 0x4c01;	/* octal 0514 byte-flipped */
#endif
short nomagic = 0;

char *execswnames[] = {
#ifdef sparc
	"elfexec", "intpexec", "aoutexec", "aoutexec", "aoutexec",
	NULL, NULL, NULL
#endif
#ifdef i386
	"elfexec", "intpexec", "coffexec", NULL, NULL, NULL, NULL,
#endif
};

struct execsw execsw[] = {
	&elfmagic, NULL, NULL, NULL,
	&intpmagic, NULL, NULL, NULL,
#ifdef sparc
	&aout_zmagic, NULL, NULL, NULL,
	&aout_nmagic, NULL, NULL, NULL,
	&aout_omagic, NULL, NULL, NULL,
#endif
#ifdef i386
	&coffmagic, NULL, NULL, NULL,
	&nomagic, NULL, NULL, NULL,
#endif
	&nomagic, NULL, NULL, NULL,
	&nomagic, NULL, NULL, NULL,
	&nomagic, NULL, NULL, NULL,
};
int nexectype = sizeof (execsw) / sizeof (execsw[0]);	/* # of exec types */
kmutex_t execsw_lock;	/* Used for allocation of execsw entries */

/*
 * symbols added to make changing max-file-descriptors
 * simple via /etc/system
 */
#define	RLIM_FD_CUR 0x40
#define	RLIM_FD_MAX 0x400

int rlim_fd_cur = RLIM_FD_CUR;
int rlim_fd_max = RLIM_FD_MAX;


/*
 * Default resource limits.
 *
 *	Softlimit	Hardlimit
 */
struct rlimit rlimits[RLIM_NLIMITS] = {
	RLIM_INFINITY,	RLIM_INFINITY,	/* max CPU time */
	RLIM_INFINITY,	RLIM_INFINITY,	/* max file size */
	DFLDSIZ,	MAXDSIZ,	/* max data size */
	DFLSSIZ,	MAXSSIZ,	/* max stack */
	RLIM_INFINITY,	RLIM_INFINITY,	/* max core file size */
	RLIM_FD_CUR,	RLIM_FD_MAX,	/* max file descriptors */
	RLIM_INFINITY,	RLIM_INFINITY,	/* max mapped memory */
};


/*
 * file and record locking
 */
struct flckinfo flckinfo;

/*
 * Streams tunables
 */
int	nstrpush = 9;
int	maxsepgcnt = 1;

/*
 * strmsgsz is the size for the maximum streams message a user can create.
 * for Release 4.0, a value of zero will indicate no upper bound.  This
 * parameter will disappear entirely in the next release.
 */

int	strmsgsz = 0x10000;
int	strctlsz = 1024;
int	rstchown = 1;		/* POSIX_CHOWN_RESTRICTED is enabled */
int	ngroups_max = NGROUPS_MAX_DEFAULT;

int	nservers = 0;		/* total servers in system */
int	n_idleservers = 0;	/* idle servers in system */
int	n_sr_msgs = 0;		/* receive descriptors in msg queue */

#define	NSTRPIPE 60		/* XXX - need right number for this! */
int spcnt = NSTRPIPE;
struct sp {
	queue_t *sp_rdq;	/* this stream's read queue */
	queue_t *sp_ordq;	/* other stream's read queue */
} sp_sp[NSTRPIPE];

/*
 * This is for the streams message debugging module.
 */

int dump_cnt = 5;
struct dmp *dump;

/*
 * This has to be allocated somewhere; allocating
 * them here forces loader errors if this file is omitted.
 */
struct	inode *ufs_inode;

/*
 * generic scheduling stuff
 *
 * Configurable parameters for RT and TS are in the respective
 * scheduling class modules.
 */
#include <sys/disp.h>

pri_t maxclsyspri = MAXCLSYSPRI;
pri_t minclsyspri = MINCLSYSPRI;

int maxclass_sz = SA(MAX((sizeof (rtproc_t)), (sizeof (tsproc_t))));
int maxclass_szd = (SA(MAX((sizeof (rtproc_t)), (sizeof (tsproc_t)))) /
	sizeof (double));
char	sys_name[] = "SYS";
char	ts_name[] = "TS";
char	rt_name[] = "RT";

extern void sys_init();
extern classfuncs_t sys_classfuncs;

sclass_t sclass[] = {
	"SYS",	sys_init,	&sys_classfuncs, STATIC_SCHED, 0, 0,
	"",	NULL,	NULL,	NULL, 0, 0,
	"",	NULL,	NULL,	NULL, 0, 0,
	"",	NULL,	NULL,	NULL, 0, 0,
	"",	NULL,	NULL,	NULL, 0, 0,
	"",	NULL,	NULL,	NULL, 0, 0,
};
int loaded_classes = 1;		/* for loaded classes */
kmutex_t class_lock;		/* lock for class[] */

int nclass = sizeof (sclass) / sizeof (sclass_t);
char initcls[] = "TS";
char *initclass = initcls;

/*
 * High Resolution Timers
 */
uint	timer_resolution = 100;

/*
 * Streams log driver
 */
#include <sys/log.h>
#ifndef NLOG
#define	NLOG 16
#endif

int		log_cnt = NLOG+CLONEMIN+1;
struct log	log_log[NLOG+CLONEMIN+1];

/*
 * Tunable system parameters.
 */
#include <sys/tuneable.h>

/*
 * The integers tune_* are done this way so that the tune
 * data structure may be "tuned" if necessary from the /etc/system
 * file. The tune data structure is initialized in param_init();
 */

tune_t tune;

/*
 * If freemem < t_getpgslow, then start to steal pages from processes.
 */
int tune_t_gpgslo = 25;

/*
 * Rate at which fsflush is run, in seconds.
 */
int tune_t_fsflushr = 5;

/*
 * The minimum available resident (not swappable) memory to maintain
 * in order to avoid deadlock.  In pages.
 */
int tune_t_minarmem = 25;

/*
 * The minimum available swappable memory to maintain in order to avoid
 * deadlock.  In pages.
 */
int tune_t_minasmem = 25;

int tune_t_flckrec = 512;	/* max # of active frlocks */

struct map *kernelmap;
struct map *ekernelmap;
u_int pages_pp_maximum = 200;

int boothowto;			/* boot flags passed to kernel */
daddr_t swplo;			/* starting disk address of swap area */
struct vnode *dumpvp;		/* ptr to vnode of dump device */
struct var v;			/* System Configuration Information */

/*
 * System Configuration Information
 */

#ifdef sparc
char hw_serial[11];		/* read from prom at boot time */
char architecture[] = "sparc";
char hw_provider[] = "Sun_Microsystems";
#endif
#ifdef i386
/*
 * On x86 machines, read hw_serial, hw_provider and srpc_domain from
 * /etc/bootrc at boot time.
 */
char architecture[] = "i386";
char hw_serial[11] = "0";
char hw_provider[SYS_NMLN] = "";
#endif
char srpc_domain[SYS_NMLN] = "";
char platform[SYS_NMLN] = "";	/* read from the devinfo root node */

#ifdef XENIX_COMPAT
int	emgetmap = 0;
int	emsetmap = 0;
int	emuneap = 0;
#endif

void
param_calc(maxusers)
int maxusers;
{
	/*
	 * We need to dynamically change any variables now so that
	 * the setting of maxusers propagates to the other variables
	 * that are dependent on maxusers.
	 */
	reserved_procs = 5;
	max_nprocs = (10 + 16 * maxusers);
	if (max_nprocs > MAXPID) {
		max_nprocs = MAXPID;
	}

	ufs_ninode = (max_nprocs + 16 + maxusers) + 64;
	ndquot =  ((maxusers * NMOUNT) / 4) + max_nprocs;
	maxuprc = (max_nprocs - reserved_procs);
	ncsize = (max_nprocs + 16 + maxusers) + 64; /* # of dnlc entries */
}

void
param_init()
{
	dump = kmem_zalloc(dump_cnt * sizeof (struct dmp), KM_SLEEP);

	/*
	 * Set each individual element of struct var v to be the
	 * default value. This is done this way
	 * so that a user can set the assigned integer value in the
	 * /etc/system file *IF* tuning is needed.
	 */
	v.v_proc = max_nprocs;	/* v_proc - max # of processes system wide */
	v.v_maxupttl = max_nprocs - reserved_procs;
	v.v_maxsyspri = (int)maxclsyspri;  /* max global pri for sysclass */
	v.v_maxup = min(maxuprc, v.v_maxupttl); /* max procs per user */
	v.v_autoup = autoup;	/* v_autoup - delay for delayed writes */

	/*
	 * Set each individual element of struct tune to be the
	 * default value. Each struct element This is done this way
	 *  so that a user can set the assigned integer value in the
	 * /etc/system file *IF* tuning is needed.
	 */
	tune.t_gpgslo = tune_t_gpgslo;
	tune.t_fsflushr = tune_t_fsflushr;
	tune.t_minarmem = tune_t_minarmem;
	tune.t_minasmem = tune_t_minasmem;
	tune.t_flckrec = tune_t_flckrec;

	/*
	 * initialization for max file descriptors
	 */
	if (rlim_fd_cur > rlim_fd_max)
		rlim_fd_cur = rlim_fd_max;

	rlimits[RLIMIT_NOFILE].rlim_cur = rlim_fd_cur;
	rlimits[RLIMIT_NOFILE].rlim_max = rlim_fd_max;
}

/*
 * check certain configurable parameters.
 */
void
param_check(void)
{
	if (ngroups_max < NGROUPS_UMIN || ngroups_max > NGROUPS_UMAX)
		ngroups_max = NGROUPS_MAX_DEFAULT;
}
