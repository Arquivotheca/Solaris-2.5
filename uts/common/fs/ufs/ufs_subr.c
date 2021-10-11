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
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */

#pragma	ident	"@(#)ufs_subr.c	2.104	95/05/23 SMI"
/* From: SVr4.0 fs:fs/ufs/ufs_subr.c  1.20	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/fs/ufs_fs.h>
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
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_trans.h>
#include <sys/fs/ufs_panic.h>
#include <sys/kmem.h>
#include <sys/vtrace.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
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
	struct ufsvfs *ufsvfs;
	dev_t vfs_dev;
};
static vfs_t *still_mounted(struct check_node *);

static void
ufs_funmount_cleanup()
{
	struct ufsvfs		*ufsvfsp;
	extern kmutex_t		ufsvfs_mutex;
	extern struct ufsvfs	*oldufsvfslist, *ufsvfslist;

	mutex_enter(&ufsvfs_mutex);
	while ((ufsvfsp = oldufsvfslist) != NULL) {
		oldufsvfslist = ufsvfsp->vfs_next;
		kmem_free((caddr_t)ufsvfsp, sizeof (struct ufsvfs));
	}
	oldufsvfslist = ufsvfslist;
	ufsvfslist = NULL;
	mutex_exit(&ufsvfs_mutex);
}


/*
 * ufs_update performs the ufs part of `sync'.  It goes through the disk
 * queues to initiate sandbagged IO; goes through the inodes to write
 * modified nodes; and it goes through the mount table to initiate
 * the writing of the modified super blocks.
 */
extern time_t	time;
time_t		ufs_sync_time;
time_t		ufs_sync_time_secs = 1;
void
ufs_update(int flag)
{
	register struct vfs *vfsp, *vfsnext;
	extern struct vfsops ufs_vfsops;
	struct fs *fs;
	struct ufsvfs *ufsvfsp;
	struct vfs *update_list = 0;
	int check_cnt = 0;
	size_t check_size;
	struct check_node *check_list, *ptr;
	int cheap = flag & SYNC_ATTR;

	/*
	 * This is a hack.  A design flaw in the forced unmount protocol
	 * could allow a thread to attempt to use a kmem_freed ufsvfs
	 * structure in ufs_lockfs_begin/ufs_check_lockfs.  This window
	 * is difficult to hit, even during the lockfs stress tests.
	 * So the hacky fix is to wait awhile before kmem_free'ing the
	 * ufsvfs structures for forcibly unmounted file systems.  `Awhile'
	 * is defined as every other call from fsflush (~60 seconds).
	 */
	if (cheap)
		ufs_funmount_cleanup();

	/*
	 * Find all ufs vfs structures and add them to the update list.
	 * This is so that we don't hold the vfslock for a long time.
	 * If vfs_lock fails then skip it because somebody is doing a
	 * unmount on it.
	 */
	mutex_enter(&vfslist);
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next)
		if (vfsp->vfs_op == &ufs_vfsops && vfs_lock(vfsp) == 0) {
			vfsp->vfs_list = update_list;
			update_list = vfsp;
			check_cnt++;
		}

	mutex_exit(&vfslist);
	if (update_list == NULL)
		return;

	check_size = sizeof (struct check_node) * check_cnt;
	check_list = ptr =
		(struct check_node *) kmem_alloc(check_size, KM_NOSLEEP);

	/*
	 * Write back modified superblocks.
	 * Consistency check that the superblock of
	 * each file system is still in the buffer cache.
	 */
	check_cnt = 0;
	for (vfsp = update_list, vfsnext = NULL; vfsp != NULL; vfsp = vfsnext) {
		if (!vfsp->vfs_data) {
			vfs_unlock(vfsp);
			continue;
		}

		/*
		 * Need to grab the next vfs ptr before we unlock this
		 * vfsp so another thread doesn't grab it and
		 * change it before we move on to the next vfs.
		 */
		vfsnext = vfsp->vfs_list;

		ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
		fs = ufsvfsp->vfs_fs;

		/*
		 * don't update a locked superblock during a panic; it
		 * may be in an inconsistent state
		 */
		if (panicstr) {
			if (!mutex_tryenter(&ufsvfsp->vfs_lock)) {
				vfs_unlock(vfsp);
				continue;
			}
		} else
			mutex_enter(&ufsvfsp->vfs_lock);
		/*
		 * Build up the STABLE check list, so we don't need to
		 * lock the vfs until the actual checking.
		 */
		if (check_list != NULL) {
			if ((fs->fs_ronly == 0) &&
			    (fs->fs_clean != FSBAD) &&
			    (fs->fs_clean != FSSUSPEND)) {
				ptr->vfsp = vfsp;
				ptr->ufsvfs = ufsvfsp;
				ptr->vfs_dev = vfsp->vfs_dev;
				ptr++;
				check_cnt++;
			}
		}

		/*
		 * superblock is not modified
		 */
		if (fs->fs_fmod == 0) {
			mutex_exit(&ufsvfsp->vfs_lock);
			vfs_unlock(vfsp);
			continue;
		}
		if (fs->fs_ronly != 0) {
			mutex_exit(&ufsvfsp->vfs_lock);
			vfs_unlock(vfsp);
			(void) ufs_fault(ufsvfsp->vfs_root,
					"fs = %s update: ro fs mod\n",
				fs->fs_fsmnt);
			return;
		}
		fs->fs_fmod = 0;
		mutex_exit(&ufsvfsp->vfs_lock);
		TRANS_SBUPDATE(ufsvfsp, vfsp, TOP_SBUPDATE_UPDATE);
		vfs_unlock(vfsp);
	}

	ufs_sync_time = time;
	(void) ufs_scan_inodes(1, ufs_sync_inode, (void *)cheap);

	/*
	 * Force stale buffer cache information to be flushed,
	 * for all devices.  This should cause any remaining control
	 * information (e.g., cg and inode info) to be flushed back.
	 */
	bflush((dev_t)NODEV);

	if (check_list == NULL)
		return;

	/*
	 * For each UFS filesystem in the STABLE check_list, update
	 * the clean flag if warranted.
	 */
	for (ptr = check_list; check_cnt > 0; check_cnt--, ptr++) {
		int	error;

		/*
		 * still_mounted() returns with vfsp and the vfs_reflock
		 * held if ptr refers to a vfs that is still mounted.
		 */
		if ((vfsp = still_mounted(ptr)) == NULL)
			continue;
		ufs_checkclean(vfsp);
		/*
		 * commit any outstanding async transactions
		 */
		ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_SYNC(ufsvfsp, TOP_COMMIT_UPDATE, TOP_COMMIT_SIZE);
		TRANS_END_SYNC(ufsvfsp, error, TOP_COMMIT_UPDATE,
				TOP_COMMIT_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;

		vfs_unlock(vfsp);
	}

	kmem_free((void *)check_list, check_size);
}

