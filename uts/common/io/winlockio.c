/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident	"@(#)winlockio.c	1.21	95/01/20 SMI"

/*
 * This is the lock device driver.
 *
 * We support the following IOCTL's:
 *
 *   GRABPAGEALLOC:
 *	Compatibility with cgsix device driver lockpage ioctls.	 Lockpages
 *	created this way must be an entire page for compatibility with
 *	older software.	 This ioctl allocates a lock context with its own
 *	private lock page.  The unique "cookie" that identifies this lock is
 *	returned.
 *
 *   GRABPAGEFREE:
 *	Compatibility with cgsix device driver lockpage ioctls.	 This
 *	ioctl releases the lock context allocated by GRABPAGEALLOC.
 *
 *   GRABLOCKINFO:
 *	Returns a one-word flag.  '1' means that multiple clients may
 *	access this lock page.	Older device drivers returned '0',
 *	meaning that only two clients could access a lock page.
 *
 *   GRABATTACH:
 *	Not supported.	This ioctl would have grabbed all lock pages
 *	on behalf of the calling program.
 *
 *   WINLOCKALLOC:
 *	Allocate a lock context.  This ioctl accepts a key value.  as
 *	its argument.  If the key is zero, a new lock context is
 *	created, and its "cookie" is returned.	If the key is nonzero,
 *	all existing contexts are checked to see if they match they
 *	key.  If a match is found, its reference count is incremented
 *	and its cookie is returned, otherwise a new context is created
 *	and its cookie is returned.
 *
 *   WINLOCKFREE:
 *	Free a lock context.  This ioctl accepts the cookie of a lock
 *	context and decrements its reference count.  Once the reference
 *	count reaches zero *and* all mappings are released, the lock
 *	context is freed.  When the last context in a lock page is
 *	freed, that lock page is freed as well.
 *
 *   WINLOCKSETTIMEOUT:
 *	Set lock timeout for a context.	 This ioctl accepts the cookie
 *	of a lock context and a timeout value in milliseconds.
 *	Whenever a lock contention occurs, the lock is broken after
 *	the timeout expires.  If the timeout value is zero, locks do
 *	not timeout.  This value will be rounded to the nearest clock
 *	tick, so don't try to use it for real-time control or something.
 *
 *   WINLOCKGETTIMEOUT:
 *	Get lock timeout from a context.
 *
 *   WINLOCKDUMP:
 *	Dump state of this device.
 *
 *
 * How /dev/lock works:
 *
 *   Every lock context consists of two mappings of the client to the lock
 *   page.  These mappings are known as the "lock page" and "unlock page"
 *   to the client.  Only one process at a time is allowed to have a valid
 *   mapping to a lock page.  A client acquires a lock by writing a '1' to
 *   the lock page.  If it does not already have a valid mapping to that
 *   page, the segment driver takes a fault, validates the client mapping
 *   and allows the client to continue.	 The client releases the lock by
 *   writing a '0' to the unlock page.	Again, if it does not have a valid
 *   mapping to the unlock page, the segment driver takes a fault,
 *   validates the mapping, and lets the client continue.  From this point
 *   forward, the client can make as many locks and unlocks as it
 *   wants, without any more attention from the kernel.
 *
 *   If a different process wants to acquire a lock, it takes a page fault
 *   when it writes the '1' to the lock page.  If the segment driver sees
 *   that the lock page contained a zero, then it invalidates the first
 *   client's mapping and continues as above.
 *
 *   If there is already a '1' in the lock page when the second client
 *   tries to access the lock page, then a lock exists.	 The segment
 *   driver sleeps the second client and, if applicable, starts the
 *   timeout on the lock.  The first client's mapping to the unlock page
 *   is invalidated so that the driver will be woken again when the first
 *   client releases the lock.
 *
 *   When the locking client finally writes a '0' to the unlock page, the
 *   segment driver takes another fault.  The client is given a valid
 *   mapping, not to the unlock page, but to the "trash page", and allowed
 *   to continue.  Meanwhile, the sleeping client is given a valid mapping
 *   to the lock page and allowed to continue as well.
 *
 *   A process is allowed to have multiple mappings to the lock page.
 *   This prevents deadlock.
 *
 *
 */

#include <sys/types.h>		/* various type defn's */
#include <sys/debug.h>		/* define ASSERT and STATIC */
#include <sys/param.h>		/* various kernel limits */
#include <sys/time.h>
#include <sys/buf.h>		/* defines buf struct  XXX */
#include <sys/errno.h>
#include <sys/systm.h>		/* defines system stuff  XXX */
#include <sys/kmem.h>		/* defines kmem_alloc() */
#include <sys/conf.h>		/* defines cdevsw */
#include <sys/autoconf.h>	/* defines SP_* constants */
#include <sys/file.h>		/* various file modes, etc. */
#include <sys/uio.h>		/* UIO stuff  XXX */
#include <sys/ioctl.h>
#include <sys/map.h>		/* resource allocation, etc. */
#include <sys/proc.h>		/* defines proc struct */
#include <sys/user.h>		/* defines user struct */
#include <sys/cred.h>		/* defines cred struct */
#include <sys/vmmac.h>		/* conversion macros */
#include <sys/mman.h>		/* defines mmap(2) parameters */
#include <sys/open.h>		/* defines arguments passed to xx_open/close */
#include <sys/stat.h>		/* defines S_IFCHR */
#include <sys/cmn_err.h>	/* use $!@# cmn_err instead of printf */
#include <sys/vnode.h>		/* defines vnodes */
#include <vm/as.h>		/* defines address-space struct */
#include <vm/seg.h>		/* defines seg struct */
#include <vm/hat.h>		/* defines hat layer */
#include <vm/devpage.h>		/* ???  XXX */
#include <sys/ddi.h>		/* ddi stuff  XXX */
#include <sys/sunddi.h>		/* ddi stuff  XXX */
#include <sys/ddi_impldefs.h>	/* ???  XXX */
#include <sys/fs/snode.h>	/* ???  XXX */
#include <sys/winlockio.h>	/* defines ioctls */
#include <sys/seg_drv.h>	/* segment driver common code */

#include <vm/page.h>

#define	_STMT(op)	do { op } while (0)

#if LOCKDEBUG >= 1
#define	assert(x) ((x) ? 0 : \
    (cmn_err(CE_WARN, "winlock: YAK! assertion failed \"x\" [line %d]\n", \
	__LINE__),0))
#else
#define	assert(x)
#endif

#if LOCKDEBUG >= 2
STATIC	int	lock_debug = 0;
#define DEBUGF(level, args)	_STMT(if (lock_debug >= (level)) cmn_err args;)
#else
#define DEBUGF(level, args)
#endif

#define	ID(x)	((x) && (x)->procp ? x->procp->p_pid : -1)
#define	LOCK(lp)	(*(lp)->lockptr)

#define	UNLINK_LIST(start, ptr)	\
	    (unlink_list((Randobj *)&(start), (Randobj *)(ptr)))

#define	LOCK_OFFBASE	(0)
#define	MAX_LOCKS	(pagesize/sizeof(int))

#define LOCKTIME	3	/* seconds */

#if LOCKDEBUG >= 2

	/* memory-leak testing */

STATIC	int	total_memory = 0 ;

static caddr_t
my_kmem_zalloc(size,flag,line)
	int	size, flag, line ;
{
	caddr_t rval ;
	total_memory += size ;
	rval = kmem_zalloc(size,flag) ;
	DEBUGF(5, (CE_CONT, "zalloc line %d, %d bytes at %x, total=%d\n",
		line, size, rval, total_memory));
	return rval ;
}

