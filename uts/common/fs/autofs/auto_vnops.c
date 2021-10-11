/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)auto_vnops.c 1.26     95/02/22 SMI"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/pathname.h>
#include <sys/dirent.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/tiuser.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/mode.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <sys/fs/autofs.h>
#include <fs/fs_subr.h>

extern int autofs_major;
extern void sigintr();
extern void sigunintr();

#ifdef	AUTODEBUG
int autodebug;
#endif	AUTODEBUG

/*
 *  Vnode ops for autofs
 */
static int auto_open(vnode_t **, int, cred_t *);
static int auto_close(vnode_t *, int, int, offset_t, cred_t *);
static int auto_getattr(vnode_t *, vattr_t *, int, cred_t *);
static int auto_access(vnode_t *, int, int, cred_t *);
static int auto_lookup(vnode_t *, char *, vnode_t **,
	pathname_t *, int, vnode_t *, cred_t *);
static int auto_create(vnode_t *, char *, vattr_t *, vcexcl_t,
	int, vnode_t **, cred_t *);
static int auto_remove(vnode_t *, char *, cred_t *);
static int auto_rename(vnode_t *, char *, vnode_t *, char *, cred_t *);
static int auto_mkdir(vnode_t *, char *, vattr_t *, vnode_t **, cred_t *);
static int auto_rmdir(vnode_t *, char *, vnode_t *, cred_t *);
static int auto_readdir(vnode_t *, uio_t *, cred_t *, int *);
static int auto_symlink(vnode_t *, char *, vattr_t *, char *, cred_t *);
static int auto_fsync(vnode_t *, int, cred_t *);
static void auto_inactive(vnode_t *, cred_t *);
static void auto_rwlock(vnode_t *, int);
static void auto_rwunlock(vnode_t *vp, int);
static int auto_seek(vnode_t *vp, offset_t, offset_t *);
static int auto_cmp(vnode_t *, vnode_t *);

vnodeops_t auto_vnodeops = {
	auto_open,	/* open */
	auto_close,	/* close */
	fs_nosys,	/* read */
	fs_nosys,	/* write */
	fs_nosys,	/* ioctl */
	fs_setfl,	/* setfl */
	auto_getattr,	/* getattr */
	fs_nosys,	/* setattr */
	auto_access,	/* access */
	auto_lookup,	/* lookup */
	auto_create,	/* create */
	auto_remove,	/* remove */
	fs_nosys,	/* link */
	auto_rename,	/* rename */
	auto_mkdir,	/* mkdir */
	auto_rmdir,	/* rmdir */
	auto_readdir,	/* readdir */
	auto_symlink,	/* symlink */
	fs_nosys,	/* readlink */
	auto_fsync,	/* fsync */
	auto_inactive,	/* inactive */
	fs_nosys,	/* fid */
	auto_rwlock,	/* rwlock */
	auto_rwunlock,	/* rwunlock */
	auto_seek,	/* seek */
	auto_cmp,	/* cmp */
	fs_nosys,	/* frlock */
	fs_nosys,	/* space */
	fs_nosys,	/* realvp */
	fs_nosys,	/* getpage */
	fs_nosys,	/* putpage */
	fs_nosys_map,	/* map */
	fs_nosys_addmap, /* addmap */
	fs_nosys,	/* delmap */
	fs_poll,	/* poll */
	fs_nosys,	/* dump */
	fs_pathconf,	/* pathconf */
	fs_nosys,	/* pageio */
	fs_nosys,	/* dumpctl */
	fs_nodispose,	/* dispose */
	fs_nosys,	/* setsecattr */
	fs_fab_acl	/* getsecattr */
};


#define	NEED_MOUNT(vp)	(vntoan(vp)->an_mntflags & MF_MNTPNT)
#define	SPECIAL_PATH(pnp) ((pnp)->pn_path[pn_pathleft(pnp) - 1] == ' ')

/* ARGSUSED */
static int
auto_open(
	vnode_t **vpp,
	int flag,
	cred_t *cr
)
{
	return (0);
}

/* ARGSUSED */
static int
auto_close(
	vnode_t *vp,
	int flag,
	int count,
	offset_t offset,
	cred_t *cr
)
{
	return (0);
}

