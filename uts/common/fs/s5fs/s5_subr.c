/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
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
 * 	(c) 1986,1987,1988,1989,1993  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */

#pragma ident	"@(#)s5_subr.c	1.6	94/04/19 SMI"
/* from ufs_subr.c	2.64	92/12/02 SMI */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/rwlock.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/fs/s5_fs.h>
#include <sys/cmn_err.h>

#ifdef _KERNEL

#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/user.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/debug.h>
#include <sys/fs/s5_inode.h>
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg_map.h>
#include <sys/swap.h>
#include <vm/seg_kmem.h>

#else  /* _KERNEL */

#define	ASSERT(x)		/* don't use asserts for fsck et al */

#endif  /* _KERNEL */

#ifdef _KERNEL

struct check_node {
	struct vfs *vfsp;
	struct s5vfs *s5vfs;
	dev_t vfs_dev;
};
static int still_mounted();

/*
 * s5_update performs the s5 part of `sync'.  It goes through the disk
 * queues to initiate sandbagged IO; goes through the inodes to write
 * modified nodes; and it goes through the mount table to initiate
 * the writing of the modified super blocks.
 */
void
s5_update(int flag)
{
	register struct vfs *vfsp;
	extern struct vfsops s5_vfsops;
	struct filsys *fs;
	struct s5vfs *s5vfsp;
	struct vfs *update_list = 0;
	time_t start_time;
	int check_cnt = 0;
	size_t check_size;
	struct check_node *check_list, *ptr;
	extern void s5_sbupdate(struct vfs *vfsp);

	mutex_enter(&s5_syncbusy);
	/*
	 * Find all s5 vfs structures and add them to the update list.
	 * This is so that we don't hold the vfslock for a long time.
	 * If vfs_lock fails then skip it because somebody is doing a
	 * unmount on it.
	 */
	mutex_enter(&vfslist);
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next)
		if (vfsp->vfs_op == &s5_vfsops && vfs_lock(vfsp) == 0) {
			vfsp->vfs_list = update_list;
			update_list = vfsp;
			check_cnt++;
		}
	mutex_exit(&vfslist);
	if (update_list == NULL) {
		mutex_exit(&s5_syncbusy);
		return;
	}

	check_size = sizeof (struct check_node) * check_cnt;
	check_list = ptr =
	    (struct check_node *) kmem_alloc(check_size, KM_NOSLEEP);

	/*
	 * Write back modified superblocks.
	 * Consistency check that the superblock of
	 * each file system is still in the buffer cache.
	 */
	check_cnt = 0;
	for (vfsp = update_list; vfsp != NULL; vfsp = vfsp->vfs_list) {
		s5vfsp = (struct s5vfs *)vfsp->vfs_data;
		fs = (struct filsys *)s5vfsp->vfs_fs;
		mutex_enter(&s5vfsp->vfs_lock);
		/*
		 * Build up the check list, so we don't need to
		 * lock the vfs until the actual checking.
		 */
		if (check_list != NULL) {
			if ((fs->s_ronly == 0) &&
			    (fs->s_state != FsBADBLK) &&
			    (fs->s_state != FsBAD)) {
				/* ignore if read only or BAD or SUPEND */
				ptr->vfsp = vfsp;
				ptr->s5vfs = s5vfsp;
				ptr->vfs_dev = vfsp->vfs_dev;
				ptr++;
				check_cnt++;
			}
		}

		if (fs->s_fmod == 0) {
			mutex_exit(&s5vfsp->vfs_lock);
			vfs_unlock(vfsp);
			continue;
		}
		if (fs->s_ronly != 0) {
			mutex_exit(&s5vfsp->vfs_lock);
			mutex_exit(&s5_syncbusy);
			cmn_err(CE_PANIC, "update ro S5FS mod\n");
		}
		fs->s_fmod = 0;
		mutex_exit(&s5vfsp->vfs_lock);

		s5_sbupdate(vfsp);
		vfs_unlock(vfsp);
	}
	s5_flushi(flag);
	/*
	 * Force stale buffer cache information to be flushed,
	 * for all devices.  This should cause any remaining control
	 * information (e.g., inode info) to be flushed back.
	 */
	bflush((dev_t)NODEV);

	if (check_list == NULL) {
		mutex_exit(&s5_syncbusy);
		return;
	}

	/*
	 * For each S5 filesystem in the check_list, update
	 * the clean flag if warranted.
	 */
	start_time = hrestime.tv_sec;
	for (ptr = check_list; check_cnt > 0; check_cnt--, ptr++) {
		if (still_mounted(ptr)) {
			s5_checkclean(ptr->vfsp, ptr->s5vfs,
			    ptr->vfs_dev, start_time);
			vfs_unlock(ptr->vfsp);
		}
	}

	mutex_exit(&s5_syncbusy);
	kmem_free(check_list, check_size);
}