int
ufs_sync_inode(struct inode *ip, void *arg)
{
	int		cheap		= (int)arg;
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;

	/*
	 * if we are panic'ing; then don't update the inode if this
	 * file system is FSSTABLE.  Otherwise, we would have to
	 * force the superblock to FSACTIVE and the superblock
	 * may not be in a good state.  Also, if the inode is
	 * IREF'ed then it may be in an inconsistent state.  Don't
	 * push it.  Finally, don't push the inode if the fs is
	 * logging; the transaction will be discarded at boot.
	 */
	if (panicstr) {

		if (ip->i_flag & IREF)
			return (0);

		if (ip->i_ufsvfs == NULL ||
		    (ip->i_fs->fs_clean == FSSTABLE ||
		    ip->i_fs->fs_clean == FSLOG))
				return (0);
	}
	/*
	 * an app issueing a sync() can take forever on a trans device
	 * when NetWorker or find is running because all of the directorys'
	 * access times have to be updated.  So, we limit the time we spend
	 * updating access times per sync.
	 */
	if (ufsvfsp && TRANS_ISTRANS(ufsvfsp) &&
	    ((ufs_sync_time + ufs_sync_time_secs) < time) &&
	    ((ip->i_flag & (IMOD|IMODACC|IUPD|ICHG|IACC)) == IMODACC))
			return (0);
	/*
	 * if we are running on behalf of the flush thread or this is
	 * a swap file, then simply do a delay update of the inode.
	 * Otherwise, push the pages and then do a delayed inode update.
	 */
	if (cheap || IS_SWAPVP(ITOV(ip))) {
		TRANS_IUPDAT(ip, 0);
	} else {
		(void) TRANS_SYNCIP(ip, B_ASYNC, I_ASYNC, TOP_SYNCIP_SYNC);
	}
	return (0);
}

/*
 * Flush all the pages associated with an inode using the given 'flags',
 * then force inode information to be written back using the given 'waitfor'.
 */
int
ufs_syncip(struct inode *ip, int flags, int waitfor)
{
	int	error;
	struct vnode *vp = ITOV(ip);

	TRACE_3(TR_FAC_UFS, TR_UFS_SYNCIP_START,
		"ufs_syncip_start:vp %x flags %x waitfor %x",
		vp, flags, waitfor);

	/*
	 * Return if file system has been forcibly umounted.
	 */
	if (ip->i_ufsvfs == NULL)
		return (0);
	/*
	 * don't need to VOP_PUTPAGE if there are no pages
	 */
	if (vp->v_pages == NULL || vp->v_type == VCHR) {
		error = 0;
	} else {
		error = VOP_PUTPAGE(vp, (offset_t)0, (u_int)0, flags, CRED());
	}
	if (panicstr && TRANS_ISTRANS(ip->i_ufsvfs))
		goto out;
	/*
	 * waitfor represents two things -
	 * 1. whether data sync or file sync.
	 * 2. if file sync then ufs_iupdat should 'waitfor' disk i/o or not.
	*/
	if (waitfor == I_DSYNC) {
		/*
		 * If data sync, only IATTCHG (size/block change) requires
		 * inode update, fdatasync()/FDSYNC implementation.
		 */
		if (ip->i_flag & (IBDWRITE|IATTCHG)) {
			rw_enter(&ip->i_contents, RW_WRITER);
			ip->i_flag &= ~IMODTIME;
			ufs_iupdat(ip, 1);
			rw_exit(&ip->i_contents);
		}
	} else {
		/* For file sync, any indoe change requires inode update */
		if (ip->i_flag & (IBDWRITE|IUPD|IACC|ICHG|IMOD|IMODACC)) {
			rw_enter(&ip->i_contents, RW_WRITER);
			ip->i_flag &= ~IMODTIME;
			ufs_iupdat(ip, waitfor);
			rw_exit(&ip->i_contents);
		}
	}

out:
	TRACE_2(TR_FAC_UFS, TR_UFS_SYNCIP_END,
		"ufs_syncip_end:vp %x error %d",
		vp, error);

	return (error);
}
/*
 * Flush all indirect blocks related to an inode.
 * N.B. No support for three levels of indirect blocks.
 */
int
ufs_sync_indir(struct inode *ip)
{
	int i;
	daddr_t blkno;
	daddr_t lbn;	/* logical blkno of last blk in file */
	daddr_t clbn;	/* current logical blk */
	daddr_t *bap;
	struct fs *fs;
	struct buf *bp;
	int bsize;
	struct ufsvfs *ufsvfsp;

	ufsvfsp = ip->i_ufsvfs;
	/*
	 * unnecessary when logging; allocation blocks are kept up-to-date
	 */
	if (TRANS_ISTRANS(ufsvfsp))
		return (0);

	fs = ufsvfsp->vfs_fs;
	bsize = fs->fs_bsize;
	lbn = lblkno(fs, ip->i_size - 1);
	if (lbn < NDADDR)
		return (0);	/* No indirect blocks used */
	if (lbn < NDADDR + NINDIR(fs)) {
		/* File has one indirect block. */
		blkflush(ip->i_dev, (daddr_t)fsbtodb(fs, ip->i_ib[0]));
		return (0);
	}

	/* Write out all the first level indirect blocks */
	for (i = 0; i <= NIADDR; i++) {
		if ((blkno = ip->i_ib[i]) == 0)
			continue;
		blkflush(ip->i_dev, (daddr_t)fsbtodb(fs, blkno));
	}
	/* Write out second level of indirect blocks */
	if ((blkno = ip->i_ib[1]) == 0)
		return (0);
	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, blkno), bsize);
	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		return (EIO);
	}
	bap = bp->b_un.b_daddr;
	clbn = NDADDR + NINDIR(fs);
	for (i = 0; i < NINDIR(fs); i++) {
		if (clbn > lbn)
			break;
		clbn += NINDIR(fs);
		if ((blkno = bap[i]) == 0)
			continue;
		blkflush(ip->i_dev, (daddr_t)fsbtodb(fs, blkno));
	}
	brelse(bp);
	return (0);
}