static	void
my_kmem_free(ptr,size,line)
	caddr_t	ptr ;
	int	size, line ;
{
	total_memory -= size ;
	DEBUGF(5, (CE_CONT, "free line %d, %d bytes at %x, total=%d\n",
		line, size, ptr, total_memory));
	kmem_free(ptr, size) ;
}

#define	KMEM_ZALLOC(size,flag)	my_kmem_zalloc(size, flag, __LINE__)
#define	KMEM_FREE(ptr,size)	my_kmem_free(ptr, size, __LINE__)



	/* mutex testing */


STATIC	int	last_enter ;
STATIC	int	last_exit ;

my_mutex_enter(mp,line)
	kmutex_t *mp ;
	int	line ;
{
	DEBUGF(5, (CE_CONT, "entering mutex %x at %d\n",mp, line));

	if( MUTEX_HELD(mp)  &&  mutex_owned(mp) )
	    cmn_err(CE_WARN,
	      "winlock: mutex %x already held at %d, last entered at %d\n",
	      mp, line, last_enter);
	else
	    mutex_enter(mp) ;
	last_enter = line ;
}

my_mutex_exit(mp, line)
	kmutex_t *mp ;
	int	line ;
{
	DEBUGF(5, (CE_CONT, "exiting mutex %x at %d\n",mp, line));
	if( MUTEX_HELD(mp) )
	    mutex_exit(mp) ;
	else
	    cmn_err(CE_WARN,
	      "winlock: mutex %x not held at %d, last exited at %d\n",
	      mp, line, last_exit);
	last_exit = line ;
}

#define	MUTEX_ENTER(mp)	my_mutex_enter(mp,__LINE__)
#define	MUTEX_EXIT(mp)	my_mutex_exit(mp,__LINE__)

#else

#define	KMEM_ZALLOC(size,flag)	kmem_zalloc(size, flag)
#define	KMEM_FREE(ptr,size)	kmem_free(ptr, size)

#define	MUTEX_ENTER(mp)	mutex_enter(mp)
#define	MUTEX_EXIT(mp)	mutex_exit(mp)

#endif /* LOCKDEBUG */


/*
 * Returns a pointer to the current proc structure.
 */
static struct proc *
getcurproc()
{
    u_long rval;

    if (drv_getparm(UPROCP, &rval) == -1) {
	return (0);
    } else {
	return ((struct proc *) rval);
    }
}


/* These structures describe a lock context.  We now permit multiple
 * clients (not just two) to access a lock page
 *
 * Note: the "next" pointer in these structures must be first.
 *
 * The "cookie" identifies the lock context.  It contains several
 * pieces of information.  The high-order bits are the "offset" that
 * must be provided to mmap(2).  The low-order bits are the byte offset
 * from the beginning of the mapped page to the lock word.  For
 * oldstyle lock pages, the cookie is the address of the private lock
 * page, with no byte offset.  For new style locks, the cookie is the
 * address of the public lock page plus the byte offset.  Note that in
 * both cases, the cookie is the pointer to the lock word.
 */

/*
 * per-process information about locks.  This is the client field of
 * a seg_drv segment.  Note that *two* segments point to this.
 */

typedef struct segproc {
	struct segproc	*next ;		/* next client of this lock */
	struct seglock	*lp ;		/* associated lock context */
	int		flag;		/* see "flag bits" in winlockio.h */
	struct	proc	*procp;		/* proc info */
	caddr_t		lockaddr;	/* TODO: lose this? */
	struct	seg	*locksegp;	/* lock mapping, if any */
	caddr_t		unlockaddr;	/* TODO: lose this? */
	struct	seg	*unlocksegp;	/* unlock mapping, if any */
	struct vnode	*vp ;		/* vnode associated with device */
	u_char		prot ;		/* current segment prot */
	u_char		maxprot ;	/* maximum segment protections */
	dev_t		dev ;		/* device */
} SegProc;

/* per-lock-context information */

typedef struct seglock {
	struct seglock	*next ;		/* next context */
	int		sleepf ;	/* someone sleeping on this lock */
	int		alloccount ;	/* how many times created? */
	int		mapcount ;	/* how many mappings to this page? */
	u_long		cookie;		/* offset into device (cookie) */
	u_long		key ;		/* key, if any */
	caddr_t		page ;		/* pointer to lock page */
	int		*lockptr;	/* kernel virtual addr of lock */
	int		timeout;	/* sleep time in ticks */
	struct segproc	*clients;	/* list of clients of this lock */
	struct segproc	*owner;		/* current owner of lock */
	kmutex_t	mutex ;		/* mutex lock */
	kcondvar_t	locksleep ;	/* for sleeping on lock */
	int		timeout_pending;/* timeout() has been called */
	int		timeout_id ;	/* ...with this id */
} SegLock ;

typedef	struct randobj {
	  struct randobj *next ;
	} Randobj ;


	/* The trash page is where unwanted writes go
	 * when a process is releasing a lock.
	 */

STATIC	caddr_t trashpage = NULL;	/* for trashing unwanted writes */
STATIC	caddr_t	lockpage = NULL;	/* bunch o' locks */


STATIC	dev_info_t	*winlock_dip ;

STATIC	kmutex_t	winlock_mutex ;	/* to lock linked list of locks */
STATIC	int	winlock_busy = 0 ;	/* # open's */
STATIC	u_int	pagesize, pageoffset, pagemask ;


	/* The allocation code tries to allocate from lock_free_list
	 * first, otherwise it uses kmem_zalloc.  On final close, all
	 * locks in lock_free_list are returned to kernel
	 */
STATIC	SegLock	*lock_list = NULL ;		/* in-use locks */
STATIC	SegLock	*lock_free_list = NULL ;	/* free locks */
STATIC	SegLock	*seglock_findlock();		/* find a lock in lock_list */
STATIC	int	next_lock = 0 ;			/* next lock cookie */

STATIC	void	winlock_lockmap(struct seg *, caddr_t, caddr_t);
STATIC	void	winlock_unlockmap(struct seg *);

static	int	seglock_dup(struct seg *, struct seg *);
static	void	seglock_free(struct seg *);
static	faultcode_t seglock_fault(struct hat *, struct seg *, caddr_t, u_int,
			enum fault_type, enum seg_rw);

struct	seg_ops seglock_ops =  {
	seglock_dup,	/* dup */
	NULL,		/* unmap */
	seglock_free,	/* free */
	seglock_fault,	/* fault */
	NULL,		/* faulta */
	NULL,		/* setprot */
	NULL,		/* checkprot */
	NULL,		/* kluster */
	(u_int (*)()) NULL,	/* swapout */
	NULL,		/* sync */
	NULL,		/* incore */
	NULL,		/* lockop */
	NULL,		/* getprot */
	NULL,		/* getoffset */
	NULL,		/* gettype */
	NULL,		/* getvp */
	NULL,		/* advise */
	NULL,		/* dump */
};


/* TODO: nuke this? */
static	int
winlock_identify(devi)
	dev_info_t *devi ;
{
	if( strcmp(ddi_get_name(devi), "winlock") == 0)
		return DDI_IDENTIFIED ;
	else
		return DDI_NOT_IDENTIFIED ;
}


