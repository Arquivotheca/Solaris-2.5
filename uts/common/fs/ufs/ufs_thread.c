/*	copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */
#ident  "@(#)ufs_thread.c 1.41 95/03/08 SMI"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/user.h>
#include <sys/cpuvar.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_trans.h>
#include <sys/fs/ufs_acl.h>
#include <sys/debug.h>

extern pri_t 		minclsyspri;
extern int		nireuse;
extern kmutex_t		ireuse_mutex;
extern struct inode	*ireuse;
extern int		ino_new;
extern struct instats	ins;
extern int		hash2ints();

/*
 * This was put in because of changes to ufs made by bonwick when he
 * changed the kma.  It may not be necessary because of the reuse list.
 */
extern struct kmem_cache	*inode_cache;	/* cache of free inodes */

/*
 * initialize a thread's queue struct
 */
void
ufs_thread_init(struct ufs_q *uq, long maxne)
{
	bzero((caddr_t)uq, sizeof (*uq));
	cv_init(&uq->uq_cv, "ufs thread cv", CV_DEFAULT, NULL);
	mutex_init(&uq->uq_mutex, "ufs thread mutex", MUTEX_DEFAULT, NULL);
	uq->uq_maxne = maxne;
	uq->uq_threadp = NULL;
}

/*
 * start a thread for a queue (assumes success)
 */
void
ufs_thread_start(struct ufs_q *uq, void (*func)(), struct vfs *vfsp)
{
	mutex_enter(&uq->uq_mutex);
	if ((uq->uq_flags & UQ_EXISTS) == 0) {
		uq->uq_threadp = thread_create(NULL, 0, func, (caddr_t)vfsp, 0,
						    &p0, TS_RUN, minclsyspri);
		if (uq->uq_threadp)
			uq->uq_flags |= UQ_EXISTS;
	}
	mutex_exit(&uq->uq_mutex);
}

/*
 * wait for the thread to exit
 */
void
ufs_thread_exit(struct ufs_q *uq)
{
	mutex_enter(&uq->uq_mutex);
	uq->uq_flags &= ~(UQ_SUSPEND | UQ_SUSPENDED);
	while (uq->uq_flags & UQ_EXISTS) {
		uq->uq_flags |= (UQ_EXIT|UQ_WAIT);
		cv_broadcast(&uq->uq_cv);
		cv_wait(&uq->uq_cv, &uq->uq_mutex);
	}
	uq->uq_threadp = NULL;
	mutex_exit(&uq->uq_mutex);
}

/*
 * wait for a thread to suspend itself on the caller's behalf
 *	the caller is responsible for continue'ing the thread
 */
void
ufs_thread_suspend(struct ufs_q *uq)
{
	mutex_enter(&uq->uq_mutex);
	if (uq->uq_flags & UQ_EXISTS) {
		/*
		 * while another thread is suspending this thread
		 */
		while (uq->uq_flags & UQ_SUSPEND)
			cv_wait(&uq->uq_cv, &uq->uq_mutex);
		/*
		 * wait for the thread to suspend itself
		 */
		uq->uq_flags |= UQ_SUSPEND;
		while ((uq->uq_flags & UQ_SUSPENDED) == 0) {
			uq->uq_flags |= UQ_WAIT;
			cv_broadcast(&uq->uq_cv);
			cv_wait(&uq->uq_cv, &uq->uq_mutex);
		}
	}
	mutex_exit(&uq->uq_mutex);
}

/*
 * allow a thread to continue from a ufs_thread_suspend()
 *	This thread must be the same as the thread that called
 *	ufs_thread_suspend.
 */
void
ufs_thread_continue(struct ufs_q *uq)
{
	mutex_enter(&uq->uq_mutex);
	uq->uq_flags &= ~(UQ_SUSPEND | UQ_SUSPENDED);
	cv_broadcast(&uq->uq_cv);
	mutex_exit(&uq->uq_mutex);
}

/*
 * some common code for managing a threads execution
 *	uq is locked at entry and return
 *	may sleep
 *	may exit
 */