/* ARGSUSED */
static int
auto_getattr(
	vnode_t *vp,
	vattr_t *vap,
	int flags,
	cred_t *cred
)
{
#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_getattr vp %x\n", vp);
#endif

	ASSERT(vp->v_type == VDIR);
	vap->va_uid	= 0;
	vap->va_gid	= 0;
	vap->va_nlink	= 2;
	vap->va_nodeid	= vntoan(vp)->an_nodeid;
	vap->va_size	= vntoan(vp)->an_size;
	vap->va_atime	= vap->va_mtime = vap->va_ctime = hrestime;
	vap->va_type	= vp->v_type;
	vap->va_mode	= vntoan(vp)->an_mode;
	vap->va_fsid	= vp->v_vfsp->vfs_dev;
	vap->va_rdev	= 0;
	vap->va_blksize	= MAXBSIZE;
	vap->va_nblocks	= btod(vap->va_size);
	vap->va_vcode	= 0;

	return (0);
}


/* ARGSUSED */
static int
auto_access(
	vnode_t *vp,
	int mode,
	int flags,
	cred_t *cred
)
{
	autonode_t *ap;

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_access vp %x\n", vp);
#endif
	ap = vntoan(vp);

	if (cred->cr_uid == 0)
		return (0);

	/*
	 * Not under a mutex because it really does
	 * not matter.  The uid fetch is atomic and the first one
	 * to get there wins anyway. The gid should not change
	 */
	if (cred->cr_uid != ap->an_uid) {
		mode >>= 3;
		if (groupmember(ap->an_gid, cred) == 0)
			mode >>= 3;
	}
	if ((ap->an_mode & mode) == mode) {
		return (0);
	}

	return (EACCES);

}

#define	MOUNTED_ON(vp) (vntoan(vp)->an_mntflags & MF_MOUNTED)

static int
auto_lookup(
	vnode_t *dvp,
	char *nm,
	vnode_t **vpp,
	pathname_t *pnp,
	int flags,
	vnode_t *rdir,
	cred_t *cred
)
{
	vnode_t *newvp;
	int error;
	char rnm[MAXNAMLEN + 1];
	autonode_t *dap;
	int mount_action = 0;
	int special_path = 0; /* has a space at end => dont block */
	k_sigset_t smask;
	struct autoinfo *daip = vfstoai(dvp->v_vfsp);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4,
		"auto_lookup dvp %x nm '%s'\n", dvp, nm);
#endif
	if (pnp->pn_buf[strlen(pnp->pn_buf) - 1] == ' ')
		special_path = 1;

	dap = vntoan(dvp);
	if (!special_path)
		dap->an_ref_time = hrestime.tv_sec;

	/*
	 * A space is appended to the pathname only in case if the
	 * the lookup originated from the daemon. Even in that
	 * case, the space is appended for indirect mounts or
	 * direct mounts with offsets.
	 *
	 * /usr/bugs	/bin	foo:/bin
	 * 		/etc	bar:/etc
	 *
	 * Therefore special path is *not* set in case of any request
	 * not originating in the daemon  -or-
	 * any kind of direct mount request (without any offset)  eg.
	 *
	 * /usr/local	/	foo:/root
	 *		/bin	bar:/bin
	 *
	 * NEED_MOUNT is true in case of all direct mount points.
	 * (!special_path && NEED_MOUNT) return true in case
	 * of direct mounts (both with and without offset).
	 * Since we want only no-offset direct mounts to attempt
	 * mount we put 2 additional conditions. dap->an_dirents == NULL
	 * ensures that.
	 */
	mutex_enter(&dap->an_lock);
	if ((!special_path) && NEED_MOUNT(dvp)) {
		if ((dvp->v_vfsmountedhere == NULL) &&
		    (dap->an_dirents == NULL)) {
			/*
			 * the following syncronises it with the do_mount in
			 * auto_readdir. If two threads are doing lookup
			 * at the same time they are serialized inside
			 * do_mount
			 */
			dap->an_mntflags |= MF_WAITING_MOUNT;
			mount_action = 1;
		} else if (MOUNTED_ON(dvp) && (dvp->v_vfsmountedhere != NULL)) {
			/*
			 * This case happens when a process has done
			 * a cd to a direct (no offset) mntpnt then
			 * it does an ls which causes the mount to
			 * happen but the current directory of the proces
			 * is still the underlying vnode. So when that
			 * process does a cd to a dierctory on the
			 * mounted filesystem, the request has to
			 * redirected.
			 */
			error = VFS_ROOT(dvp->v_vfsmountedhere, &newvp);
			mutex_exit(&dap->an_lock);
			if (!error) {
				error = VOP_LOOKUP(newvp, nm, vpp, pnp,
						    flags, rdir, cred);
				VN_RELE(newvp);
			}
			return (error);
		}
	}
	mutex_exit(&dap->an_lock);

	if (mount_action == 1) {
		mutex_enter(&dap->an_lock);
		error = do_mount(dvp, nm, cred);
		mutex_exit(&dap->an_lock);

direct_mount:
		if (error)
			return (error);
		if (dvp->v_vfsmountedhere == NULL) {
			/*
			 * can be null in case of direct mount
			 * with offset in that case proceed like
			 * an offset mount otherwise if it is a
			 * simple direct mount return ENOENT
			 */
			if (dap->an_dirents == NULL)
				return (ENOENT);
			else
				goto offset_mount;
		}
		error = VFS_ROOT(dvp->v_vfsmountedhere, &newvp);
		if (!error) {
			error = VOP_LOOKUP(newvp, nm, vpp, pnp, flags,
					    rdir, cred);
			VN_RELE(newvp);
		}
		return (error);
	}

