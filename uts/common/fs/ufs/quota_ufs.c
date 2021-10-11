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

#ident	"@(#)quota_ufs.c	2.32	95/08/10 SMI"

/*
 * Routines used in checking limits on file system usage.
 */

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
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_quota.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/session.h>

/*
 * Find the dquot structure that should
 * be used in checking i/o on inode ip.
 */
struct dquot *
getinoquota(ip)
	register struct inode *ip;
{
	register struct dquot *dqp;
	register struct ufsvfs *ufsvfsp;
	struct dquot *xdqp;

	ufsvfsp = (struct ufsvfs *)(ip->i_vnode.v_vfsp->vfs_data);
	/*
	 * Check for quotas enabled.
	 */
	rw_enter(&dq_rwlock, RW_READER);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0) {
		rw_exit(&dq_rwlock);
		return (NULL);
	}
	/*
	 * Check for someone doing I/O to quota file.
	 */
	if (ip == ufsvfsp->vfs_qinod) {
		rw_exit(&dq_rwlock);
		return (NULL);
	}
	if (getdiskquota((uid_t)ip->i_uid, ufsvfsp, 0, &xdqp)) {
		rw_exit(&dq_rwlock);
		return (NULL);
	}
	dqp = xdqp;
	mutex_enter(&dqp->dq_lock);
	if (dqp->dq_fhardlimit == 0 && dqp->dq_fsoftlimit == 0 &&
	    dqp->dq_bhardlimit == 0 && dqp->dq_bsoftlimit == 0) {
		dqput(dqp);
		mutex_exit(&dqp->dq_lock);
		dqp = NULL;
	} else {
		mutex_exit(&dqp->dq_lock);
	}
	ip->i_dquot = dqp;
	rw_exit(&dq_rwlock);
	return (dqp);
}

/*
 * Update disk usage, and take corrective action.
 */
int
chkdq(ip, change, force, cr)
	struct inode *ip;
	long change;
	int force;
	struct cred *cr;
{
	register struct dquot *dqp;
	register u_long ncurblocks;
	struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	int error = 0;