long
ufs_thread_run(struct ufs_q *uq)
{
again:
	if (uq->uq_flags & UQ_SUSPEND)
		uq->uq_flags |= UQ_SUSPENDED;
	else if (uq->uq_flags & UQ_EXIT) {
		/*
		 * exiting; empty the queue (may infinite loop)
		 */
		if (uq->uq_ne)
			return (uq->uq_ne);
		if (uq->uq_flags & UQ_WAIT)
			cv_broadcast(&uq->uq_cv);
		uq->uq_flags &= ~(UQ_EXISTS | UQ_EXIT | UQ_WAIT);
		mutex_exit(&uq->uq_mutex);
		thread_exit();
	} else if (uq->uq_ne >= uq->uq_maxne) {
		/*
		 * process a block of entries until below high water mark
		 */
		return (uq->uq_ne - (uq->uq_maxne >> 1));
	}
	if (uq->uq_flags & UQ_WAIT) {
		uq->uq_flags &= ~UQ_WAIT;
		cv_broadcast(&uq->uq_cv);
	}
	cv_wait(&uq->uq_cv, &uq->uq_mutex);
	goto again;
}
/*
 * DELETE INODE
 * The following routines implement the protocol for freeing the resources
 * held by an idle and deleted inode.
 */
void
ufs_delete(struct ufsvfs *ufsvfsp, struct inode *ip, int dolockfs)
{
	ushort_t	mode;
	struct vnode	*vp	= ITOV(ip);
	struct ulockfs	*ulp;
	int		trans_size;

	/*
	 * not on a trans device or not part of a transaction
	 */
	ASSERT(!TRANS_ISTRANS(ufsvfsp) ||
		((curthread->t_flag & T_DONTBLOCK) == 0));

	/*
	 * Ignore if deletes are not allowed (wlock/hlock)
	 */
	if (ULOCKFS_IS_NOIDEL(ITOUL(ip))) {
		VN_RELE(vp);
		return;
	}

	if ((vp->v_count > 1) || (ip->i_mode == 0)) {
		VN_RELE(vp);
		return;
	}
	/*
	 * If we are called as part of setting a fs lock, then only
	 * do part of the lockfs protocol.  In other words, don't hang.
	 */
	if (dolockfs) {
		if (ufs_lockfs_begin(ufsvfsp, &ulp, ULOCKFS_DELETE_MASK))
			return;
	} else {
		/*
		 * check for recursive VOP call
		 */
		if (curthread->t_flag & T_DONTBLOCK)
			ulp = NULL;
		else {
			ulp = &ufsvfsp->vfs_ulockfs;
			curthread->t_flag |= T_DONTBLOCK;
		}
	}

	(void) TRANS_ITRUNC(ip, (u_long)0, I_FREE, CRED());

	/*
	 * the inode's space has been freed; now free the inode
	 */
	if (ulp) {
		trans_size = TOP_IFREE_SIZE(ip);
		TRANS_BEGIN_ASYNC(ufsvfsp, TOP_IFREE, trans_size);
	}
	rw_enter(&ip->i_contents, RW_WRITER);
	TRANS_INODE(ufsvfsp, ip);
	mode = ip->i_mode;
	ip->i_mode = 0;
	ip->i_rdev = 0;
	ip->i_ordev = 0;
	ip->i_flag |= IMOD;
	if (ip->i_ufs_acl) {
		(void) ufs_si_free(ip->i_ufs_acl, ITOV(ip)->v_vfsp, CRED());
		ip->i_ufs_acl = NULL;
		ip->i_shadow = 0;
	}
	/*
	 * free the inode
	 */
	ufs_ifree(ip, ip->i_number, mode);
	/*
	 * release quota resources
	 */
#ifdef QUOTA
	(void) chkiq((struct ufsvfs *)ip->i_vnode.v_vfsp->
	    vfs_data, ip, (uid_t)ip->i_uid, 0, CRED());
	dqrele(ip->i_dquot);
	ip->i_dquot = NULL;
#endif
	rw_exit(&ip->i_contents);
	VN_RELE(vp);

	/*
	 * End of transaction
	 */
	if (ulp) {
		TRANS_END_ASYNC(ufsvfsp, TOP_IFREE, trans_size);
		if (dolockfs)
			ufs_lockfs_end(ulp);
		else
			curthread->t_flag &= ~T_DONTBLOCK;
	}
}

