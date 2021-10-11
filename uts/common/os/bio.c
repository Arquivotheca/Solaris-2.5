/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)bio.c	1.65	95/07/04 SMI"	/* SVr4.0 1.29	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/cpuvar.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/buf.h>
#include <sys/var.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <sys/vtrace.h>
#include <sys/tnf_probe.h>

/* Locks */
static	kmutex_t	blist_lock;	/* protects b_list */
static	kmutex_t	bhdr_lock;	/* protects the bhdrlist */
static	kmutex_t	bfree_lock;	/* protects the bfreelist strcuture */

struct hbuf	*hbuf;			/* Hash buckets */
struct dwbuf	*dwbuf;			/* Delayed write buckets */
static struct buf *bhdrlist;		/* buf header free list */
static int 	nbuf;			/* number of buffer headers allocated */

static int	lastindex;		/* Reference point on where to start */
					/* when looking for free buffers */

#define	bio_bhash(dev, bn)	(hash2ints((dev), (bn)) & v.v_hmask)

static kcondvar_t	bio_mem_cv; 	/* Condition variables */

/*
 * Statistics on the buffer cache
 */
struct biostats biostats = {
	{ "buffer_cache_lookups",		KSTAT_DATA_ULONG },
	{ "buffer_cache_hits",			KSTAT_DATA_ULONG },
	{ "new_buffer_requests",		KSTAT_DATA_ULONG },
	{ "waits_for_buffer_allocs",		KSTAT_DATA_ULONG },
	{ "buffers_locked_by_someone",		KSTAT_DATA_ULONG },
	{ "duplicate_buffers_found",		KSTAT_DATA_ULONG }
};

/*
 * kstat data
 */
kstat_named_t	*biostats_ptr = (kstat_named_t *) &biostats;
ulong_t		biostats_ndata = sizeof (biostats) / sizeof (kstat_named_t);

/* Private routines */
static struct buf	*bio_getfreeblk(long);
static void 		bio_mem_get(int);
static void		bio_bhdr_free(struct buf *);
static struct buf	*bio_bhdr_alloc(void);
static void		bio_recycle(int, int);
static void 		bio_pageio_done(struct buf *);
static int 		bio_incore(dev_t, daddr_t);

/*
 * Buffer cache constants
 */
#define	BIO_BUF_PERCENT	(100/2)		/* default: 2% of memory */
#define	BIO_MAX_PERCENT	(100/20)	/* max is 20% of real memory */
#define	BIO_BHDR_POOL	100		/* Default bhdr pool size */
#define	BIO_MIN_HDR	10		/* Minimum number of buffer headers */
#define	BIO_MIN_HWM	(BIO_MIN_HDR*MAXBSIZE/1024)
#define	BIO_HASHLEN	4		/* Target length of hash chains */


/* Flags for bio_recycle() */
#define	BIO_HEADER	0x01
#define	BIO_MEM		0x02

extern	int bufhwm;		/* User tunable - high water mark for mem  */
extern	char Syslimit[];
extern	char Sysbase[];

/*
 * The following routines allocate and free
 * buffers with various side effects.  In general the
 * arguments to an allocate routine are a device and
 * a block number, and the value is a pointer to
 * to the buffer header; the buffer returned is locked with a
 * binary semaphore so that no one else can touch it. If the block was
 * already in core, no I/O need be done; if it is
 * already locked, the process waits until it becomes free.
 * The following routines allocate a buffer:
 *	getblk
 *	bread
 *	breada
 * Eventually the buffer must be released, possibly with the
 * side effect of writing it out, by using one of
 *	bwrite
 *	bdwrite
 *	bawrite
 *	brelse
 *
 * The B_WANTED/B_BUSY bits are NOT used by these routines for synchronization.
 * Instead, a binary semaphore, b_sem is used to gain exclusive access to
 * a buffer and a binary semaphore, b_io is used for I/O synchronization.
 * B_DONE is still used to denote a buffer with I/O complete on it.
 *
 * The bfreelist.b_bcount field is computed everytime fsflush runs. It is
 * should not be used where a very accurate count of the free buffers is
 * needed.
 */

/*
 * Read in (if necessary) the block and return a buffer pointer.
 */
struct buf *
bread(register dev_t dev, daddr_t blkno, long bsize)
{
	register struct buf *bp;
	klwp_t *lwp = ttolwp(curthread);

	CPU_STAT_ADD_K(cpu_sysinfo.lread, 1);
	bp = getblk(dev, blkno, bsize);
	if (bp->b_flags & B_DONE)
		return (bp);
	bp->b_flags |= B_READ;
	ASSERT(bp->b_bcount == bsize);
	bdev_strategy(bp);
	if (lwp != NULL)
		lwp->lwp_ru.inblock++;
	CPU_STAT_ADD_K(cpu_sysinfo.bread, 1);
	(void) biowait(bp);
	return (bp);
}

/*
 * Read in the block, like bread, but also start I/O on the
 * read-ahead block (which is not allocated to the caller).
 */
struct buf *
breada(register dev_t dev, daddr_t blkno, daddr_t rablkno, long bsize)
{
	register struct buf *bp, *rabp;
	klwp_t *lwp = ttolwp(curthread);

	bp = NULL;
	if (!bio_incore(dev, blkno)) {
		CPU_STAT_ADD_K(cpu_sysinfo.lread, 1);
		bp = getblk(dev, blkno, bsize);
		if ((bp->b_flags & B_DONE) == 0) {
			bp->b_flags |= B_READ;
			bp->b_bcount = bsize;
			bdev_strategy(bp);
			if (lwp != NULL)
				lwp->lwp_ru.inblock++;
			CPU_STAT_ADD_K(cpu_sysinfo.bread, 1);
		}
	}
	if (rablkno && bfreelist.b_bcount > 1 &&
	    !bio_incore(dev, rablkno)) {
		rabp = getblk(dev, rablkno, bsize);
		if (rabp->b_flags & B_DONE)
			brelse(rabp);
		else {
			rabp->b_flags |= B_READ|B_ASYNC;
			rabp->b_bcount = bsize;
			bdev_strategy(rabp);
			if (lwp != NULL)
				lwp->lwp_ru.inblock++;
			CPU_STAT_ADD_K(cpu_sysinfo.bread, 1);
		}
	}
	if (bp == NULL)
		return (bread(dev, blkno, bsize));
	(void) biowait(bp);
	return (bp);
}