/*
 * Flush all indirect blocks related to an offset of a file.
 * read/write in sync mode may have to flush indirect blocks.
 */
int
ufs_indirblk_sync(ip, off)
	register struct inode *ip;
	register long off;
{
	daddr_t	lbn;
	struct	fs *fs;
	struct	buf *bp;
	int	i, j, shft;
	daddr_t	ob, nb, tbn, *bap;
	int	nindirshift, nindiroffset;
	struct ufsvfs *ufsvfsp;

	ufsvfsp = ip->i_ufsvfs;
	/*
	 * unnecessary when logging; allocation blocks are kept up-to-date
	 */
	if (TRANS_ISTRANS(ufsvfsp))
		return (0);

	fs = ufsvfsp->vfs_fs;

	lbn = lblkno(fs, off);
	if (lbn < 0)
		return (EFBIG);

	/* The first NDADDR are direct so nothing to do */
	if (lbn < NDADDR)
		return (0);

	nindirshift = ip->i_ufsvfs->vfs_nindirshift;
	nindiroffset = ip->i_ufsvfs->vfs_nindiroffset;

	/* Determine level of indirect blocks */
	shft = 0;
	tbn = lbn - NDADDR;
	for (j = NIADDR; j > 0; j--) {
		int	sh;

		shft += nindirshift;
		sh = 1 << shft;
		if (tbn < sh)
			break;
		tbn -= sh;
	}

	if (j == 0)
		return (EFBIG);

	if ((nb = ip->i_ib[NIADDR - j]) == 0)
			return (0);		/* UFS Hole */

	/* Flush first level indirect block */
	blkflush(ip->i_dev, fsbtodb(fs, nb));

	/* Fetch through next levels */
	for (; j < NIADDR; j++) {
		ob = nb;
		bp = bread(ip->i_dev, fsbtodb(fs, ob), fs->fs_bsize);
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			return (EIO);
		}
		bap = bp->b_un.b_daddr;
		shft -= nindirshift;		/* sh / nindir */
		i = (tbn >> shft) & nindiroffset; /* (tbn /sh) & nindir */
		nb = bap[i];
		brelse(bp);
		if (nb == 0) {
			return (0); 		/* UFS hole */
		}
		blkflush(ip->i_dev, fsbtodb(fs, nb));
	}
	return (0);
}
/*
 * Check that a given indirect block contains blocks in range
 */
int
ufs_indir_badblock(struct inode *ip, daddr_t *bap)
{
	int i;
	int err = 0;

	for (i = 0; i < NINDIR(ip->i_fs) - 1; i++) {
		if (bap[i] != 0 && (err = ufs_badblock(ip, bap[i]))) {
			mutex_enter(&ip->i_tlock);
			ip->i_flag |= IBAD;
			mutex_exit(&ip->i_tlock);
		}
	}
	return (err);
}

/*
 * Check that a specified block number is in range.
 */
int
ufs_badblock(struct inode *ip, daddr_t bn)
{
	long	c;
	daddr_t	sum = 0;

	if (ip == NULL) {
		return (ufs_fault(NULL, "ufs_badblock: NULL ip, bn %d",
				    (int) bn));
	}
	if (ip->i_fs == NULL) {
		return (ufs_fault(ITOV(ip),
			"ufs_badblock: called with NULL fs, inum %d, idev 0x%x",
			(int)ip->i_number, (int)ip->i_dev));
	}

	if (bn <= 0 || bn > ip->i_fs->fs_size) {
		return (ufs_fault(ITOV(ip),
			"ufs_badblock: block %d, fs_size %d, fs %s: bad block",
				(int)bn, (int)ip->i_fs->fs_size,
				ip->i_fs->fs_fsmnt));
	}

	c = dtog(ip->i_fs, bn);
	if (c == 0) {
		sum = howmany(ip->i_fs->fs_cssize, ip->i_fs->fs_fsize);
	}
	/*
	 * if block no. is below this cylinder group,
	 * within the space reserved for superblock, inodes, (summary data)
	 * or if it is above this cylinder group
	 * then its invalid
	 * It's hard to see how we'd be outside this cyl, but let's be careful.
	 */
	if ((bn < cgbase(ip->i_fs, c)) ||
	    (bn >= cgsblock(ip->i_fs, c) && bn < cgdmin(ip->i_fs, c)+sum) ||
	    (bn >= cgbase(ip->i_fs, c+1))) {
		return (ufs_fault(ITOV(ip),
		    "ufs_badblock: inum %d block %d cyl %d fs %s: bad block",
		    (int)ip->i_number, (int)bn, (int)c, ip->i_fs->fs_fsmnt));
	}

	return (0);	/* not a bad block */
}

#if defined(DEBUG)

#define	CE_UFSDEBUG	((ufs_debug & UFS_DEBUG_PANIC) ? CE_PANIC : CE_WARN)
extern int ufs_debug;
static void ufs_markbadi(struct inode *);

/*
 * Check that a ufs inode appears valid
 * returns true if inode is valid, false otherwise
 *  Assumes some (i_contents, i_rwlock) lock held when called.
 */