offset_mount:
	/*
	 * In case of an direct offset mount if unmounting
	 * is going on, block it right here if the lookup is
	 * not daemon initiated.
	 * It is necessary to block at this point i.e.
	 * at the top of the heitarchy for
	 * direct offset mounts, because we do not
	 * even to attempt an autodir_lookup, which
	 * is OK for indirect mounts. That is why there
	 * is little duplication of code here and below.
	 */
	mutex_enter(&dap->an_lock);
	if ((dap->an_mntflags & MF_MNTPNT) &&
	    (dap->an_mntflags & MF_UNMOUNTING) &&
	    (!special_path)) {
		dap->an_waiters++;
		dap->an_mntflags |= MF_WAITING_UMOUNT;
		sigintr(&smask, 1);
		while (dap->an_mntflags & MF_UNMOUNTING) {
			if (!cv_wait_sig(&dap->an_cv_umount, &dap->an_lock)) {
				if (--dap->an_waiters == 0)
					dap->an_mntflags &= ~MF_WAITING_UMOUNT;
				sigunintr(&smask);
				mutex_exit(&dap->an_lock);
				return (EINTR);
			}
		}
		sigunintr(&smask);
		if (--dap->an_waiters == 0)
			dap->an_mntflags &= ~MF_WAITING_UMOUNT;
		if (dap->an_mntflags & MF_DONTMOUNT)
			dap->an_mntflags &= ~MF_DONTMOUNT;
	}
	mutex_exit(&dap->an_lock);

	error = VOP_ACCESS(dvp, VEXEC, 0, cred);
	if (error)
		return (error);

	if (strcmp(nm, ".") == 0) {
		VN_HOLD(dvp);
		*vpp = dvp;
		return (0);
	} else if (strcmp(nm, "..") == 0) {
		autonode_t *pdap;

		pdap = (vntoan(dvp))->an_parent;
		ASSERT(pdap != NULL);
		*vpp = antovn(pdap);
		VN_HOLD(*vpp);
		return (0);
	}

	if (nm[strlen(nm) - 1] == ' ') {
		bcopy(nm, rnm, strlen(nm) - 1);
		rnm[strlen(nm) - 1] = '\0';
	}
	else
		bcopy(nm, rnm, strlen(nm) + 1);

