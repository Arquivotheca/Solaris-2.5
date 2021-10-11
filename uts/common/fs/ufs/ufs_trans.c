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

#ident		"@(#)ufs_trans.c 1.40     95/03/08 SMI"

#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/t_lock.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/thread.h>
#include <sys/vfs.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/fs/ufs_trans.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_quota.h>
#include <sys/fs/ufs_panic.h>
#include <sys/cmn_err.h>
#include <sys/file.h>
#include <sys/debug.h>

kmutex_t	ufs_trans_lock;

void
ufs_trans_init()
{
	mutex_init(&ufs_trans_lock, "ufs trans lock", MUTEX_DEFAULT, NULL);
}

static struct ufstrans	*ufstrans;

struct ufstrans *
ufs_trans_set(dev_t dev, struct ufstransops *ops, void *data)
{
	struct ufstrans	*ut;

	mutex_enter(&ufs_trans_lock);
	ut = ufstrans;
	while (ut != NULL && ut->ut_dev != dev)
		ut = ut->ut_next;

	if (ut == NULL) {
		ut = (struct ufstrans *)kmem_zalloc(sizeof (*ut), KM_SLEEP);
		ut->ut_dev = dev;
		ut->ut_data = data;
		ut->ut_ops = ops;
		ut->ut_next = ufstrans;
		ufstrans = ut;
	}
	mutex_exit(&ufs_trans_lock);
	return (ut);
}

void
ufs_trans_reset(dev_t dev)
{
	struct ufstrans	*ut;
	struct ufstrans	**utp;

	mutex_enter(&ufs_trans_lock);
	for (ut = 0, utp = &ufstrans; *utp != NULL; utp = &ut->ut_next) {
		ut = *utp;
		if (ut->ut_dev == dev) {
			*utp = ut->ut_next;
			kmem_free(ut, sizeof (*ut));
			break;
		}
	}
	mutex_exit(&ufs_trans_lock);
}

/*
 * mounting a fs; check for metatrans device
 */
struct ufstrans *
ufs_trans_get(dev_t dev, struct vfs *vfsp)
{
	struct ufstrans	*ut;

	mutex_enter(&ufs_trans_lock);
	for (ut = ufstrans; ut; ut = ut->ut_next)
		if (ut->ut_dev == dev) {
			ut->ut_vfsp = vfsp;
			ut->ut_validfs = UT_MOUNTED;
			break;
		}
	mutex_exit(&ufs_trans_lock);
	return (ut);
}

/*
 * umounting a fs; mark metatrans device as unmounted
 */
int
ufs_trans_put(dev_t dev)
{
	int		error	= 0;
	struct ufstrans	*ut;

	mutex_enter(&ufs_trans_lock);
	for (ut = ufstrans; ut; ut = ut->ut_next)
		if (ut->ut_dev == dev) {
			/* hlock in progress; unmount fails */
			if (ut->ut_validfs == UT_HLOCKING)
				error = EAGAIN;
			else
				ut->ut_validfs = UT_UNMOUNTED;
			break;
		}
	mutex_exit(&ufs_trans_lock);
	return (error);
}
/*
 * hlock any file systems w/errored metatrans devices
 */
