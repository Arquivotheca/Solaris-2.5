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
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#ident	"@(#)quotacalls.c	1.46	95/08/10 SMI"

/*
 * Quota system calls.
 */
#ifdef QUOTA
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_quota.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/pathname.h>

static int opendq();
static int setquota();
static int getquota();
static int quotasync();

extern void	qtinit2();

/*
 * Sys call to allow users to find out
 * their current position wrt quota's
 * and to allow super users to alter it.
 */
static int quotas_initialized = 0;

/*ARGSUSED*/
int
quotactl(vp, arg, cr)
	struct vnode *vp;
	int arg;
	struct cred *cr;
{
	struct quotctl quot;
	struct ufsvfs *ufsvfsp;
	register int error = 0;

	if (copyin((caddr_t)arg, (caddr_t)&quot, sizeof (struct quotctl)))
		return (EFAULT);
	if (quot.uid < 0)
		quot.uid = cr->cr_ruid;
	if (quot.op == Q_SYNC && vp == NULL) {
		ufsvfsp = NULL;
	} else if (quot.op != Q_ALLSYNC) {
		ufsvfsp = (struct ufsvfs *)(vp->v_vfsp->vfs_data);
	}
	switch (quot.op) {

	case Q_QUOTAON:
		rw_enter(&dq_rwlock, RW_WRITER);
		if (quotas_initialized == 0) {
			qtinit2();
			quotas_initialized = 1;
		}
		rw_exit(&dq_rwlock);
		error = opendq(ufsvfsp, quot.addr, cr);
		break;

	case Q_QUOTAOFF:
		error = closedq(ufsvfsp, cr);
		break;

	case Q_SETQUOTA:
	case Q_SETQLIM:
		error = setquota(quot.op, (uid_t)quot.uid, ufsvfsp,
		    quot.addr, cr);
		break;

	case Q_GETQUOTA:
		error = getquota((uid_t)quot.uid, ufsvfsp, quot.addr, cr);
		break;

	case Q_SYNC:
		error = qsync(ufsvfsp);
		break;

	case Q_ALLSYNC:
		(void) qsync(NULL);
		break;

	default:
		error = EINVAL;
		break;
	}
	return (error);
}

/*
 * Set the quota file up for a particular file system.
 * Called as the result of a setquota system call.
 */
static int
opendq(ufsvfsp, addr, cr)
	register struct ufsvfs *ufsvfsp;
	caddr_t addr;			/* quota file */
	struct cred *cr;
{
	struct vnode *vp;
	struct inode *qip;
	struct dquot *dqp;
	int error;
	int quotaon = 0;