lookup_retry:
	rw_enter(&dap->an_rwlock, RW_READER);
	error = autodir_lookup(dvp, rnm, vpp, cred, special_path);
	rw_exit(&dap->an_rwlock);
	if (!special_path) {
		if (error == ENOENT)
			mutex_enter(&dap->an_lock);
		else if ((error == 0) && (dap->an_mntflags & MF_INPROG)) {
			/*
			 * This mountpoint has been flagged with a mount
			 * in progress.
			 */
			if ((dap->an_mntflags & MF_MNTPNT) ||
			    (strcmp(rnm, daip->ai_current) == 0)) {
				VN_RELE(*vpp);
				mutex_enter(&dap->an_lock);
				error = ENOENT;
			}
		}
	}

	if ((error == 0) && (!special_path)) {
		autonode_t *ap;
		ap = vntoan(*vpp);
		mutex_enter(&ap->an_lock);
		if (ap->an_error) {
			/*
			 * We can get here by branching to lookup_retry after
			 * we had to sleep waiting for a mount locked on the
			 * same autonode we were to finish, and obviously it
			 * failed.
			 * A previous mount on this node failed, automountd
			 * is in the process of removing this autonode,
			 * return the same error here.
			 */
			error = ap->an_error;
			mutex_exit(&ap->an_lock);
			return (error);
		}
		if (ap->an_mntflags & MF_UNMOUNTING) {
			ap->an_waiters++;
			ap->an_mntflags |= MF_WAITING_UMOUNT;
			sigintr(&smask, 1);
			while (ap->an_mntflags & MF_UNMOUNTING) {
				if (!cv_wait_sig(&ap->an_cv_umount,
				    &ap->an_lock)) {
					if (--ap->an_waiters == 0)
						ap->an_mntflags &=
						    ~MF_WAITING_UMOUNT;
					sigunintr(&smask);
					mutex_exit(&ap->an_lock);
					VN_RELE(*vpp);
					return (EINTR);
				}
			}
			sigunintr(&smask);
			if (--ap->an_waiters == 0)
				ap->an_mntflags &= ~MF_WAITING_UMOUNT;
			if (ap->an_mntflags & MF_DONTMOUNT) {
				/*
				 * It means the unmount was not
				 * successful so the filesystem
				 * should still be mounted so continue life.
				 */
				ap->an_mntflags &= ~MF_DONTMOUNT;
			} else {
				/*
				 * Unmount successful but autofs dirs
				 * are still hanging around, so we need
				 * another do_mount. We just simulate
				 * a ENOENT so that it does the do_mount
				 * for us below.
				 */
				VN_RELE(*vpp);
				mutex_enter(&dap->an_lock);
				error = ENOENT;
			}
		} else if (((*vpp)->v_vfsmountedhere == NULL) &&
				(ap->an_dirents == NULL)) {
			/*
			 * This takes care of the case where user
			 * does a umount without removing the
			 * directory, or automountd unmounted this directory
			 * as a member of a hierarchy and could not remount it
			 * leaving the autonode behind.
			 *
			 * If this directory is the root of the vfs, and it
			 * is an indirect mount (then node is remountable),
			 * set ENOENT to trigger mount again below. We fall
			 * through if the node is not remountable and return
			 * the vnode for this autonode.
			 */
			if ((dvp->v_flag & VROOT) &&
			    ((dap->an_mntflags & MF_MNTPNT) == 0)) {
				VN_RELE(*vpp);
				mutex_enter(&dap->an_lock);
				error = ENOENT;
			} else {
				/*
				 * force the mount of the whole hierachy again.
				 */
				mutex_exit(&ap->an_lock);
				VN_RELE(*vpp);
				error = force_remount(dap, nm, cred);
				if (error == EAGAIN)
					goto lookup_retry;

				if (dap->an_mntflags & MF_MNTPNT)
					goto direct_mount;

				rw_enter(&dap->an_rwlock, RW_READER);
				error = autodir_lookup(dvp, rnm, vpp,
					cred, special_path);
				rw_exit(&dap->an_rwlock);
				mutex_enter(&ap->an_lock);
			}
		}
		mutex_exit(&ap->an_lock);
	}

	if (error == ENOENT) {
		/*
		 * If the last component of pathname has a space
		 * at the end it means that the lookup was initiated
		 * by the daemon. We should return ENOENT so that
		 * the daemon can do a mkdir and proceed with the
		 * mount. Therefore we allow it to proceed.
		 */
		if (special_path) {
			return (ENOENT);
		} else {
			/*
			 * If this directory is the root of a vfs
			 * and it is an indirect mount, trigger the
			 * mount, otherwise fall through and return
			 * ENOENT.
			 * The MF_MNTPNT flag is not set for indirect
			 * maps.
			 */
			if ((dvp->v_flag & VROOT) &&
			    ((dap->an_mntflags & MF_MNTPNT) == 0)) {
				error = do_mount(dvp, nm, cred);
				mutex_exit(&dap->an_lock);
				if (error == 0) {
					rw_enter(&dap->an_rwlock, RW_READER);
					error = autodir_lookup(dvp, rnm, vpp,
						cred, special_path);
					rw_exit(&dap->an_rwlock);
				} else if (error == EAGAIN)
					goto lookup_retry;
			} else
				mutex_exit(&dap->an_lock);
		}
	} else if (error == 0) {
		/*
		 * If autonode is found, and if it happens
		 * to be mounted on, we should strip off
		 * the trailing space. Because it only has
		 * a meaning within autofs. So when we cross
		 * over to another vfs, we dont need it. This is
		 * the case in heirarchical mounts.
		 */
		if ((*vpp)->v_vfsmountedhere && special_path &&
			(*vpp)->v_vfsp->vfs_op
				!= (*vpp)->v_vfsmountedhere->vfs_op) {
			pnp->pn_buf[strlen(pnp->pn_buf) - 1] = '\0';
		}
	}

	return (error);
}