/*
 * Write the buffer, waiting for completion.
 * Then release the buffer.
 */
void
bwrite(register struct buf *bp)
{
	register int flag;
	klwp_t *lwp = ttolwp(curthread);
	struct cpu *cpup;

	ASSERT(SEMA_HELD(&bp->b_sem));
	flag = bp->b_flags;
	bp->b_flags &= ~(B_READ | B_DONE | B_ERROR | B_DELWRI);
	if (lwp != NULL)
		lwp->lwp_ru.oublock++;
	CPU_STAT_ENTER_K();
	cpup = CPU;		/* get pointer AFTER preemption is disabled */
	CPU_STAT_ADDQ(cpup, cpu_sysinfo.lwrite, 1);
	CPU_STAT_ADDQ(cpup, cpu_sysinfo.bwrite, 1);
	CPU_STAT_EXIT_K();
	bdev_strategy(bp);
	if ((flag & B_ASYNC) == 0) {
		(void) biowait(bp);
		brelse(bp);
	} else {
		CPU_STAT_ADDQ(cpup, cpu_sysinfo.bawrite, 1);
	}
}

/*
 * Write the buffer, waiting for completion.
 * But don't release the buffer afterwards.
 */
void
bwrite2(register struct buf *bp)
{
	klwp_t *lwp = ttolwp(curthread);
	struct cpu *cpup;

	ASSERT(SEMA_HELD(&bp->b_sem));
	bp->b_flags &= ~(B_READ | B_DONE | B_ERROR | B_DELWRI);
	if (lwp != NULL)
		lwp->lwp_ru.oublock++;
	CPU_STAT_ENTER_K();
	cpup = CPU;		/* get pointer AFTER preemption is disabled */
	CPU_STAT_ADDQ(cpup, cpu_sysinfo.lwrite, 1);
	CPU_STAT_ADDQ(cpup, cpu_sysinfo.bwrite, 1);
	CPU_STAT_EXIT_K();
	bdev_strategy(bp);
	(void) biowait(bp);
}

/*
 * Release the buffer, marking it so that if it is grabbed
 * for another purpose it will be written out before being
 * given up (e.g. when writing a partial block where it is
 * assumed that another write for the same block will soon follow).
 * Also save the time that the block is first marked as delayed
 * so that it will be written in a reasonable time.
 */
void
bdwrite(register struct buf *bp)
{
	ASSERT(SEMA_HELD(&bp->b_sem));
	CPU_STAT_ADD_K(cpu_sysinfo.lwrite, 1);
	if ((bp->b_flags & B_DELWRI) == 0)
		bp->b_start = lbolt;
	bp->b_flags |= B_DELWRI | B_DONE;
	bp->b_resid = 0;
	brelse(bp);
}

/*
 * Release the buffer, start I/O on it, but don't wait for completion.
 */
void
bawrite(register struct buf *bp)
{
	ASSERT(SEMA_HELD(&bp->b_sem));

	/* Use bfreelist.b_bcount as a hueristic */
	if (bfreelist.b_bcount > 4)
		bp->b_flags |= B_ASYNC;
	bwrite(bp);
}


/*
 * Release the buffer, with no I/O implied.
 */
void
brelse(register struct buf *bp)
{
	register struct buf **backp;
	u_int		index;
	kmutex_t	*hmp;
	struct	buf	*dp;
	struct	hbuf	*hp;


	ASSERT(SEMA_HELD(&bp->b_sem));

	/* Check for anomalous conditions */
	if (bp->b_flags & (B_ERROR|B_NOCACHE)) {
		if (bp->b_flags & B_NOCACHE) {
			/* Don't add to the freelist. Destroy it now */
			kmem_free(bp->b_un.b_addr, bp->b_bufsize);
			sema_destroy(&bp->b_sem);
			sema_destroy(&bp->b_io);
			kmem_free(bp, sizeof (struct buf));
			return;
		}
		bp->b_flags |= B_AGE|B_STALE;
		bp->b_flags &= ~B_ERROR;
		bp->b_error = 0;
		bp->b_oerror = 0;
	}

	/*
	 * If delayed write is set then put in on the delayed
	 * write list instead of the free buffer list.
	 */
	index = bio_bhash(bp->b_edev, bp->b_blkno);
	hmp   = &hbuf[index].b_lock;

	mutex_enter(hmp);
	hp = &hbuf[index];
	dp = (struct buf *)hp;

	/*
	 * Make sure that the number of entries on this list are
	 * Zero <= count <= total # buffers
	 */
	ASSERT(hp->b_length >= 0);
	ASSERT(hp->b_length < nbuf);

	hp->b_length++;		/* We are adding this buffer */

	if (bp->b_flags & B_DELWRI) {
		/*
		 * This buffer goes on the delayed write buffer list
		 */
		dp = (struct buf *)&dwbuf[index];
	}


	if (bp->b_flags & B_AGE) {
		backp = &dp->av_forw;
		(*backp)->av_back = bp;
		bp->av_forw = *backp;
		*backp = bp;
		bp->av_back = dp;
	} else {
		backp = &dp->av_back;
		(*backp)->av_forw = bp;
		bp->av_back = *backp;
		*backp = bp;
		bp->av_forw = dp;
	}
	mutex_exit(hmp);

	if (bfreelist.b_flags & B_WANTED) {
		/*
		 * Should come here very very rarely.
		 */
		mutex_enter(&bfree_lock);
		if (bfreelist.b_flags & B_WANTED) {
			bfreelist.b_flags &= ~B_WANTED;
			cv_broadcast(&bio_mem_cv);
		}
		mutex_exit(&bfree_lock);
	}

	bp->b_flags &= ~(B_WANTED|B_BUSY|B_ASYNC);
	bp->b_reltime = (unsigned long)lbolt;
	/*
	 * Don't let anyone get the buffer off the freelist before we
	 * release our hold on it.
	 */
	sema_v(&bp->b_sem);
}