/*
 * thread that frees up deleted inodes
 */
void
ufs_thread_delete(struct vfs *vfsp)
{
	struct ufsvfs	*ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	struct ufs_q	*uq	= &ufsvfsp->vfs_delete;
	struct inode	*ip;
	long		ne;

	mutex_enter(&uq->uq_mutex);
again:
	/*
	 * sleep until there is work to do
	 */
	ne = ufs_thread_run(uq);
	/*
	 * process up to ne entries
	 */
	while (ne-- && (ip = uq->uq_ihead)) {
		/*
		 * process first entry on queue.  Assumed conditions are:
		 *	ip is held (v_count >= 1)
		 *	ip is referenced (i_flag & IREF)
		 *	ip is free (i_nlink <= 0)
		 */
		if ((uq->uq_ihead = ip->i_freef) == ip)
			uq->uq_ihead = NULL;
		ip->i_freef->i_freeb = ip->i_freeb;
		ip->i_freeb->i_freef = ip->i_freef;
		ip->i_freef = ip;
		ip->i_freeb = ip;
		uq->uq_ne--;
		mutex_exit(&uq->uq_mutex);
		ufs_delete(ufsvfsp, ip, 1);
		mutex_enter(&uq->uq_mutex);
	}
	goto again;
}

/*
 * drain ne entries off the delete queue
 */
void
ufs_delete_drain(struct vfs *vfsp, int ne, int dolockfs)
{
	struct ufsvfs	*ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	struct ufs_q	*uq;
	struct inode	*ip;

	/*
	 * if forcibly unmounted; ignore
	 */
	if (ufsvfsp == NULL)
		return;

	uq = &ufsvfsp->vfs_delete;
	mutex_enter(&uq->uq_mutex);
	if (ne <= 0)
		ne = uq->uq_ne;
	/*
	 * process up to ne entries
	 */
	while (ne-- && (ip = uq->uq_ihead)) {
		if ((uq->uq_ihead = ip->i_freef) == ip)
			uq->uq_ihead = NULL;
		ip->i_freef->i_freeb = ip->i_freeb;
		ip->i_freeb->i_freef = ip->i_freef;
		ip->i_freef = ip;
		ip->i_freeb = ip;
		uq->uq_ne--;
		mutex_exit(&uq->uq_mutex);
		ufs_delete(ufsvfsp, ip, dolockfs);
		mutex_enter(&uq->uq_mutex);
	}
	mutex_exit(&uq->uq_mutex);
}
/*
 * IDLE INODE
 * The following routines implement the protocol for maintaining an
 * LRU list of idle inodes and for moving the idle inodes to the
 * reuse list when the number of allocated inodes exceeds the user
 * tunable high-water mark (ufs_ninode).
 */

/*
 * clean an idle inode and move it to the reuse list
 */
