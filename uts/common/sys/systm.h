/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_SYSTM_H
#define	_SYS_SYSTM_H

#pragma ident	"@(#)systm.h	1.51	95/10/18 SMI"	/* SVr4.0 11.53	*/

#include <sys/types.h>
#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Random set of variables used by more than one routine.
 */

#ifdef _KERNEL
#include <sys/varargs.h>

extern int hz;			/* XXX */
extern struct vnode *rootdir;	/* pointer to vnode of root directory */
extern clock_t lbolt;		/* time in HZ since last boot */

extern char runin;		/* scheduling flag */
extern char runout;		/* scheduling flag */
extern char wake_sched;		/* causes clock to wake swapper on next tick */
extern char wake_sched_sec;	/* causes clock to wake swapper after a sec */

extern int	maxmem;		/* max available memory (clicks) */
extern int	physmem;	/* physical memory (clicks) on this CPU */
extern int	physmax;	/* highest numbered physical page present */
extern int	physinstalled;	/* physical pages including PROM/boot use */
extern int	maxclick;	/* Highest physical click + 1.		*/

extern int	availrmem;	/* Available resident (not swapable)	*/
				/* memory in pages.			*/
extern int	availsmem;	/* Available swapable memory in pages.	*/
extern int	freemem;	/* Current free memory.			*/

extern daddr_t	swplo;		/* block number of start of swap space */
extern		nswap;		/* size of swap space in blocks */
extern dev_t	rootdev;	/* device of the root */
extern dev_t	swapdev;	/* swapping device */
extern struct vnode *rootvp;	/* vnode of root filesystem */
extern struct vnode *dumpvp;	/* vnode to dump on */
extern char	*panicstr;	/* panic string pointer */
extern va_list  panicargs;	/* panic arguments */
extern int	blkacty;	/* active block devices */
extern int	pwr_cnt, pwr_act;
extern int	(*pwr_clr[])();

extern int	rstchown;	/* 1 ==> restrictive chown(2) semantics */
extern int	klustsize;

extern int	conslogging;

#ifdef C2_AUDIT
extern int	audit_active;	/* C2 auditing activate 1, absent 0. */
#endif

