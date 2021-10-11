#ident	"@(#)mt_init.c	1.46	95/10/20 SMI"

/*
 * define and initialize MT data.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/t_lock.h>
#include <sys/avintr.h>
#include <sys/poll.h>
#include <sys/cmn_err.h>
#include <sys/spl.h>

#include <vm/as.h>
#include <vm/seg_kmem.h>
#include <vm/page.h>

kmutex_t pidlock;	/* locks process tree, signal queues, process groups */
			/* sessions, and process states. */
kmutex_t reaplock;	/* protects thread_deathrow */
kmutex_t lwplock;	/* used to implement user level condition variables */
			/* and mutexes. */
kmutex_t cpu_lock;	/* protects ncpus, ncpus_online and cpu_flag */
			/* as well as dispatch queue reallocations */
kmutex_t delay_lock;	/* protects delay() wakeups */
kmutex_t swapinfo_lock;
kmutex_t anoninfo_lock;
kmutex_t swap_lock;	/* serializes swapadd and swapdel */
kmutex_t ssl_randlock;	/* serializes calls to ssl_random() */
#ifdef _VPIX
kmutex_t segvpix_kmemlock;	/* protects kmem alloc freelist for segvpix */
#endif
kmutex_t fb_kmemlock;	/* protects kmem alloc freelist for fbio */
kmutex_t pageio_kmemlock; /* protects kmem alloc freelist for pageio bufs */

extern	kmutex_t freemem_lock;	/* protects all global variables associated */
				/* with freemem.  */

kmutex_t async_lock;		/* protects async request list */

kmutex_t unsafe_driver;		/* protects mt_unsafe drivers */

kmutex_t ualock;		/* uadmin serialization lock */
kmutex_t udevlock;		/* lock for getudev */

kmutex_t flock_lock;		/* file/record locking monitor */

kmutex_t av_lock;		/* autovector interrupt update lock */

kmutex_t tod_lock; 		/* for time-of-day stuff */

kmutex_t thread_stop_lock; 	/* for thread stop related actions */

extern kmutex_t aclock;		/* accounting lock */

/* global device locks XXX should not go here?? */
kmutex_t	pt_excl;

extern kmutex_t log_lock;	/* for stream logging driver */

kmutex_t prf_lock;	/* protects putbuf, skmsg buffers and related indexes */

extern kmutex_t framebuffer_lock; /* single thread writes to the frame buffer */

int ncpus = 1;
int ncpus_online = 1;
int interruptthread = 0;

static struct mutex_init_table {
	kmutex_t	*addr;
	char		*name;
};

static struct mutex_init_table mutex_init_table[] = {
	&p0.p_lock,		"proc 0",
	&p0.p_crlock,		"proc 0 crlock",
	&pidlock,		"global process lock",
	&cpu_lock,		"cpu lock",
	&delay_lock,		"delay lock",
	&lwplock,		"user level lwp lock",
	&unsafe_driver,		"MT-unsafe driver mutex",
	&ualock,		"uadmin lock",
	&udevlock,		"getudev lock",
	&flock_lock,		"monitor for file/record locking",
	/*
	 * VM related locks
	 */
	&kas.a_contents,	"kernel as contents lock",
	&swapinfo_lock,		"swapinfo_lock",
	&anoninfo_lock,		"anoninfo_lock",
	&swap_lock,		"swap serialization lock",
	&freemem_lock,		"freemem lock",
	&async_lock,		"async reg lock",
	/*
	 * kmem_fast_alloc freelist locks
	 */
	&ssl_randlock,		"skiplist random number generator",
#ifdef _VPIX
	&segvpix_kmemlock,	"kmem segvpix freelist",
#endif
	&fb_kmemlock,		"kmem fbio freelist lock",
	&pageio_kmemlock,	"pageio buf freelist lock",

	/*
	 * Miscellaneous locks
	 */
	&tod_lock,		"tod_lock",
	&log_lock,		"streams logging lock",
	&aclock,		"acct lock",
	&av_lock,		"autovect lock",
	&thread_stop_lock,	"thread stop lock",

	/*
	 * global device locks
	 */
	&pt_excl,		"master/slave pty table",
	NULL,			NULL
};

/*
 * Initialize global locks in the system
 */
void
mt_lock_init()
{
	struct mutex_init_table *mp;
	char buf[100];

	/*
	 * Initialize kernel address space lock and
	 * global page locks.
	 */
	rw_init(&kas.a_lock, "kernel as lock", RW_DEFAULT, NULL);
	page_lock_init();
	hat_lock_init();

	mutex_init(&framebuffer_lock, "frame buffer lock", MUTEX_DEFAULT, NULL);
	/*
	 * Initialize reap queue lock
	 */
	mutex_init(&reaplock, "reap queue lock",
	    MUTEX_SPIN_DEFAULT, (void *)ipltospl(LOCK_LEVEL));
	/*
	 * Initialize per-CPU statistics locks for master cpu.  The locks
	 * for other cpus will be initialized when they are set to run.
	 */
	sprintf(buf, "master cpu statistics lock");
	mutex_init(&(CPU->cpu_stat.cpu_stat_lock), buf,
	    MUTEX_DEFAULT, NULL);

	mutex_init(&prf_lock, "printf lock",
	    MUTEX_SPIN_DEFAULT, (void *)ipltospl(SPL7));

	for (mp = mutex_init_table; mp->addr != NULL; mp++)
		mutex_init(mp->addr, mp->name, MUTEX_DEFAULT, NULL);

	pplock_init();

	rw_mutex_init();
}