static void
ufs_idle_free(struct inode *ip)
{
	int			pages;
	int			hno;
	kmutex_t		*ihm;
	struct ufsvfs		*ufsvfsp	= ip->i_ufsvfs;
	struct vnode		*vp		= ITOV(ip);

	UFS_DEBUG_ICACHE_CHECK();

	/*
	 * inode is held
	 */

	/*
	 * remember `pages' for stats below
	 */
	pages = (ip->i_mode && vp->v_pages && vp->v_type != VCHR);

	/*
	 * start the dirty pages to disk and then invalidate them
	 */
	(void) TRANS_SYNCIP(ip, B_ASYNC, I_ASYNC, TOP_SYNCIP_FREE);
	(void) TRANS_SYNCIP(ip,
		(TRANS_ISERROR(ufsvfsp)) ? B_INVAL | B_FORCE : B_INVAL,
		I_ASYNC, TOP_SYNCIP_FREE);

	/*
	 * wait for any current ufs_iget to finish and block future ufs_igets
	 */
	hno = INOHASH(ip->i_dev, ip->i_number);
	ihm = &ih_lock[hno];
	mutex_enter(ihm);

	/*
	 * if inode has been referenced during the cleanup; re-idle it
	 */
	if ((vp->v_type != VCHR && vp->v_pages) || vp->v_count != 1 ||
		ip->i_flag & (IMOD|IMODACC|IACC|ICHG|IUPD|IATTCHG)) {
		mutex_exit(ihm);
		VN_RELE(vp);
	} else {
		/*
		 * remove it from the cache
		 */
		remque(ip);
		UFS_DEBUG_ICACHE_DEC(hno);
		mutex_exit(ihm);
		TRANS_DQRELE(ip->i_dquot);
		ip->i_dquot = NULL;
		ufs_si_del(ip);
		mutex_enter(&ireuse_mutex);
		if (pages) {
			CPU_STAT_ADDQ(CPU, cpu_sysinfo.ufsipage, 1);
		} else {
			CPU_STAT_ADDQ(CPU, cpu_sysinfo.ufsinopage, 1);
		}
		if (ino_new > ufs_ninode && nireuse > ufs_idle_max) {
			/*
			 * too many inodes; kmem_cache_free the inode
			 */
			ino_new--;
			mutex_exit(&ireuse_mutex);
			cv_destroy(&ip->i_wrcv);  /* throttling */
			rw_destroy(&ip->i_rwlock);
			rw_destroy(&ip->i_contents);
			ASSERT((vp->v_type == VCHR) || (vp->v_pages == NULL));
			kmem_cache_free(inode_cache, ip);
			ins.in_mfree.value.ul++;
		} else {
			/*
			 * reuse this inode later
			 */
			++nireuse;
			ip->i_freef = ireuse;
			ireuse = ip;
			mutex_exit(&ireuse_mutex);
		}
	}
	UFS_DEBUG_ICACHE_CHECK();
}
/*
 * this thread processes the global idle queue
 */
struct ufs_q	ufs_idle_q;
void
ufs_thread_idle(void)
{
	struct inode		*ip;
	long			ne;

again:
	mutex_enter(&ufs_idle_q.uq_mutex);
	/*
	 * sleep until there is work to do
	 */
	ne = ufs_thread_run(&ufs_idle_q);

	/*
	 * process up to ne entries
	 */
	while (ne-- && (ip = ufs_idle_q.uq_ihead)) {
		/*
		 * emulate ufs_iget
		 */
		VN_HOLD(ITOV(ip));
		mutex_exit(&ufs_idle_q.uq_mutex);
		rw_enter(&ip->i_contents, RW_WRITER);
		ufs_rmidle(ip);
		rw_exit(&ip->i_contents);
		/*
		 * try and free the idle inode
		 */
		ufs_idle_free(ip);
		mutex_enter(&ufs_idle_q.uq_mutex);
	}
	mutex_exit(&ufs_idle_q.uq_mutex);
	goto again;
}

/*
 * drain entries for vfsp from the idle queue
 */
void
ufs_idle_drain(struct vfs *vfsp)
{
	struct inode		*ip;
again:
	mutex_enter(&ufs_idle_q.uq_mutex);
	if ((ip = ufs_idle_q.uq_ihead) != 0)
		do {
			if (ip->i_vfs == vfsp)
				break;
			ip = ip->i_freef;
		} while (ip != ufs_idle_q.uq_ihead);
	mutex_exit(&ufs_idle_q.uq_mutex);
	if (ip == NULL || ip->i_vfs != vfsp)
		return;
	VN_HOLD(ITOV(ip));
	rw_enter(&ip->i_contents, RW_WRITER);
	ufs_rmidle(ip);
	rw_exit(&ip->i_contents);
	ufs_idle_free(ip);
	goto again;
}
/*
 * RECLAIM DELETED INODES
 * The following thread scans the file system once looking for deleted files
 */