static int
auto_create(
	vnode_t *dvp,
	char *nm,
	vattr_t *va,
	vcexcl_t exclusive,
	int mode,
	vnode_t **vpp,
	cred_t *cr
)
{
	int error;
#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_create dvp %x nm %s\n", dvp, nm);
#endif

	if (dvp->v_vfsmountedhere == NULL)
		return (ENOSYS);

	/*
	 * Redirect call to filesystem mounted at dvp
	 */
	error = VFS_ROOT(dvp->v_vfsmountedhere, &dvp);
	if (error)
		return (error);

	error = VOP_CREATE(dvp, nm, va, exclusive, mode, vpp, cr);
	VN_RELE(dvp);

	return (error);
}

static int
auto_remove(
	vnode_t *dvp,
	char *nm,
	cred_t *cr
)
{
	int error;
#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_remove dvp %x nm %s\n", dvp, nm);
#endif

	if (dvp->v_vfsmountedhere == NULL)
		return (ENOSYS);

	/*
	 * Redirect call to filesystem mounted at dvp
	 */
	error = VFS_ROOT(dvp->v_vfsmountedhere, &dvp);
	if (error)
		return (error);

	error = VOP_REMOVE(dvp, nm, cr);
	VN_RELE(dvp);

	return (error);
}

static int
auto_rename(
	vnode_t *odvp,
	char *onm,
	vnode_t *ndvp,
	char *nnm,
	cred_t *cr
)
{
	int error;

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4,
		"auto_rename odvp %x onm %s to ndvp %x nnm %s\n",
		odvp, onm, ndvp, nnm);
#endif
	if (odvp->v_vfsmountedhere == NULL || ndvp->v_vfsmountedhere == NULL)
		return (ENOSYS);

	/*
	 * Redirect call to filesystem mounted at odvp/ndvp
	 */
	error = VFS_ROOT(odvp->v_vfsmountedhere, &odvp);
	if (error)
		return (error);
	error = VFS_ROOT(ndvp->v_vfsmountedhere, &ndvp);
	if (error)
		return (error);

	error = VOP_RENAME(odvp, onm, ndvp, nnm, cr);
	VN_RELE(odvp);
	VN_RELE(ndvp);

	return (error);
}

static int
auto_mkdir(
	vnode_t *dvp,
	char *nm,
	vattr_t *va,
	vnode_t **vpp,
	cred_t *cred
)
{
	autonode_t *ap, *dap;
	vnode_t *vp;
	int error;
	int namelen = strlen(nm);
	char rnm[MAXNAMLEN + 1];

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_mkdir dvp %x nm %s\n", dvp, nm);
#endif

	/*
	 * Check if mounted-on
	 */
	if (dvp->v_vfsmountedhere) {
		error = VFS_ROOT(dvp->v_vfsmountedhere, &vp);
		if (error)
			return (error);
		error = VOP_MKDIR(vp, nm, va, vpp, cred);
		VN_RELE(vp);
		return (error);
	}

	/*
	 * If the last char in name is a space, it means
	 * that it is from the daemon, strip it off.
	 * Everbody else should get an ENOSYS.
	 */
	if (nm[namelen - 1] == ' ') {
		bcopy(nm, rnm, namelen - 1);
		rnm[namelen - 1] = '\0';
	}
	else
		return (ENOSYS);

	if ((nm[0] == '.') &&
	    (namelen == 1 || (namelen == 2 && nm[1] == '.')))
		return (EINVAL);

	error = VOP_ACCESS(dvp, VEXEC|VWRITE, 0, cred);
	if (error)
		return (error);

	dap = vntoan(dvp);
	rw_enter(&dap->an_rwlock, RW_READER);
	error = autodir_lookup(dvp, rnm, vpp, cred, 1);
	rw_exit(&dap->an_rwlock);

	if (!error) {
		VN_RELE(*vpp);
		return (EEXIST);
	}

	vp = makeautonode(VDIR, dvp->v_vfsp, cred);

	ap = vntoan(vp);
	bcopy(rnm, ap->an_name, strlen(rnm) + 1);
	ap->an_mode = MAKEIMODE(VDIR, va->va_mode);

	error = auto_direnter(dap, ap);
	if (error) {
		freeautonode(ap);
		ap = NULL;
		*vpp = (vnode_t *) 0;
	} else {
		ap->an_parent = dap;
		VN_HOLD(dvp);
		*vpp = vp;
	}
	return (error);

}