	if (!suser(cr))
		return (EPERM);
	error =
	    lookupname(addr, UIO_USERSPACE, FOLLOW, (struct vnode **)NULL,
	    &vp);
	if (error)
		return (error);
	if ((struct ufsvfs *)(vp->v_vfsp->vfs_data) != ufsvfsp ||
	    vp->v_type != VREG) {
		VN_RELE(vp);
		return (EACCES);
	}
	rw_enter(&dq_rwlock, RW_WRITER);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) != 0) {
		/*
		 * If the "quotas" file was replaced (different inode)
		 * while quotas were enabled we don't want to re-enable
		 * them with a new "quotas" file. Simply print a warning
		 * message to the console, release the new vnode, and
		 * return.
		 * XXX - The right way to fix this is to return EBUSY
		 * for the ioctl() issued by 'quotaon'.
		 */
		if (VTOI(vp) != ufsvfsp->vfs_qinod) {
			cmn_err(CE_WARN,
"Previous quota file still in use. Disable quotas on %s before enabling.\n",
				VTOI(vp)->i_fs->fs_fsmnt);
			VN_RELE(vp);
			rw_exit(&dq_rwlock);
			return (0);
		}
		(void) quotasync(ufsvfsp);
		/* remove extra hold on quota file */
		VN_RELE(vp);
		quotaon++;
	}

	ufsvfsp->vfs_qinod = VTOI(vp);
	qip = ufsvfsp->vfs_qinod;
	if (TRANS_ISTRANS(ufsvfsp) && !quotaon) {
		int qlen;
		/*
		 * force the file to have no partially allocated blocks
		 * to prevent a realloc from changing the location of
		 * the metadata
		 */
		qlen = qip->i_fs->fs_bsize * NDADDR;
		if (qip->i_size < qlen) {
			rw_enter(&qip->i_contents, RW_WRITER);
			if (ufs_itrunc(qip, qlen, (int) 0, cr) != 0)
				cmn_err(CE_WARN, "opendq failed to remove frags"
				    "from quota file\n");
			rw_exit(&qip->i_contents);
			VOP_PUTPAGE(vp, 0, qip->i_size, B_INVAL, kcred);
		}
		TRANS_MATA_IGET(ufsvfsp, qip);
	}
	/*
	 * The file system time limits are in the super user dquot.
	 * The time limits set the relative time the other users
	 * can be over quota for this file system.
	 * If it is zero a default is used (see quota.h).
	 */
	error = getdiskquota((uid_t)0, ufsvfsp, 1, &dqp);
	if (error == 0) {
		mutex_enter(&dqp->dq_lock);
		ufsvfsp->vfs_btimelimit =
		    (dqp->dq_btimelimit? dqp->dq_btimelimit: DQ_BTIMELIMIT);
		ufsvfsp->vfs_ftimelimit =
		    (dqp->dq_ftimelimit? dqp->dq_ftimelimit: DQ_FTIMELIMIT);

		dqput(dqp);
		mutex_exit(&dqp->dq_lock);
		ufsvfsp->vfs_qflags = MQ_ENABLED;	/* enable quotas */
	} else {
		/*
		 * Some sort of I/O error on the quota file.
		 */
		if (!quotaon) {
			ufsvfsp->vfs_qflags = 0;
			ufsvfsp->vfs_qinod = NULL;
			VN_RELE(ITOV(qip));
		}
	}
	rw_exit(&dq_rwlock);
	return (error);
}
static int
closedq_scan_inode(struct inode *ip, void *arg)
{
	struct dquot *dqp;

	/*
	 * wrong file system; keep looking
	 */
	if (ip->i_ufsvfs != (struct ufsvfs *)arg)
		return (0);

	rw_enter(&ip->i_contents, RW_WRITER);
	if ((dqp = ip->i_dquot) != NULL) {
		ip->i_dquot = NULL;
		rw_exit(&ip->i_contents);
		/*
		 * Since I cleared i_dquot and the MQ_ENABLED
		 * was cleared earlier, no thread can change
		 * this dq's identity so there is not a race
		 * between exit of i_contents and the enter
		 * here.
		 */
		mutex_enter(&dqp->dq_lock);
		dqput(dqp);
		mutex_exit(&dqp->dq_lock);
	} else
		rw_exit(&ip->i_contents);

	return (0);
}

/*
 * Close off disk quotas for a file system.
 */
int
closedq(ufsvfsp, cr)
	register struct ufsvfs *ufsvfsp;
	register struct cred *cr;
{
	register struct dquot *dqp;
	register struct inode *qip;

	if (!suser(cr))
		return (EPERM);
	rw_enter(&dq_rwlock, RW_WRITER);

	/*
	 * If we're retrying, that means we've already cleared vfs_qflags
	 * but we haven't cleared vfs_qinod. In that case, we don't want
	 * to return yet.
	 */
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0) {
		rw_exit(&dq_rwlock);
		return (0);
	}
	qip = ufsvfsp->vfs_qinod;
	if (!qip)
		return (ufs_fault(ufsvfsp->vfs_root, "closedq: NULL qip"));
	ufsvfsp->vfs_qflags = 0;	/* disable quotas */

	/*
	 * We want to downgrade the lock because ufs_scan_inodes() attempts
	 * to grab an i_contents lock (while holding dq_rwlock). While that's
	 * happening, it's possible for ufs_iget() to grab the i_contents
	 * lock, then attempt to get dq_rwlock as a reader causing a deadlock.
	 * (See bug 1161078)
	 */
	rw_downgrade(&dq_rwlock);
	(void) ufs_scan_inodes(0, closedq_scan_inode, ufsvfsp);

	/*
	 * Run down the dquot table and clean and invalidate the
	 * dquots for this file system.
	 */
	for (dqp = dquot; dqp < dquotNDQUOT; dqp++) {
		mutex_enter(&dqp->dq_lock);
		if (dqp->dq_ufsvfsp == ufsvfsp) {
			if (dqp->dq_flags & DQ_MOD)
				dqupdate(dqp);
			/*
			 * dqinval will release the dqp->dq_lock because
			 * it needs to acquire the dq_cachelock.
			 */
			dqinval(dqp);
		} else
			mutex_exit(&dqp->dq_lock);
	}
	/*
	 * Sync and release the quota file inode.
	 *
	 * Even though we aren't holding the dq_rwlock as a writer
	 * we are safe because ufsvfsp->vfs_qinod has already been
	 * cleared so everyone thinks quotas are disabled.
	 */
	ufsvfsp->vfs_qinod = NULL;
	rw_exit(&dq_rwlock);
	(void) TRANS_SYNCIP(qip, 0, I_SYNC, TOP_SYNCIP_CLOSEDQ);
	VN_RELE(ITOV(qip));
	return (0);
}

