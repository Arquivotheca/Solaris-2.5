#pragma ident  "@(#)tmp_vfsops.c 1.34    95/08/28 SMI"
/*  tmp_vfsops.c 1.11 90/03/30 SMI */

/*
 * Copyright (c) 1989-1995 by Sun Microsystems, Inc.
 * All rights reserved
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/time.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <vm/page.h>
#include <vm/anon.h>

#include <sys/fs/swapnode.h>
#include <sys/fs/tmp.h>
#include <sys/fs/tmpnode.h>

/*
 * tmpfs vfs operations.
 */
static int tmp_mount(struct vfs *, struct vnode *,
	    struct mounta *, struct cred *);
static int tmp_unmount(struct vfs *, struct cred *);
static int tmp_root(struct vfs *, struct vnode **);
static int tmp_statvfs(struct vfs *, struct statvfs *);
static int tmp_sync(struct vfs *, short, struct cred *);
static int tmp_vget(struct vfs *, struct vnode **, struct fid *);
static int tmp_mountroot(struct vfs *, enum whymountroot);
static int tmp_swapvp(struct vfs *, struct vnode **, char *);

/*
 * Loadable module wrapper
 */
#include <sys/modctl.h>

static struct vfssw vfw = {
	"tmpfs",
	tmpfsinit,
	&tmp_vfsops,
	0
};

/*
 * Module linkage information
 */