void
ufs_thread_reclaim(struct vfs *vfsp)
{
	struct ufsvfs		*ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	struct ufs_q		*uq	= &ufsvfsp->vfs_reclaim;
	struct fs		*fs	= ufsvfsp->vfs_fs;
	struct buf		*bp	= 0;
	int			err	= 0;
	daddr_t			bno;
	ino_t			ino;
	struct dinode		*dp;
	struct inode		*ip;

	/*
	 * mount decided that we don't need a reclaim thread
	 */
	if ((fs->fs_reclaim & FS_RECLAIMING) == 0)
		err++;

	/*
	 * don't reclaim if readonly
	 */
	if (fs->fs_ronly)
		err++;

	for (ino = 0; ino < (fs->fs_ncg * fs->fs_ipg) && !err; ++ino) {

		/*
		 * exit at our convenience
		 */
		mutex_enter(&uq->uq_mutex);
		if (uq->uq_flags & UQ_EXIT)
			err++;
		mutex_exit(&uq->uq_mutex);

		/*
		 * if we don't already have the buf; get it
		 */
		bno = fsbtodb(fs, itod(fs, ino));
		if ((bp == 0) || (bp->b_blkno != bno)) {
			if (bp)
				brelse(bp);
			bp = bread(vfsp->vfs_dev, bno, fs->fs_bsize);
			bp->b_flags |= B_AGE;
		}
		if (bp->b_flags & B_ERROR) {
			err++;
			continue;
		}
		/*
		 * nlink <= 0 and mode != 0 means deleted
		 */
		dp = (struct dinode *)bp->b_un.b_addr + itoo(fs, ino);
		if ((dp->di_nlink <= 0) && (dp->di_mode != 0)) {
			/*
			 * can't hold the buf (deadlock)
			 */
			brelse(bp);
			bp = 0;
			/*
			 * iget/iput sequence will put inode on ifree
			 * thread queue if it is idle.  This is a nop
			 * for busy (open, deleted) inodes
			 */
			if (ufs_iget(vfsp, ino, &ip, CRED()))
				err++;
			else
				VN_RELE(ITOV(ip));
		}
	}

	if (bp)
		brelse(bp);
	if (!err) {
		/*
		 * reset the reclaiming-bit
		 */
		mutex_enter(&ufsvfsp->vfs_lock);
		fs->fs_reclaim &= ~FS_RECLAIMING;
		mutex_exit(&ufsvfsp->vfs_lock);
		TRANS_SBWRITE(ufsvfsp, TOP_SBWRITE_RECLAIM);
	}

	/*
	 * exit the reclaim thread
	 */
	mutex_enter(&uq->uq_mutex);
	cv_broadcast(&uq->uq_cv);
	uq->uq_flags &= ~(UQ_EXISTS | UQ_WAIT);
	mutex_exit(&uq->uq_mutex);
	thread_exit();
}
/*
 * HLOCK FILE SYSTEM
 *	hlock the file system's whose metatrans devices have device errors
 */
struct ufs_q	ufs_hlock;
/*ARGSUSED*/
void
ufs_thread_hlock(void *ignore)
{
	int	retry;

	for (;;) {
		/*
		 * sleep until there is work to do
		 */
		mutex_enter(&ufs_hlock.uq_mutex);
		(void) ufs_thread_run(&ufs_hlock);
		ufs_hlock.uq_ne = 0;
		mutex_exit(&ufs_hlock.uq_mutex);
		/*
		 * hlock the error'ed fs's
		 *	retry after a bit if another app is doing lockfs stuff
		 */
		do {
			retry = ufs_trans_hlock();
			if (retry) {
				mutex_enter(&ufs_hlock.uq_mutex);
				(void) cv_timedwait(&ufs_hlock.uq_cv,
							&ufs_hlock.uq_mutex,
							lbolt + HZ);
				mutex_exit(&ufs_hlock.uq_mutex);
			}
		} while (retry);
	}
}