int
ufs_ivalid(struct inode *ip)
{
	int	 i, nb, nind; /* index, no. of blocks, no. of ind. blocks */
	o_mode_t mode = ip->i_smode & IFMT;

	/*
	 * do no checks without this set
	 */
	if (!(ufs_debug & UDBG_IVALID)) {
		return (1);
	}

	/*
	 * appropriate locks held
	 */
	ASSERT(RW_WRITE_HELD(&ip->i_contents) ||
		RW_WRITE_HELD(&ip->i_rwlock) ||
		RW_READ_HELD(&ip->i_contents) ||
		RW_READ_HELD(&ip->i_rwlock));

	if (mode == 0) {
		return (1);	/* it's valid to be unallocated */
	}

	if (ip->i_ufsvfs == NULL || ip->i_fs == NULL) {
		cmn_err(CE_UFSDEBUG,
			"ufs_ivalid: called with NULL ufsvfs or fs");
		ufs_markbadi(ip);
		return (0);	/* it's not valid to be on an unmounted fs */
	}

	if (ip->i_number < UFSROOTINO) {
		if (!(ip->i_flag & IBAD)) {
			cmn_err(CE_UFSDEBUG,
				"ufs_ivalid: bad inumber %d, fs %s",
				(int)ip->i_number, ip->i_fs->fs_fsmnt);
			ufs_markbadi(ip);
		}
		return (0);
	}
	if (ip->i_size < 0) {
		if (!(ip->i_flag & IBAD)) {
			cmn_err(CE_UFSDEBUG,
				"ufs_ivalid: bad i_size %d, inum %d, fs %s",
				(int)ip->i_size, (int)ip->i_number,
				ip->i_fs->fs_fsmnt);
			ufs_markbadi(ip);
		}
		return (0);
	}
	if (ip->i_size_o != 0) {
		if (!(ip->i_flag & IBAD)) {
			cmn_err(CE_UFSDEBUG,
				"ufs_ivalid: bad i_size_o %d, inum %d, fs %s",
				(int)ip->i_size_o, (int)ip->i_number,
				ip->i_fs->fs_fsmnt);
			ufs_markbadi(ip);
		}
		return (0);
	}

	if (ip->i_nlink < 0) {
		if (!(ip->i_flag & IBAD)) {
			cmn_err(CE_UFSDEBUG,
				"ufs_ivalid: bad i_nlink %d, inum %d, fs %s",
				(int)ip->i_nlink, (int)ip->i_number,
				ip->i_fs->fs_fsmnt);
			ufs_markbadi(ip);
		}
		return (0);
	}
	if (ip->i_blocks < 0) {
		if (!(ip->i_flag & IBAD)) {
			cmn_err(CE_UFSDEBUG,
				"ufs_ivalid: bad i_blocks %d, inum %d, fs %s",
				(int)ip->i_blocks, (int)ip->i_number,
				ip->i_fs->fs_fsmnt);
			ufs_markbadi(ip);
		}
		return (0);
	}

	if ((mode != IFIFO) && (mode != IFCHR) && (mode != IFBLK) &&
	    (mode != IFDIR) && (mode != IFREG) && (mode != IFLNK) &&
	    (mode != IFSOCK)) {
		if (!(ip->i_flag & IBAD)) {
			cmn_err(CE_UFSDEBUG,
				"ufs_ivalid: bad i_smode %d, inum %d, fs %s",
				ip->i_smode,
				(int)ip->i_number,
				ip->i_fs->fs_fsmnt);
			ufs_markbadi(ip);
		}
		return (0);
	}
	if (mode == IFREG || mode == IFDIR ||
	    (mode == IFLNK && !(ip->i_flag & IFASTSYMLNK))) {
		/* check direct blocks */
		nb = howmany(ip->i_size, ip->i_fs->fs_bsize);
		for (i = 0; i < NDADDR; i++) {
			if (ip->i_db[i] != 0 && (i > nb ||
			    ufs_badblock(ip, ip->i_db[i]))) {
				if (i > nb && !(ip->i_flag & IBAD)) {
					cmn_err(CE_UFSDEBUG,
			"ufs_ivalid: bad %d block %d, nb %d, inum %d, fs %s",
						    i, (int)ip->i_db[i], nb,
						    (int)ip->i_number,
						    ip->i_fs->fs_fsmnt);
				}
				ufs_markbadi(ip);
				return (0);
			}
		}

		/* check indirect blocks */
		for (nind = 0, nb -= NDADDR; nb > 0; nind++)
			nb /= NINDIR(ip->i_fs);

		for (i = 0; i < NIADDR; i++) {
			if (ip->i_ib[i] != 0 && (i > nind ||
			    ufs_badblock(ip, ip->i_ib[i]))) {
				if (i > nind && !(ip->i_flag & IBAD)) {
					cmn_err(CE_UFSDEBUG,
		"ufs_ivalid: bad %d ind block %d, nind %d, inum %d, fs %s",
						    i, (int)ip->i_ib[i], nind,
						    (int)ip->i_number,
						    ip->i_fs->fs_fsmnt);
				}
				ufs_markbadi(ip);
				return (0);
			}
		}
	}

	if (!(ufs_cksum("ufs_ivalid", CK_INO, ip, ip->i_fs))) {
		ufs_markbadi(ip);
		return (0);
	}

	return (1);	/* valid */
}

/*
 * flag an inode as bad
 */
static void
ufs_markbadi(struct inode *ip)
{
	mutex_enter(&ip->i_tlock);
	ip->i_flag |= IBAD;
	mutex_exit(&ip->i_tlock);
}

/*
 * generic checksummmer (sic)
 */
int
ufs_cksum(char *caller, int type, void *obj, struct fs *fs)
{
	struct inode *ip;
	unsigned long cksum;

	switch (type) {
		case CK_INO:
			if (!(ufs_debug & UDBG_ICKSUM)) {
				return (1);
			}
			ip = (struct inode *)obj;
			cksum = ufs_icksum(ip);
			if (ip->i_cksum != 0 && ip->i_cksum != cksum) {
				cmn_err(CE_UFSDEBUG,
				    "%s: inum %d fs %s bad cksum %d != %d",
					    caller? caller: "ufs_cksum",
					    (int)ip->i_number,
					    fs->fs_fsmnt,
					    (int)ip->i_cksum,
					    (int)cksum);
				return (0);
			}
			ip->i_cksum = 0;
			break;
	}
	return (1);	/* if we got here, we must be ok */
}

/*
 * calculate inode checksum
 */
unsigned long
ufs_icksum(struct inode *ip)
{
	unsigned long cksum, hold_cksum;
	unsigned char *cp = (unsigned char *) &(ip->i_ic);
	int i;

	if (!(ufs_debug & UDBG_ICKSUM)) {
		return (ip->i_cksum);
	}

	hold_cksum = ip->i_cksum;
	ip->i_cksum = 0;
	cksum = 0;

	for (i = 0; i < sizeof (struct icommon); i++)
		cksum += *cp++;

	ip->i_cksum = hold_cksum;
	return (cksum);
}