int
ufs_trans_hlock()
{
	struct ufstrans	*ut;
	struct ufsvfs	*ufsvfsp;
	struct lockfs	lockfs;
	int		error;
	int		retry	= 0;

	/*
	 * find fs's that paniced or have errored logging devices
	 */
	mutex_enter(&ufs_trans_lock);
	for (ut = ufstrans; ut; ut = ut->ut_next) {
		/*
		 * not mounted; continue
		 */
		if (ut->ut_vfsp == NULL || (ut->ut_validfs == UT_UNMOUNTED))
			continue;
		/*
		 * disallow unmounts (hlock occurs below)
		 */
		ufsvfsp = (struct ufsvfs *)ut->ut_vfsp->vfs_data;
		if (TRANS_ISERROR(ufsvfsp))
			ut->ut_validfs = UT_HLOCKING;
	}
	mutex_exit(&ufs_trans_lock);

	/*
	 * hlock the fs's that paniced or have errored logging devices
	 */
again:
	mutex_enter(&ufs_trans_lock);
	for (ut = ufstrans; ut; ut = ut->ut_next)
		if (ut->ut_validfs == UT_HLOCKING)
			break;
	mutex_exit(&ufs_trans_lock);
	if (ut == NULL)
		return (retry);
	/*
	 * hlock the file system
	 */
	ufsvfsp = (struct ufsvfs *)ut->ut_vfsp->vfs_data;
	(void) ufs_fiolfss(ufsvfsp->vfs_root, &lockfs);
	if (!LOCKFS_IS_ELOCK(&lockfs)) {
		lockfs.lf_lock = LOCKFS_HLOCK;
		lockfs.lf_flags = 0;
		lockfs.lf_comlen = 0;
		lockfs.lf_comment = NULL;
		error = ufs_fiolfs(ufsvfsp->vfs_root, &lockfs);
		/*
		 * retry after awhile; another app currently doing lockfs
		 */
		if (error == EBUSY || error == EINVAL)
			retry = 1;
	} else {
		if (ufsfx_get_failure_qlen() > 0) {
			if (mutex_tryenter(&ufs_fix.uq_mutex)) {
				ufs_fix.uq_maxne = ufs_fix.uq_ne;
				cv_broadcast(&ufs_fix.uq_cv);
				mutex_exit(&ufs_fix.uq_mutex);
			}
		}
		retry = 1;
	}

	/*
	 * allow unmounts
	 */
	ut->ut_validfs = UT_MOUNTED;
	goto again;
}

/*
 * wakeup the hlock thread
 */
/*ARGSUSED*/
void
ufs_trans_onerror()
{
	mutex_enter(&ufs_hlock.uq_mutex);
	ufs_hlock.uq_ne = ufs_hlock.uq_maxne;
	cv_broadcast(&ufs_hlock.uq_cv);
	mutex_exit(&ufs_hlock.uq_mutex);
}