/*
 * Return a count of the number of B_BUSY buffers in the system
 * Can only be used as a good estimate.
 */
int
bio_busy(void)
{
	struct buf *bp, *dp;
	int busy = 0;
	int i;
	kmutex_t *hmp;

	for (i = 0; i < v.v_hbuf; i++) {
		dp = (struct buf *)&hbuf[i];
		hmp = &hbuf[i].b_lock;

		mutex_enter(hmp);
		for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
			if (bp->b_flags & B_BUSY)
				busy++;
		}
		mutex_exit(hmp);
	}
	return (busy);
}

/*
 * Assign a buffer for the given block.  If the appropriate
 * block is already associated, return it; otherwise search
 * for the oldest non-busy buffer and reassign it.
 */
struct buf *
getblk(register dev_t dev, register daddr_t blkno, long bsize)
{
	register struct buf *bp;
	register struct buf *dp;
	register struct buf *nbp = NULL;
	u_int		index;
	kmutex_t	*hmp;
	struct	hbuf	*hp;

	if (getmajor(dev) >= devcnt)
		cmn_err(CE_PANIC, "blkdev");

	biostats.bio_lookup.value.ul++;

	index = bio_bhash(dev, blkno);
	hp    = &hbuf[index];
	dp    = (struct buf *)hp;
	hmp   = &hp->b_lock;

	mutex_enter(hmp);
loop:
	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
		if (bp->b_blkno != blkno || bp->b_edev != dev ||
		    (bp->b_flags & B_STALE))
			continue;
		/*
		 * Avoid holding the hash lock in the event that
		 * the buffer is locked by someone. Since the hash chain
		 * may change when we drop the hash lock
		 * we have to start at the beginning of the chain if the
		 * buffer identity/contents aren't valid.
		 */
		if (sema_tryp(&bp->b_sem) == NULL) {
			biostats.bio_bufbusy.value.ul++;
			mutex_exit(hmp);
			/*
			 * For the following line of code to work
			 * correctly never kmem_free the buffer "header".
			 */
			sema_p(&bp->b_sem);
			if (bp->b_blkno != blkno || bp->b_edev != dev ||
			    (bp->b_flags & B_STALE)) {
				sema_v(&bp->b_sem);
				mutex_enter(hmp);
				goto loop;	/* start over */
			}
			mutex_enter(hmp);
		}
		/* Found */
		biostats.bio_hit.value.ul++;
		bp->b_flags &= ~B_AGE;

		/*
		 * Yank it off the free/delayed write lists
		 */
		hp->b_length--;
		notavail(bp);
		mutex_exit(hmp);

		ASSERT((bp->b_flags & B_NOCACHE) == NULL);

		if (nbp == NULL) {
			/*
			 * Make the common path short.
			 */
			ASSERT(SEMA_HELD(&bp->b_sem));
			return (bp);
		} else {
			biostats.bio_bufdup.value.ul++;
			/*
			 * The buffer must have entered during the lock upgrade
			 * so free the new buffer we allocated and return the
			 * found buffer.
			 */
			kmem_free(nbp->b_un.b_addr, nbp->b_bufsize);
			nbp->b_un.b_addr = NULL;

			/*
			 * Account for the memory
			 */
			mutex_enter(&bfree_lock);
			bfreelist.b_bufsize += nbp->b_bufsize;
			mutex_exit(&bfree_lock);
			/*
			 * Destroy buf identity, and place on avail list
			 */
			nbp->b_dev = (o_dev_t)NODEV;
			nbp->b_edev = (dev_t)NODEV;
			nbp->b_flags = B_KERNBUF;

			sema_v(&nbp->b_sem);
			bio_bhdr_free(nbp);

			ASSERT(SEMA_HELD(&bp->b_sem));
			return (bp);
		}
	}
	/*
	 * bio_getfreeblk may block so check the hash chain again.
	 */
	if (!nbp) {
		mutex_exit(hmp);
		nbp = bio_getfreeblk(bsize);
		mutex_enter(hmp);
		goto loop;
	}

	/*
	 * New buffer. Assign nbp and stick it on the hash.
	 */
	nbp->b_flags = B_KERNBUF | B_BUSY;
	nbp->b_edev = dev;
	nbp->b_dev = (o_dev_t)cmpdev(dev);
	nbp->b_blkno = blkno;
	nbp->b_iodone = NULL;
	nbp->b_bcount = bsize;

	ASSERT((nbp->b_flags & B_NOCACHE) == NULL);

	binshash(nbp, dp);
	mutex_exit(hmp);

	ASSERT(SEMA_HELD(&nbp->b_sem));

	return (nbp);
}

/*
 * Get an empty block, not assigned to any particular device.
 * Returns a locked buffer that is not on any hash or free list.
 */
struct buf *
ngeteblk(long bsize)
{
	register struct buf *bp;

	bp = (struct buf *)getrbuf(KM_SLEEP);
	bp->av_forw = bp->av_back = NULL;
	bp->b_un.b_addr = (caddr_t)kmem_alloc(bsize, KM_SLEEP);
	bp->b_bufsize = bsize;
	bp->b_flags = B_KERNBUF | B_BUSY | B_NOCACHE | B_AGE;
	bp->b_dev = (o_dev_t)NODEV;
	bp->b_edev = (dev_t)NODEV;
	bp->b_lblkno = 0;
	bp->b_bcount = bsize;
	bp->b_iodone = NULL;
	return (bp);
}

/*
 * Interface of geteblk() is kept intact to maintain driver compatibility.
 * Use ngeteblk() to allocate block size other than 1 KB.
 */
struct buf *
geteblk(void)
{
	return (ngeteblk((long)1024));
}

/*
 * Return a buffer w/o sleeping
 */