void
ufs_cgcheck(char *msg, struct ufsvfs *ufsvfsp, struct cg *cgp)
{
	int i;
	int j;
	int k;
	int frags;
	int inc, finc;
	long cg;
	int usedfragcnt, freefragcnt;
	int csumfragcnt;
	u_char *imp;
	u_char *fmp;
	long *btotp;
	short *boffp;
	long *nbtotp;
	short *nboffp;
	char *cgbuf = (char *)cgp;
	struct fs *sbp = ufsvfsp->vfs_fs;
	char newcgbuf[sizeof (struct cg) + 1024];
	struct cg *newcgp = (struct cg *)newcgbuf;

	/*
	 * cg checks are disabled
	 */
	if (!(ufs_debug & UFS_DEBUG_CGCHECK))
		return;

	/* Compare summaries against those in superblock */
	cg = cgp->cg_cgx;
	if (cgp->cg_cs.cs_ndir != sbp->fs_cs(sbp, cg).cs_ndir)
		cmn_err(CE_UFSDEBUG,
		"%s: cgp 0x%x (fs=%s) "
		"DIR COUNT MISMATCH CYL %ld (%ld/%ld)\n",
		msg, (int)cgp, sbp->fs_fsmnt, cg,
		cgp->cg_cs.cs_ndir, sbp->fs_cs(sbp, cg).cs_ndir);
	if (cgp->cg_cs.cs_nbfree != sbp->fs_cs(sbp, cg).cs_nbfree)
		cmn_err(CE_UFSDEBUG,
		"%s: cgp 0x%x (fs=%s) "
		"FREE BLOCK COUNT MISMATCH CYL %ld (%ld/%ld)\n",
		msg, (int)cgp, sbp->fs_fsmnt, cg,
		cgp->cg_cs.cs_nbfree, sbp->fs_cs(sbp, cg).cs_nbfree);
	if (cgp->cg_cs.cs_nifree != sbp->fs_cs(sbp, cg).cs_nifree)
		cmn_err(CE_UFSDEBUG,
		"%s: cgp 0x%x (fs=%s) "
		"FREE INODE COUNT MISMATCH CYL %ld (%ld/%ld)\n",
		msg, (int)cgp, sbp->fs_fsmnt, cg,
		cgp->cg_cs.cs_nifree, sbp->fs_cs(sbp, cg).cs_nifree);
	if (cgp->cg_cs.cs_nffree != sbp->fs_cs(sbp, cg).cs_nffree)
		cmn_err(CE_UFSDEBUG,
		"%s: cgp 0x%x (fs=%s) "
		"FREE FRAG COUNT MISMATCH CYL %ld (%ld/%ld)\n",
		msg, (int)cgp, sbp->fs_fsmnt, cg,
		cgp->cg_cs.cs_nffree, sbp->fs_cs(sbp, cg).cs_nffree);

	imp = (u_char *)&cgbuf[cgp->cg_iusedoff];
	fmp = (u_char *)&cgbuf[cgp->cg_freeoff];
	btotp = (long *) &cgbuf[cgp->cg_btotoff];
	boffp = (short *) &cgbuf[cgp->cg_boff];

	bzero(newcgbuf, sizeof (newcgbuf));

	/* Add up # bits on/off in inode bitmap */
	inc = finc = 0;
	for (i = 0; i < cgp->cg_niblk; i++)
		if (isset(imp, i)) {
			inc++;
		} else {
			finc++;
		}
	if (finc != cgp->cg_cs.cs_nifree) {
		cmn_err(CE_UFSDEBUG,
		"%s: cg 0x%x: BITMAP FREE INODES != CSUM FREE INODES\n",
			msg, (int)cgp);
	}

	/* Add up # bits on/off in used frag map */
	usedfragcnt = freefragcnt = 0;
	for (i = 0; i < cgp->cg_ndblk; i++)
		if (isset(fmp, i)) {
			freefragcnt++;
		} else {
			usedfragcnt++;
		}
	csumfragcnt = cgp->cg_cs.cs_nbfree * sbp->fs_frag +
					cgp->cg_cs.cs_nffree;
	if (freefragcnt != csumfragcnt) {
		cmn_err(CE_UFSDEBUG,
		"%s: cgp 0x%x: BITMAP FREE FRAGS != CSUM FREE FRAGS\n",
			msg, (int)cgp);
	}
	/* Calc cyl rot block tables */
	newcgp->cg_magic = cgp->cg_magic;
	newcgp->cg_btotoff = cgp->cg_btotoff;
	newcgp->cg_boff = cgp->cg_boff;
	nbtotp = (long *) &newcgbuf[newcgp->cg_btotoff];
	nboffp = (short *) &newcgbuf[newcgp->cg_boff];
	for (i = 0; i < cgp->cg_ndblk; i += sbp->fs_frag) {
		frags = 0;
		for (j = 0; j < sbp->fs_frag; j++) {
			if (isset(fmp, i + j))
				frags++;
		}
		if (frags == sbp->fs_frag) {
			newcgp->cg_cs.cs_nbfree++;
			j = cbtocylno(sbp, i);
			cg_blktot(newcgp)[j]++;
			cg_blks(ufsvfsp, newcgp, j)[cbtorpos(ufsvfsp, i)]++;
		} else if (frags > 0) {
			newcgp->cg_cs.cs_nffree += frags;
		}
	}
	/* Compare new tables against originals */
	for (i = 0; i < sbp->fs_cpg; i++) {
		if (nbtotp[i] != btotp[i]) {
			cmn_err(CE_UFSDEBUG,
			"%s: cgp 0x%x: CYL BLOCK TOT MISMATCH CYL %d\n",
				msg, (int)cgp, i);
		}
		for (j = 0; j < sbp->fs_nrpos; j++) {
			k = i * sbp->fs_nrpos + j;
			if (nboffp[k] != boffp[k]) {
				cmn_err(CE_UFSDEBUG,
				"%s: cgp 0x%x: "
				"CYL ROTBLK MISMATCH CYL %d POS %d\n",
				msg, (int)cgp, i, j);
			}
		}
	}
}

/*
 * check the inode count for all chains in the inode cache
 */
void
ufs_debug_icache_check(void)
{
	int ne;
	int hno;
	struct inode *ip;
	union ihead *ih;

	if (!(ufs_debug & UDBG_ICKCACHE)) {
		return;
	}
	for (hno = 0, ih = ihead; hno < inohsz; hno++, ih++) {
		mutex_enter(&ih_lock[hno]);
		for (ne = 0, ip = ih->ih_chain[0];
			ip != (struct inode *)ih;
				ip = ip->i_forw) {
			ne++;
		}
		if (ne != ih_ne[hno]) {
			cmn_err(CE_UFSDEBUG,
			    "%s: bad ne hash %d, is %d should be %d\n",
				"ufs_debug_icache_check",
				hno,
				ih_ne[hno],
				ne);
			ih_ne[hno] = ne;
		}
		mutex_exit(&ih_lock[hno]);
	}
}

/*
 * check the inode cache counts for a chain
 */