/*ARGSUSED*/
static	int
winlock_attach(devi, cmd)
	dev_info_t		*devi ;
	ddi_attach_cmd_t	cmd ;
{
	DEBUGF(1, (CE_CONT, "winlock_attach, devi=%x, cmd=%d, mem used=%d\n",
		devi, (int)cmd, total_memory));
	winlock_dip = devi ;
	if( ddi_create_minor_node(devi, "winlock", S_IFCHR, 0, DDI_PSEUDO, 0)
	    == DDI_FAILURE ) {
		ddi_remove_minor_node(devi, NULL);
		DEBUGF(1, (CE_CONT,
			"winlock_attach, create_minor_node failed\n"));
		return DDI_FAILURE ;
	}

	return DDI_SUCCESS ;
}


static	int
winlock_detach(devi)
	dev_info_t		*devi ;
{
	DEBUGF(1, (CE_CONT, "winlock_detach, devi=%x, mem used=%d\n",
		devi, total_memory));
	assert( winlock_dip == devi ) ;
	assert( lock_list == NULL && !winlock_busy ) ;

	/* shut down device */

	MUTEX_ENTER(&winlock_mutex);
	lock_destroyall() ;
	MUTEX_EXIT(&winlock_mutex);

	return DDI_SUCCESS ;
}



/*ARGSUSED*/
static	int
winlock_open(dev, flag, otyp, cred)
	dev_t	*dev;
	int	flag;
	int	otyp ;
	cred_t	*cred ;
{
	DEBUGF(2, (CE_CONT, "winlock_open, flag=%x, mem used=%d\n",
		flag, total_memory));

	/* make sure we don't get called during _fini */
	/* TODO: nuke this? */
	MUTEX_ENTER(&winlock_mutex);
	++winlock_busy ;
	MUTEX_EXIT(&winlock_mutex);

	return 0 ;
}

/*ARGSUSED*/
int
winlock_close(dev, flag, otyp, cred)
	dev_t	dev ;
	int	flag ;
	int	otyp ;
	cred_t	*cred ;
{
	SegLock	*lp, *next ;

	DEBUGF(2, (CE_CONT, "\nwinlock_close(%d), mem used=%d\n",
		getminor(dev), total_memory));

	/* Grab driver-wide lock.  TODO: nuke this? */
	MUTEX_ENTER(&winlock_mutex);

	/* implicitly execute a free on all contexts */

	for(lp = lock_list; lp != NULL; lp = next) {
	    next = lp->next ;
	    lp->alloccount = 0 ;
	    if( lp->mapcount <= 0 )
		seglock_destroylock(lp) ;
	}

#if LOCKDEBUG >= 2
	if( lock_debug >= 3 )
	    dump_all() ;
#endif
	winlock_busy = 0 ;
	MUTEX_EXIT(&winlock_mutex);
	return 0 ;
}


/* TODO: Nuke this? */
static	int
winlock_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev = (dev_t) arg;
	register int instance, error;
	register struct cg6_softc *softc;

	if ((instance = getminor(dev)) >= 1)
		return (DDI_FAILURE);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *) winlock_dip ;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}


/*ARGSUSED*/
int
winlock_ioctl(dev, cmd, arg, mode, cred, rval)
	dev_t	dev ;
	int	cmd ;
	int	arg ;
	int	mode ;
	cred_t	*cred ;
	int	*rval ;
{
    DEBUGF(3,(CE_CONT, "winlockioctl: cmd=%d, arg=0x%x\n",cmd,arg));

    switch(cmd)
    {
	/* ioctls that used to be handled by /dev/cgsix.  Need to emulate
	 * cgsix behavior.
	 */

	case GRABPAGEALLOC:
	    return(seglock_graballoc(arg, 1, mode));
	case GRABLOCKINFO:
	    return(seglock_grabinfo(arg, mode));
	case GRABATTACH:
	    return(EINVAL);	/* GRABATTACH is not supported (never was) */

	case WINLOCKALLOC:
	    return (seglock_graballoc(arg, 0, mode));
	case WINLOCKFREE:
	case GRABPAGEFREE:
	    return (seglock_grabfree(arg, mode));
	case WINLOCKSETTIMEOUT:
	    return (seglock_settimeout(arg, mode));
	case WINLOCKGETTIMEOUT:
	    return (seglock_gettimeout(arg, mode));
	case WINLOCKDUMP:
	    seglock_dump_all() ;
	    return 0 ;

#if LOCKDEBUG >= 2
	case 255:
	    lock_debug = arg ;
	    return 0 ;
#endif

	default:
	    return ENOTTY;
    }
}


winlock_mmap(dev,off,prot)
	dev_t	dev ;
	off_t	off ;
	int	prot ;
{
	return trashpage == NULL ? -1 : hat_getkpfnum(trashpage) ;
}


/*ARGSUSED*/
int
winlock_segmap(dev, off, as, addr, len, prot, maxprot, flags, cred)
    dev_t	dev ;		/* major:minor */
    off_t	off ;		/* cookie from mmap(2) */
    struct as	*as ;		/* user's address space. */
    caddr_t	*addr ;		/* address from mmap(2) */
    off_t	len ;		/* length from mmap(2) */
    u_int	prot ;		/* user wants this access */
    u_int	maxprot ;	/* this is the maximum the user can have */
    u_int	flags ;		/* flags from mmap(2) */
    cred_t	*cred ;
{
    struct segproc *sdp;
    int seglock_create();
    register int i;
    register SegLock *seglock;
    SegProc *seglock_findclient() ;

    DEBUGF(3, (CE_CONT,
	"\nwinlock_segmap: dev=%x,off=%x,as=%x,addr=%x,len=%x,flags=%x\n",
	dev, off, as, addr, len, flags));

    if( (seglock = seglock_findlock((u_long)off)) != NULL )
    {
	if (len != pagesize)	/* TODO: do we need to check this? */
	    return(EINVAL);
    }
    else
	return ENXIO ;

    MUTEX_ENTER(&seglock->mutex);

    sdp = seglock_findclient(seglock) ;
    ++seglock->mapcount ;

    /* Now, create the mapping in the user's address space. */

    DEBUGF(3, (CE_CONT, "winlock_segmap: as=%x,*addr=%x,len=%x,sdp=%x\n",
	as, *addr, len, sdp));

    MUTEX_EXIT(&seglock->mutex);
    return segdrv_segmap(dev, off, as, addr, len, prot, maxprot,
	flags, cred, (caddr_t)sdp, &seglock_ops, seglock_create);
}

static	int
seglock_create(seg, argsp)
	struct seg	*seg ;		/* segment */
	caddr_t		argsp ;		/* old private data if this is unmap */
{
	register Segdrv_Data *segdat = (Segdrv_Data *)seg->s_data;
	register SegProc *sdp = (SegProc *)segdat->client;

	DEBUGF(3, (CE_CONT, "seglock_create: seg:%x, argsp:%x\n", seg, argsp));

	/* Note: if we're called from unmap due to a split segment, both
	 * new segments will have the same client pointer.  Actually, this
	 * should never happen anyway since all lockpage mappings are only
	 * one page.
	 */

#ifdef	LOCKDEBUG
	if( argsp != NULL )	/* called from unmap */
	{
	  printf("seglock_create called from unmap()\n") ;
	}
#endif	/* LOCKDEBUG */


	if( sdp->locksegp == NULL ) {
	    sdp->locksegp = seg;
	    sdp->lockaddr = seg->s_base;	/* TODO: nuke this? */
	} else
	if( sdp->unlocksegp == NULL ) {
	    sdp->unlocksegp = seg;
	    sdp->unlockaddr = seg->s_base;	/* TODO: nuke this? */
	} else {
	    DEBUGF(1, (CE_CONT,
		"winlock: YAK! attempt to map a lock context thrice\n"));
	    return (ENOMEM) ;
	}

	return SEGDRV_CONTINUE ;
}