static struct modlfs modlfs = {
	&mod_fsops, "filesystem for tmpfs", &vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

static int module_keepcnt = 0;	/* ==0 means the module is unloadable */

_init()
{
	return (mod_install(&modlinkage));
}

_fini()
{
	if (module_keepcnt != 0)
		return (EBUSY);

	return (mod_remove(&modlinkage));
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

struct vfsops tmp_vfsops = {
	tmp_mount,
	tmp_unmount,
	tmp_root,
	tmp_statvfs,
	tmp_sync,
	tmp_vget,
	tmp_mountroot,
	tmp_swapvp
};

/*
 * global statistics
 */
int tmp_kmemspace = 0;	/* bytes of kernel heap used by all tmpfs */
int tmp_files = 0;	/* number of files or directories in all tmpfs */

#define	TMPFSMAXMOUNT	256
#define	TMPMAP		TMPFSMAXMOUNT/NBBY
static u_char tmp_mdevmap[TMPMAP];	/* map for tmpfs minor dev #'s */

static struct tmount *tmpfs_mountp = 0;	/* linked list of tmpfs mount structs */

static int
tmp_mount(
	struct vfs *vfsp,
	struct vnode *mvp,
	struct mounta *uap,
	struct cred *cr)
{
	struct tmount *tmx;
	struct tmount *tm = NULL;
	struct tmpnode *tp;
	struct pathname dpn;
	int error = 0;
	int mdev;
	char *data = uap->dataptr;
	int datalen = uap->datalen;
	struct tmpfs_args targs;
	struct vattr rattr;
	int got_attrs;

	module_keepcnt++;

	if (!suser(cr)) {
		module_keepcnt--;
		return (EPERM);
	}

	if (mvp->v_type != VDIR) {
		module_keepcnt--;
		return (ENOTDIR);
	}
	mutex_enter(&mvp->v_lock);
	if (mvp->v_count != 1 || (mvp->v_flag & VROOT)) {
			mutex_exit(&mvp->v_lock);
			module_keepcnt--;
			return (EBUSY);
	}
	mutex_exit(&mvp->v_lock);

	/*
	 * Get arguments
	 */
	if (datalen != 0) {
		if (datalen != sizeof (targs)) {
			module_keepcnt--;
			return (EINVAL);
		} else {
			if (copyin(data, (caddr_t)&targs, sizeof (targs))) {
				module_keepcnt--;
				return (EFAULT);
			}
		}
	} else {
		targs.anonmax = 0;
		targs.flags = 0;
	}

	if (error = pn_get(uap->dir, UIO_USERSPACE, &dpn)) {
		module_keepcnt--;
		return (error);
	}

	TMP_PRINT(T_DEBUG, "tmp_mount: mounting %s on mvp %x\n",
	    dpn.pn_path, mvp, 0, 0, 0);

	mutex_enter(&tmpfs_mutex);
	if ((uap->flags & MS_OVERLAY) == 0) {
		for (tmx = tmpfs_mountp; tmx; tmx = tmx->tm_next) {
			if (tmx->tm_vfsp->vfs_vnodecovered == mvp) {
				mutex_exit(&tmpfs_mutex);
				error = EBUSY;
				goto err;
			}
		}
	}

	/*
	 * allocate tmount structure
	 */
	if (tmp_kmemspace + sizeof (struct tmount) > tmpfs_maxkmem) {
		mutex_exit(&tmpfs_mutex);
		error = ENOMEM;
		goto err;
	}
	tmp_kmemspace += sizeof (struct tmount);
	mutex_exit(&tmpfs_mutex);

	tm = (struct tmount *)kmem_zalloc(sizeof (struct tmount), KM_SLEEP);

	/*
	 * find first available minor device number for this mount
	 */
	mutex_enter(&tmpfs_mutex);
	for (mdev = 0; mdev < TMPFSMAXMOUNT; mdev++)
		if (!TESTBIT(tmp_mdevmap, mdev))
			break;
	if (mdev == TMPFSMAXMOUNT) {
		mutex_exit(&tmpfs_mutex);
		cmn_err(CE_WARN, "Number of available tmpfs mounts over %d\n",
		    TMPFSMAXMOUNT);
		error = EINVAL; /* ? */
		goto err;
	}
	SETBIT(tmp_mdevmap, mdev);
	mutex_exit(&tmpfs_mutex);

	/*
	 * Set but don't bother entering the mutex
	 * (tmount not on mount list yet)
	 */
	mutex_init(&tm->tm_contents, "tmpfs mount contents lock",
	    MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&tm->tm_renamelck, "tmpfs rename lock",
	    MUTEX_DEFAULT, DEFAULT_WT);

	tm->tm_vfsp = vfsp;
	tm->tm_dev = makedevice(tmpdev, mdev);

	tm->tm_inomap = (struct tmpimap *)tmp_memalloc(tm,
	    (u_int)sizeof (struct tmpimap));
	if (tm->tm_inomap == NULL) {
		error = ENOMEM;
		goto err;
	}
	TMP_PRINT(T_ALLOC, "tmp_mount: allocated tmpimap at %x size %d\n",
	    tm->tm_inomap, sizeof (struct tmpimap), 0, 0, 0);

	/*
	 * nodes 0 and 1 on a file system are unused
	 * i.e. the first or "root" directory of a given
	 * filesystem has inode number 2.
	 */
	SETBIT(tm->tm_inomap->timap_bits, 0);
	SETBIT(tm->tm_inomap->timap_bits, 1);

	/*
	 * Initialize the pseudo generation number counter
	 */
	tm->tm_gen = 0;

	vfsp->vfs_data = (caddr_t)tm;
	vfsp->vfs_fstype = tmpfsfstype;
	vfsp->vfs_dev = tm->tm_dev;
	vfsp->vfs_bsize = PAGESIZE;
	vfsp->vfs_flag |= VFS_NOTRUNC;
	vfsp->vfs_fsid.val[0] = tm->tm_dev;
	vfsp->vfs_fsid.val[1] = tmpfsfstype;
	tm->tm_mntpath = (char *)tmp_memalloc(tm, (u_int)dpn.pn_pathlen + 1);
	if (tm->tm_mntpath == NULL) {
		error = ENOMEM;
		goto err;
	}
	strcpy(tm->tm_mntpath, dpn.pn_path);
	TMP_PRINT(T_ALLOC, "tmp_mount: allocated mntpath at %x size %d\n",
	    tm->tm_mntpath, dpn.pn_pathlen + 1, 0, 0, 0);

	/*
	 * allocate and initialize root tmpnode structure
	 */
	bzero((caddr_t)&rattr, sizeof (struct vattr));
	rattr.va_mode = (mode_t)(S_IFDIR | 0777);	/* XXX modes */
	rattr.va_type = VDIR;
	rattr.va_rdev = 0;
	tp = tmpnode_alloc(tm, &rattr, cr);
	if (tp == NULL) {
		error = ENOSPC;
		goto err;
	}

	/*
	 * Get the mode, uid, and gid from the underlying mount point.
	 */
	rattr.va_mask = AT_MODE|AT_UID|AT_GID;	/* Hint to getattr */
	got_attrs = VOP_GETATTR(mvp, &rattr, 0, cr);

	rw_enter(&tp->tn_rwlock, RW_WRITER);
	TNTOV(tp)->v_flag |= VROOT;

	/*
	 * If the getattr succeeded, use its results.  Otherwise allow
	 * the previously set hardwired defaults to prevail.
	 */
	if (got_attrs == 0) {
		tp->tn_mode = rattr.va_mode;
		tp->tn_uid = rattr.va_uid;
		tp->tn_gid = rattr.va_gid;
	}

	/*
	 * initialize linked list of tmpnodes so that the back pointer of
	 * the root tmpnode always points to the last one on the list
	 * and the forward pointer of the last node is null
	 */
	tp->tn_back = tp;
	tp->tn_forw = NULL;
	tp->tn_nlink = 0;
	tm->tm_rootnode = tp;
	tm->tm_next = NULL;

	if (error = tdirinit(tm, tp, tp, cr)) {
		rw_exit(&tp->tn_rwlock);
		tmpnode_rele(tp);
		goto err;
	}

	/*
	 * tm_anonmax is set according to the mount arguments
	 * if any.  Otherwise, it is set to a maximum value.
	 */
	if (targs.anonmax)
		tm->tm_anonmax = btopr(targs.anonmax);
	else
		tm->tm_anonmax = INT_MAX;

	TMP_PRINT(T_ALLOC, "tmp_mount: tm_anonmax %d pages\n",
	    tm->tm_anonmax, 0, 0, 0, 0);

	/*
	 * link the new tmpfs mount point
	 * into the list of all tmpfs mounts
	 */
	mutex_enter(&tmpfs_mutex);
	tm->tm_next = tmpfs_mountp;
	tmpfs_mountp = tm;
	mutex_exit(&tmpfs_mutex);
	rw_exit(&tp->tn_rwlock);

	pn_free(&dpn);
	return (0);
err:
	/*
	 * We had an error during the mount,
	 * so everything we've allocated must be freed.
	 */
	TMP_PRINT(T_DEBUG, "tmp_mount: error mounting %s\n",
	    dpn.pn_path, 0, 0, 0, 0);
	if (tm && tm->tm_inomap) {
		TMP_PRINT(T_ALLOC, "tmp_mount: freeing inomap %x %d\n",
		    tm->tm_inomap, sizeof (struct tmpimap), 0, 0, 0);
		tmp_memfree(tm, (char *)tm->tm_inomap,
		    (u_int)sizeof (struct tmpimap));
	}
	if (tm && tm->tm_mntpath != NULL) {
		TMP_PRINT(T_ALLOC, "tmp_mount: freeing mntpath %x %d\n",
		    tm->tm_mntpath, (u_int)dpn.pn_pathlen + 1, 0, 0, 0);
		tmp_memfree(tm, tm->tm_mntpath, (u_int)dpn.pn_pathlen + 1);
	}
	if (tm) {
		TMP_PRINT(T_ALLOC, "tmp_mount: freeing tmount\n",
		    0, 0, 0, 0, 0);
		kmem_free((char *)tm, sizeof (struct tmount));
		mutex_enter(&tmpfs_mutex);
		tmp_kmemspace -= sizeof (struct tmount);
		mutex_exit(&tmpfs_mutex);
	}
	module_keepcnt--;	/* allow module to be unloaded */
	pn_free(&dpn);
	return (error);
}

static int
tmp_unmount(struct vfs *vfsp, struct cred *cr)
{
	register struct tmount *tm = (struct tmount *)VFSTOTM(vfsp);
	register struct tmount **tmpp, *tmp;
	register struct tmpimap *tmapp0, *tmapp1;
	register struct tmpnode *tnp;
	kcondvar_t cv;

	TMP_PRINT(T_DEBUG, "tmp_unmount: tm %x %s\n",
	    tm, tm->tm_mntpath, 0, 0, 0);

	if (!suser(cr))
		return (EPERM);

	mutex_enter(&tm->tm_contents);

	/*
	 * Don't close down the tmpfs if there are opened files
	 * There should be only one file referenced (the rootnode)
	 * and only one reference to the vnode for that file.
	 */
	tnp = tm->tm_rootnode;
	if (tm->tm_filerefcnt > 1 || TNTOV(tnp)->v_count > 1) {
		mutex_exit(&tm->tm_contents);
		TMP_PRINT(T_DEBUG, "tmpfs_umount: %s still busy\n",
		    tm->tm_mntpath, 0, 0, 0, 0);
		return (EBUSY);
	}

	/*
	 * Remove from tmp mount list
	 */
	mutex_enter(&tmpfs_mutex);
	tmpp = &tmpfs_mountp;
	for (;;) {
		tmp = *tmpp;
		if (tmp == NULL) {
			cmn_err(CE_WARN,
			    "tmp_umount: Couldn't find %s in mount list\n",
			    tm->tm_mntpath);
			break;
		}
		if (tmp == tm) {
			*tmpp = tm->tm_next;
			break;
		}
		tmpp = &tmp->tm_next;
	}
	CLEARBIT(tmp_mdevmap, (u_long)minor(tm->tm_dev));
	mutex_exit(&tmpfs_mutex);

	/*
	 * We can drop the mutex now because no one can find this mount
	 */
	mutex_exit(&tm->tm_contents);

	/*
	 * Free all kmemalloc'd and anonalloc'd memory associated with
	 * this filesystem.  To do this, we go through the file list twice,
	 * once to remove all the directory entries, and then to remove
	 * all the files.  We do this because there is useful code in
	 * tmpnode_free which assumes that the directory entry has been
	 * removed before the file.
	 */
	/*
	 * Remove all directory entries
	 */
	for (tnp = tm->tm_rootnode; tnp; tnp = tnp->tn_forw) {
		rw_enter(&tnp->tn_rwlock, RW_WRITER);
		if (tnp->tn_type == VDIR)
			tdirtrunc(tm, tnp, cr);
		rw_exit(&tnp->tn_rwlock);
	}

	ASSERT(tm->tm_direntries == 0);
	ASSERT(tm->tm_rootnode);

	/*
	 * We re-acquire the lock to prevent others who have a HOLD on
	 * a tmpnode via its pages or anon slots from blowing it away
	 * (in tmp_inactive) while we're trying to get to it here. Once
	 * we have a HOLD on it we know it'll stick around.
	 */
	mutex_enter(&tm->tm_contents);
	/*
	 * Remove all the files backwards (ending with rootnode).
	 */
	while ((tnp = tm->tm_rootnode) != NULL) {
		tnp = tnp->tn_back;
		/*
		 * Blow the tmpnode away by HOLDing it and RELE'ing it.
		 * The RELE calls inactive and blows it away because there
		 * we have the last HOLD. The root tmpnode always has one
		 * HOLD so we don't have to do an explicit one.
		 */
		if (tnp != tm->tm_rootnode)
			VN_HOLD(TNTOV(tnp));
		mutex_exit(&tm->tm_contents);
		VN_RELE(TNTOV(tnp));
		mutex_enter(&tm->tm_contents);
		/*
		 * It's still there after the RELE. Someone else like pageout
		 * has a hold on it so wait a bit and then try again - we know
		 * they'll give it up soon.
		 */
		if (tm->tm_rootnode && tnp == tm->tm_rootnode->tn_back) {
			extern clock_t lbolt;

			cv_init(&cv, "", CV_DEFAULT, (void *)NULL);
			(void) cv_timedwait(&cv, &tm->tm_contents, lbolt + 25);
			cv_destroy(&cv);
		}
	}
	mutex_exit(&tm->tm_contents);

	ASSERT(tm->tm_directories == 0);
	ASSERT(tm->tm_files == 0);
	ASSERT(tm->tm_filerefcnt == 0);

	/*
	 * Free the inode maps
	 */
	tmapp0 = tm->tm_inomap;
	while (tmapp0 != NULL) {
		tmapp1 = tmapp0->timap_next;
		TMP_PRINT(T_ALLOC, "tmp_umount: freeing inomap %x %d\n",
		    tmapp0, sizeof (struct tmpimap), 0, 0, 0);
		tmp_memfree(tm, (char *)tmapp0, sizeof (struct tmpimap));
		tmapp0 = tmapp1;
	}
	ASSERT(tm->tm_mntpath);
	TMP_PRINT(T_ALLOC, "tmp_umount: freeing mntpath %x %d\n",
	    tm->tm_mntpath, strlen(tm->tm_mntpath) + 1, 0, 0, 0);

	tmp_memfree(tm, tm->tm_mntpath, (u_int)strlen(tm->tm_mntpath) + 1);

	ASSERT(tm->tm_anonmem == 0);
	ASSERT(tm->tm_kmemspace == 0);

	mutex_destroy(&tm->tm_contents);
	mutex_destroy(&tm->tm_renamelck);
	TMP_PRINT(T_ALLOC, "tmp_umount: freeing tmount\n", 0, 0, 0, 0, 0);
	kmem_free((char *)tm, sizeof (struct tmount));
	mutex_enter(&tmpfs_mutex);
	tmp_kmemspace -= sizeof (struct tmount);
	mutex_exit(&tmpfs_mutex);

	module_keepcnt--;
	return (0);
}

/*
 * return root tmpnode for given vnode
 */
static int
tmp_root(struct vfs *vfsp, struct vnode **vpp)
{
	struct tmount *tm = (struct tmount *)VFSTOTM(vfsp);
	struct tmpnode *tp = tm->tm_rootnode;
	struct vnode *vp;

	ASSERT(tp);

	vp = TNTOV(tp);
	VN_HOLD(vp);
	*vpp = vp;
	return (0);
}

static int
tmp_statvfs(struct vfs *vfsp, struct statvfs *sbp)
{
	struct tmount	*tm = (struct tmount *)VFSTOTM(vfsp);
	int		blocks;

	sbp->f_bsize = PAGESIZE;
	sbp->f_frsize = PAGESIZE;

	/*
	 * Find the amount of available physical swap
	 */
	mutex_enter(&anoninfo_lock);
	blocks = MAX((int)anoninfo.ani_max - (int)anoninfo.ani_resv, 0);
	mutex_exit(&anoninfo_lock);

	/*
	 * Add the amount of available memory swap
	 */
	mutex_enter(&freemem_lock);
	blocks += MAX(availrmem - swapfs_minfree, 0);
	mutex_exit(&freemem_lock);

	/*
	 * If tm_anonmax for this mount is less than the available swap space
	 * (minus the amount tmpfs can't use), use that instead
	 */
	sbp->f_bfree = MIN(MAX(blocks - (int)tmpfs_minfree, 0),
	    (int)(tm->tm_anonmax - tm->tm_anonmem));
	sbp->f_bavail = sbp->f_bfree;

	/*
	 * Total number of blocks is what's available plus what's been used
	 */
	sbp->f_blocks = sbp->f_bfree + tm->tm_anonmem;

	/*
	 * The maximum number of files available is approximately the number
	 * of tmpnodes we can allocate from the remaining kernel memory
	 * available to tmpfs.  This is fairly inaccurate since it doesn't
	 * take into account the names stored in the directory entries.
	 */
	sbp->f_ffree = MAX(((int)tmpfs_maxkmem - (int)tmp_kmemspace) /
	    (int)(sizeof (struct tmpnode) + sizeof (struct tdirent)), 0);
	sbp->f_files = sbp->f_ffree + tmp_files;
	sbp->f_favail = sbp->f_ffree;

	sbp->f_fsid = vfsp->vfs_dev;
	strcpy(sbp->f_basetype, vfssw[tmpfsfstype].vsw_name);
	strcpy(sbp->f_fstr, tm->tm_mntpath);
	sbp->f_flag = vf_to_stf(vfsp->vfs_flag);
	sbp->f_namemax = MAXNAMELEN;
	return (0);
}

/*
 * Don't need to sync anonymous pages
 * tmp_sync(struct vfs *vfsp, short flag, struct cred *cr)
 */
/*ARGSUSED*/
static int
tmp_sync(struct vfs *vfsp, short flag, struct cred *cr)
{
	return (0);
}


static int
tmp_vget(struct vfs *vfsp, struct vnode **vpp, struct fid *fidp)
{
	register struct tfid *tfid;
	register struct tmount *tm = (struct tmount *)VFSTOTM(vfsp);
	register struct tmpnode *tp = NULL;

	tfid = (struct tfid *)fidp;
	*vpp = NULL;

	mutex_enter(&tm->tm_contents);
	for (tp = tm->tm_rootnode; tp; tp = tp->tn_forw) {
		mutex_enter(&tp->tn_tlock);
		if (tp->tn_nodeid == tfid->tfid_ino) {
			/*
			 * If the gen numbers don't match we know the
			 * file won't be found since only one tmpnode
			 * can have this number at a time.
			 */
			if (tp->tn_gen != tfid->tfid_gen || tp->tn_nlink == 0) {
				mutex_exit(&tp->tn_tlock);
				mutex_exit(&tm->tm_contents);
				return (0);
			}
			*vpp = (struct vnode *)TNTOV(tp);

			/*
			 * XXX - Inline tmpnode_hold.
			 */
			VN_HOLD(*vpp);
			if ((tp->tn_flags & TREF) == 0) {
				tp->tn_flags |= TREF;
				tm->tm_filerefcnt++;
			}

			if ((tp->tn_mode & S_ISVTX) &&
			    !(tp->tn_mode & (S_IXUSR | S_IFDIR))) {
				mutex_enter(&(*vpp)->v_lock);
				(*vpp)->v_flag |= VISSWAP;
				mutex_exit(&(*vpp)->v_lock);
			}
			mutex_exit(&tp->tn_tlock);
			mutex_exit(&tm->tm_contents);
			return (0);
		}
		mutex_exit(&tp->tn_tlock);
	}
	mutex_exit(&tm->tm_contents);
	return (0);
}

/*ARGSUSED*/
static int
tmp_mountroot(struct vfs *vfsp, enum whymountroot why)
{
	return (ENOSYS);
}

/*ARGSUSED*/
static int
tmp_swapvp(struct vfs *vfsp, struct vnode **vpp, char *nm)
{
	return (ENOSYS);
}