void
ufs_debug_icache_linecheck(int hno)
{
	int ne = 0;
	struct inode *ip;
	union ihead *ih = &ihead[hno];

	/*
	 * do no checks without this set
	 */
	if (!(ufs_debug & UDBG_ICKCACHE)) {
		return;
	}
	/* verify hash number */
	if (hno >= inohsz) {
		cmn_err(CE_UFSDEBUG,
			"ufs_debug_icache_linecheck: called with hash > max\n");
		return;
	}
	ASSERT(MUTEX_HELD(&ih_lock[hno]));
	/* Count how many are on this inode hash chain */
	for (ip = ih->ih_chain[0]; ip != (struct inode *)ih; ip = ip->i_forw)
		ne++;
	if (ne != ih_ne[hno]) {
		cmn_err(CE_UFSDEBUG,
		    "%s: bad ne, hash %d is %d should be %d\n",
			"ufs_debug_icache_linecheck",
			hno,
			ih_ne[hno],
			ne);
		ih_ne[hno] = ne;
		return;
	}
}
#endif /* DEBUG */

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
static void
ufs_icheck(struct ufsvfs *ufsvfsp, int *isbusyp, int *isreclaimp)
{
	union  ihead	*ih;
	struct inode	*ip;
	int		i;
	int		isnottrans	= !TRANS_ISTRANS(ufsvfsp);
	int		isbusy		= *isbusyp;
	int		isreclaim	= *isreclaimp;

	for (i = 0, ih = ihead; i < inohsz; i++, ih++) {
		mutex_enter(&ih_lock[i]);
		for (ip = ih->ih_chain[0];
		    ip != (struct inode *)ih;
		    ip = ip->i_forw) {
			/*
			 * if inode is busy/modified/deleted, filesystem is busy
			 */
			if (ip->i_ufsvfs != ufsvfsp)
				continue;
			if ((ip->i_flag & (IMOD | IUPD | ICHG)) ||
			    (RW_ISWRITER(&ip->i_rwlock)))
				isbusy = 1;
			if ((ip->i_nlink <= 0) && (ip->i_flag & IREF))
				isreclaim = 1;
			if (isbusy && (isreclaim || isnottrans))
				break;
		}
		mutex_exit(&ih_lock[i]);
		if (isbusy && (isreclaim || isnottrans))
			break;
	}
	*isbusyp = isbusy;
	*isreclaimp = isreclaim;
}

/*
 * As part of the ufs 'sync' operation, this routine is called to mark
 * the filesystem as STABLE if there is no modified metadata in memory.
 */
void
ufs_checkclean(struct vfs *vfsp)
{
	struct ufsvfs	*ufsvfsp	= (struct ufsvfs *)vfsp->vfs_data;
	struct fs	*fs		= ufsvfsp->vfs_fs;
	int		isbusy;
	int		isreclaim;
	int		updatesb;

	ASSERT(MUTEX_HELD(&vfsp->vfs_reflock));

	/*
	 * filesystem is stable or cleanflag processing is disabled; do nothing
	 *	no transitions when panic'ing
	 */
	if (fs->fs_ronly ||
	    fs->fs_clean == FSBAD ||
	    fs->fs_clean == FSSUSPEND ||
	    fs->fs_clean == FSSTABLE ||
	    panicstr)
		return;

	/*
	 * if logging and nothing to reclaim; do nothing
	 */
	if ((fs->fs_clean == FSLOG) &&
	    (((fs->fs_reclaim & FS_RECLAIM) == 0) ||
	    (fs->fs_reclaim & FS_RECLAIMING)))
		return;

	/*
	 * FS_CHECKCLEAN is reset if the file system goes dirty
	 * FS_CHECKRECLAIM is reset if a file gets deleted
	 */
	mutex_enter(&ufsvfsp->vfs_lock);
	fs->fs_reclaim |= (FS_CHECKCLEAN | FS_CHECKRECLAIM);
	mutex_exit(&ufsvfsp->vfs_lock);

	updatesb = 0;

	/*
	 * if logging or buffers are busy; do nothing
	 */
	isbusy = isreclaim = 0;
	if ((fs->fs_clean == FSLOG) ||
	    (bcheck(vfsp->vfs_dev, ufsvfsp->vfs_bufp)))
		isbusy = 1;

	/*
	 * isreclaim == TRUE means can't change the state of fs_reclaim
	 */
	isreclaim =
		((fs->fs_clean == FSLOG) &&
		(((fs->fs_reclaim & FS_RECLAIM) == 0) ||
		(fs->fs_reclaim & FS_RECLAIMING)));

	/*
	 * if fs is busy or can't change the state of fs_reclaim; do nothing
	 */
	if (isbusy && isreclaim)
		return;

	/*
	 * look for busy or deleted inodes; (deleted == needs reclaim)
	 */
	ufs_icheck(ufsvfsp, &isbusy, &isreclaim);

	mutex_enter(&ufsvfsp->vfs_lock);

	/*
	 * IF POSSIBLE, RESET RECLAIM
	 */
	/*
	 * the reclaim thread is not running
	 */
	if ((fs->fs_reclaim & FS_RECLAIMING) == 0)
		/*
		 * no files were deleted during the scan
		 */
		if (fs->fs_reclaim & FS_CHECKRECLAIM)
			/*
			 * no deleted files were found in the inode cache
			 */
			if ((isreclaim == 0) && (fs->fs_reclaim & FS_RECLAIM)) {
				fs->fs_reclaim &= ~FS_RECLAIM;
				updatesb = 1;
			}
	/*
	 * IF POSSIBLE, SET STABLE
	 */
	/*
	 * not logging
	 */
	if (fs->fs_clean != FSLOG)
		/*
		 * file system has not gone dirty since the scan began
		 */
		if (fs->fs_reclaim & FS_CHECKCLEAN)
			/*
			 * nothing dirty was found in the buffer or inode cache
			 */
			if ((isbusy == 0) && (isreclaim == 0) &&
			    (fs->fs_clean != FSSTABLE)) {
				fs->fs_clean = FSSTABLE;
				updatesb = 1;
			}

	mutex_exit(&ufsvfsp->vfs_lock);
	if (updatesb) {
		TRANS_SBWRITE(ufsvfsp, TOP_SBWRITE_STABLE);
	}
}

/*
 * called whenever an unlink occurs
 */