/* duplicate a segment, as in fork()
 * The results of trying to use lock pages after a fork are undefined.
 * In fact, the results of unmapping lock pages after a fork are probably
 * also undefined.  TODO: look into this.
 */

static int
seglock_dup(seg,newseg)
	struct seg *seg, *newseg ;
{
	register Segdrv_Data *segdat = (Segdrv_Data *)seg->s_data;
	register SegProc *ndp, *sdp = (SegProc *)segdat->client;
	register SegLock *lp = sdp->lp ;

	DEBUGF(3, (CE_CONT, "\nseglock_dup: seg=%x, newseg=%x, sdp=%x, lp=%x\n",
		seg, newseg, sdp, lp));


	/* can't use seglock_findclient() since newseg belongs to a process
	 * whose process id hasn't been assigned yet.  Instead, we make
	 * this an orphan segment by allocating a SegProc structure for
	 * it and setting everything to NULL.  The first time there's a
	 * fault on this page, it will be matched up with its parent.
	 */

	MUTEX_ENTER(&lp->mutex);
	ndp = (SegProc *) KMEM_ZALLOC(sizeof(SegProc),KM_SLEEP) ;
	ASSERT(ndp != NULL);
	segdat = (Segdrv_Data *)newseg->s_data ;
	segdat->client = (caddr_t) ndp ;
	ndp->lp = lp ;
	++lp->mapcount ;
	MUTEX_EXIT(&lp->mutex);
	return SEGDRV_CONTINUE ;
}


static void
seglock_free(seg)
	struct seg *seg;
{
	register Segdrv_Data	*segdat = (Segdrv_Data *)seg->s_data;
	register SegProc	*sdp = (SegProc *)segdat->client;
	register SegLock	*lp = sdp->lp ;
	int			s ;

	DEBUGF(3, (CE_CONT, "\nseglock_free: seg:%x,pid:%x\n",
		seg, getcurproc()->p_pid));
	assert(lp != NULL && sdp != NULL) ;
	DEBUGF(4, (CE_CONT, "  sdp = %x, lp=%x, lp->owner = %x\n",
		sdp,lp,lp->owner)) ;

	MUTEX_ENTER(&lp->mutex);

	/* make sure this process doesn't own any locks */
	if( sdp == lp->owner )
	    lock_wakeup(lp,0) ;

	if( sdp->procp == NULL ) {		/* orphan page */
	    DEBUGF(3,(CE_CONT, "  free orphan page\n"));
	    KMEM_FREE((caddr_t)sdp, sizeof(*sdp)) ;
	}
	else {
	    if( sdp->locksegp == seg ) {
		sdp->locksegp = NULL ;
	    } else
	    if( sdp->unlocksegp == seg ) {
		sdp->unlocksegp = NULL ;
	    } else {
		DEBUGF(1,(CE_CONT,
			"winlock: Yak! seg %x not found in sdp %x\n",
			seg,sdp));
	    }

	    /* see if this finishes the client structure */
	    if( sdp->locksegp == NULL  &&  sdp->unlocksegp == NULL )
		seglock_deleteclient(lp,sdp) ;
	}

	/* see if this finished the entire lock context */

	if( --lp->mapcount <= 0  &&  lp->alloccount <= 0 ) {
	    MUTEX_ENTER(&winlock_mutex);
	    MUTEX_EXIT(&lp->mutex);
	    seglock_destroylock(lp) ;
	    MUTEX_EXIT(&winlock_mutex);
	}
	else
	    MUTEX_EXIT(&lp->mutex);
}

/*ARGSUSED*/
static	faultcode_t
seglock_fault(hat, seg, addr, len, type, rw)
	register struct hat *hat;
	register struct seg *seg;
	caddr_t addr;
	u_int len;
	enum fault_type type;
	enum seg_rw rw;
{
	register Segdrv_Data	*segdat;
	register SegProc	*sdp;
	register SegLock	*lp;
	faultcode_t		rval ;

	DEBUGF(3, (CE_CONT,
	    "\nseglock_fault: seg = %x, addr = %x, len=%x, type=%d, rw=%d\n",
	    seg, addr, len, (int)type, (int)rw));

	if( type == F_PROT || type == F_SOFTLOCK || type == F_SOFTUNLOCK )
		return SEGDRV_CONTINUE ;

	assert(type == F_INVAL) ;

top:

	segdat = (Segdrv_Data *)seg->s_data;
	sdp = (SegProc *)segdat->client;
	lp = sdp->lp ;

	MUTEX_ENTER(&lp->mutex);
	if ((rval = seglock_lockfault(seg,addr,sdp,lp)) != 0) {
		/*
		 * The only case where the return value is non-zero is when an
	  	 * attempt to obtain the 'as' lock of the thread (not the
		 * current thread whose fault is being resolved here) whose
		 * mapping's have to be unloaded, fails.
		 * We drop the mutex and retry the fault handling
		 */
		MUTEX_EXIT(&lp->mutex);
		delay(HZ/8);
		goto top;
	}
	MUTEX_EXIT(&lp->mutex);
	return(SEGDRV_HANDLED);
}





	/* INTERNAL ROUTINES START HERE */



static caddr_t
winlock_getpages(npages,flag)
	int npages,flag;
{
	register caddr_t	p1;

       	p1 = KMEM_ZALLOC(ptob(npages), flag);
	ASSERT (p1 != NULL && !((u_int)p1 & pageoffset));
	return(p1);
}


static void
winlock_freepages(ptr, size)
	void	*ptr ;
	int	size ;
{
	assert (ptr != NULL && !((u_int)ptr & pageoffset));
	KMEM_FREE(ptr, ptob(size));
}


/*
 * search the lock_list list for the specified cookie (mt-safe)
 *
 */

static SegLock *
seglock_findlock(cookie)
	u_long	cookie;
{
	register SegLock	*lp ;

	MUTEX_ENTER(&winlock_mutex);
	cookie &= pagemask ;
	for(lp=lock_list; lp != NULL; lp = lp->next)
	    if( cookie == lp->cookie ) {
		MUTEX_EXIT(&winlock_mutex);
		return(lp);
	    }
	MUTEX_EXIT(&winlock_mutex);
	return((struct seglock *)NULL);
}



/*
 * search the lock_list list for the specified key
 */

static SegLock *
seglock_findkey(key)
	u_long	key ;
{
	register SegLock	*lp ;

	for(lp=lock_list; lp != NULL; lp = lp->next)
	    if( key == lp->key )
		return(lp);
	return((struct seglock *)NULL);
}


/* search for an item in a linked list and remove it from that list */

static
unlink_list(first,ptr)
    register Randobj	*first, *ptr ;
{
	for(; first != NULL; first = first->next )
	    if( first->next == ptr ) {
		first->next = ptr->next ;
		return ;
	    }
}


/*
 * Create a new lock context
 */

