/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_PROC_PRDATA_H
#define	_SYS_PROC_PRDATA_H

#pragma ident	"@(#)prdata.h	1.36	95/03/24 SMI"	/* SVr4.0 1.18	*/

#include <sys/prsystm.h>
#include <sys/thread.h>
#include <sys/poll.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	round(r)	(((r) + sizeof (int) - 1) & (~(sizeof (int) - 1)))

#define	PNSIZ	5			/* size of /proc name entries */

extern int procfstype;
extern int prmounted;		/* Set to 1 if /proc is mounted. */
extern struct vfs *procvfs;	/* Points to /proc vfs entry. */
extern dev_t procdev;
extern struct vnodeops prvnodeops;
extern struct vfsops prvfsops;

/*
 * Macros for mapping between i-numbers and pids.
 */
#define	PRBIAS	64
#define	itop(n)	((int)((n)-PRBIAS))	/* i-number to pid */
#define	ptoi(n)	((int)((n)+PRBIAS))	/* pid to i-number */

typedef struct prnode {
	struct vnode	*pr_vnext;	/* list of all vnodes for process */
	struct proc	*pr_proc;	/* process being traced */
	kthread_t 	*pr_thread;	/* lwp being traced */
	kcondvar_t	pr_wait;	/* to wait for the proc/lwp to stop */
	kmutex_t	pr_lock;	/* to wait for the proc/lwp to stop */
	u_int		pr_hatid;	/* hat layer id for page data */
	u_short		pr_mode;	/* file mode bits */
	u_char		pr_flags;	/* private flags */
	u_char		pr_type;	/* type of /proc file */
	u_int		pr_opens;	/* count of opens */
	u_int		pr_writers;	/* count of opens for writing */
	struct pollhead	pr_pollhead;	/* list of all pollers */
	struct vnode	pr_vnode;	/* associated vnode */
} prnode_t;

/*
 * Conversion macros.
 */
#define	VTOP(vp)	((struct prnode *)(vp)->v_data)
#define	PTOV(pnp)	((struct vnode *)&(pnp)->pr_vnode)

/*
 * Flags for pr_flags.
 */
#define	PREXCL		0x01	/* exclusive-use (disallow opens for write) */
#define	PRINVAL		0x02	/* vnode is invalid (security provision) */
#define	PRZOMB		0x04	/* process/lwp is (about to be) a zombie */
#define	PRPOLL		0x08	/* poll() in progress on this file */

/*
 * /proc file types (pr_type).
 * type 0 is not defined, on purpose.
 */
#define	PRT_PROC	1	/* process file */
#define	PRT_LWP		2	/* lwp file */
#define	PRT_PDATA	3	/* page data file */

/*
 * Flags to prlock().
 */
#define	ZNO	0	/* Fail on encountering a zombie process. */
#define	ZYES	1	/* Allow zombies. */

/*
 * Assign one set to another (possible different sizes).
 *
 * Assigning to a smaller set causes members to be lost.
 * Assigning to a larger set causes extra members to be cleared.
 */
#define	prassignset(ap, sp)					\
{								\
	register int _i_ = sizeof (*(ap))/sizeof (u_long);	\
	while (--_i_ >= 0)					\
		((u_long*)(ap))[_i_] =				\
		    (_i_ >= sizeof (*(sp))/sizeof (u_long)) ?	\
		    0L : ((u_long*)(sp))[_i_];			\
}

/*
 * Determine whether or not a set (of arbitrary size) is empty.
 */
#define	prisempty(sp) \
	setisempty((u_long *)(sp), sizeof (*(sp))/sizeof (u_long))

#ifdef	_KERNEL
extern	struct prnode prrootnode;
extern	kmutex_t pr_mount_lock;
#endif	/* _KERNEL */

/*
 * Resource usage with times as hrtime_t rather than timestruc_t.
 * Each member exactly matches the corresponding member in prusage_t.
 * This is for convenience of internal computation.
 */