/*
 * Set various fields of the dqblk according to the command.
 * Q_SETQUOTA - assign an entire dqblk structure.
 * Q_SETQLIM - assign a dqblk structure except for the usage.
 */
static int
setquota(cmd, uid, ufsvfsp, addr, cr)
	int cmd;
	uid_t uid;
	struct ufsvfs *ufsvfsp;
	caddr_t addr;
	struct cred *cr;
{
	register struct dquot *dqp;
	struct inode	*qip;
	struct dquot *xdqp;
	struct dqblk newlim;
	int error;

	if (!suser(cr))
		return (EPERM);
	rw_enter(&dq_rwlock, RW_WRITER);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0) {
		rw_exit(&dq_rwlock);
		return (ESRCH);
	}
	qip = ufsvfsp->vfs_qinod;
	if (copyin(addr, (caddr_t)&newlim, sizeof (struct dqblk)) != 0) {
		rw_exit(&dq_rwlock);
		return (EFAULT);
	}
	error = getdiskquota(uid, ufsvfsp, 0, &xdqp);
	if (error) {
		rw_exit(&dq_rwlock);
		return (error);
	}
	dqp = xdqp;
	/*
	 * Don't change disk usage on Q_SETQLIM
	 */
	mutex_enter(&dqp->dq_lock);
	if (cmd == Q_SETQLIM) {
		newlim.dqb_curblocks = dqp->dq_curblocks;
		newlim.dqb_curfiles = dqp->dq_curfiles;
	}
	if (uid == 0) {
		/*
		 * Timelimits for the super user set the relative time
		 * the other users can be over quota for this file system.
		 * If it is zero a default is used (see quota.h).
		 */
		ufsvfsp->vfs_btimelimit =
		    newlim.dqb_btimelimit? newlim.dqb_btimelimit: DQ_BTIMELIMIT;
		ufsvfsp->vfs_ftimelimit =
		    newlim.dqb_ftimelimit? newlim.dqb_ftimelimit: DQ_FTIMELIMIT;
	} else {
		if (newlim.dqb_bsoftlimit &&
		    newlim.dqb_curblocks >= newlim.dqb_bsoftlimit) {
			if (dqp->dq_bsoftlimit == 0 ||
			    dqp->dq_curblocks < dqp->dq_bsoftlimit) {
				/* If we're suddenly over the limit(s),	*/
				/* start the timer(s)			*/
				newlim.dqb_btimelimit =
					(u_long)hrestime.tv_sec +
					ufsvfsp->vfs_btimelimit;
				dqp->dq_flags &= ~DQ_BLKS;
			} else {
				/* If we're currently over the soft	*/
				/* limit and were previously over the	*/
				/* soft limit then preserve the old	*/
				/* time limit but make sure the DQ_BLKS	*/
				/* flag is set since we must have been	*/
				/* previously warned.			*/
				newlim.dqb_btimelimit = dqp->dq_btimelimit;
				dqp->dq_flags |= DQ_BLKS;
			}
		} else {
			/* Either no quota or under quota, clear time limit */
			newlim.dqb_btimelimit = 0;
			dqp->dq_flags &= ~DQ_BLKS;
		}

		if (newlim.dqb_fsoftlimit &&
		    newlim.dqb_curfiles >= newlim.dqb_fsoftlimit) {
			if (dqp->dq_fsoftlimit == 0 ||
			    dqp->dq_curfiles < dqp->dq_fsoftlimit) {
				/* If we're suddenly over the limit(s),	*/
				/* start the timer(s)			*/
				newlim.dqb_ftimelimit =
					(u_long)hrestime.tv_sec +
					ufsvfsp->vfs_ftimelimit;
				dqp->dq_flags &= ~DQ_FILES;
			} else {
				/* If we're currently over the soft	*/
				/* limit and were previously over the	*/
				/* soft limit then preserve the old	*/
				/* time limit but make sure the		*/
				/* DQ_FILES flag is set since we must	*/
				/* have been previously warned.		*/
				newlim.dqb_ftimelimit = dqp->dq_ftimelimit;
				dqp->dq_flags |= DQ_FILES;
			}
		} else {
			/* Either no quota or under quota, clear time limit */
			newlim.dqb_ftimelimit = 0;
			dqp->dq_flags &= ~DQ_FILES;
		}
	}
	dqp->dq_dqb = newlim;
	dqp->dq_flags |= DQ_MOD;

	/*
	 *  push the new quota to disk now.  If this is a trans device
	 *  then force the page out with ufs_putpage so it will be deltaed
	 *  by ufs_startio.
	 */
	rw_enter(&qip->i_contents, RW_WRITER);
	(void) ufs_rdwri(UIO_WRITE, FWRITE | FSYNC, qip, (caddr_t)&dqp->dq_dqb,
		(int) sizeof (struct dqblk), dqoff(uid), UIO_SYSSPACE,
		(int *) NULL, kcred);
	rw_exit(&qip->i_contents);

	VOP_PUTPAGE(ITOV(qip),
	    dqoff(dqp->dq_uid) & ~qip->i_fs->fs_bmask,
	    qip->i_fs->fs_bsize, B_INVAL, kcred);

	if (TRANS_ISTRANS(ufsvfsp)) {
		daddr_t bn;
		int contig;
		rw_enter(&qip->i_contents, RW_WRITER);
		error = bmap_read(qip, dqoff(dqp->dq_uid), &bn, &contig);
		rw_exit(&qip->i_contents);
		if (error || (bn == UFS_HOLE)) {
			dqp->dq_mof = UFS_HOLE;
		} else
		dqp->dq_mof = ldbtob(bn) +
		    (offset_t) ((dqoff(dqp->dq_uid)) & (DEV_BSIZE - 1));
	}
	dqp->dq_flags &= ~DQ_MOD;
	dqput(dqp);
	rw_exit(&dq_rwlock);
	mutex_exit(&dqp->dq_lock);
	return (0);
}