static SegLock *
seglock_createlock(oldstyle)
	int	oldstyle ;	/* GRABPAGEALLOC requires private lock page */
{
	register SegLock	*lp ;

	DEBUGF(3, (CE_CONT, "seglock_createlock: free_list=%x\n",
		lock_free_list));

	if( oldstyle ) {
	    lp = (SegLock *) KMEM_ZALLOC(sizeof(SegLock), KM_SLEEP);
	    DEBUGF(4,(CE_CONT, "  allocating oldstyle context at %x\n", lp));
	    if( lp == NULL )
		return(NULL) ;
	    lp->page = winlock_getpages(1,KM_SLEEP);
	    lp->lockptr = (int *) lp->page ;
	    lp->cookie = (off_t) lp->page ;
	    DEBUGF(4,(CE_CONT, "  allocating oldstyle page at %x\n",
		lp->page));
	} else
	if( lock_free_list != NULL ) {
	    lp = lock_free_list ;
	    lock_free_list = lp->next ;
	} else if( next_lock >= MAX_LOCKS ) {
	    return(NULL);
	} else {
	    lp = (SegLock *) KMEM_ZALLOC(sizeof(SegLock), KM_SLEEP);
	    if( lp == NULL )
		return(NULL) ;
	    lp->cookie = (u_long) lockpage + next_lock * pagesize ;
	    lp->page = lockpage ;
	    lp->lockptr = (int *) lockpage + next_lock ;
	    ++next_lock ;
	}

	lp->sleepf = 0;
	lp->alloccount = 1;
	lp->mapcount = 0;
	lp->timeout = LOCKTIME*hz ;
	lp->clients = NULL;
	lp->owner = NULL;
	lp->timeout_pending = 0 ;
	LOCK(lp) = 0 ;
	mutex_init(&lp->mutex, "winlock", MUTEX_DEFAULT, DEFAULT_WT);
	cv_init(&lp->locksleep, "winlock", CV_DEFAULT, DEFAULT_WT);
	lp->next = lock_list ;
	lock_list = lp ;

	DEBUGF(3, (CE_CONT, "seglock_createlock done lp=%x\n", lp));
	return(lp) ;
}


static
seglock_destroylock(lp)
	register SegLock	*lp ;
{
	DEBUGF(3, (CE_CONT, "seglock_destroylock: lp=%x\n", lp));

	{
	  register SegProc	*pp, *next ;

	  for(pp = lp->clients; pp != NULL; pp = next) {
	      next = pp->next ;
	      assert(pp->locksegp == NULL && pp->unlocksegp == NULL);
	      KMEM_FREE((caddr_t)pp, sizeof(SegProc)) ;
	  }
	}

	UNLINK_LIST(lock_list, lp) ;

	if( lp->page != lockpage ) {	/* old-style GRABPAGEALLOC */
	    DEBUGF(4, (CE_CONT,
	      "  seglock_destroylock returns oldstyle page at %x\n", lp->page));
	    winlock_freepages(lp->page,1) ;
	    DEBUGF(4, (CE_CONT,
		"  seglock_destroylock returns oldstyle context at %x\n", lp));
	    mutex_destroy(&lp->mutex);
#ifdef	COMMENT
	    cv_destroy(&lp->locksleep);
#endif	/* COMMENT */
	    KMEM_FREE((caddr_t)lp,sizeof(SegLock)) ;
	} else {
	    lp->next = lock_free_list ;
	    lock_free_list = lp ;
	    mutex_destroy(&lp->mutex);
#ifdef	COMMENT
	    cv_destroy(&lp->locksleep);
#endif	/* COMMENT */
	}

	if( lock_list == NULL )
	    lock_destroyall() ;

	DEBUGF(3, (CE_CONT, "  seglock_destroylock done\n"));
}



/*
 * search a context's client list for the current process.
 * create an entry if neccessary.
 */

static SegProc *
seglock_findclient(lp)
	SegLock	*lp ;
{
	register SegProc *pp ;

	DEBUGF(3, (CE_CONT, "seglock_findclient: lp=%x\n", lp));

	for( pp = lp->clients; pp != NULL; pp = pp->next)
	    if( pp->procp == getcurproc() )
		return(pp) ;

	/* didn't find it, better create it. */
	pp = (SegProc *) KMEM_ZALLOC(sizeof(SegProc),KM_SLEEP) ;
	ASSERT(pp != NULL) ;
	pp->next = lp->clients ;
	lp->clients = pp ;
	pp->lp = lp ;
	pp->procp = getcurproc() ;

	DEBUGF(3, (CE_CONT, "  seglock_findclient creates pp=%x\n",pp));

	return(pp) ;
}

/*
 * search a context's client list for the given client and delete
 */

static
seglock_deleteclient(lp, pp)
	SegLock	*lp ;
	SegProc	*pp ;
{
	UNLINK_LIST(lp->clients, pp) ;

	KMEM_FREE((caddr_t)pp, sizeof(SegProc)) ;
}




	/* IOCTLS START HERE */




static int
seglock_grabinfo(arg, mode)	/* IOCTL */
	int	arg;
	int	mode;
{
	int i = 1;

	/* multilock now supported */
	(void) ddi_copyout((caddr_t)&i, (caddr_t)arg, sizeof (int), mode);
	return (0);
}



static int
seglock_graballoc(arg, oldstyle, mode)	/* IOCTL */
	int	arg;
	int	oldstyle ;
	int	mode;
{
	register	int i;
	register	struct seglock	*lp = lock_list;
	u_long		key ;
	struct		winlockalloc wla;

	DEBUGF(3, (CE_CONT,
		"seglock_graballoc: arg=%x, style=%d\n", arg,oldstyle));

	if (oldstyle) {
	    key = 0;
	} else {
	    if (ddi_copyin((caddr_t)arg, (caddr_t)&wla, sizeof (wla), mode)) {
		return (EFAULT);
	    }
	    key = wla.sy_key;
	}


	/* for redirecting unwanted writes */
	MUTEX_ENTER(&winlock_mutex);
	if (trashpage == NULL) {
	    lockpage = (caddr_t) winlock_getpages(2,KM_SLEEP);
	    trashpage = lockpage + pagesize;
	    DEBUGF(4,(CE_CONT, "  allocating lock pages at %x\n", lockpage));
#if LOCKDEBUG
	    for(i=0;i<pagesize/sizeof(long);i++)
		*((int *)lockpage + i) = i; 
#endif
	}

	/* is there a key?  Does it match an existing lock? */

	if( key != 0  &&  (lp=seglock_findkey(key)) != NULL )
	    ++lp->alloccount ;
	else if( (lp=seglock_createlock(oldstyle)) != NULL )
	    lp->key = key ;
	else {
	    MUTEX_EXIT(&winlock_mutex);
	    return(ENOMEM);
	}


	if (oldstyle) {
	    (void) ddi_copyout((caddr_t)&lp->cookie, (caddr_t)arg,
		sizeof (long), mode);
	} else {
	    wla.sy_ident = (u_long)lp->cookie +
		    ((int)lp->lockptr & pageoffset);
	    (void) ddi_copyout((caddr_t)&wla, (caddr_t)arg, sizeof (wla), mode);
	}
		
	MUTEX_EXIT(&winlock_mutex);
	return(0) ;
}



static int
seglock_grabfree(arg, mode)	/* IOCTL */
	int	arg;
	int	mode;
{
    register	struct seglock	*lp;
    u_long	cookie;

    DEBUGF(3, (CE_CONT, "seglock_grabfree: arg=%x\n", arg));

    if (ddi_copyin((caddr_t)arg, (caddr_t)&cookie, sizeof (u_long), mode)
	!= 0) {
	return (EFAULT);
    }
    if ((lp = seglock_findlock(cookie)) == NULL)
	return(EINVAL);

    MUTEX_ENTER(&lp->mutex);
    if( --lp->alloccount <= 0  &&  lp->mapcount <= 0 ) {
	MUTEX_ENTER(&winlock_mutex);
	MUTEX_EXIT(&lp->mutex);
	seglock_destroylock(lp) ;
	MUTEX_EXIT(&winlock_mutex);
    }
    else
	MUTEX_EXIT(&lp->mutex);

    return(0);
}