struct buf *
trygetblk(register dev_t dev, register daddr_t blkno)
{
	register struct buf *bp;
	register struct buf *dp;
	struct hbuf	*hp;
	kmutex_t	*hmp;
	u_int		index;

	index = bio_bhash(dev, blkno);
	hp = &hbuf[index];
	hmp = &hp->b_lock;

	if (!mutex_tryenter(hmp))
		return (NULL);

	dp = (struct buf *)hp;
	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
		if (bp->b_blkno != blkno || bp->b_edev != dev ||
		    (bp->b_flags & B_STALE))
			continue;
		/*
		 * Get access to a valid buffer without sleeping
		 */
		if (sema_tryp(&bp->b_sem)) {
			if (bp->b_flags & B_DONE) {
				hp->b_length--;
				notavail(bp);
				mutex_exit(hmp);
				return (bp);
			} else {
				sema_v(&bp->b_sem);
				break;
			}
		}
		break;
	}
	mutex_exit(hmp);
	return (NULL);
}


/*
 * Wait for I/O completion on the buffer; return errors
 * to the user.
 */
int
iowait(struct buf *bp)
{
	ASSERT(SEMA_HELD(&bp->b_sem));
	return (biowait(bp));
}

/*
 * Mark I/O complete on a buffer, release it if I/O is asynchronous,
 * and wake up anyone waiting for it.
 */
void
iodone(struct buf *bp)
{
	ASSERT(SEMA_HELD(&bp->b_sem));
	(void) biodone(bp);
}

/*
 * Zero the core associated with a buffer.
 */
void
clrbuf(struct buf *bp)
{
	ASSERT(SEMA_HELD(&bp->b_sem));
	bzero(bp->b_un.b_addr, bp->b_bcount);
	bp->b_resid = 0;
}

/*
 * Make sure all write-behind blocks on dev (or NODEV for all)
 * are flushed out.
 */
void
bflush(register dev_t dev)
{
	register struct buf *bp, *dp;
	register struct hbuf *hp;
	register struct buf *delwri_list = NULL;
	register int i, index;
	kmutex_t	*hmp;


	mutex_enter(&blist_lock);
	/*
	 * Gather all B_DELWRI buffer for device.
	 * Lock ordering is b_sem > hash lock (brelse).
	 * Since we are finding the buffer via the delayed write list,
	 * it may be busy and we would block trying to get the
	 * b_sem lock while holding hash lock. So transfer all the
	 * cadidates on the delwri_list and then drop the hash locks.
	 */
	for (i = 0; i < v.v_hbuf; i++) {
		hmp = &hbuf[i].b_lock;
		dp = (struct buf *)&dwbuf[i];
		mutex_enter(hmp);
		for (bp = dp->av_forw; bp != dp; bp = bp->av_forw) {
			if (dev == NODEV || bp->b_edev == dev) {
				bp->b_list = delwri_list;
				delwri_list = bp;
			}
		}
		mutex_exit(hmp);
	}

	/*
	 * Now that the hash locks have been dropped grap the semaphores
	 * and write back all the buffers that have B_DELWRI set.
	 */
	while (delwri_list != NULL) {
		bp = delwri_list;
		delwri_list = bp->b_list;
		bp->b_list = NULL;

		sema_p(&bp->b_sem);	/* may block */
		if ((dev != bp->b_edev && dev != NODEV) ||
		    (panicstr && bp->b_flags & B_BUSY)) {
			sema_v(&bp->b_sem);
			continue;	/* No longer a candidate */
		}
		if (bp->b_flags & B_DELWRI) {

			index = bio_bhash(bp->b_edev, bp->b_blkno);
			hp = &hbuf[index];
			hmp = &hp->b_lock;
			dp = (struct buf *)hp;

			bp->b_flags |= B_ASYNC;
			mutex_enter(hmp);
			hp->b_length--;
			notavail(bp);
			mutex_exit(hmp);
			bwrite(bp);
		} else {
			sema_v(&bp->b_sem);
		}
	}
	mutex_exit(&blist_lock);
}

/*
 * Ensure that a specified block is up-to-date on disk.
 */
void
blkflush(dev_t dev, daddr_t blkno)
{
	register struct buf *bp, *dp;
	register struct hbuf *hp;
	register struct buf *sbp = NULL;
	u_int		index;
	kmutex_t	*hmp;

	index = bio_bhash(dev, blkno);
	hp    = &hbuf[index];
	dp    = (struct buf *)hp;
	hmp   = &hp->b_lock;

	/*
	 * Identify the buffer in the cache belonging to
	 * this device and blkno (if any).
	 */
	mutex_enter(hmp);
	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
		if (bp->b_blkno != blkno || bp->b_edev != dev ||
		    (bp->b_flags & B_STALE))
			continue;
		sbp = bp;
		break;
	}
	mutex_exit(hmp);
	if (sbp == NULL)
		return;
	/*
	 * Now check the buffer we have identified and
	 * make sure it still belongs to the device and is B_DELWRI
	 */
	sema_p(&sbp->b_sem);
	if (sbp->b_blkno == blkno && sbp->b_edev == dev &&
	    (sbp->b_flags & (B_DELWRI|B_STALE)) == B_DELWRI) {
		mutex_enter(hmp);
		hp->b_length--;
		notavail(sbp);
		mutex_exit(hmp);
		bwrite(sbp);	/* synchronous write */
	} else {
		sema_v(&sbp->b_sem);
	}
}


/*
 * If possible, invalidate blocks for a dev on demand
 */
void
binval(register dev_t dev)
{
	register struct buf *dp;
	register struct buf *bp;
	register struct buf *binval_list = NULL;
	register int i;
	kmutex_t *hmp;

	mutex_enter(&blist_lock);

	/* Gather bp's */
	for (i = 0; i < v.v_hbuf; i++) {
		dp = (struct buf *)&hbuf[i];
		hmp = &hbuf[i].b_lock;

		mutex_enter(hmp);
		for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
			if (bp->b_edev == dev) {
				bp->b_list = binval_list;
				binval_list = bp;
			}
		}
		mutex_exit(hmp);
	}

	/* Invalidate all bp's found */
	while (binval_list != NULL) {
		bp = binval_list;
		binval_list = bp->b_list;

		sema_p(&bp->b_sem);
		if ((bp->b_edev == dev) && ((bp->b_flags & B_DELWRI) == 0))
			bp->b_flags |= B_STALE|B_AGE;
		sema_v(&bp->b_sem);

	}
	mutex_exit(&blist_lock);
}

/*
 * Initialize the buffer I/O system by freeing
 * all buffers and setting all device hash buffer lists to empty.
 */