void swtch_to(kthread_id_t);
void startup(void);
void clkstart(void);
void post_startup(void);
void mp_init(void);
void kern_setup1(void);
int kern_setup2(void);
void iomove(caddr_t, int, int);
int is32b(void);
void wakeprocs(caddr_t);
void wakeup(caddr_t);
int sleep(caddr_t, int);
int min(int, int);
int max(int, int);
u_int umin(u_int, u_int);
u_int umax(u_int, u_int);
void trap_ret(void);
int grow(int *);
int timeout(void (*)(), caddr_t, long);
int realtime_timeout(void (*)(), caddr_t, long);
int untimeout(int);
void delay(long);
int nodev();
int nulldev();
int getudev(void);
int bcmp(char *, char *, size_t);
int memlow(void);
int stoi(char **);
void numtos(u_long, char *);
size_t ustrlen(const char *);
size_t strlen(const char *);
char *strcat(char *, const char *);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *knstrcpy(char *, const char *, size_t *);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strnrchr(const char *, int, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int ffs(long);
int copyin(caddr_t, caddr_t, size_t);
void copyin_noerr(caddr_t, caddr_t, size_t);
int xcopyin(caddr_t, caddr_t, size_t);
int copyout(caddr_t, caddr_t, size_t);
void copyout_noerr(caddr_t, caddr_t, size_t);
int xcopyout(caddr_t, caddr_t, size_t);
int copyinstr(char *, char *, size_t, size_t *);
char *copyinstr_noerr(char *, char *, size_t *);
int copyoutstr(char *, char *, size_t, size_t *);
char *copyoutstr_noerr(char *, char *, size_t *);
int copystr(char *, char *, size_t, size_t *);
void bcopy(caddr_t, caddr_t, size_t);
void ucopy(caddr_t, caddr_t, size_t);
void pgcopy(caddr_t, caddr_t, size_t);
void ovbcopy(char *, char *, size_t);
void fbcopy(int *, int *, size_t);
void bzero(caddr_t, size_t);
void uzero(caddr_t, size_t);
void bzeroa(caddr_t, size_t);
void bzeroba(caddr_t, size_t);
int kcopy(caddr_t, caddr_t, size_t);
int kzero(caddr_t, size_t);
int upath(caddr_t, caddr_t, size_t);
int spath(caddr_t, caddr_t, size_t);
int fubyte(caddr_t);
int fuibyte(caddr_t);
int fubyte_noerr(caddr_t);
int lfubyte(caddr_t);
int fuword(int *);
int fuiword(int *);
int fuword_noerr(int *);
int lfuword(int *);
int subyte(caddr_t, char);
int subyte_noerr(caddr_t, char);
int suword(int *, int);
int suword_noerr(int *, int);
int setjmp(label_t *);
void longjmp(label_t *);
void xrele(struct vnode *);
int arglistsz(caddr_t *, int *, int *, int);
int copyarglist(int, caddr_t *, int, caddr_t *, caddr_t, int);
caddr_t caller(void);
caddr_t callee(void);
int getpcstack(u_int *, int);
int on_fault(label_t *);
void no_fault(void);
void halt(char *);
int scanc(u_int, u_char *, u_char *, u_char);
int movtuc(size_t, u_char *, u_char *, u_char *);
int splr(int);
int splx(int);
int spl6(void);
int spl7(void);
int spl8(void);
int splhigh(void);
int splhi(void);
int spl0(void);
int splclock(void);
void set_base_spl(void);
int __ipltospl(int);

void softcall_init(void);
void softcall(void (*)(caddr_t), caddr_t);
void softint(void);

void _insque(caddr_t, caddr_t);
void _remque(caddr_t);

/* casts to keep lint happy */
#define	insque(q, p)	_insque((caddr_t)q, (caddr_t)p)
#define	remque(q)	_remque((caddr_t)q)

#pragma unknown_control_flow(setjmp)
#pragma unknown_control_flow(on_fault)

struct timeval;
extern void	uniqtime(struct timeval *);

extern int Dstflag;
extern int Timezone;

#define	struct_zero	bzero	/* cover for 3b2 hack */

int num_hw_pagesizes(void);
u_int get_hw_pagesize(u_int level);

#endif /* _KERNEL */

/*
 * Structure of the system-entry table.
 *
 * 	Changes to struct sysent should maintain binary compatibility with
 *	loadable system calls, although the interface is currently private.
 *
 *	This means it should only be expanded on the end, and flag values
 * 	should not be reused.
 *
 *	It is desirable to keep the size of this struct a power of 2 for quick
 *	indexing.
 */
struct sysent {
	char		sy_narg;	/* total number of arguments */
	char		sy_flags;	/* various flags as defined below */
	int		(*sy_call)();	/* argp, rvalp-style handler */
	krwlock_t	*sy_lock;	/* lock for loadable system calls */
	longlong_t	(*sy_callc)();	/* C-style call hander or wrapper */
};

extern struct sysent	sysent[];
extern struct sysent	nosys_ent;	/* entry for invalid system call */

#define	NSYSCALL 	213		/* number of system calls */

#define	LOADABLE_SYSCALL(s)	(s->sy_flags & SE_LOADABLE)
#define	LOADED_SYSCALL(s)	(s->sy_flags & SE_LOADED)

/*
 * sy_flags values
 * 	Values 1, 2, and 4 were used previously for SETJUMP, ASYNC, and IOSYS.
 */
#define	SE_LOADABLE	0x08		/* syscall is loadable */
#define	SE_LOADED	0x10		/* syscall is completely loaded */
#define	SE_NOUNLOAD	0x20		/* syscall never needs unload */
#define	SE_ARGC		0x40		/* syscall takes C-style args */

/*
 * Structure of the return-value parameter passed by reference to
 * system entries.
 */
union rval {
	struct	{
		int	r_v1;
		int	r_v2;
	} r_v;
	off_t	r_off;
	offset_t r_offset;
	time_t	r_time;
	longlong_t	r_vals;
};
#define	r_val1	r_v.r_v1
#define	r_val2	r_v.r_v2

typedef union rval rval_t;

#ifdef	_KERNEL

extern int	save_syscall_args();
extern uint_t	get_syscall_args(klwp_t *lwp, int *argp, int *nargsp);
extern uint_t	set_errno(u_int errno);

extern longlong_t syscall_ap();
extern longlong_t loadable_syscall();
extern longlong_t nosys(void);

#ifdef	KPERF
/*
 *	This is the structure for the kernel performance measurement code.
 */
#define	NUMRC		512
#define	NUMPHASE	64
#define	PFCHAR		10

#define	KPFCHILDSLP	35
#define	KPFTRON		36
#define	KPFTRON2	37
#define	KPFTROFF	38

/*
 *	The following structure describes the records written
 *	by the kernel performance measurement code.
 *
 *	Not all fields of the structure have meaningful values for
 *	records types.
 */
typedef struct kernperf {
	unsigned char	kp_type;	/* the record type as defined below */
	unsigned char	kp_level;	/* A priority level.		*/
	pid_t 		kp_pid;		/* A process id.	*/
	clock_t 	kp_time;	/* A relative time in 10 	*/
					/* microseconds units		*/
	unsigned long	kp_pc;		/* A pc (kernel address).	*/
} kernperf_t;

/*
 * the possible record types are as follows.
 */

#define	KPT_SYSCALL	0	/* System call - pc determines which	*/
				/* one.					*/
#define	KPT_INTR	1	/* An interrupt - pc determines which 	*/
				/* one.					*/
#define	KPT_TRAP_RET	2	/* Return from trap to user level	*/

#define	KPT_INT_KRET	3	/* Return from interrupt to kernel	*/
				/* level.				*/
#define	KPT_INT_URET	4	/* Return from interrupt to user level	*/

#define	KPT_SLEEP	5	/* Call to "sleep" - pc is caller. The  */
				/* pid is that of the caller		*/
#define	KPT_WAKEUP	6	/* Call of "wakeup" - pc is caller. The	*/
				/* pid is that of process being		*/
				/* awakened.				*/
#define	KPT_PSWTCH	7	/* Process switch.  The pid is the new	*/
				/* process about to be run		*/
#define	KPT_SPL		8	/* Change of priority level.  The pc is	*/
				/* that of the caller.  The level is 	*/
				/* the new priority level.		*/
#define	KPT_CSERVE	9	/* Call of a streams service procedure.	*/
				/* the pc tells which one.		*/
#define	KPT_RSERVE	10	/* Return from a streams service 	*/
				/* procedure.  the pc tells which one.	*/
#define	KPT_UXMEMF	11	/* memory fault because of paging	*/
				/* or stack exception.			*/
#define	KPT_SWTCH	12	/* call to swtch			*/
#define	KPT_QSWTCH	13	/* call to qswtch			*/
#define	KPT_STKBX	14	/* stack boundary exceptions		*/
#define	KPT_END		15	/* end of trace				*/
#define	KPT_IDLE	16	/* in scheduler sitting idle		*/
#define	KPT_PREEMPT	17	/* hit a preemption point		*/
				/* however preemption did not occur	*/
#define	KPT_P_QSWTCH	18	/* reached a preemption point, and will */
				/* Qswtch				*/
#define	KPT_LAST	19	/* last record of a proc		*/

#define	swtch() \
{\
	if (kpftraceflg) {\
		asm(" MOVAW 0(%pc),Kpc"); \
		kperf_write(KPT_SWTCH, Kpc, curproc); \
	} \
	if (kpftraceflg && exitflg) {\
		kperf_write(KPT_LAST, Kpc, curproc); \
		exitflg = 0; \
	} \
	KPswtch(); \
}
extern int kpchildslp;
extern int pre_trace;
extern int kpftraceflg;
extern int takephase;
extern int putphase;
extern int outbuf;
/* extern int out_of_tbuf; */
extern int numrc;
extern int numrccount;
extern int Kpc;
extern int KPF_opsw;
extern kernperf_t kpft[];
extern int exitflg;

#else	/* KPERF */

extern void swtch(void);

#endif	/* KPERF */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SYSTM_H */