	if (change == 0)
		return (0);
	dqp = ip->i_dquot;
	if (dqp == NULL)
		return (0);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0)
		return (0);
	rw_enter(&dq_rwlock, RW_READER);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0) {
		rw_exit(&dq_rwlock);
		return (0);
	}
	mutex_enter(&dqp->dq_lock);
	dqp->dq_flags |= DQ_MOD;
	if (change < 0) {
		if ((int)dqp->dq_curblocks + change >= 0)
			dqp->dq_curblocks += change;
		else
			dqp->dq_curblocks = 0;
		if (dqp->dq_curblocks < dqp->dq_bsoftlimit)
			dqp->dq_btimelimit = 0;
		dqp->dq_flags &= ~DQ_BLKS;
		TRANS_QUOTA(dqp);
		mutex_exit(&dqp->dq_lock);
		rw_exit(&dq_rwlock);
		return (0);
	}

	ncurblocks = dqp->dq_curblocks + change;
	/*
	 * Allocation. Check hard and soft limits.
	 * Skip checks for super user.
	 */
	if (cr->cr_uid == 0)
		goto out;
	/*
	 * Dissallow allocation if it would bring the current usage over
	 * the hard limit or if the user is over his soft limit and his time
	 * has run out.
	 */
	if (ncurblocks >= dqp->dq_bhardlimit && dqp->dq_bhardlimit && !force) {
		/* If the user was not informed yet and the caller	*/
		/* is the owner of the file and the controlling tty	*/
		/* (curproc->p_sessp->s_vp) is non-NULL...		*/
		if ((dqp->dq_flags & DQ_BLKS) == 0 &&
		    ip->i_uid == cr->cr_ruid && curproc->p_sessp->s_vp) {
			uprintf(
	"\nquota_ufs: over hard disk limit (pid %d, uid %d, inum %d, fs %s)\n",
				(int)ttoproc(curthread)->p_pid,
				(int)ip->i_uid, (int)ip->i_number,
				ip->i_fs->fs_fsmnt);
			dqp->dq_flags |= DQ_BLKS;
		} else {
			/*
			 * Send it to the messages file. Don't set DQ_BLKS
			 * since we haven't informed a real user yet.
			 */
			cmn_err(CE_NOTE,
	"!quota_ufs: over hard disk limit (pid %d, uid %d, inum %d, fs %s)\n",
				(int)ttoproc(curthread)->p_pid,
				(int)ip->i_uid, (int)ip->i_number,
				ip->i_fs->fs_fsmnt);
		}
		error = EDQUOT;
	}
	if (ncurblocks >= dqp->dq_bsoftlimit && dqp->dq_bsoftlimit) {
		if (dqp->dq_curblocks < dqp->dq_bsoftlimit ||
		    dqp->dq_btimelimit == 0) {
			dqp->dq_btimelimit =
			    hrestime.tv_sec +
			    ((struct ufsvfs *) ip->i_vnode.v_vfsp->vfs_data)
				->vfs_btimelimit;
			/* If the caller owns the file and there is a	*/
			/* (non-NULL) controlling tty...		*/
			if (ip->i_uid == cr->cr_ruid &&
						curproc->p_sessp->s_vp) {
				uprintf(
"\nquota_ufs: Warning: over disk limit (pid %d, uid %d, inum %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)ip->i_uid, (int)ip->i_number,
					ip->i_fs->fs_fsmnt);
			} else {
				/* Send it to the messages file */
				cmn_err(CE_NOTE,
"!quota_ufs: Warning: over disk limit (pid %d, uid %d, inum %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)ip->i_uid, (int)ip->i_number,
					ip->i_fs->fs_fsmnt);
			}
		} else if (hrestime.tv_sec > dqp->dq_btimelimit && !force) {
			/* If the user was not informed yet and the	*/
			/* caller is the owner of the file and the	*/
			/* controlling tty is non-NULL...		*/
			if ((dqp->dq_flags & DQ_BLKS) == 0 &&
			    ip->i_uid == cr->cr_ruid &&
						curproc->p_sessp->s_vp) {
				uprintf(
"\nquota_ufs: over disk and time limit (pid %d, uid %d, inum %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)ip->i_uid, (int)ip->i_number,
					ip->i_fs->fs_fsmnt);
				dqp->dq_flags |= DQ_BLKS;
			} else {
				/*
				 * Send it to the messages file. Don't set
				 * DQ_BLKS since we haven't informed a real
				 * user yet.
				 */
				cmn_err(CE_NOTE,
"!quota_ufs: over disk and time limit (pid %d, uid %d, inum %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)ip->i_uid, (int)ip->i_number,
					ip->i_fs->fs_fsmnt);
			}
			error = EDQUOT;
		}
	}
out:
	TRANS_QUOTA(dqp);
	if (error == 0)
		dqp->dq_curblocks = ncurblocks;
	mutex_exit(&dqp->dq_lock);
	rw_exit(&dq_rwlock);
	return (error);
}

/*
 * Check the inode limit, applying corrective action.
 */
int
chkiq(ufsvfsp, ip, uid, force, cr)
	struct ufsvfs *ufsvfsp;
	struct inode *ip;
	uid_t uid;
	int force;
	struct cred *cr;
{
	register struct dquot *dqp;
	register u_long ncurfiles;
	struct dquot *xdqp;
	int error = 0;