void
binit(void)
{
	register struct buf *bp;
	register unsigned int i;
	int	bio_max_hwm;
	u_int	max_real;	/* max value of hwm based on memory */
	u_int	max_virt;	/* max value of hwm based of virt mem */

	mutex_init(&blist_lock, "blist_lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&bhdr_lock, "bhdr_lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&bfree_lock, "bfree_lock", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Maximum value for bufhwm based on a percentage of real memory
	 */
	max_real = (physmem/BIO_MAX_PERCENT) * (PAGESIZE/1024);
	/*
	 * Maximum value for bufhwm based on a percentage of available
	 * kmem_alloc virtual address space (No more than 1/4 of available
	 * space can be used by the buffer cache).
	 */
	max_virt = (Syslimit - Sysbase);
	max_virt = max_virt/4/1024;

	bio_max_hwm = min(max_real, max_virt);
	/*
	 * Determine the high water mark for buffer headers if
	 * it hasn't been explicitly set.
	 */
	if ((v.v_bufhwm = bufhwm) == 0)
		v.v_bufhwm = (physmem/BIO_BUF_PERCENT) * (PAGESIZE/1024);

	if (v.v_bufhwm < BIO_MIN_HWM || v.v_bufhwm > bio_max_hwm) {
		v.v_bufhwm = bio_max_hwm;
		if (bufhwm != 0) {	/* User specified value */
			cmn_err(CE_WARN,
				"binit: bufhwm out of range (%d). Using %d",
				bufhwm, bio_max_hwm);
		}
	}
	/*
	 * Determine the number of hash buckets. Default is to
	 * create ~BIO_HASHLEN entries per chain based on MAXBSIZE buffers.
	 * Round up number to the next power of 2.
	 */
	v.v_hbuf = 1 << highbit((v.v_bufhwm*1024/MAXBSIZE)/BIO_HASHLEN);
	v.v_hmask = v.v_hbuf - 1;
	v.v_buf = BIO_BHDR_POOL;

	hbuf = (struct hbuf *) kmem_zalloc(v.v_hbuf * sizeof (struct hbuf),
					KM_SLEEP);

	dwbuf = (struct dwbuf *) kmem_zalloc(v.v_hbuf * sizeof (struct dwbuf),
					KM_SLEEP);

	bfreelist.b_bufsize = v.v_bufhwm * 1024;
	bp = &bfreelist;
	bp->b_forw = bp->b_back = bp->av_forw = bp->av_back = bp;

	for (i = 0; i < v.v_hbuf; i++) {
		char	buf[100];

		hbuf[i].b_forw = hbuf[i].b_back = (struct buf *)&hbuf[i];
		hbuf[i].av_forw = hbuf[i].av_back = (struct buf *)&hbuf[i];

		sprintf(buf, "hbuf_%i", i);
		mutex_init(&hbuf[i].b_lock, buf, MUTEX_DEFAULT, NULL);

		/*
		 * Initialize the delayed write buffer list.
		 */
		dwbuf[i].b_forw = dwbuf[i].b_back = (struct buf *)&dwbuf[i];
		dwbuf[i].av_forw = dwbuf[i].av_back = (struct buf *)&dwbuf[i];
	}
}

/*
 * Wait for I/O completion on the buffer; return error code.
 * If bp was for synchronous I/O, bp is invalid and associated
 * resources are freed on return.
 */
int
biowait(register struct buf *bp)
{
	int error = 0;
	/* LINTED */
	struct cpu *cpup;

	ASSERT(SEMA_HELD(&bp->b_sem));

	cpup = CPU;
	CPU_STAT_ADD_K(cpu_syswait.iowait, 1);

	/*
	 * In case of panic, busy wait at low ipl
	 */
	if (panicstr) {
		while ((bp->b_flags & B_DONE) == 0)
			panic_hook();
	} else
		sema_p(&bp->b_io);

	CPU_STAT_ADD_K(cpu_syswait.iowait, -1);

	error = geterror(bp);
	if ((bp->b_flags & B_ASYNC) == 0) {
		if (bp->b_flags & B_REMAPPED)
			bp_mapout(bp);
	}
	return (error);
}

/*
 * Mark I/O complete on a buffer, release it if I/O is asynchronous,
 * and wake up anyone waiting for it.
 */
void
biodone(register struct buf *bp)
{
	/* Kernel probe */
	TNF_PROBE_3(biodone, "io blockio", /* CSTYLED */,
		tnf_device,	device,		bp->b_edev,
		tnf_diskaddr,	block,		bp->b_lblkno,
		tnf_opaque,	buf,		bp);

	if (bp->b_iodone && (bp->b_flags & B_KERNBUF)) {
		(*(bp->b_iodone))(bp);
		return;
	}
	ASSERT((bp->b_flags & B_DONE) == 0);
	ASSERT(SEMA_HELD(&bp->b_sem));
	bp->b_flags |= B_DONE;
	if (bp->b_flags & B_ASYNC) {
		if (bp->b_flags & (B_PAGEIO|B_REMAPPED))
			bio_pageio_done(bp);
		else
			brelse(bp);	/* release bp to freelist */
	} else {
		sema_v(&bp->b_io);
	}
}

/*
 * Pick up the device's error number and pass it to the user;
 * if there is an error but the number is 0 set a generalized code.
 */
int
geterror(register struct buf *bp)
{
	int error = 0;

	ASSERT(SEMA_HELD(&bp->b_sem));
	if (bp->b_flags & B_ERROR) {
		if (bp->b_flags & B_KERNBUF)
			error = bp->b_error;
		if (!error)
			error = bp->b_oerror;
		if (!error)
			error = EIO;
	}
	return (error);
}

/*
 * Support for pageio buffers.
 *
 * This stuff should be generalized to provide a generalized bp
 * header facility that can be used for things other than pageio.
 */

/*
 * Variables for maintaining the free list of buf structures.
 */
static struct buf *pageio_freelist;
static kcondvar_t pageio_freelist_cv;
static int npageio_incr = 32;
static char pageio_wakeup;	/* XXX - wakeup thread */
/*
 * Lock (mutex) to protect pageio buffer free list.
 */
extern kmutex_t pageio_kmemlock;

/*
 * Allocate and initialize a buf struct for use with pageio.
 */
struct buf *
pageio_setup(struct page *pp, u_int len, struct vnode *vp, int flags)
{
	register struct buf *bp;
	struct cpu *cpup;

	if (flags & B_READ) {
		CPU_STAT_ENTER_K();
		cpup = CPU;	/* get pointer AFTER preemption is disabled */
		CPU_STAT_ADDQ(cpup, cpu_vminfo.pgin, 1);
		CPU_STAT_ADDQ(cpup, cpu_vminfo.pgpgin, btopr(len));
		if ((flags & B_ASYNC) == 0) {
			klwp_t *lwp = ttolwp(curthread);
			if (lwp != NULL)
				lwp->lwp_ru.majflt++;
			CPU_STAT_ADDQ(cpup, cpu_vminfo.maj_fault, 1);
			/* Kernel probe */
			TNF_PROBE_2(major_fault, "vm pagefault", /* CSTYLED */,
				tnf_opaque,	vnode,		pp->p_vnode,
				tnf_offset,	offset,		pp->p_offset);
		}
		CPU_STAT_EXIT_K();
		TRACE_3(TR_FAC_VM, TR_PAGE_WS_IN,
			"page_ws_in:pp %x vp %x offset %x",
			pp, pp->p_vnode, pp->p_offset);
		/* Kernel probe */
		TNF_PROBE_3(pagein, "vm pageio io", /* CSTYLED */,
			tnf_opaque,	vnode,		pp->p_vnode,
			tnf_offset,	offset,		pp->p_offset,
			tnf_size,	size,		len);
	}

	mutex_enter(&pageio_kmemlock);
	while ((bp = (struct buf *)
	    kmem_fast_zalloc((caddr_t *)&pageio_freelist,
	    sizeof (*pageio_freelist), npageio_incr, KM_NOSLEEP)) == NULL) {
		pageio_wakeup = 1;
		cv_wait(&pageio_freelist_cv, &pageio_kmemlock);
	}
	mutex_exit(&pageio_kmemlock);

	bp->b_bcount = len;
	bp->b_bufsize = len;
	bp->b_pages = pp;
	bp->b_flags = B_KERNBUF | B_PAGEIO | B_NOCACHE | B_BUSY | flags;
	sema_init(&bp->b_io, 0, "bp pageio io", SEMA_DEFAULT, DEFAULT_WT);

	/* Initialize bp->b_sem in "locked" state */
	sema_init(&bp->b_sem, 0, "bp pageio owner", SEMA_DEFAULT, DEFAULT_WT);

	VN_HOLD(vp);
	bp->b_vp = vp;
	THREAD_KPRI_RELEASE_N(btopr(len)); /* release kpri from page_locks */
	/*
	 * Caller sets dev & blkno and can adjust
	 * b_addr for page offset and can use bp_mapin
	 * to make pages kernel addressable.
	 */
	return (bp);
}

void
pageio_done(register struct buf *bp)
{
	ASSERT(SEMA_HELD(&bp->b_sem));
	if (bp->b_flags & B_REMAPPED)
		bp_mapout(bp);
	VN_RELE(bp->b_vp);
	ASSERT((bp->b_flags & B_NOCACHE) != 0);

	/* A sema_v(bp->b_sem) is implied if we are destroying it */
	sema_destroy(&bp->b_sem);
	sema_destroy(&bp->b_io);

	mutex_enter(&pageio_kmemlock);
	if (pageio_wakeup) {
		pageio_wakeup = 0;
		cv_broadcast(&pageio_freelist_cv);
	}
	kmem_fast_free((caddr_t *)&pageio_freelist, (caddr_t)bp);
	mutex_exit(&pageio_kmemlock);
}

/*
 * Check to see whether the buffers, except the one pointed by sbp,
 * associated with the device are busy.
 * NOTE: This expensive operation shall be improved together with ufs_icheck().
 */
int
bcheck(register dev_t dev, register struct buf *sbp)
{
	register struct buf	*bp;
	register struct buf	*dp;
	register int i;
	kmutex_t *hmp;

	/*
	 * check for busy bufs for this filesystem
	 */
	for (i = 0; i < v.v_hbuf; i++) {
		dp = (struct buf *)&hbuf[i];
		hmp = &hbuf[i].b_lock;

		mutex_enter(hmp);
		for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
			/*
			 * if buf is busy or dirty, then filesystem is busy
			 */
			if ((bp->b_edev == dev) &&
			    ((bp->b_flags & B_STALE) == 0) &&
			    (bp->b_flags & (B_DELWRI|B_BUSY)) &&
			    (bp != sbp)) {
				mutex_exit(hmp);
				return (1);
			}
		}
		mutex_exit(hmp);
	}
	return (0);
}