static int
seglock_settimeout(arg, mode)	/* IOCTL */
	int	arg;
	int	mode;
{
	register SegLock		*lp ;
	register SegProc		*pp ;
	struct winlocktimeout		wlt;

	if (ddi_copyin((caddr_t)arg, (caddr_t)&wlt, sizeof (wlt), mode) != 0) {
	    return (EFAULT);
	}

	if( (lp = seglock_findlock(wlt.sy_ident)) == NULL )
	    return EINVAL ;

	MUTEX_ENTER(&lp->mutex);
	lp->timeout = wlt.sy_timeout * hz / 1000 ;
	/* watch out for roundoff error */
	if( lp->timeout == 0  &&  wlt.sy_timeout != 0 )
	  lp->timeout = 1 ;
	pp = seglock_findclient(lp) ;
	pp->flag = pp->flag & KFLAGS  |  wlt.sy_flags & UFLAGS ;
	MUTEX_EXIT(&lp->mutex);

	return 0 ;
}

static int
seglock_gettimeout(arg, mode)
	int	arg;
	int	mode;
{
	register SegLock		*lp ;
	struct winlocktimeout		wlt;

	if (ddi_copyin((caddr_t)arg, (caddr_t)&wlt, sizeof (wlt), mode) != 0) {
	    return (EFAULT);
	}

	if( (lp = seglock_findlock(wlt.sy_ident)) == NULL )
	  return EINVAL ;

	wlt.sy_timeout = lp->timeout * 1000 / hz ;
	wlt.sy_flags = seglock_findclient(lp)->flag & UFLAGS ;

	(void) ddi_copyout((caddr_t)&wlt, (caddr_t)arg, sizeof (wlt), mode);

	return 0 ;
}

static int
seglock_dump_all()	/* IOCTL */
{
	MUTEX_ENTER(&winlock_mutex);
	dump_all() ;
	MUTEX_EXIT(&winlock_mutex);
	return 0 ;
}





#define	_PAGE_LOOKUP(addr, vp, off, seg)	\
	devpage_lookup(vp, off + ((addr) - (seg)->s_base), PG_SHARED)

#define _LOCKMAP(sp, addr, pgaddr)	winlock_lockmap(sp, addr, pgaddr)
#define _LOCKUNMAP(sp)			winlock_unlockmap(sp)

#ifdef	COMMENT
#define	_LOCKMAP(sp,addr,page)	segdrv_loadpages(sp,addr,page,1)
#endif	/* COMMENT */



/*
 * handle a lock time-out.  This is the BOTTOM half of the driver,
 * be careful.
 */
static void
seglock_timeout(lp)
    struct	seglock *lp;
{
    cmn_err(CE_CONT,"winlock: lock timed out on process %d\n", ID(lp->owner));

    lock_wakeup(lp,1) ;		/* locking process now loses the lock page */
}

/*
 * winlock_lockmap()
 */
static void
winlock_lockmap(struct seg *seg, caddr_t addr, caddr_t pgaddr)
{
	u_int pfnum;
	register page_t *pp;
	register struct as *as;

	as = seg->s_as;

	ASSERT(AS_LOCK_HELD(as, &as->a_lock));

	pfnum = hat_getkpfnum(pgaddr);
	pp = page_numtopp_nolock(pfnum);

	ASSERT(pp != NULL);

	(void) hat_memload(seg->s_as->a_hat, seg->s_as, addr, pp,
		PROT_READ|PROT_WRITE|PROT_USER, 0);
}

/*
 * winlock_unlockmap()
 */
static void
winlock_unlockmap(struct seg *seg)
{
	register struct as *as;

	as = seg->s_as;

	ASSERT(AS_LOCK_HELD(as, &as->a_lock));

	(void) hat_unload(seg->s_as, seg->s_base, seg->s_size, HAT_UNLOAD);
}

/*
 * Handle lock segment faults here...
 *
 * This is where the magic happens.
 *
 * mutex already held at this point
 */