/*
 * Q_GETQUOTA - return current values in a dqblk structure.
 */
static int
getquota(uid, ufsvfsp, addr, cr)
	uid_t uid;
	struct ufsvfs *ufsvfsp;
	caddr_t addr;
	struct cred *cr;
{
	register struct dquot *dqp;
	struct dquot *xdqp;
	int error;

	if (uid != cr->cr_ruid && !suser(cr))
		return (EPERM);
	rw_enter(&dq_rwlock, RW_WRITER);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0) {
		rw_exit(&dq_rwlock);
		return (ESRCH);
	}
	error = getdiskquota(uid, ufsvfsp, 0, &xdqp);
	if (error) {
		rw_exit(&dq_rwlock);
		return (error);
	}
	dqp = xdqp;
	mutex_enter(&dqp->dq_lock);
	if (dqp->dq_fhardlimit == 0 && dqp->dq_fsoftlimit == 0 &&
	    dqp->dq_bhardlimit == 0 && dqp->dq_bsoftlimit == 0) {
		error = ESRCH;
	} else {
		error = (copyout((caddr_t)&dqp->dq_dqb, addr,
		    sizeof (struct dqblk)) != 0) ? EFAULT : 0;
	}
	rw_exit(&dq_rwlock);
	dqput(dqp);
	mutex_exit(&dqp->dq_lock);
	return (error);
}

/*
 * Q_SYNC - sync quota files to disk.
 */
int
qsync(ufsvfsp)
	struct ufsvfs *ufsvfsp;
{
	int err;
	rw_enter(&dq_rwlock, RW_WRITER);
	err = quotasync(ufsvfsp);
	rw_exit(&dq_rwlock);
	return (err);
}
int
quotasync(ufsvfsp)
	register struct ufsvfs *ufsvfsp;
{
	register struct dquot *dqp;

	if (!quotas_initialized)
		return (ESRCH);
	if (ufsvfsp && TRANS_ISTRANS(ufsvfsp))
		return (0);
	if (ufsvfsp != NULL && (ufsvfsp->vfs_qflags & MQ_ENABLED) == 0)
		return (ESRCH);

	for (dqp = dquot; dqp < dquotNDQUOT; dqp++) {
		if (mutex_tryenter(&dqp->dq_lock)) {
			if ((dqp->dq_flags & DQ_MOD) &&
			    (ufsvfsp == NULL || dqp->dq_ufsvfsp == ufsvfsp) &&
			    (dqp->dq_ufsvfsp->vfs_qflags & MQ_ENABLED)) {
				if (!TRANS_ISTRANS(dqp->dq_ufsvfsp))
					dqupdate(dqp);
			}
			mutex_exit(&dqp->dq_lock);
		}
	}
	return (0);
}

#ifdef notneeded
static int
fdevtoufsvfsp(fdev, ufsvfspp)
	char *fdev;
	struct ufsvfs **ufsvfspp;
{
	struct vnode *vp;
	dev_t dev;
	int error;

	error =
	    lookupname(fdev, UIO_USERSPACE, FOLLOW, (struct vnode **)NULL,
	    &vp);
	if (error)
		return (error);
	if (vp->v_type != VBLK) {
		VN_RELE(vp);
		return (ENOTBLK);
	}
	dev = vp->v_rdev;
	VN_RELE(vp);
	*mpp = getmp(dev);
	if (*mpp == NULL)
		return (ENODEV);
	else
		return (0);
}
#endif
#endif QUOTA