/*
 * Hash two 32 bit entities.
 */
int
hash2ints(int x, int y)
{
	int hash = 0;

	hash = x - 1;
	hash = ((hash * 7) + (x >> 8)) - 1;
	hash = ((hash * 7) + (x >> 16)) - 1;
	hash = ((hash * 7) + (x >> 24)) - 1;
	hash = ((hash * 7) + y) - 1;
	hash = ((hash * 7) + (y >> 8)) - 1;
	hash = ((hash * 7) + (y >> 16)) - 1;
	hash = ((hash * 7) + (y >> 24)) - 1;

	return (hash);
}


/*
 * Return a new buffer struct.
 *	Create a new buffer if we haven't gone over our high water
 *	mark for memory, otherwise try to get one off the freelist.
 *
 * Returns a locked buf that has no id and is not on any hash or free
 * list.
 */
static struct buf *
bio_getfreeblk(long bsize)
{
	register struct buf *bp, *dp;
	register struct hbuf *hp;
	kmutex_t	*hmp;
	u_int		start, end;

	/*
	 * bfreelist.b_bufsize represents the amount of memory
	 * we are allowed to allocate in the cache before we hit our hwm.
	 */
	bio_mem_get(bsize);	/* Account for our memory request */

again:
	bp = bio_bhdr_alloc();	/* Get a buf hdr */
	sema_p(&bp->b_sem);	/* Should never fail */

	ASSERT(bp->b_un.b_addr == NULL);
	bp->b_un.b_addr = (caddr_t)kmem_alloc(bsize, KM_NOSLEEP);
	if (bp->b_un.b_addr != NULL) {
		/*
		 * Make the common path short
		 */
		bp->b_bufsize = bsize;
		ASSERT(SEMA_HELD(&bp->b_sem));
		return (bp);
	} else {
		struct buf *save;

		save = bp;	/* Save bp we allocated */
		start = end = lastindex;

		biostats.bio_bufwant.value.ul++;

		/*
		 * Memory isn't available from the system now. Scan
		 * the hash buckets till enough space is found.
		 */
		do {
			hp = &hbuf[start];
			hmp = &hp->b_lock;
			dp = (struct buf *)hp;

			mutex_enter(hmp);
			bp = dp->av_forw;

			while (bp != dp) {

				ASSERT(bp != NULL);

				if (!sema_tryp(&bp->b_sem)) {
					bp = bp->av_forw;
					continue;
				}

				/*
				 * Since we are going down the freelist
				 * associated with this hash bucket the
				 * B_DELWRI flag should not be set.
				 */
				ASSERT(!(bp->b_flags & B_DELWRI));

				if (bp->b_bufsize == bsize) {
					hp->b_length--;
					notavail(bp);
					bremhash(bp);
					mutex_exit(hmp);

					/*
					 * Didn't kmem_alloc any more, so don't
					 * count it twice.
					 */
					mutex_enter(&bfree_lock);
					bfreelist.b_bufsize += bsize;
					mutex_exit(&bfree_lock);

					/*
					 * Update the lastindex value.
					 */
					lastindex = start;

					/*
					 * Put our saved bp back on the list
					 */
					sema_v(&save->b_sem);
					bio_bhdr_free(save);
					ASSERT(SEMA_HELD(&bp->b_sem));
					return (bp);
				}
				sema_v(&bp->b_sem);
				bp = bp->av_forw;
			}
			mutex_exit(hmp);
			start = ((start + 1) % v.v_hbuf);
		} while (start != end);

		biostats.bio_bufwait.value.ul++;
		bp = save;		/* Use original bp */
		bp->b_un.b_addr = (caddr_t)kmem_alloc(bsize, KM_SLEEP);
	}

	bp->b_bufsize = bsize;
	ASSERT(SEMA_HELD(&bp->b_sem));
	return (bp);
}