	/*
	 * check for quotas enabled
	 */
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0)
		return (0);
	rw_enter(&dq_rwlock, RW_READER);
	if ((ufsvfsp->vfs_qflags & MQ_ENABLED) == 0) {
		rw_exit(&dq_rwlock);
		return (0);
	}
	/*
	 * Free.
	 */
	if (ip != NULL) {
		dqp = ip->i_dquot;
		rw_exit(&dq_rwlock);
		if (dqp == NULL)
			return (0);
		mutex_enter(&dqp->dq_lock);
		dqp->dq_flags |= DQ_MOD;
		if (dqp->dq_curfiles)
			dqp->dq_curfiles--;
		if (dqp->dq_curfiles < dqp->dq_fsoftlimit)
			dqp->dq_ftimelimit = 0;
		dqp->dq_flags &= ~DQ_FILES;
		TRANS_QUOTA(dqp);
		mutex_exit(&dqp->dq_lock);
		return (0);
	}

	/*
	 * Allocation. Get dquot for for uid, fs.
	 */
	if (getdiskquota(uid, ufsvfsp, 0, &xdqp)) {
		rw_exit(&dq_rwlock);
		return (0);
	}
	dqp = xdqp;
	mutex_enter(&dqp->dq_lock);
	if (dqp->dq_fsoftlimit == 0 && dqp->dq_fhardlimit == 0) {
		rw_exit(&dq_rwlock);
		dqput(dqp);
		mutex_exit(&dqp->dq_lock);
		return (0);
	}
	dqp->dq_flags |= DQ_MOD;
	/*
	 * Skip checks for super user.
	 */
	if (cr->cr_uid == 0)
		goto out;
	ncurfiles = dqp->dq_curfiles + 1;
	/*
	 * Dissallow allocation if it would bring the current usage over
	 * the hard limit or if the user is over his soft limit and his time
	 * has run out.
	 */
	if (ncurfiles >= dqp->dq_fhardlimit && dqp->dq_fhardlimit && !force) {
		/* If the user was not informed yet and the caller	*/
		/* is the owner of the file and the controlling tty	*/
		/* is non-NULL...					*/
		if ((dqp->dq_flags & DQ_FILES) == 0 && uid == cr->cr_ruid &&
						curproc->p_sessp->s_vp) {
			uprintf(
		"\nquota_ufs: over file hard limit (pid %d, uid %d, fs %s)\n",
				(int)ttoproc(curthread)->p_pid,
				(int)uid, ufsvfsp->vfs_fs->fs_fsmnt);
			dqp->dq_flags |= DQ_FILES;
		} else {
			/*
			 * Send it to the messages file. Don't set DQ_FILES
			 * since we haven't informed a real user yet.
			 */
			cmn_err(CE_NOTE,
		"!quota_ufs: over file hard limit (pid %d, uid %d, fs %s)\n",
				(int)ttoproc(curthread)->p_pid,
				(int)uid, ufsvfsp->vfs_fs->fs_fsmnt);
		}
		error = EDQUOT;
	} else if (ncurfiles >= dqp->dq_fsoftlimit && dqp->dq_fsoftlimit) {
		if (ncurfiles == dqp->dq_fsoftlimit ||
		    dqp->dq_ftimelimit == 0) {
			dqp->dq_ftimelimit = hrestime.tv_sec +
			    ufsvfsp->vfs_ftimelimit;
			/* If the caller owns the file and there is a	*/
			/* (non-NULL) controlling tty...		*/
			if (uid == cr->cr_ruid && curproc->p_sessp->s_vp) {
				uprintf(
	"\nquota_ufs: Warning: too many files (pid %d, uid %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)uid, ufsvfsp->vfs_fs->fs_fsmnt);
			} else {
				/* Send it to the messages file */
				cmn_err(CE_NOTE,
	"!quota_ufs: Warning: too many files (pid %d, uid %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)uid, ufsvfsp->vfs_fs->fs_fsmnt);
			}
		} else if (hrestime.tv_sec > dqp->dq_ftimelimit && !force) {
			/* If the user was not informed yet and the	*/
			/* caller is the owner of the file and the	*/
			/* controlling tty is non-NULL...		*/
			if ((dqp->dq_flags & DQ_FILES) == 0 &&
			    uid == cr->cr_ruid && curproc->p_sessp->s_vp) {
				uprintf(
	"\nquota_ufs: over file and time limit (pid %d, uid %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)uid, ufsvfsp->vfs_fs->fs_fsmnt);
				dqp->dq_flags |= DQ_FILES;
			} else {
				/*
				 * Send it to the messages file. Don't set
				 * DQ_FILES since we haven't informed a real
				 * user yet.
				 */
				cmn_err(CE_NOTE,
	"!quota_ufs: over file and time limit (pid %d, uid %d, fs %s)\n",
					(int)ttoproc(curthread)->p_pid,
					(int)uid, ufsvfsp->vfs_fs->fs_fsmnt);
			}
			error = EDQUOT;
		}
	}
out:
	TRANS_QUOTA(dqp);
	if (error == 0)
		dqp->dq_curfiles++;
	rw_exit(&dq_rwlock);
	dqput(dqp); /* XXX should dq_cnt be decremented here? */
	mutex_exit(&dqp->dq_lock);
	return (error);
}

/*
 * Release a dquot.
 */
void
dqrele(dqp)
	register struct dquot *dqp;
{

	if (dqp != NULL) {
		mutex_enter(&dqp->dq_lock);
		if (dqp->dq_cnt == 1 && dqp->dq_flags & DQ_MOD)
			dqupdate(dqp);
		dqput(dqp);
		mutex_exit(&dqp->dq_lock);
	}
}