void
ufs_setreclaim(struct inode *ip)
{
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;
	struct fs	*fs		= ufsvfsp->vfs_fs;

	if (ip->i_nlink || fs->fs_ronly || (fs->fs_clean != FSLOG))
		return;

	/*
	 * reclaim-needed bit is already set or we need to tell
	 * ufs_checkclean that a file has been deleted
	 */
	if ((fs->fs_reclaim & (FS_RECLAIM | FS_CHECKRECLAIM)) == FS_RECLAIM)
		return;

	mutex_enter(&ufsvfsp->vfs_lock);
	/*
	 * inform ufs_checkclean that the file system has gone dirty
	 */
	fs->fs_reclaim &= ~FS_CHECKRECLAIM;

	/*
	 * set the reclaim-needed bit
	 */
	if ((fs->fs_reclaim & FS_RECLAIM) == 0) {
		fs->fs_reclaim |= FS_RECLAIM;
		ufs_sbwrite(ufsvfsp);
	}
	mutex_exit(&ufsvfsp->vfs_lock);
}

/*
 * Before any modified metadata written back to the disk, this routine
 * is called to mark the filesystem as ACTIVE.
 */
void
ufs_notclean(struct ufsvfs *ufsvfsp)
{
	struct fs *fs = ufsvfsp->vfs_fs;

	ASSERT(MUTEX_HELD(&ufsvfsp->vfs_lock));
	ULOCKFS_SET_MOD((&ufsvfsp->vfs_ulockfs));

	/*
	 * inform ufs_checkclean that the file system has gone dirty
	 */
	fs->fs_reclaim &= ~FS_CHECKCLEAN;

	/*
	 * ignore if active or bad or suspended or readonly or logging
	 */
	if ((fs->fs_clean == FSACTIVE) || (fs->fs_clean == FSLOG) ||
	    (fs->fs_clean == FSBAD) || (fs->fs_clean == FSSUSPEND) ||
	    (fs->fs_ronly)) {
		mutex_exit(&ufsvfsp->vfs_lock);
		return;
	}
	fs->fs_clean = FSACTIVE;
	/*
	 * write superblock synchronously
	 */
	ufs_sbwrite(ufsvfsp);
	mutex_exit(&ufsvfsp->vfs_lock);
}

/*
 * ufs specific fbwrite()
 */
int
ufs_fbwrite(struct fbuf *fbp, struct inode *ip)
{
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;

	if (TRANS_ISTRANS(ufsvfsp))
		return (fbwrite(fbp));
	mutex_enter(&ufsvfsp->vfs_lock);
	ufs_notclean(ufsvfsp);
	return ((ufsvfsp->vfs_dio) ? fbdwrite(fbp) : fbwrite(fbp));
}

/*
 * ufs specific fbiwrite()
 */
int
ufs_fbiwrite(struct fbuf *fbp, struct inode *ip, daddr_t bn, long bsize)
{
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;
	o_mode_t	ifmt		= ip->i_mode & IFMT;

	mutex_enter(&ufsvfsp->vfs_lock);
	ufs_notclean(ufsvfsp);
	if (ifmt == IFDIR || ifmt == IFSHAD ||
	    (ip->i_ufsvfs->vfs_qinod == ip)) {
		TRANS_DELTA(ufsvfsp, ldbtob(bn * (offset_t)(btod(bsize))),
			fbp->fb_count, DT_FBI, 0, 0);
	}
	return (fbiwrite(fbp, ip->i_devvp, bn, (int)bsize));
}

/*
 * Write the ufs superblock only.
 */
void
ufs_sbwrite(struct ufsvfs *ufsvfsp)
{
	char sav_fs_fmod;
	struct fs *fs = ufsvfsp->vfs_fs;
	struct buf *bp = ufsvfsp->vfs_bufp;

	ASSERT(MUTEX_HELD(&ufsvfsp->vfs_lock));

	/*
	 * for ulockfs processing, limit the superblock writes
	 */
	if ((ufsvfsp->vfs_ulockfs.ul_sbowner) &&
	    (curthread != ufsvfsp->vfs_ulockfs.ul_sbowner)) {
		/* try again later */
		fs->fs_fmod = 1;
		return;
	}

	ULOCKFS_SET_MOD((&ufsvfsp->vfs_ulockfs));
	/*
	 * update superblock timestamp and fs_clean checksum
	 * if marked FSBAD, we always want an erroneous
	 * checksum to force repair
	 */
	fs->fs_time = hrestime.tv_sec;
	fs->fs_state = fs->fs_clean != FSBAD? FSOKAY - (long)fs->fs_time:
						-(FSOKAY - (long)fs->fs_time);
	switch (fs->fs_clean) {
	case FSCLEAN:
	case FSSTABLE:
		fs->fs_reclaim &= ~FS_RECLAIM;
		break;
	case FSACTIVE:
	case FSSUSPEND:
	case FSBAD:
	case FSLOG:
		break;
	default:
		fs->fs_clean = FSACTIVE;
		break;
	}
	/*
	 * reset incore only bits
	 */
	fs->fs_reclaim &= ~(FS_CHECKCLEAN | FS_CHECKRECLAIM);

	/*
	 * delta the whole superblock
	 */
	TRANS_DELTA(ufsvfsp, ldbtob(SBLOCK), sizeof (struct fs),
		DT_SB, NULL, 0);
	/*
	 * retain the incore state of fs_fmod; set the ondisk state to 0
	 */
	sav_fs_fmod = fs->fs_fmod;
	fs->fs_fmod = 0;

	/*
	 * Don't release the buffer after written to the disk
	 */
	bwrite2(bp);
	fs->fs_fmod = sav_fs_fmod;	/* reset fs_fmod's incore state */
}

/*
 * Returns 1 and hold the lock if the vfs is still being mounted.
 * Otherwise, returns 0.
 */
static vfs_t *
still_mounted(struct check_node *checkp)
{
	struct vfs	*vfsp;

	mutex_enter(&vfslist);
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next) {
		if (vfsp == checkp->vfsp &&
		    (struct ufsvfs *)vfsp->vfs_data == checkp->ufsvfs &&
		    vfsp->vfs_dev == checkp->vfs_dev && vfs_lock(vfsp) == 0) {
			mutex_exit(&vfslist);
			return (vfsp);
		}
	}
	mutex_exit(&vfslist);
	return (NULL);
}

/*
 * ufs_getsummaryinfo
 */