/*
 * Allocate a buffer header. If none currently available, allocate
 * a new pool.
 */
static struct buf *
bio_bhdr_alloc(void)
{
	struct buf *dp, *sdp;
	struct buf *bp;
	int i;


	for (;;) {
		mutex_enter(&bhdr_lock);
		if (bhdrlist != NULL) {
			bp = bhdrlist;
			bhdrlist = bp->av_forw;
			mutex_exit(&bhdr_lock);
			bp->av_forw = NULL;
			return (bp);
		}
		mutex_exit(&bhdr_lock);
		/*
		 * Need to allocate a new pool. If the system is currently
		 * out of memory, then try freeing things on the freelist.
		 */
		dp = (struct buf *)kmem_zalloc(sizeof (struct buf) * v.v_buf,
						KM_NOSLEEP);
		if (dp == NULL) {
			/*
			 * System can't give us a pool of headers, try
			 * recycling from the free lists.
			 */
			bio_recycle(BIO_HEADER, 0);
		} else {

			sdp = dp;

			for (i = 0; i < v.v_buf; i++, dp++) {
				/*
				 * The next two lines are needed since NODEV
				 * is -1 and not NULL
				 */
				dp->b_dev = (o_dev_t)NODEV;
				dp->b_edev = (dev_t)NODEV;
				dp->av_forw = dp + 1;
				dp->b_flags = B_KERNBUF;
				sema_init(&dp->b_sem, 1, "bp owner",
						SEMA_DEFAULT, DEFAULT_WT);
				sema_init(&dp->b_io, 0, "bp io",
						SEMA_DEFAULT, DEFAULT_WT);
			}
			(--dp)->av_forw = NULL;	/* Fix last pointer */

			mutex_enter(&bhdr_lock);
			bhdrlist = sdp;
			nbuf += v.v_buf;
			bp = bhdrlist;
			bhdrlist = bp->av_forw;
			mutex_exit(&bhdr_lock);

			bp->av_forw = NULL;
			return (bp);
		}
	}
}


static  void
bio_bhdr_free(struct buf *bp)
{
	ASSERT(bp->b_back == NULL);
	ASSERT(bp->b_forw == NULL);
	ASSERT(bp->av_back == NULL);
	ASSERT(bp->av_forw == NULL);
	ASSERT(bp->b_un.b_addr == NULL);
	ASSERT(bp->b_dev == (o_dev_t)NODEV);
	ASSERT(bp->b_edev == (dev_t)NODEV);
	ASSERT(bp->b_flags == B_KERNBUF);


	mutex_enter(&bhdr_lock);
	bp->av_forw = bhdrlist;
	bhdrlist = bp;
	mutex_exit(&bhdr_lock);
}

/*
 * If we haven't gone over the high water mark, it's o.k. to
 * allocate more buffer space, otherwise recycle buffers
 * from the freelist until enough memory is free for a bsize request.
 *
 * We account for this memory, even though
 * we don't allocate it here.
 */
static void
bio_mem_get(int bsize)
{
	mutex_enter(&bfree_lock);
	if (bfreelist.b_bufsize > bsize) {
		bfreelist.b_bufsize -= bsize;
		mutex_exit(&bfree_lock);
		return;
	}
	mutex_exit(&bfree_lock);
	bio_recycle(BIO_MEM, bsize);
}

/*
 * Start recycling buffers on the freelist for one of 2 reasons:
 *	- we need a buffer header
 *	- we need to free up memory
 * Once started we continue to recycle buffers until the B_AGE
 * buffers are gone.
 */