/* ARGSUSED */
static	faultcode_t
seglock_lockfault(seg,addr,sdp,lp)
	register struct seg *seg;
	caddr_t	addr;
	register SegProc *sdp ;
	register SegLock *lp ;
{
	register SegProc	*ndp;
	register struct as *old_as;
	void	hat_devload();
	void	hat_unload();
	extern	int	hz;
	int	s;
	clock_t	win_lbolt;

	assert(sdp != NULL && lp != NULL) ;
	DEBUGF(3, (CE_CONT,
		"seglock_lockfault: seg=%x, sdp=%x, lp=%x proc %d\n",
		seg, sdp, lp, ID(sdp))) ;

#if LOCKDEBUG
	if( sdp == NULL )
	    return (FC_MAKE_ERR(EFAULT));

	if( lp == NULL)
	    return (FC_MAKE_ERR(EFAULT));
#endif

	/* is this an orphan page?  If so, find it's parent and bind it. */

	if( sdp->procp == NULL ) {
	    register Segdrv_Data *segdat = (Segdrv_Data *)seg->s_data;

	    assert(sdp->locksegp == NULL);
	    assert(sdp->unlocksegp == NULL);
	    ndp = seglock_findclient(lp) ;
	    DEBUGF(3,(CE_CONT,
		"matching orphan page %x with parent %x\n",sdp,ndp));
	    ndp->flag = sdp->flag ;
	    if( ndp->locksegp == NULL ) {
		ndp->locksegp = seg ;
		ndp->lockaddr = seg->s_base;	/* TODO: lose this? */
	    } else
	    if( ndp->unlocksegp == NULL ) {
		ndp->unlocksegp = seg ;
		ndp->unlockaddr = seg->s_base;	/* TODO: lose this? */
	    } else
	      assert(0) ;
	    KMEM_FREE((caddr_t)sdp, sizeof(*sdp)) ;
	    sdp = ndp ;
	    segdat->client = (caddr_t) ndp ;
	}


	/* Got past all the sanity checking.  Take care of the fault. */


	/* Before reading the lock value in LOCK(lp), we must make sure that
	    the owner cannot change its value before we change the mappings
	    or else we could end up either with a hung process or more than
	    one process thinking they have the lock.
	*/
	if (lp->owner != NULL  &&  sdp != lp->owner) {
	    old_as = lp->owner->locksegp->s_as;
	    if (!rw_tryenter(&old_as->a_lock, RW_READER))
		return (1);
	    if (lp->owner->flag & LOCKMAP) {
		DEBUGF(4, (CE_CONT, "  owner loses lock mapping\n"));
		assert(lp->owner->locksegp != NULL) ;
		_LOCKUNMAP(lp->owner->locksegp);
		lp->owner->flag &= ~LOCKMAP;
	    }
	    if (lp->owner->flag & UNLOCKMAP) {
		DEBUGF(4, (CE_CONT, "  owner loses unlock mapping\n"));
		assert(lp->owner->unlocksegp != NULL) ;
		_LOCKUNMAP(lp->owner->unlocksegp);
		lp->owner->flag &= ~(TRASHPAGE|UNLOCKMAP) ;
	    }
	    AS_LOCK_EXIT(old_as, &old_as->a_lock);
	}

	if( LOCK(lp) == 0 ) {		/* if not locked, go ahead */
	    DEBUGF(4, (CE_CONT, "  unlocked, calling give_mapping()\n")) ;
	    return (give_mapping(lp, sdp)) ;
	}

#if LOCKDEBUG
	if( seg == sdp->locksegp )		/* TODO: lose these? */
	    assert(sdp->lockaddr == addr) ;
	else if( seg == sdp->unlocksegp )
	    assert(sdp->unlockaddr == addr) ;
#endif LOCKDEBUG

	/* There's a lock.  This gets ugly.  If the owning process is
	 * trying to write to the unlock page, give it a trashpage mapping
	 * and awaken any sleepers.
	 */

	DEBUGF(4, (CE_CONT, "  locked\n")) ;

	/* did the owner take a fault? */
	if( sdp == lp->owner ) {
	    DEBUGF(4, (CE_CONT, "  owner faulted\n")) ;
	    if( seg == sdp->locksegp ) {
		/* owner shouldn't fault on it's own lockpage, but sometimes
		 * the system takes mappings away asynchronously.
		 */
		DEBUGF(4, (CE_CONT, "  ..on lockpage\n"));
		_LOCKMAP(sdp->locksegp,sdp->lockaddr,lp->page);
		sdp->flag |= LOCKMAP;
	    } else if( lp->sleepf == 0 ) {
		/* either the sleeper has exited, or this is another
		 * case where the system took our mapping away
		 */
		DEBUGF(4, (CE_CONT, "  ..on unlockpage, no sleeper\n"));
		if( sdp->flag & TRASHPAGE ) {
		    ASSERT(sdp->unlocksegp->s_as == seg->s_as);
		    _LOCKUNMAP(sdp->unlocksegp);
		    sdp->flag &= ~TRASHPAGE ;
		}
		_LOCKMAP(sdp->unlocksegp,sdp->unlockaddr,lp->page);
		sdp->flag |= UNLOCKMAP ;
	    } else {
		/* sleeper gets valid mappings */
		DEBUGF(4, (CE_CONT, "  there is a sleeper\n"));
		lock_wakeup(lp,1) ;
	    }
	    return(0) ;
	}


	/* Non-owning process tried to write (presumably to the lock page,
	 * but it doesn't matter).  Put it to sleep, and start the timer
	 * if necessary.
	 * Note: lock owner has already lost unlockpage mapping so we'll
	 * wake up.
	 */


	/* If there was already somebody sleeping on the lock, we just
	 * sleep along.  When the current sleeper wakes up, we'll wake
	 * up too, take another page fault, and go back to sleep
	 */
	if( lp->sleepf == 0 ) {
	    lp->sleepf = 1 ;
	    ASSERT(lp->owner != NULL) ;
	    drv_getparm(LBOLT, (unsigned long *)&win_lbolt);
	    if( lp->timeout != 0  && !(lp->owner->flag & SY_NOTIMEOUT) ) {
		    if (cv_timedwait(&lp->locksleep, &lp->mutex,
				win_lbolt + lp->timeout) <= 0) {
			    if (lp->owner == NULL) {
				    lp->sleepf = 0;
				    return (1);
			    }
			    if (lp->owner->unlocksegp != NULL) {
				    old_as = lp->owner->unlocksegp->s_as;
				    if (!rw_tryenter(&old_as->a_lock,
						RW_READER)) {
					    lp->sleepf = 0;
					    return (1);
				    }
				    (void) seglock_timeout(lp);
	    			    AS_LOCK_EXIT(old_as, &old_as->a_lock);
			    } else {
				    (void) seglock_timeout(lp);
			    }
		    }
	    }
	}

	DEBUGF(4, (CE_CONT, "  proc %d sleeping on lp=%x\n", ID(sdp), lp)) ;
#ifdef	COMMENT
	(void)sleep((caddr_t)lp, PRIBIO);	/* goodnight, gracie */
#endif	/* COMMENT */
	while (lp->sleepf)
		cv_wait(&lp->locksleep, &lp->mutex);
	DEBUGF(4, (CE_CONT, "  proc %d wakes up\n", ID(sdp), lp)) ;

	return(0) ;
}


/* Utility: give a valid mapping to lock and unlock pages to specified
 * process, if any.  If some other process has mappings, it loses them.
 */

static int
give_mapping(lp, pp)
	register SegLock	*lp;	/* lock context */
	register SegProc	*pp;	/* new owner */
{
	register struct as *old_as;
	DEBUGF(4, (CE_CONT,
		"give_mapping: owner=%d, pp=%d\n", ID(lp->owner),ID(pp)));
	/* previous owner, if any, loses all mappings */
	if( lp->owner != NULL  &&  pp != lp->owner ) {
	    old_as = lp->owner->locksegp->s_as;
	    if (!rw_tryenter(&old_as->a_lock, RW_READER))
		return (1);
	    if (lp->owner->flag & LOCKMAP) {
		DEBUGF(4, (CE_CONT, "  owner loses lock mapping\n"));
		assert(lp->owner->locksegp != NULL) ;
		_LOCKUNMAP(lp->owner->locksegp);
		lp->owner->flag &= ~LOCKMAP;
	    }
	    if (lp->owner->flag & UNLOCKMAP) {
		DEBUGF(4, (CE_CONT, "  owner loses unlock mapping\n"));
		assert(lp->owner->unlocksegp != NULL) ;
		_LOCKUNMAP(lp->owner->unlocksegp);
		lp->owner->flag &= ~(TRASHPAGE|UNLOCKMAP) ;
	    }
	    lp->owner = NULL ;
	    AS_LOCK_EXIT(old_as, &old_as->a_lock);
	}

	/* we have a new owner now */
	if( pp != NULL ) {
	    lp->owner = pp ;
	    if( pp->locksegp != NULL ) {
		DEBUGF(4, (CE_CONT,
			"    new owner %d gets lock mapping\n",ID(pp))) ;
		_LOCKMAP(pp->locksegp, pp->lockaddr, lp->page) ;
		pp->flag |= LOCKMAP ;
	    }

	    /* while we're here, give new owner a valid mapping to unlock
	     * page so we don't get called again.
	     */
	    if( pp->unlocksegp != NULL ) {
		DEBUGF(4, (CE_CONT,
			"    new owner %d gets unlock mapping\n", ID(pp))) ;
		/* blow away any current unlock mappings */
		if( pp->flag & TRASHPAGE ) {
		    _LOCKUNMAP(pp->unlocksegp);
		    pp->flag &= ~TRASHPAGE;
		}
		_LOCKMAP(pp->unlocksegp, pp->unlockaddr, lp->page) ;
		pp->flag |= UNLOCKMAP ;
	    }
	}
	return (0);
}

/*
 * Here to wakeup some process that was sleeping on a lock.
 */