static int
auto_rmdir(
	vnode_t *dvp,
	char *nm,
	vnode_t *cdir,
	cred_t *cred
)
{
	autonode_t *dap, *ap, *cap, **app;
	int error, namelen;
	vnode_t *vp;
	char rnm[MAXNAMLEN + 1];

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_rmdir dvp %x nm %s\n", dvp, nm);
#endif
	/*
	 * Check if mounted-on
	 */
	if (dvp->v_vfsmountedhere) {
		error = VFS_ROOT(dvp->v_vfsmountedhere, &vp);
		if (error)
			return (error);

		error = VOP_RMDIR(vp, nm, cdir, cred);
		VN_RELE(vp);
		return (error);
	}

	namelen = strlen(nm);
	if (namelen == 0)
		cmn_err(CE_PANIC, "autofs: autofs_rmdir");

	/*
	 * If the last char in name is a space, it means
	 * that it is from the daemon, strip it off.
	 * Everbody else should get an ENOSYS.
	 */
	if (nm[namelen - 1] == ' ') {
		bcopy(nm, rnm, namelen - 1);
		rnm[namelen - 1] = '\0';
	}
	else
		return (ENOSYS);

	if (nm[0] == '.') {
		if (namelen == 1)
			return (EINVAL);
		else if ((namelen == 2) && (nm[1] == '.'))
			return (EEXIST);
	}

	/*
	 * XXX -- Since we've gone to all the trouble of
	 * calling VOP_ACCESS, it would be nice to do
	 * something with the result...
	 */
	error = VOP_ACCESS(dvp, VEXEC|VWRITE, 0, cred);

	dap = vntoan(dvp);
	rw_enter(&dap->an_rwlock, RW_WRITER);

	error = autodir_lookup(dvp, rnm, &vp, cred, 1);
	if (error) {
		rw_exit(&dap->an_rwlock);
		return (ENOENT);
	}

	/*
	 * prevent a race between mount and rmdir
	 */
	if (vn_vfslock(vp)) {
		error = EBUSY;
		goto bad;
	}
	if (vp->v_vfsmountedhere != NULL) {
		error = EBUSY;
		goto vfs_unlock_bad;
	}

	ap = vntoan(vp);

	/*
	 * Can't remove directory if the sticky bit is set
	 * unless you own the parent dir and curr dir.
	 */
	if ((dap->an_mode & S_ISVTX) && (cred->cr_uid != 0) &&
	    (cred->cr_uid != dap->an_uid) &&
	    (cred->cr_uid != ap->an_uid)) {
		error = EPERM;
		goto vfs_unlock_bad;
	}

	if ((vp == dvp) || (vp == cdir)) {
		error = EINVAL;
		goto vfs_unlock_bad;
	}
	if (ap->an_dirents != NULL) {
		error = ENOTEMPTY;
		goto vfs_unlock_bad;
	}
	if ((app = &dap->an_dirents) == NULL)
		cmn_err(CE_PANIC,
			"autofs: auto_rmdir: null directory list 0x%x",
			(int) dap);

	for (;;) {
		cap = *app;
		if (cap == NULL) {
			cmn_err(CE_WARN,
				"autofs: auto_rmdir: No entry for %s\n", nm);
			error = ENOENT;
			goto vfs_unlock_bad;
		}
		if ((cap == ap) &&
		    (strcmp(cap->an_name, rnm) == 0))
			break;
		app = &cap->an_next;
	}

	/*
	 * cap points to the correct directory entry
	 */
	*app = cap->an_next;
	rw_exit(&dap->an_rwlock);
	mutex_enter(&dap->an_lock);
	dap->an_size--;
	mutex_exit(&dap->an_lock);

	vn_vfsunlock(vp);
	/*
	 * the autonode had a pointer to parent Vnode
	 */

	VN_RELE(dvp);

	mutex_enter(&cap->an_lock);
	cap->an_size -= 2;
	mutex_exit(&cap->an_lock);
	VN_RELE(antovn(cap));
	return (0);

vfs_unlock_bad:
	vn_vfsunlock(vp);
bad:
	VN_RELE(vp);
	rw_exit(&dap->an_rwlock);
	return (error);

}

