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
 *		All rights reserved.
 *
 */

#pragma ident	"@(#)mount.c	1.1	94/10/12 SMI"	/* SVr4 1.42	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/fstyp.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/dnlc.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/swap.h>
#include <sys/debug.h>
#include <sys/pathname.h>

/*
 * System calls.
 */

/*
 * "struct mounta" defined in sys/vfs.h.
 */

/* ARGSUSED */
int
mount(uap, rvp)
	register struct mounta *uap;
	rval_t *rvp;
{
	vnode_t *vp = NULL;
	register struct vfs *vfsp;
	struct vfssw *vswp;
	struct vfsops *vfsops;
	register int error;
	int remount = 0, ovflags;

	/*
	 * Resolve second path name (mount point).
	 */
	if (error = lookupname(uap->dir, UIO_USERSPACE, FOLLOW, NULLVPP, &vp))
		return (error);

	if (vn_vfslock(vp)) {
		VN_RELE(vp);
		return (EBUSY);
	}
	if (vp->v_vfsmountedhere != NULL) {
		vn_vfsunlock(vp);
		VN_RELE(vp);
		return (EBUSY);
	}
	if (vp->v_flag & VNOMOUNT) {
		vn_vfsunlock(vp);
		VN_RELE(vp);
		return (EINVAL);
	}

	/*
	 * Backward compatibility: require the user program to
	 * supply a flag indicating a new-style mount, otherwise
	 * assume the fstype of the root file system and zero
	 * values for dataptr and datalen.  MS_FSS indicates an
	 * SVR3 4-argument mount; MS_DATA is the preferred way
	 * and indicates a 6-argument mount.
	 */
	if (uap->flags & (MS_DATA|MS_FSS)) {
		u_int n, fstype;
		char fsname[FSTYPSZ];

		/*
		 * Even funnier: we want a user-supplied fstype name here,
		 * but for backward compatibility we have to accept a
		 * number if one is provided.  The heuristic used is to
		 * assume that a "pointer" with a numeric value of less
		 * than 256 is really an int.
		 */
		if ((fstype = (u_int)uap->fstype) < 256) {
			if (fstype == 0 || fstype >= nfstype ||
			    !ALLOCATED_VFSSW(&vfssw[fstype])) {
				vn_vfsunlock(vp);
				VN_RELE(vp);
				return (EINVAL);
			}
			strcpy(fsname, vfssw[fstype].vsw_name);
		} else if (error = copyinstr(uap->fstype, fsname,
		    FSTYPSZ, &n)) {
			if (error == ENAMETOOLONG)
				error = EINVAL;
			vn_vfsunlock(vp);
			VN_RELE(vp);
			return (error);
		}
		if ((vswp = vfs_getvfssw(fsname)) == NULL) { /* Locks vfssw */
			vn_vfsunlock(vp);
			VN_RELE(vp);
			return (EINVAL);
		} else
			vfsops = vswp->vsw_vfsops;
	} else {
		vfsops = rootvfs->vfs_op;
		RLOCK_VFSSW();
	}

	/* vfssw was implicitly locked in vfs_getvfssw or explicitly here */
	ASSERT(VFSSW_LOCKED());

	if ((uap->flags & MS_DATA) == 0) {
		uap->dataptr = NULL;
		uap->datalen = 0;
	}

	/*
	 * If this is a remount we don't want to create a new VFS.
	 * Instead we pass the existing one with a remount flag.
	 */
	if (uap->flags & MS_REMOUNT) {
		remount = 1;
		/*
		 * Confirm that the vfsp associated with the mount point
		 * has already been mounted on.
		 */
		if ((vp->v_flag & VROOT) == 0) {
			vn_vfsunlock(vp);
			VN_RELE(vp);
			RUNLOCK_VFSSW();
			return (ENOENT);
		}
		/*
		 * Disallow making file systems read-only.  Ignore other flags.
		 */
		if (uap->flags & MS_RDONLY) {
			vn_vfsunlock(vp);
			VN_RELE(vp);
			RUNLOCK_VFSSW();
			return (EINVAL);
		}
		vfsp = vp->v_vfsp;
		ovflags = vfsp->vfs_flag;
		vfsp->vfs_flag |= VFS_REMOUNT;
		vfsp->vfs_flag &= ~VFS_RDONLY;

	} else {
		vfsp = kmem_alloc(sizeof (vfs_t), KM_SLEEP);
		VFS_INIT(vfsp, vfsops, (caddr_t) NULL);
	}

	/*
	 * Lock the vfs so that lookuppn() will not venture into the
	 * covered vnode's subtree.
	 */
	if (error = vfs_lock(vfsp)) {
		vn_vfsunlock(vp);
		VN_RELE(vp);
		if (!remount)
			kmem_free((caddr_t) vfsp, sizeof (struct vfs));
		RUNLOCK_VFSSW();
		return (error);
	}

	dnlc_purge_vp(vp);

	error = VFS_MOUNT(vfsp, vp, uap, CRED());
	if (error) {
		vfs_unlock(vfsp);
		if (remount)
			vfsp->vfs_flag = ovflags;
		else
			kmem_free((caddr_t) vfsp, sizeof (struct vfs));
		vn_vfsunlock(vp);
		VN_RELE(vp);
	} else {
		if (remount) {
			vfsp->vfs_flag &= ~VFS_REMOUNT;
			vn_vfsunlock(vp);
			VN_RELE(vp);
		} else {
			vfs_add(vp, vfsp, uap->flags);
			vp->v_vfsp->vfs_nsubmounts++;
			vn_vfsunlock(vp);
		}
		vfs_unlock(vfsp);
	}
	RUNLOCK_VFSSW();

	return (error);
}