void
s5_flushi(flag)
	int flag;
{
	register struct inode *ip, *lip;
	register struct vnode *vp;
	register int cheap = flag & SYNC_ATTR;

	/*
	 * Write back each (modified) inode,
	 * but don't sync back pages if vnode is
	 * part of the virtual swap device.
	 */
	register union  ihead *ih;
	extern krwlock_t	icache_lock;

	rw_enter(&icache_lock, RW_READER);
	for (ih = ihead; ih < &ihead[INOHSZ]; ih++) {
		for (ip = ih->ih_chain[0], lip = NULL;
		    ip && ip != (struct inode *)ih; ip = ip->i_forw) {
			int	flag = ip->i_flag;	/* Atomic read */

			vp = ITOV(ip);
			/*
			 * Skip locked & inactive inodes.
			 * Skip inodes w/ no pages and no inode changes.
			 * Skip inodes from read only vfs's.
			 */
			if ((flag & IREF) == 0 ||
			    ((vp->v_pages == NULL) &&
			    ((flag & (IMOD|IACC|IUPD|ICHG)) == 0)) ||
			    (vp->v_vfsp == NULL) ||
			    ((vp->v_vfsp->vfs_flag & VFS_RDONLY) != 0))
				continue;

			if (!rw_tryenter(&ip->i_contents, RW_WRITER))
				continue;

			VN_HOLD(vp);
			/*
			 * Can't call s5_iput with ip because we may
			 * kmem_free the inode destorying i_forw.
			 */
			if (lip != NULL)
				s5_iput(lip);
			lip = ip;

			/*
			 * If this is an inode sync for file system hardening
			 * or this is a full sync but file is a swap file,
			 * don't sync pages but make sure the inode is up
			 * to date.  In other cases, push everything out.
			 */
			if (cheap || IS_SWAPVP(vp)) {
				s5_iupdat(ip, 0);
			} else {
				(void) s5_syncip(ip, B_ASYNC, I_SYNC);
			}
			rw_exit(&ip->i_contents);
		}
		if (lip != NULL)
			s5_iput(lip);
	}
	rw_exit(&icache_lock);
}

/*
 * Flush all the pages associated with an inode using the given 'flags',
 * then force inode information to be written back using the given 'waitfor'.
 */
int
s5_syncip(register struct inode *ip, int flags, int waitfor)
{
	int	error;
	register struct vnode *vp = ITOV(ip);

	ASSERT(RW_WRITE_HELD(&ip->i_contents));

	TRACE_3(TR_FAC_S5, TR_S5_SYNCIP_START,
		"s5_syncip_start:vp %x flags %x waitfor %x",
		vp, flags, waitfor);

	/*
	 * Return if file system has been forcibly umounted.
	 * (Shouldn't happen with S5.)
	 */
	if (ip->i_s5vfs == NULL)
		return (0);

	/*
	 * The data for directories is always written synchronously
	 * so we can skip pushing pages if we are just doing a B_ASYNC
	 */
	if (vp->v_pages == NULL || vp->v_type == VCHR ||
	    (vp->v_type == VDIR && flags == B_ASYNC)) {
		error = 0;
	} else {
		rw_exit(&ip->i_contents);
		error = VOP_PUTPAGE(vp, (offset_t)0, (u_int)0, flags, CRED());
		rw_enter(&ip->i_contents, RW_WRITER);
	}
	if (ip->i_flag & (IUPD |IACC | ICHG | IMOD))
		s5_iupdat(ip, waitfor);

	TRACE_2(TR_FAC_S5, TR_S5_SYNCIP_END,
		"s5_syncip_end:vp %x error %d",
		vp, error);

	return (error);
}