static int
auto_readdir(
	vnode_t *vp,
	register uio_t *uiop,
	cred_t *cred,
	int *eofp
)
{

	autonode_t *ap = vntoan(vp);
	autonode_t *cap;
	register int total_bytes_wanted;
	register dirent_t *dp;
	register u_int offset;
	register int outcount = 0;
	int namelen;
	int mount_action = 0;
	caddr_t outbuf;
	int direntsz, error, bufsize;
	int reached_max = 0;
	k_sigset_t smask;

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_readdir vp %x\n", vp);
#endif

	error = 0;
	/*
	 * For direct mounts trigger a mount here.
	 * If /usr/share/man is such a mount point,
	 * an ls /usr/share/man should do the right thing.
	 */
	/*
	 * Undo the  lock held by getdents, before
	 * attempting a do_mount, because do_mount
	 * may cause a mkdir to happen, which will
	 * again grab this lock.
	 */
	auto_rwunlock(vp, 0);
	mutex_enter(&ap->an_lock);
	if (NEED_MOUNT(vp) && (((vp->v_vfsmountedhere == NULL) &&
	    (ap->an_dirents == NULL)) || (ap->an_mntflags & MF_INPROG))) {
		/*
		 * Start by assuming we have to trigger the mount ourselves.
		 */
		mount_action = 1;
		sigintr(&smask, 1);
		while (ap->an_mntflags & (MF_WAITING_MOUNT | MF_INPROG)) {
			ap->an_mntflags |= MF_WAITING_MOUNT;
			if (!cv_wait_sig(&ap->an_cv_mount, &ap->an_lock)) {
				sigunintr(&smask);
				mutex_exit(&ap->an_lock);
				auto_rwlock(vp, 0);
				return (EINTR);
			} else if (ap->an_error == 0) {
				/*
				 * Someone else did the mount for us.
				 */
				mount_action = 0;
			} else {
				/*
				 * Return the error encountered.
				 */
				error = ap->an_error;
				sigunintr(&smask);
				mutex_exit(&ap->an_lock);
				auto_rwlock(vp, 0);
				return (error);
			}
		}
		sigunintr(&smask);
		if (mount_action) {
			ap->an_mntflags |= MF_WAITING_MOUNT;
			error = do_mount(vp, ".", cred);
			mutex_exit(&ap->an_lock);
			auto_rwlock(vp, 0);
			if (error)
				return (error);
		} else {
			mutex_exit(&ap->an_lock);
			auto_rwlock(vp, 0);
		}
	} else {
		mutex_exit(&ap->an_lock);
		auto_rwlock(vp, 0);
	}

	if (vp->v_vfsmountedhere != NULL) {
		vnode_t *newvp;

		error = VFS_ROOT(vp->v_vfsmountedhere, &newvp);
		if (error)
			return (error);
		VOP_RWLOCK(newvp, 0);
		error = VOP_READDIR(newvp, uiop, cred, eofp);
		VOP_RWUNLOCK(newvp, 0);
		VN_RELE(newvp);
		return (error);
	}
	/*
	 * XXX need code to list indirect
	 * directory or offset path directories.
	 * For indirect case - need to think
	 * about showing potential names as
	 * well as mounted names.
	 */

	if (uiop->uio_iovcnt != 1)
		return (EINVAL);

	/* XXX : always return . and .. */

	total_bytes_wanted = uiop->uio_iov->iov_len;
	bufsize = total_bytes_wanted + sizeof (dirent_t);
	dp = (dirent_t *) kmem_zalloc(bufsize, KM_SLEEP);
	outbuf = (caddr_t) dp;
	direntsz = (char *) dp->d_name - (char *) dp;
	offset = 0;
	/*
	 * Held when getdents calls VOP_RWLOCK....
	 */
	ASSERT(RW_READ_HELD(&ap->an_rwlock));
	cap = ap->an_dirents;
	if (uiop->uio_offset == 0) {
		/*
		 * first time: so fudge the . and ..
		 */
		dp->d_reclen = (direntsz + 2 + (NBPW - 1)) & ~(NBPW - 1);
		dp->d_ino = ap->an_nodeid;
		dp->d_off = 0;
		(void) strcpy(dp->d_name, ".");
		outcount += dp->d_reclen;
		dp = (dirent_t *) ((int)dp + dp->d_reclen);

		dp->d_reclen = (direntsz + 3 + (NBPW - 1)) & ~(NBPW - 1);
		dp->d_ino = ap->an_parent->an_nodeid;
		dp->d_off = 1;
		(void) strcpy(dp->d_name, "..");
		outcount += dp->d_reclen;
		dp = (dirent_t *) ((int)dp + dp->d_reclen);
	}
	/*
	 * Offset is set to 1 in all cases because we are fudging
	 * the . and .. stuff.
	 */
	offset = 1;

	while (cap) {
		namelen = strlen(cap->an_name) + 1;
		if (offset >= uiop->uio_offset) {
			dp->d_reclen = (direntsz + namelen
					+ (NBPW - 1)) & ~(NBPW - 1);
			if (outcount + (int) dp->d_reclen >
			    total_bytes_wanted) {
				reached_max = 1;
				break;
			}
			dp->d_ino = (u_long) cap->an_nodeid;
			dp->d_off = cap->an_offset;
			(void) strcpy(dp->d_name, cap->an_name);
			outcount += dp->d_reclen;
			dp = (dirent_t *) ((int)dp + dp->d_reclen);
		}
		offset = cap->an_offset;
		cap = cap->an_next;
	}

	if (outcount)
		error = uiomove((char *) outbuf, (long)outcount, UIO_READ,
				uiop);
	if (!error) {
		if (reached_max) {
			/*
			 * This entry did not get added to the buffer on this,
			 * call. We need to add it on the next call therefore
			 * set uio_offset to this entry's offset.
			 */
			uiop->uio_offset = offset;
		} else {
			/*
			 * Process next entry on next call.
			 */
			uiop->uio_offset = offset + 1;
		}

		if (cap == NULL && eofp)
			*eofp = 1;
	}
	kmem_free((void *)outbuf, bufsize);
	return (error);
}