void
ufs_trans_sbupdate(struct ufsvfs *ufsvfsp, struct vfs *vfsp, top_t topid)
{
	if (curthread->t_flag & T_DONTBLOCK) {
		sbupdate(vfsp);
		return;
	} else {

		if (panicstr && TRANS_ISTRANS(ufsvfsp))
			return;

		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_ASYNC(ufsvfsp, topid, TOP_SBUPDATE_SIZE);
		sbupdate(vfsp);
		TRANS_END_ASYNC(ufsvfsp, topid, TOP_SBUPDATE_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;
	}
}

ufs_trans_syncip(struct inode *ip, int bflags, int iflag, top_t topid)
{
	int		error;
	struct ufsvfs	*ufsvfsp;

	if (curthread->t_flag & T_DONTBLOCK)
		return (ufs_syncip(ip, bflags, iflag));
	else {
		ufsvfsp = ip->i_ufsvfs;
		if (ufsvfsp == NULL)
			return (0);

		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_ASYNC(ufsvfsp, topid, TOP_SYNCIP_SIZE);
		error = ufs_syncip(ip, bflags, iflag);
		TRANS_END_ASYNC(ufsvfsp, topid, TOP_SYNCIP_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;
	}
	return (error);
}

void
ufs_trans_iupdat(struct inode *ip, int waitfor)
{
	struct ufsvfs	*ufsvfsp;

	if (curthread->t_flag & T_DONTBLOCK) {
		rw_enter(&ip->i_contents, RW_WRITER);
		ufs_iupdat(ip, waitfor);
		rw_exit(&ip->i_contents);
		return;
	} else {
		ufsvfsp = ip->i_ufsvfs;

		if (panicstr && TRANS_ISTRANS(ufsvfsp))
			return;

		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_ASYNC(ufsvfsp, TOP_IUPDAT, TOP_IUPDAT_SIZE);
		rw_enter(&ip->i_contents, RW_WRITER);
		ufs_iupdat(ip, waitfor);
		rw_exit(&ip->i_contents);
		TRANS_END_ASYNC(ufsvfsp, TOP_IUPDAT, TOP_IUPDAT_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;
	}
}

void
ufs_trans_sbwrite(struct ufsvfs *ufsvfsp, top_t topid)
{
	if (curthread->t_flag & T_DONTBLOCK) {
		mutex_enter(&ufsvfsp->vfs_lock);
		ufs_sbwrite(ufsvfsp);
		mutex_exit(&ufsvfsp->vfs_lock);
		return;
	} else {

		if (panicstr && TRANS_ISTRANS(ufsvfsp))
			return;

		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_ASYNC(ufsvfsp, topid, TOP_SBWRITE_SIZE);
		mutex_enter(&ufsvfsp->vfs_lock);
		ufs_sbwrite(ufsvfsp);
		mutex_exit(&ufsvfsp->vfs_lock);
		TRANS_END_ASYNC(ufsvfsp, topid, TOP_SBWRITE_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;
	}
}

/*ARGSUSED*/
int
ufs_trans_push_si(struct ufstrans *ut, delta_t dtyp, int ignore)
{
	struct ufsvfs	*ufsvfsp;
	struct fs	*fs;

	ufsvfsp = (struct ufsvfs *)ut->ut_vfsp->vfs_data;
	fs = ufsvfsp->vfs_fs;
	mutex_enter(&ufsvfsp->vfs_lock);
	TRANS_LOG(ufsvfsp, (char *)fs->fs_csp[0],
		ldbtob(fsbtodb(fs, fs->fs_csaddr)), fs->fs_cssize);
	mutex_exit(&ufsvfsp->vfs_lock);
	return (0);
}

/*ARGSUSED*/
int
ufs_trans_push_buf(struct ufstrans *ut, delta_t dtyp, daddr_t bno)
{
	struct buf	*bp;

	if ((bp = (struct buf *)getblk(ut->ut_dev, bno, 0)) == NULL)
		return (ENOENT);

	if (bp->b_flags & B_DELWRI) {
		bwrite(bp);
		return (0);
	}
	brelse(bp);
	return (ENOENT);
}

/*ARGSUSED*/
ufs_trans_push_inode(struct ufstrans *ut, delta_t dtyp, ino_t ino)
{
	struct inode	*ip;

	if (ufs_iget(ut->ut_vfsp, ino, &ip, kcred))
		return (ENOENT);

	if (ip->i_flag & (IUPD|IACC|ICHG|IMOD|IMODACC|IATTCHG)) {
		rw_enter(&ip->i_contents, RW_WRITER);
		ufs_iupdat(ip, 1);
		rw_exit(&ip->i_contents);
		VN_RELE(ITOV(ip));
		return (0);
	}
	VN_RELE(ITOV(ip));
	return (ENOENT);
}
/*
 * DEBUG ROUTINES
 *	These routines maintain the metadata map (matamap)
 */
/*
 * update the metadata map at mount
 */
static int
ufs_trans_mata_mount_scan(struct inode *ip, void *arg)
{
	/*
	 * wrong file system; keep looking
	 */
	if (ip->i_ufsvfs != (struct ufsvfs *)arg)
		return (0);

	/*
	 * load the metadata map
	 */
	rw_enter(&ip->i_contents, RW_WRITER);
	ufs_trans_mata_iget(ip);
	rw_exit(&ip->i_contents);
	return (0);
}
void
ufs_trans_mata_mount(struct ufsvfs *ufsvfsp)
{
	struct fs	*fs	= ufsvfsp->vfs_fs;
	ino_t		ino;
	int		i;

	/*
	 * put static metadata into matamap
	 *	superblock
	 *	cylinder groups
	 *	inode groups
	 *	existing inodes
	 */
	TRANS_MATAADD(ufsvfsp, ldbtob(SBLOCK), fs->fs_sbsize);

	for (ino = i = 0; i < fs->fs_ncg; ++i, ino += fs->fs_ipg) {
		TRANS_MATAADD(ufsvfsp,
		    ldbtob(fsbtodb(fs, cgtod(fs, i))), fs->fs_cgsize);
		TRANS_MATAADD(ufsvfsp,
		    ldbtob(fsbtodb(fs, itod(fs, ino))),
		    fs->fs_ipg * sizeof (struct dinode));
	}
	(void) ufs_scan_inodes(0, ufs_trans_mata_mount_scan, ufsvfsp);
}
/*
 * clear the metadata map at umount
 */
void
ufs_trans_mata_umount(struct ufsvfs *ufsvfsp)
{
	TRANS_MATACLR(ufsvfsp);
}

/*
 * summary info (may be extended during growfs test)
 */
void
ufs_trans_mata_si(struct ufsvfs *ufsvfsp, struct fs *fs)
{
	TRANS_MATAADD(ufsvfsp, ldbtob(fsbtodb(fs, fs->fs_csaddr)),
			fs->fs_cssize);
}
/*
 * scan an allocation block (either inode or true block)
 */
static void
ufs_trans_mata_direct(
	struct inode *ip,
	daddr_t *fragsp,
	daddr_t *blkp,
	u_long nblk)
{
	int		i;
	daddr_t		frag;
	u_long		nb;
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;
	struct fs	*fs		= ufsvfsp->vfs_fs;

	for (i = 0; i < nblk && *fragsp; ++i, ++blkp)
		if ((frag = *blkp) != 0) {
			if (*fragsp > fs->fs_frag) {
				nb = fs->fs_bsize;
				*fragsp -= fs->fs_frag;
			} else {
				nb = *fragsp * fs->fs_fsize;
				*fragsp = 0;
			}
			TRANS_MATAADD(ufsvfsp, ldbtob(fsbtodb(fs, frag)), nb);
		}
}
/*
 * scan an indirect allocation block (either inode or true block)
 */
static void
ufs_trans_mata_indir(
	struct inode *ip,
	daddr_t *fragsp,
	daddr_t frag,
	int level)
{
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;
	struct fs	*fs		= ufsvfsp->vfs_fs;
	int		ne		= fs->fs_bsize / sizeof (daddr_t);
	int		i;
	struct buf	*bp;
	daddr_t		*blkp;
	o_mode_t	ifmt = ip->i_mode & IFMT;

	bp = bread(ip->i_dev, fsbtodb(fs, frag), fs->fs_bsize);
	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		return;
	}
	blkp = bp->b_un.b_daddr;

	if (level || (ifmt == IFDIR) || (ifmt == IFSHAD) ||
	    (ip == ip->i_ufsvfs->vfs_qinod))
		ufs_trans_mata_direct(ip, fragsp, blkp, ne);

	if (level)
		for (i = 0; i < ne && *fragsp; ++i, ++blkp)
			ufs_trans_mata_indir(ip, fragsp, *blkp, level-1);
	brelse(bp);
}
/*
 * put appropriate metadata into matamap for this inode
 */
void
ufs_trans_mata_iget(struct inode *ip)
{
	int		i;
	daddr_t		frags	= dbtofsb(ip->i_fs, ip->i_blocks);
	o_mode_t	ifmt 	= ip->i_mode & IFMT;

	if (frags && ((ifmt == IFDIR) || (ifmt == IFSHAD) ||
	    (ip == ip->i_ufsvfs->vfs_qinod)))
		ufs_trans_mata_direct(ip, &frags, &ip->i_db[0], NDADDR);

	if (frags)
		ufs_trans_mata_direct(ip, &frags, &ip->i_ib[0], NIADDR);

	for (i = 0; i < NIADDR && frags; ++i)
		if (ip->i_ib[i])
			ufs_trans_mata_indir(ip, &frags, ip->i_ib[i], i);
}
/*
 * freeing possible metadata (block of user data)
 */
void
ufs_trans_mata_free(struct ufsvfs *ufsvfsp, offset_t mof, off_t nb)
{
	TRANS_MATADEL(ufsvfsp, mof, nb);

}
/*
 * allocating metadata
 */
void
ufs_trans_mata_alloc(
	struct ufsvfs *ufsvfsp,
	struct inode *ip,
	daddr_t frag,
	u_long nb,
	int indir)
{
	struct fs	*fs	= ufsvfsp->vfs_fs;
	o_mode_t	ifmt 	= ip->i_mode & IFMT;

	if (indir || ((ifmt == IFDIR) || (ifmt == IFSHAD) ||
	    (ip == ip->i_ufsvfs->vfs_qinod)))
		TRANS_MATAADD(ufsvfsp, ldbtob(fsbtodb(fs, frag)), nb);
}
/*
 * END DEBUG ROUTINES
 */

/*
 * ufs_trans_dir is used to declare a directory delta
 */
int
ufs_trans_dir(struct inode *ip, off_t offset)
{
	daddr_t	bn;
	int	contig, error;
	int	dolock	= (ip->i_owner != curthread);

	ASSERT(ip);
	if (dolock)
		rw_enter(&ip->i_contents, RW_WRITER);
	error = bmap_read(ip, (u_int) offset, &bn, &contig);
	if (dolock)
		rw_exit(&ip->i_contents);
	if (error || (bn == UFS_HOLE)) /* XXX error handling needed */
		cmn_err(CE_WARN, "ufs_trans_dir - could not get block"
		    " number error = %d bn = %d\n", error, (int)bn);
	TRANS_DELTA(ip->i_ufsvfs, ldbtob(bn), DIRBLKSIZ, DT_DIR, 0, 0);
	return (error);
}
/*ARGSUSED*/
int
ufs_trans_push_quota(struct ufstrans *ut, delta_t dtyp, struct dquot *dqp)
{
	struct ufsvfs	*ufsvfsp = dqp->dq_ufsvfsp;

	mutex_enter(&dqp->dq_lock);
	if (dqp->dq_flags & DQ_ERROR) {
		mutex_exit(&dqp->dq_lock);
		return (1);
	}
	if (dqp->dq_flags & (DQ_MOD | DQ_BLKS | DQ_FILES)) {
		ASSERT((dqp->dq_mof != UFS_HOLE) && (dqp->dq_mof != 0));
		TRANS_LOG(ufsvfsp, (caddr_t) &dqp->dq_dqb,
		    (offset_t) dqp->dq_mof, (int) sizeof (struct dqblk));
		dqp->dq_flags &= ~DQ_MOD;
	}
	mutex_exit(&dqp->dq_lock);
	return (0);
}

/*
 * ufs_trans_quota take in a uid, allocates the disk space, placing the
 * quota record into the metamap, then declares the delta.
 */
/*ARGSUSED*/
void
ufs_trans_quota(struct dquot *dqp)
{

	struct inode	*qip = dqp->dq_ufsvfsp->vfs_qinod;

	ASSERT(qip);
	ASSERT(MUTEX_HELD(&dqp->dq_lock));
	ASSERT(dqp->dq_flags & DQ_MOD);

	if ((dqp->dq_flags & DQ_MOD) == 0) {
		cmn_err(CE_WARN, "ufs_trans_quota: dqp %x not modified\n",
		    (int)dqp);
		return;
	}
	ASSERT((dqp->dq_mof != 0) && (dqp->dq_mof != UFS_HOLE));

	TRANS_DELTA(qip->i_ufsvfs, (offset_t) dqp->dq_mof,
	    sizeof (struct dqblk), DT_QR, ufs_trans_push_quota, (int) dqp);
}

void
ufs_trans_dqrele(struct dquot *dqp)
{
	struct ufsvfs	*ufsvfsp;

	if (curthread->t_flag & T_DONTBLOCK) {
		dqrele(dqp);
		return;
	} else {
		ufsvfsp = dqp->dq_ufsvfsp;

		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_ASYNC(ufsvfsp, TOP_QUOTA, TOP_QUOTA_SIZE);
		dqrele(dqp);
		TRANS_END_ASYNC(ufsvfsp, TOP_QUOTA, TOP_QUOTA_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;
	}
}

int
ufs_trans_itrunc(struct inode *ip, u_long length, int flags, cred_t *cr)
{
	struct fs	*fs;
	u_long 		eof, maxitrunc;
	u_long 		blks;
	int 		err;
	int 		tflags;
	int		deltasize;
	int		issync;
	int		do_block	= 0;
	struct ufsvfs	*ufsvfsp	= ip->i_ufsvfs;

	if (!TRANS_ISTRANS(ufsvfsp)) {
		rw_enter(&ip->i_contents, RW_WRITER);
		err = ufs_itrunc(ip, length, flags, cr);
		rw_exit(&ip->i_contents);
		return (err);
	}

	/*
	 * within the lockfs protocol but *not* part of a transaction
	 */
	do_block = curthread->t_flag & T_DONTBLOCK;
	curthread->t_flag |= T_DONTBLOCK;

	/*
	 * trunc'ing up; at most 4 blocks will be allocated
	 */
	if (length >= ip->i_size) {
		TRANS_BEGIN_CSYNC(ufsvfsp, issync, TOP_ITRUNC,
					TOP_ITRUNC_UP_SIZE);
		rw_enter(&ip->i_contents, RW_WRITER);
		err = ufs_itrunc(ip, length, flags, cr);
		if (!do_block)
			curthread->t_flag &= ~T_DONTBLOCK;
		rw_exit(&ip->i_contents);
		TRANS_END_CSYNC(ufsvfsp, err, issync, TOP_ITRUNC,
					TOP_ITRUNC_UP_SIZE);
		return (err);
	}

	/*
	 * trunc'ing down a small file; just do it
	 */
	fs = ufsvfsp->vfs_fs;
	blks = TOP_ITRUNC_DBTOBLKS(fs, ip);
	if (blks <= TOP_ITRUNC_BLOCKS) {
		deltasize = (blks * TOP_ITRUNC_SIZECG) + INODESIZE + QUOTASIZE;
		TRANS_BEGIN_CSYNC(ufsvfsp, issync, TOP_ITRUNC, deltasize);
		rw_enter(&ip->i_contents, RW_WRITER);
		err = ufs_itrunc(ip, length, flags, cr);
		if (!do_block)
			curthread->t_flag &= ~T_DONTBLOCK;
		rw_exit(&ip->i_contents);
		TRANS_END_CSYNC(ufsvfsp, err, issync, TOP_ITRUNC, deltasize);
		return (err);
	}
	/*
	 * trunc a large file in pieces (currently, 8mb pieces)
	 */
	eof = ip->i_size;
	maxitrunc = TOP_ITRUNC_BLOCKS << fs->fs_bshift;
	deltasize = (TOP_ITRUNC_BLOCKS * TOP_ITRUNC_SIZECG) + INODESIZE +
			QUOTASIZE;
	do {
		/*
		 * partially trunc file down to eof
		 * 	only retain I_FREE at last partial trunc
		 */
		eof = (eof - length) > maxitrunc ? eof - maxitrunc : length;
		tflags = (eof == length) ? flags : flags & ~I_FREE;

		TRANS_BEGIN_CSYNC(ufsvfsp, issync, TOP_ITRUNC, deltasize);
		rw_enter(&ip->i_contents, RW_WRITER);
		err = ufs_itrunc(ip, eof, tflags, cr);
		rw_exit(&ip->i_contents);
		TRANS_END_CSYNC(ufsvfsp, err, issync, TOP_ITRUNC, deltasize);

	} while ((err == 0) && (eof > length));

	if (!do_block)
		curthread->t_flag &= ~T_DONTBLOCK;

	return (err);
}
int
ufs_trans_write(struct inode *ip, struct uio *uiop, int ioflag, cred_t *cr)
{

	struct ufsvfs	*ufsvfsp = ip->i_ufsvfs;
	int		wr_size = 0;
	int		maxwrite = ufsvfsp->vfs_maxwrite;
	int		smallwrite = ufsvfsp->vfs_smallwrite;

	struct iovec 	*iovp, *iov_hold, tiov;
	long		tlen;
	int		i, iolen, err, resid;
	int		trans_log_size;
	int		uio_cnt_hold;

	for (i = 0, iovp = uiop->uio_iov; i < uiop->uio_iovcnt; i++, iovp++)
		wr_size += iovp->iov_len;

	/*
	 * if this is a small write that requires only TOP_WRITE_SMALL_SIZE
	 * of log space then just do the write.
	 */
	if (wr_size < smallwrite)
		return (rwip(ip, uiop, UIO_WRITE, ioflag, cr));

	/*
	 * if the write can be done in a single large log chunck then
	 * do it.  First end the existing transaction and start a larger
	 * transaction.
	 */
	rw_exit(&ip->i_contents);
	if (ioflag & (FSYNC|FDSYNC)) {
		TRANS_END_SYNC(ufsvfsp, err, TOP_WRITE_SYNC,
		    TOP_WRITE_SMALL_SIZE);
	} else {
		TRANS_END_ASYNC(ufsvfsp, TOP_WRITE, TOP_WRITE_SMALL_SIZE);
	}

	trans_log_size = (NIADDR * ALLOCSIZE) +
	    ((wr_size >> ip->i_fs->fs_bshift) * (ALLOCSIZE + SIZECG));
	if (trans_log_size < TOP_WRITE_BIG_SIZE) {
		if (ioflag & (FSYNC|FDSYNC)) {
			TRANS_BEGIN_SYNC(ufsvfsp, TOP_WRITE_SYNC,
			    trans_log_size);
		} else {
			TRANS_BEGIN_ASYNC(ufsvfsp, TOP_WRITE, trans_log_size);
		}
		rw_enter(&ip->i_contents, RW_WRITER);
		err = rwip(ip, uiop, UIO_WRITE, ioflag, cr);
		rw_exit(&ip->i_contents);
		if (ioflag & (FSYNC|FDSYNC)) {
			TRANS_END_SYNC(ufsvfsp, err, TOP_WRITE_SYNC,
			    trans_log_size);
			TRANS_BEGIN_SYNC(ufsvfsp, TOP_WRITE_SYNC,
			    TOP_WRITE_SMALL_SIZE);
		} else {
			TRANS_END_ASYNC(ufsvfsp, TOP_WRITE, trans_log_size);
			TRANS_BEGIN_ASYNC(ufsvfsp, TOP_WRITE,
			    TOP_WRITE_SMALL_SIZE);
		}
		rw_enter(&ip->i_contents, RW_WRITER);
		return (err);
	}
	/*
	 * since the write is too big and would "HOG THE LOG" it needs to
	 * be broken up and done in pieces.
	 */

	iov_hold = uiop->uio_iov;
	uio_cnt_hold = uiop->uio_iovcnt;
	uiop->uio_iovcnt = 1;
	uiop->uio_iov = &tiov;
	resid = uiop->uio_resid;

	for (i = 0, iovp = iov_hold; i < uio_cnt_hold; i++, iovp++) {
		tlen = 0;
		while (tlen < iovp->iov_len) {
			tiov.iov_base = iovp->iov_base + tlen;
			uiop->uio_resid = tiov.iov_len = iolen =
			    ((iovp->iov_len - tlen) < maxwrite) ?
			    iovp->iov_len - tlen : maxwrite;

			if (ioflag & (FSYNC|FDSYNC)) {
				TRANS_BEGIN_SYNC(ufsvfsp, TOP_WRITE_SYNC,
				    TOP_WRITE_BIG_SIZE);
			} else {
				TRANS_BEGIN_ASYNC(ufsvfsp,
				    TOP_WRITE, TOP_WRITE_BIG_SIZE);
			}

			rw_enter(&ip->i_contents, RW_WRITER);
			err = rwip(ip, uiop, UIO_WRITE, ioflag, cr);

			resid -= iolen - uiop->uio_resid;
			tlen += iolen;

			rw_exit(&ip->i_contents);
			if (ioflag & (FSYNC|FDSYNC)) {
				TRANS_END_SYNC(ufsvfsp, err, TOP_WRITE_SYNC,
				    TOP_WRITE_BIG_SIZE);
			} else {
				TRANS_END_ASYNC(ufsvfsp, TOP_WRITE,
				    TOP_WRITE_BIG_SIZE);
			}
			if (err)
				goto out;
		}
	}

out:
	if (ioflag & (FSYNC|FDSYNC)) {
		TRANS_BEGIN_SYNC(ufsvfsp, TOP_WRITE_SYNC, TOP_WRITE_SMALL_SIZE);
	} else {
		TRANS_BEGIN_ASYNC(ufsvfsp, TOP_WRITE, TOP_WRITE_SMALL_SIZE);
	}
	rw_enter(&ip->i_contents, RW_WRITER);
	uiop->uio_iov = iov_hold;
	uiop->uio_resid = resid;
	uiop->uio_iovcnt = uio_cnt_hold;
	return (err);
}