/*
 * Flush all indirect blocks related to an inode.
 */
int
s5_sync_indir(register struct inode *ip)
{
	register int i;
	register daddr_t lbn;	/* logical blkno of last blk in file */
	daddr_t sbn;	/* starting blkno of next indir block */
	struct filsys *fs;
	int bsize;
	static int s5_flush_indir();
	int error;

	fs = ITOF(ip);
	bsize = ip->i_s5vfs->vfs_bsize;
	lbn = lblkno(ip->i_s5vfs, ip->i_size - 1);
	sbn = NDADDR;

	for (i = 0; i < 3; i++) {
		if (lbn < sbn)
			return (0);
		if (error = s5_flush_indir(fs, ip, ip->i_addr[NADDR+i],
		    i, lbn, bsize, &sbn))
			return (error);
	}
	return (EFBIG);	/* Shouldn't happen */
}

static int
s5_flush_indir(fs, ip, lvl, iblk, lbn, bsize, sbnp)
	struct filsys *fs;
	register struct inode *ip;
	daddr_t iblk;			/* indirect block */
	int lvl;			/* indirect block level */
	daddr_t lbn;			/* last block number in file */
	int bsize;
	daddr_t *sbnp;			/* ptr to blkno of starting block */
{
	register int i;
	daddr_t clbn = *sbnp;	/* current logical blk */
	register daddr_t *bap;
	struct buf *bp;
	register int nindir = NINDIR(ip->i_s5vfs);
	int error = 0;

	if (iblk) {
		if (lvl > 0) {
			bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, iblk),
			    bsize);
			if (bp->b_flags & B_ERROR) {
				brelse(bp);
				error = EIO;
				goto out;
			}
			bap = bp->b_un.b_daddr;
			for (i = 0; i < nindir; i++) {
				if (clbn > lbn)
					break;
				if (error = s5_flush_indir(fs, ip, lvl-1,
				    bap[i], lbn, bsize, &clbn))
					break;
			}
			brelse(bp);
		}
		blkflush(ip->i_dev, (daddr_t)fsbtodb(fs, iblk));
	}

out:
	for (i = nindir; --lvl >= 0; i *= nindir)
		/* null */;
	*sbnp += i;
	return (error);
}

/*
 * As part of the s5 'sync' operation, this routine is called to mark
 * the filesystem as STABLE if there is no modified metadata in memory.
 * S5, unlike UFS, doesn't have a "STABLE" state; instead, we write all
 * superblocks that pass the bcheck and icheck tests.
 */
void
s5_checkclean(vfsp, s5vfsp, dev, timev)
	register struct vfs *vfsp;
	register struct s5vfs *s5vfsp;
	register dev_t dev;
	register time_t timev;
{
	register struct filsys *fs;
	static int s5_icheck();
	struct ulockfs *ulp = &s5vfsp->vfs_ulockfs;

	ASSERT(MUTEX_HELD(&vfsp->vfs_reflock) || MUTEX_HELD(&ulp->ul_lock));

	fs = (struct filsys *)s5vfsp->vfs_fs;
	/*
	 * ignore if buffers or inodes are busy
	 */
	if ((bcheck(dev, s5vfsp->vfs_bufp)) || (s5_icheck(s5vfsp)))
		return;
	mutex_enter(&s5vfsp->vfs_lock);
	/*
	 * ignore if someone else modified the superblock while we
	 * are doing the "stable" checking.
	 * for S5, the time check here is coarser than for UFS.
	 */
	if (fs->s_time > timev) {
		mutex_exit(&s5vfsp->vfs_lock);
		return;
	}

	/*
	 * write superblock synchronously
	 */
	s5_sbwrite(s5vfsp);
	mutex_exit(&s5vfsp->vfs_lock);
}