static int
auto_symlink(
	vnode_t *dvp,
	char *lnm,
	vattr_t *tva,
	char *tnm,
	cred_t *cr
)
{
	int error;

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_symlink dvp %x %s\n", dvp, lnm);
#endif
	if (dvp->v_vfsmountedhere == NULL)
		return (ENOSYS);

	/*
	 * Redirect call to filesystem mounted at dvp.
	 */
	error = VFS_ROOT(dvp->v_vfsmountedhere, &dvp);
	if (error)
		return (error);

	error = VOP_SYMLINK(dvp, lnm, tva, tnm, cr);
	VN_RELE(dvp);

	return (error);
}

/* ARGSUSED */
static int
auto_fsync(
	vnode_t *cp,
	int syncflag,
	cred_t *cred
)
{
	return (0);
}

/* ARGSUSED */
static void
auto_inactive(
	vnode_t *vp,
	cred_t *cred
)
{
	autonode_t *ap = vntoan(vp);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_inactive vp %x\n", vp);
#endif

	mutex_enter(&vp->v_lock);
	ASSERT(vp->v_count > 0);
	if (--vp->v_count > 0) {
		mutex_exit(&vp->v_lock);
		return;
	}
	mutex_exit(&vp->v_lock);
	if (ap->an_size == 0)
		freeautonode(ap);
}

static void
auto_rwlock(
	vnode_t *vp,
	int write_lock
)
{
	autonode_t *ap = vntoan(vp);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_rwlock vp %x\n", vp);
#endif
	if (write_lock)
		rw_enter(&ap->an_rwlock, RW_WRITER);
	else
		rw_enter(&ap->an_rwlock, RW_READER);

}

/* ARGSUSED */
static void
auto_rwunlock(
	vnode_t *vp,
	int write_lock
)
{
	autonode_t *ap = vntoan(vp);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_rwunlock vp %x\n", vp);
#endif
	rw_exit(&ap->an_rwlock);
}

/* ARGSUSED */
static int
auto_seek(vp, ooff, noffp)
	struct vnode *vp;
	offset_t ooff;
	offset_t *noffp;
{
#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_seek vp %x\n", vp);
#endif
	/*
	 * Return 0 unconditionally since we
	 * always expect a VDIR vnode.
	 */
	return (0);
}

static int
auto_cmp(
	vnode_t *vp1,
	vnode_t *vp2
)
{
#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_cmp vp1 vp2 %x %x\n", vp1, vp2);
#endif
	return (vp1 == vp2);
}