static
lock_wakeup(lp,trash)
	SegLock	*lp ;
	int	trash ;		/* give old owner a trashpage mapping */
{
	register struct as *as;
	SegProc *owner = lp->owner ;
	register int rc;

	DEBUGF(4, (CE_CONT, "winlock_wakeup: lp=%x, owner=%d\n",
		lp, ID(lp->owner))) ;

	assert( lp->owner != NULL ) ;
	LOCK(lp) = 0 ;

	/* owner loses lockpage and unlockpage mappings and gains a
	 * trashpage mapping.
	 */

	if( lp->owner->flag & LOCKMAP ) {
	    DEBUGF(4, (CE_CONT, "  owner loses lock mapping\n"));
	    assert(lp->owner->locksegp != NULL) ;
	    as = lp->owner->locksegp->s_as;
	    rc = rw_tryenter(&as->a_lock, RW_READER);
	    _LOCKUNMAP(lp->owner->locksegp);
	    if (rc != 0)
		    AS_LOCK_EXIT(as, &as->a_lock);
	    lp->owner->flag &= ~LOCKMAP;
	}
	if (lp->owner->flag & UNLOCKMAP) {
	    DEBUGF(4, (CE_CONT, "  owner loses unlock mapping\n"));
	    assert(lp->owner->unlocksegp != NULL) ;
	    as = lp->owner->unlocksegp->s_as;
	    rc = rw_tryenter(&as->a_lock, RW_READER);
	    _LOCKUNMAP(lp->owner->unlocksegp);
	    if (rc != 0)
		    AS_LOCK_EXIT(as, &as->a_lock);
	    lp->owner->flag &= ~(TRASHPAGE|UNLOCKMAP) ;
	}
	if( trash  &&  lp->owner->unlocksegp != NULL ) {
	    /* old owner now gets mapping to trash page so it can continue */
	    DEBUGF(4, (CE_CONT, "  giving trashpage mapping to owner\n")) ;
	    as = owner->unlocksegp->s_as;
	    AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
	    _LOCKMAP(owner->unlocksegp,owner->unlockaddr,trashpage);
	    AS_LOCK_EXIT(as, &as->a_lock);
	    owner->flag |= (TRASHPAGE|UNLOCKMAP) ;
	}

	lp->owner = NULL ;

	if( lp->sleepf ) {
	    lp->sleepf = 0 ;
	    DEBUGF(4, (CE_CONT, "  calling wakeup, lp=%x\n", lp)) ;
#ifdef	COMMENT
	    wakeup((caddr_t)lp) ;
#endif	/* COMMENT */
	    cv_broadcast(&lp->locksleep) ;
	}
}











/*
 * destroy all allocated memory.
 */

static
lock_destroyall()
{
	SegLock		*lp, *next_lp ;

	for( lp = lock_free_list; lp != NULL ; )
	{
	  next_lp =  lp->next ;
	  DEBUGF(4, (CE_CONT,
		"lock_destroyall frees unused context at %x\n", lp));
	  KMEM_FREE((caddr_t)lp, sizeof(SegLock)) ;
	  lp = next_lp ;
	}
	lock_free_list = NULL ;

	if( lock_list == NULL  &&  lockpage != NULL ) {
	  DEBUGF(4, (CE_CONT,
		"lock_destroyall frees lock pages at %x\n", lockpage));
	    next_lock = 0 ;
	    winlock_freepages(lockpage,2) ;
	    lockpage = trashpage = NULL ;
	}
}


static
dump_all()
{
	SegLock	*lp ;
	SegProc	*pp ;

	cmn_err(CE_CONT,"\nID\t\tKEY\tNATTCH\tNMAP\tLOCK\tWAIT\n") ;

	for(lp = lock_list; lp != NULL; lp = lp->next)
	    cmn_err(CE_CONT,"%x\t%x\t%d\t%d\t%c\t%c\n",
		lp->cookie, lp->key, lp->alloccount, lp->mapcount,
		lp->lockptr != 0 && LOCK(lp) ? 'Y' : 'N',
		lp->sleepf ? 'Y' : 'N') ;

#if LOCKDEBUG >= 2
	if( lock_debug >= 3 )
	{
	  cmn_err(CE_CONT, "\n") ;

	  for( lp = lock_list; lp != NULL; lp = lp->next)
	  {
	    cmn_err(CE_CONT,
		  "lock %x, key=%x, nattch=%d, nmap=%d, lock=%d, wait=%d,\n",
		  lp, lp->key, lp->alloccount, lp->mapcount,
		  lp->lockptr != 0 ? LOCK(lp) : -1, lp->sleepf) ;

	    cmn_err(CE_CONT, "    cookie=%x, page=%x, lockptr=%x, timeout=%d\n",
		  lp->cookie, lp->page, lp->lockptr, lp->timeout) ;
	    cmn_err(CE_CONT, "    clients=%x, owner=%x\n",
		  lp->clients, lp->owner) ;
	    for( pp = lp->clients; pp != NULL; pp = pp->next )
	    {
	      cmn_err(CE_CONT,
		  "  client %x%s, lp=%x, flag=%x, procp=%x, pid=%d,\n",
		  pp, pp == lp->owner ? " (owner)" : "",
		  pp->lp, pp->flag, pp->procp, ID(pp)) ;
	      cmn_err( CE_CONT,
		  "   lockaddr=%x, locksegp=%x, unlockaddr=%x, unlocksegp=%x\n",
		  pp->lockaddr, pp->locksegp, pp->unlockaddr, pp->unlocksegp) ;
	    }
	  }
	}
#endif	/* LOCKDEBUG */
}








	/* LOADABLE DRIVER STARTS HERE */

STATIC struct cb_ops	winlock_cb_ops = {
	winlock_open,		/* open */
	winlock_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nulldev,		/* read */
	nulldev,		/* write */
	winlock_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	winlock_segmap,		/* segmap */
#ifdef	COMMENT
	winlock_mmap,			/* mmap */
	nodev,			/* segmap */
#endif	/* COMMENT */
	nochpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	0,			/* streamtab */
	D_NEW|D_MP		/* Driver compatibility flag */
} ;


STATIC struct dev_ops	winlock_ops = {
	DEVO_REV,
	0,			/* refcount */
	winlock_info,		/* info */
	winlock_identify,	/* identify */
	nulldev,		/* probe */
	winlock_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	&winlock_cb_ops,	/* driver ops */
	NULL,			/* bus ops */
} ;


#include <sys/modctl.h>

char	_depends_on[] = "misc/seg_drv" ;

STATIC struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"Winlock Driver v1.21",	/* Name of the module */
	&winlock_ops,		/* driver ops */
} ;

STATIC struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
} ;



	/* and here's the code that loads us in */

int
_init(void)
{
	register int e,i;

	DEBUGF(1, (CE_CONT, "winlock: compiled %s, %s\n", __TIME__, __DATE__));

	pagesize = ptob(1) ;
	pageoffset = pagesize - 1 ;
	pagemask = ~pageoffset ;

	mutex_init(&winlock_mutex, "winlock_drv", MUTEX_DEFAULT, NULL);

	e = mod_install(&modlinkage) ;
	DEBUGF(1, (CE_CONT, "  mod_install returns %d\n", e));

	if( e )
	  mutex_destroy(&winlock_mutex);

	DEBUGF(1, (CE_CONT, "  _init returns %d\n", e));
	return (e);
}


int
_info(struct modinfo *modinfop)
{
	return mod_info(&modlinkage, modinfop) ;
}


int
_fini(void)
{
	int	e ;
	/* verify not still active */

	DEBUGF(1,(CE_CONT, "_fini: lock_list=%x, busy=%d\n",
		lock_list, winlock_busy));

#if LOCKDEBUG
	assert( lock_list == NULL && !winlock_busy ) ;
	if( lock_list != NULL || winlock_busy )
	    return EBUSY ;
#endif LOCKDEBUG

	e = mod_remove(&modlinkage) ;
	if (e == 0)
		mutex_destroy(&winlock_mutex);

	DEBUGF(1,(CE_CONT, "_fini returns %d, mem used=%d\n",
		e, total_memory));
	return e ;
}
