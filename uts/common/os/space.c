/*
 * Copyright (c) 1989-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)space.c	1.39	94/04/20 SMI"

/*
 * The intent of this file is to contain any data that must remain
 * resident in the kernel.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/acct.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/utsname.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/sysinfo.h>
#include <sys/t_lock.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/msg.h>

#include <sys/strredir.h>

struct	acct	acctbuf;	/* needs to be here if acct.c is loadable */
struct	vnode	*acctvp;
kmutex_t	aclock;

struct	buf	bfreelist;	/* Head of the free list of buffers */

sysinfo_t	sysinfo;
vminfo_t	vminfo;		/* VM stats protected by sysinfolock mutex */

/*
 * The following describe the physical memory configuration.
 *
 * 	maxclick -  The largest physical click number.
 *		    ctob(maxclick) is the largest physical
 *		    address configured plus 1.  Currently
 *		    unset and unused.
 *
 *	physmem	 -  The amount of physical memory configured
 *		    in clicks.  ctob(maxclick) is the amount
 *		    of physical memory in bytes.  Defined in
 *		    .../os/startup.c.
 *
 *	physmax  -  The highest numbered physical page in memory.
 *
 *	maxmem	 -  Maximum available memory, in pages.  Defined
 *		    in main.c.
 *
 *	physinstalled
 *		 -  Pages of physical memory installed;
 *		    includes use by PROM/boot not counted in
 *		    physmem.
 */

int	maxclick;
int	physmax;
int	physinstalled;

struct var v;
struct utsname utsname;

#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/bootconf.h>

/*
 * Data from swapgeneric.c that must be resident.
 */
struct vnode *rootvp;
dev_t rootdev;
int netboot;
int obpdebug;

/*
 * Data from arp.c that must be resident.
 */
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/sockio.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/log.h>
#include <sys/strlog.h>
#include <sys/dlpi.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>

ether_addr_t etherbroadcastaddr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/*
 * Data from timod that must be resident
 */

/*
 * state transition table for TI interface
 */
#include <sys/tihdr.h>

#define	nr	127		/* not reachable */

char ti_statetbl[TE_NOEVENTS][TS_NOSTATES] = {
				/* STATES */
	/* 0  1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16 */

	{ 1, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr,  2, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr,  4, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr,  3, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr,  3, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr,  0,  3, nr,  3,  3, nr, nr,  7, nr, nr, nr,  6,  7,  9, 10, 11},
	{nr, nr,  0, nr, nr,  6, nr, nr, nr, nr, nr, nr,  3, nr,  3,  3,  3},
	{nr, nr, nr, nr, nr, nr, nr, nr,  9, nr, nr, nr, nr,  3, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr,  3, nr, nr, nr, nr,  3, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr,  7, nr, nr, nr, nr,  7, nr, nr, nr},
	{nr, nr, nr,  5, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr,  8, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, 12, 13, nr, 14, 15, 16, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr, nr,  9, nr, 11, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr, nr,  9, nr, 11, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr, nr, 10, nr,  3, nr, nr, nr, nr, nr},
	{nr, nr, nr,  7, nr, nr, nr,  7, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr,  9, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr, nr,  9, 10, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr, nr,  9, 10, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr, nr, nr, 11,  3, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr,  3, nr, nr,  3,  3,  3, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr,  3, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr, nr, nr, nr, nr,  7, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr,  9, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr,  3, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr,  3, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
	{nr, nr, nr,  3, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr, nr},
};


#include <sys/sad.h>
#include <sys/ptms.h>
#include <sys/tty.h>
#include <sys/ptyvar.h>

/*
 * Allocate tunable structures at runtime.
 */
void
space_init(void)
{
	sad_initspace();
	ptms_initspace();
	pty_initspace();
}

/*
 * moved from ts.c because slp.c references it!!!!! BLECH!!!
 *
 */
#define	NKMDPRIS  40

short ts_maxkmdpri = NKMDPRIS - 1; /* maximum kernel mode ts priority */

/*
 * Previously defined in consmsconf.c ...
 */
dev_t kbddev = NODEV;
dev_t mousedev = NODEV;
dev_t stdindev = NODEV;
struct vnode *wsconsvp;

dev_t fbdev = NODEV;
struct vnode *fbvp;

/*
 * from shm.c
 */

struct shmid_ds	*shmem;		/* shared memory id pool */
struct shminfo	shminfo;	/* shared memory parameters */

/*
 * from sem.c
 */

struct semid_ds *sema;		/* semaphore id pool */
struct sem	*sem;		/* semaphore pool */
struct map	*semmap;	/* semaphore allocation map */
struct sem_undo	**sem_undo;	/* per process undo table */
struct sem_undo  *semunp;	/* ptr to head of undo chain */
struct sem_undo  *semfup;	/* ptr to head of free undo chain */
int		*semu;		/* undo structure pool */
struct seminfo	seminfo;	/* semaphore parameters */

/*
 * moved from log.c because they must be resident in the kernel.
 */

int conslogging = 0;
int numlogtrc;		/* number of processes reading trace log */
int numlogerr;		/* number of processes reading error log */
int numlogcons;		/* number of processes reading console log */

kmutex_t log_lock;

int log_errseq, log_trcseq, log_conseq;	/* logger sequence numbers */

/*
 * moved from cons.c because they must be resident in the kernel.
 */

vnode_t	*rconsvp;
dev_t	rconsdev;
dev_t	uconsdev = NODEV;

/*
 * This flag, when set marks rconsvp in a transition state.
 */

int	cn_conf;

/* From ip_main.c */
unsigned char ip_protox[IPPROTO_MAX];
struct ip_provider *lastprov;

/*
 * Moved from sad_conf.c because of the usual in loadable modules
 */

#ifndef NSTRPHASH
#define	NSTRPHASH	128
#endif
struct autopush **strpcache;
int strpmask = NSTRPHASH - 1;

/*
 * Moved here from wscons.c
 * Package the redirection-related routines into an ops vector of the form
 * that the redirecting driver expects.
 */
extern int wcvnget();
extern void wcvnrele();
srvnops_t	wscons_srvnops = {
	wcvnget,
	wcvnrele
};

/*
 * consconfig() in autoconf.c sets this; it's the vnode of the distinguished
 * keyboard/frame buffer combination, aka the workstation console.
 */

vnode_t *rwsconsvp;
dev_t	rwsconsdev;

/* used in mem.c and machdep.c */

caddr_t mm_map;

/*
 * From msg.c
 */

caddr_t		msg;			/* base address of message buffer */
struct map	*msgmap;		/* msg allocation map */
struct msg	*msgh;			/* message headers */
struct msqid_ds	*msgque;		/* msg queue headers */
struct msglock	*msglock; 		/* locks for the message queues */
struct msg	*msgfp;			/* ptr to head of free header list */
struct msginfo	msginfo;		/* message parameters */

/* from iwscons.c */

kthread_id_t	iwscn_thread;	/* thread that is allowed to push redirm */
wcm_data_t	*iwscn_wcm_data; /* allocated data for redirm */