static void
bio_recycle(int want, int bsize)
{
	struct buf *bp, *dp, *dwp;
	struct hbuf *hp;
	int	found = 0;
	kmutex_t	*hmp;
	int		start, end;

	/*
	 * Recycle buffers.
	 */
top:
	start = end = lastindex;
	do {
		hp = &hbuf[start];
		hmp = &hp->b_lock;
		dp = (struct buf *)hp;

		mutex_enter(hmp);
		bp = dp->av_forw;

		while (bp != dp) {

			ASSERT(bp != NULL);

			if (!sema_tryp(&bp->b_sem)) {
				bp = bp->av_forw;
				continue;
			}
			/*
			 * Do we really want to nuke all of the B_AGE stuff??
			 */
			if ((bp->b_flags & B_AGE) == 0 && found) {
				sema_v(&bp->b_sem);
				mutex_exit(hmp);
				lastindex = start;
				return;	/* All done */
			}

			ASSERT(MUTEX_HELD(&hp->b_lock));
			ASSERT(!(bp->b_flags & B_DELWRI));
			hp->b_length--;
			notavail(bp);

			/*
			 * Remove bhdr from cache, free up memory,
			 * and add the hdr to the freelist.
			 */
			bremhash(bp);
			mutex_exit(hmp);

			if (bp->b_bufsize) {
				kmem_free(bp->b_un.b_addr, bp->b_bufsize);
				bp->b_un.b_addr = NULL;
				mutex_enter(&bfree_lock);
				bfreelist.b_bufsize += bp->b_bufsize;
				mutex_exit(&bfree_lock);
			}

			bp->b_dev = (o_dev_t)NODEV;
			bp->b_edev = (dev_t)NODEV;
			bp->b_flags = B_KERNBUF;
			sema_v(&bp->b_sem);
			bio_bhdr_free(bp);
			if (want == BIO_HEADER) {
				found = 1;
			} else {
				ASSERT(want == BIO_MEM);
				if (!found && bfreelist.b_bufsize >= bsize) {
					/* Account for the memory we want */
					mutex_enter(&bfree_lock);
					bfreelist.b_bufsize -= bsize;
					mutex_exit(&bfree_lock);
					found = 1;
				}
			}

			/*
			 * Since we dropped hmp start from the
			 * begining.
			 */
			mutex_enter(hmp);
			bp = dp->av_forw;
		}

		/*
		 * Look at the delayed write list. We are still
		 * holding the mutex hmp.
		 */
		dwp = (struct buf *)&dwbuf[start];
		bp = dwp->av_forw;
		while (bp != dwp) {

			ASSERT(bp != NULL);

			if (!sema_tryp(&bp->b_sem)) {
				bp = bp->av_forw;
				continue;
			}
			/*
			 * Do we really want to nuke all of the B_AGE stuff??
			 */

			if ((bp->b_flags & B_AGE) == 0 && found) {
				sema_v(&bp->b_sem);
				mutex_exit(hmp);
				lastindex = start;
				return; /* All done */
			}

			ASSERT(bp->b_flags & B_DELWRI);

			/*
			 * We are still on the same bucket.
			 */
			hp->b_length--;
			notavail(bp);
			mutex_exit(hmp);
			bp->b_flags |= B_AGE | B_ASYNC;
			/*
			 * Get it out of here and don't wait.
			 */
			bwrite(bp);

			/*
			 * lock dropped, start over
			 */
			mutex_enter(hmp);
			bp = dwp->av_forw;
		}
		mutex_exit(hmp);
		start = (start + 1) % v.v_hbuf;

	} while (start != end);

	if (found)
		return;

	/*
	 * Free lists exhausted and we haven't satisfied the request.
	 * Wait here for more entries to be added to freelist
	 */
	mutex_enter(&bfree_lock);
	bfreelist.b_flags |= B_WANTED;
	cv_wait(&bio_mem_cv, &bfree_lock);
	mutex_exit(&bfree_lock);
	goto top;
}

/*
 * See if the block is associated with some buffer
 * (mainly to avoid getting hung up on a wait in breada).
 */
static int
bio_incore(register dev_t dev, register daddr_t blkno)
{
	register struct buf *bp;
	register struct buf *dp;
	u_int		index;
	kmutex_t	*hmp;

	index = bio_bhash(dev, blkno);
	dp = (struct buf *)&hbuf[index];
	hmp = &hbuf[index].b_lock;

	mutex_enter(hmp);
	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw) {
		if (bp->b_blkno == blkno && bp->b_edev == dev &&
		    (bp->b_flags & B_STALE) == 0) {
			mutex_exit(hmp);
			return (1);
		}
	}
	mutex_exit(hmp);
	return (0);
}

static void
bio_pageio_done(struct buf *bp)
{
	if (bp->b_flags & B_PAGEIO) {

		if (bp->b_flags & B_REMAPPED)
			bp_mapout(bp);

		if (bp->b_flags & B_READ)
			pvn_read_done(bp->b_pages, bp->b_flags);
		else
			pvn_write_done(bp->b_pages, B_WRITE | bp->b_flags);
		pageio_done(bp);
	} else {
		/* bp was B_ASYNC and B_REMAPPED (and not B_PAGEIO) */
		if ((bp->b_flags & B_REMAPPED) == 0)
			cmn_err(CE_PANIC, "cleanup: not remapped");
		bp_mapout(bp);
		brelse(bp);
	}
}

/*
 * bioerror(9F) - indicate error in buffer header
 * If 'error' is zero, remove the error indication.
 */
void
bioerror(struct buf *bp, int error)
{
	ASSERT(bp != NULL);
	ASSERT(error >= 0);
	ASSERT(SEMA_HELD(&bp->b_sem));

	if (error != 0) {
		bp->b_flags |= B_ERROR;
	} else {
		bp->b_flags &= ~B_ERROR;
	}
	bp->b_error = error;
}

/*
 * bioreset(9F) - reuse a private buffer header after I/O  is  complete
 */
void
bioreset(struct buf *bp)
{
	ASSERT(bp != NULL);

	sema_destroy(&bp->b_sem);
	sema_destroy(&bp->b_io);

	/* zero out buffer header storage allocated */
	struct_zero((caddr_t)bp, sizeof (struct buf));

	/* Initialize semaphores */
	sema_init(&bp->b_sem, 0, "bp semaphore", SEMA_DEFAULT, NULL);
	sema_init(&bp->b_io, 0, "bp io semaphore", SEMA_DEFAULT, NULL);
}