typedef struct prhusage {
	id_t		pr_lwpid;	/* lwp id.  0: process or defunct */
	u_long		pr_count;	/* number of contributing lwps */
	hrtime_t	pr_tstamp;	/* current time stamp */
	hrtime_t	pr_create;	/* process/lwp creation time stamp */
	hrtime_t	pr_term;	/* process/lwp termination time stamp */
	hrtime_t	pr_rtime;	/* total lwp real (elapsed) time */
	hrtime_t	pr_utime;	/* user level CPU time */
	hrtime_t	pr_stime;	/* system call CPU time */
	hrtime_t	pr_ttime;	/* other system trap CPU time */
	hrtime_t	pr_tftime;	/* text page fault sleep time */
	hrtime_t	pr_dftime;	/* data page fault sleep time */
	hrtime_t	pr_kftime;	/* kernel page fault sleep time */
	hrtime_t	pr_ltime;	/* user lock wait sleep time */
	hrtime_t	pr_slptime;	/* all other sleep time */
	hrtime_t	pr_wtime;	/* wait-cpu (latency) time */
	hrtime_t	pr_stoptime;	/* stopped time */
	hrtime_t	filltime[6];	/* filler for future expansion */
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
} prhusage_t;

proc_t *	pr_p_lock(prnode_t *);
int		prusrio(struct as *, enum uio_rw, struct uio *);
int		prlock(struct prnode *, int);
int		prunmark(proc_t *);
void		prunlock(struct prnode *);
void		prgetstatus(kthread_t *, prstatus_t *);
void		prgetaction(proc_t *, user_t *, u_int, struct sigaction *);
void		prgetmap(struct as *, caddr_t, caddr_t, prmap_t *);
vnode_t *	prlwpnode(proc_t *, u_int, prnode_t *);
vnode_t *	prpdnode(proc_t *, prnode_t *);
prnode_t *	prgetnode(void);
void		prfreenode(prnode_t *);
long		prpdsize(struct as *);
int		prpdread(struct as *, u_int, struct uio *);
void		prgetpsinfo(proc_t *, struct prpsinfo *, kthread_t *);
void		prgetusage(kthread_t *, struct prhusage *);
void		praddusage(kthread_t *, struct prhusage *);
void		prcvtusage(struct prhusage *);
kthread_t *	prchoose(proc_t *);
void		allsetrun(proc_t *);
int		setisempty(u_long *, unsigned);

/*
 * Machine-dependent routines (defined in prmachdep.c).
 */

void		prpokethread(kthread_t *t);
user_t *	prumap(proc_t *);
void		prunmap(proc_t *);
void		prgetprregs(klwp_t *, prgregset_t);
void		prsetprregs(klwp_t *, prgregset_t);
prgreg_t	prgetpc(prgregset_t);
void		prgetprfpregs(klwp_t *, prfpregset_t *);
void		prsetprfpregs(klwp_t *, prfpregset_t *);
void		prgetprxregs(klwp_t *, caddr_t);
void		prsetprxregs(klwp_t *, caddr_t);
int		prgetprxregsize(void);
int		prhasfp(void);
int		prhasx(void);
caddr_t		prgetstackbase(proc_t *);
caddr_t		prgetpsaddr(proc_t *);
void		prstep(klwp_t *);
void		prnostep(klwp_t *);
int		prisstep(klwp_t *);
void		prsvaddr(klwp_t *, caddr_t);
caddr_t		prmapin(struct as *, caddr_t, int);
void		prmapout(struct as *, caddr_t, caddr_t, int);
int		prfetchinstr(klwp_t *, long *);
#if defined(i386) || defined(__i386)
struct ssd;
int		prnldt(proc_t *);
void		prgetldt(proc_t *, struct ssd *);
#endif
#if defined(sparc) || defined(__sparc)
void		prgetwindows(klwp_t *, gwindows_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PROC_PRDATA_H */