int
ufs_getsummaryinfo(dev_t dev, struct ufsvfs *ufsvfsp, struct fs *fs)
{
	int		i;		/* `for' loop counter */
	int		size;		/* bytes of summary info to read */
	daddr_t		frags;		/* frags of summary info to read */
	caddr_t		sip, osip;	/* summary info */
	struct buf	*tp;		/* tmp buf */

	/*
	 * maintain metadata map for trans device (debug only)
	 */
	TRANS_MATA_SI(ufsvfsp, fs);

	/*
	 * Compute #frags and allocate space for summary info
	 */
	frags = howmany(fs->fs_cssize, fs->fs_fsize);
	osip = sip = kmem_alloc((u_int)fs->fs_cssize, KM_SLEEP);
	/*
	 * Read summary info a fs block at a time and fill in array of pointers
	 */
	for (i = 0; i < frags; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > frags)
			size = (frags - i) * fs->fs_fsize;
		tp = bread(dev, (daddr_t) fsbtodb(fs, fs->fs_csaddr+i),
			size);
		tp->b_flags |= B_STALE | B_AGE;
		if (tp->b_flags & B_ERROR) {
			kmem_free(osip, (u_int)fs->fs_cssize);
			brelse(tp);
			return (EIO);
		}
		bcopy((caddr_t)tp->b_un.b_addr, sip, (u_int)size);
		fs->fs_csp[fragstoblks(fs, i)] = (struct csum *)sip;
		sip += size;
		brelse(tp);
	}
	bzero((caddr_t)&fs->fs_cstotal, sizeof (fs->fs_cstotal));
	for (i = 0; i < fs->fs_ncg; ++i) {
		fs->fs_cstotal.cs_ndir += fs->fs_cs(fs, i).cs_ndir;
		fs->fs_cstotal.cs_nbfree += fs->fs_cs(fs, i).cs_nbfree;
		fs->fs_cstotal.cs_nifree += fs->fs_cs(fs, i).cs_nifree;
		fs->fs_cstotal.cs_nffree += fs->fs_cs(fs, i).cs_nffree;
	}
	return (0);
}
#endif	_KERNEL

extern	int around[9];
extern	int inside[9];
extern	u_char *fragtbl[];

/*
 * Update the frsum fields to reflect addition or deletion
 * of some frags.
 */
void
fragacct(struct fs *fs, int fragmap, long *fraglist, int cnt)
{
	int inblk;
	register int field, subfield;
	register int siz, pos;

	/*
	 * ufsvfsp->vfs_lock is held when calling this.
	 */
	inblk = (int)(fragtbl[fs->fs_frag][fragmap]) << 1;
	fragmap <<= 1;
	for (siz = 1; siz < fs->fs_frag; siz++) {
		if ((inblk & (1 << (siz + (fs->fs_frag % NBBY)))) == 0)
			continue;
		field = around[siz];
		subfield = inside[siz];
		for (pos = siz; pos <= fs->fs_frag; pos++) {
			if ((fragmap & field) == subfield) {
				fraglist[siz] += cnt;
				ASSERT(fraglist[siz] >= 0);
				pos += siz;
				field <<= siz;
				subfield <<= siz;
			}
			field <<= 1;
			subfield <<= 1;
		}
	}
}

/*
 * Block operations
 */

/*
 * Check if a block is available
 */
int
isblock(struct fs *fs, u_char *cp, daddr_t h)
{
	u_char mask;

	ASSERT(fs->fs_frag == 8 || fs->fs_frag == 4 || fs->fs_frag == 2 || \
		    fs->fs_frag == 1);
	/*
	 * ufsvfsp->vfs_lock is held when calling this.
	 */
	switch ((int)fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
#ifndef _KERNEL
		cmn_err(CE_PANIC, "isblock: illegal fs->fs_frag value (%d)",
			    fs->fs_frag);
#endif /* _KERNEL */
		return (0);
	}
}

/*
 * Take a block out of the map
 */
void
clrblock(struct fs *fs, u_char *cp, daddr_t h)
{
	ASSERT(fs->fs_frag == 8 || fs->fs_frag == 4 || fs->fs_frag == 2 || \
		fs->fs_frag == 1);
	/*
	 * ufsvfsp->vfs_lock is held when calling this.
	 */
	switch ((int)fs->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
#ifndef _KERNEL
		cmn_err(CE_PANIC, "clrblock: illegal fs->fs_frag value (%d)",
			    fs->fs_frag);
#endif /* _KERNEL */
		return;
	}
}

/*
 * Is block allocated?
 */
int
isclrblock(struct fs *fs, u_char *cp, daddr_t h)
{
	u_char	mask;
	int	frag;
	/*
	 * ufsvfsp->vfs_lock is held when calling this.
	 */
	frag = fs->fs_frag;
	ASSERT(frag == 8 || frag == 4 || frag == 2 || frag == 1);
	switch (frag) {
	case 8:
		return (cp[h] == 0);
	case 4:
		mask = ~(0x0f << ((h & 0x1) << 2));
		return (cp[h >> 1] == (cp[h >> 1] & mask));
	case 2:
		mask =	~(0x03 << ((h & 0x3) << 1));
		return (cp[h >> 2] == (cp[h >> 2] & mask));
	case 1:
		mask = ~(0x01 << (h & 0x7));
		return (cp[h >> 3] == (cp[h >> 3] & mask));
	default:
#ifndef _KERNEL
		cmn_err(CE_PANIC, "isclrblock: illegal fs->fs_frag value (%d)",
			    fs->fs_frag);
#endif /* _KERNEL */
		break;
	}
	return (0);
}

/*
 * Put a block into the map
 */
void
setblock(struct fs *fs, u_char *cp, daddr_t h)
{
	ASSERT(fs->fs_frag == 8 || fs->fs_frag == 4 || fs->fs_frag == 2 || \
		    fs->fs_frag == 1);
	/*
	 * ufsvfsp->vfs_lock is held when calling this.
	 */
	switch ((int)fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
#ifndef _KERNEL
		cmn_err(CE_PANIC, "setblock: illegal fs->fs_frag value (%d)",
			    fs->fs_frag);
#endif /* _KERNEL */
		return;
	}
}

#if !(defined(vax) || defined(sun)) || defined(VAX630)
/*
 * C definitions of special vax instructions.
 */
int
scanc(u_int size, u_char *cp, u_char *table, u_char mask)
{
	u_char *end = &cp[size];

	while (cp < end && (table[*cp] & mask) == 0)
		cp++;
	return (end - cp);
}
#endif !(defined(vax) || defined(sun)) || defined(VAX630)

#if !defined(vax)

int
skpc(char c, u_int len, char *cp)
{
	if (len == 0)
		return (0);
	while (*cp++ == c && --len)
		;
	return (len);
}

#endif !defined(vax)