/*
 * When i_rwlock is write-locked or has a writer pended, then the inode
 * is going to change in a way that the filesystem will be marked as
 * active. So no need to let the filesystem be mark as stable now.
 * Also to ensure the filesystem consistency during the directory
 * operations, filesystem cannot be marked as stable if i_rwlock of
 * the directory inode is write-locked.
 */

/*
 * Check for busy inodes for this filesystem.
 * NOTE: Needs better way to do this expensive operation in the future.
 */
static int
s5_icheck(s5vfsp)
	register struct s5vfs	*s5vfsp;
{
	register union  ihead	*ih;
	register struct inode	*ip;
	extern krwlock_t icache_lock;

	rw_enter(&icache_lock, RW_READER);
	for (ih = ihead; ih < &ihead[INOHSZ]; ih++) {
		for (ip = ih->ih_chain[0];
		    ((ip != NULL) && (ip != (struct inode *)ih));
		    ip = ip->i_forw) {
			/*
			 * if inode is busy/modified/deleted, filesystem is busy
			 */
			if ((ip->i_s5vfs == s5vfsp) &&
			    ((ip->i_flag & (IMOD|IUPD|ICHG)) ||
			    (RW_ISWRITER(&ip->i_rwlock)) ||
			    ((ip->i_nlink <= 0) && (ip->i_flag & IREF)))) {
				rw_exit(&icache_lock);
				return (1);
			}
		}

	}
	rw_exit(&icache_lock);
	return (0);
}

/*
 * s5 specific fbwrite()
 */
/*ARGSUSED1*/
int
s5_fbwrite(fbp, ip)
	register struct fbuf *fbp;
	register struct inode *ip;
{
	return (fbwrite(fbp));
}

/*
 * s5 specific fbiwrite()
 */
int
s5_fbiwrite(fbp, ip, bn, bsize)
	register struct fbuf *fbp;
	register struct inode *ip;
	daddr_t bn;
	long bsize;
{
	return (fbiwrite(fbp, ip->i_devvp, bn, (int) bsize));
}

/*
 * Write the s5 superblock only.
 */
void
s5_sbwrite(register struct s5vfs *s5vfsp)
{
	register struct filsys *fs = (struct filsys *)s5vfsp->vfs_fs;
	char save_mod;
	int  isactive;

	ASSERT(MUTEX_HELD(&s5vfsp->vfs_lock));
	/*
	 * update superblock timestamp and s_state
	 */
	fs->s_time = hrestime.tv_sec;
	if (fs->s_state == FsACTIVE) {
		fs->s_state = FsOKAY - (long)fs->s_time;
		isactive = 1;
	} else
		isactive = 0;

	save_mod = fs->s_fmod;
	fs->s_fmod = 0;	/* s_fmod must always be 0 */
	/*
	 * Don't release the buffer after writing to the disk
	 */
	bwrite2(s5vfsp->vfs_bufp);		/* update superblock */

	if (isactive)
		fs->s_state = FsACTIVE;

	fs->s_fmod = save_mod;
}

/*
 * Returns 1 and hold the lock if the vfs is still being mounted.
 * Otherwise, returns 0.
 */
static int
still_mounted(checkp)
	register struct check_node *checkp;
{
	register struct vfs	*vfsp;

	mutex_enter(&vfslist);
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next) {
		if (vfsp == checkp->vfsp &&
		    (struct s5vfs *)vfsp->vfs_data == checkp->s5vfs &&
		    vfsp->vfs_dev == checkp->vfs_dev && vfs_lock(vfsp) == 0) {
			mutex_exit(&vfslist);
			return (1);
		}
	}
	mutex_exit(&vfslist);
	return (0);
}
#endif	_KERNEL
