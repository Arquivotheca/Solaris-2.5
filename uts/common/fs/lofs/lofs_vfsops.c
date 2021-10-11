/*
 * Copyright (c) 1988, 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)lofs_vfsops.c	1.12	94/11/14 SMI"	/* SunOS-4.1.1 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/pathname.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/statvfs.h>
#include <sys/fs/lofs_info.h>
#include <sys/fs/lofs_node.h>
#include <sys/mount.h>
#include <sys/mkdev.h>
#include <sys/sysmacros.h>
#include "fs/fs_subr.h"

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct vfsops lo_vfsops;
extern int lofsinit(struct vfssw *, int);
extern void lofs_subrinit(void);
extern kmutex_t lofs_minor_lock;

static struct vfssw vfw = {
	"lofs",
	lofsinit,
	&lo_vfsops,
	0
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_fsops;

static struct modlfs modlfs = {
	&mod_fsops, "filesystem for lofs", &vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

/*
 * This is the module initialization routine.
 */

/*
 * Don't allow the lofs module to be unloaded for now.
 * There is a memory leak if it gets unloaded.
 */

static int module_keepcnt = 1;	/* ==0 means the module is unloadable */

_init()
{
	lofs_subrinit();
	return (mod_install(&modlinkage));
}

_fini()
{
	if (module_keepcnt != 0)
		return (EBUSY);

	return (mod_remove(&modlinkage));
}

_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

struct vnode *makelonode(struct vnode *, struct loinfo *);
extern struct vnodeops lo_vnodeops;
#ifdef	LODEBUG
extern int lodebug;
#endif	LODEBUG

extern int lofs_major;
extern int lofs_minor;

static int lofsfstype;

int
lofsinit(struct vfssw *vswp, int fstyp)
{
	vswp->vsw_vfsops = &lo_vfsops;
	lofsfstype = fstyp;

	return (0);
}

/*
 * lo mount vfsop
 * Set up mount info record and attach it to vfs struct.
 */
/*ARGSUSED*/
static int
lo_mount(struct vfs *vfsp,
	struct vnode *vp,
	struct mounta *uap,
	struct cred *cr)
{
	int error;
	struct vnode *srootvp = NULL;	/* the server's root */
	struct vnode *realrootvp;
	struct loinfo *li;
	dev_t lofs_rdev;

	module_keepcnt++;

	if (!suser(cr)) {
		module_keepcnt--;
		return (EPERM);
	}

	/*
	 * Find real root, and make vfs point to real vfs
	 */
	if (error = lookupname(uap->spec, UIO_USERSPACE, FOLLOW, NULLVPP,
	    &realrootvp)) {
		module_keepcnt--;
		return (error);
	}

	/*
	 * allocate a vfs info struct and attach it
	 */
	li = (struct loinfo *)kmem_zalloc(sizeof (*li), KM_SLEEP);
	li->li_realvfs = realrootvp->v_vfsp;
	li->li_mountvfs = vfsp;
	/*
	 * Set mount flags to be inherited by loopback vfs's
	 */
	if (uap->flags & MS_RDONLY)
		li->li_mflag |= VFS_RDONLY;
	if (uap->flags & MS_NOSUID)
		li->li_mflag |= VFS_NOSUID;

	/*
	 * Propagate inheritable mount flags from the real vfs.
	 */
	if (li->li_realvfs->vfs_flag & VFS_RDONLY)
		uap->flags |= MS_RDONLY;
	if (li->li_realvfs->vfs_flag & VFS_NOSUID)
		uap->flags |= MS_NOSUID;

	li->li_refct = 0;
	mutex_enter(&lofs_minor_lock);
	do {
		lofs_minor = (lofs_minor + 1) & MAXMIN;
		lofs_rdev = makedevice(lofs_major, lofs_minor);
	} while (vfs_devsearch(lofs_rdev));
	mutex_exit(&lofs_minor_lock);
	li->li_rdev = lofs_rdev;
	vfsp->vfs_data = (caddr_t)li;
	vfsp->vfs_bcount = 0;
	vfsp->vfs_fstype = lofsfstype;
	vfsp->vfs_bsize = li->li_realvfs->vfs_bsize;
	vfsp->vfs_dev = li->li_realvfs->vfs_dev;
	vfsp->vfs_fsid.val[0] = li->li_realvfs->vfs_fsid.val[0];
	vfsp->vfs_fsid.val[1] = li->li_realvfs->vfs_fsid.val[1];
	if (realrootvp->v_op == &lo_vnodeops) {
		li->li_depth = 1 + vtoli(realrootvp->v_vfsp)->li_depth;
	} else {
		li->li_depth = 0;
	}

	/*
	 * Make the root vnode
	 */
	srootvp = makelonode(realrootvp, li);
	if ((srootvp->v_flag & VROOT) &&
	    ((uap->flags & MS_OVERLAY) == 0)) {
		VN_RELE(srootvp);
		kmem_free(li, sizeof (*li));
		module_keepcnt--;
		return (EBUSY);
	}
	srootvp->v_flag |= VROOT;
	li->li_rootvp = srootvp;

#ifdef LODEBUG
	lo_dprint(lodebug, 4,
	    "lo_mount: vfs %x realvfs %x root %x realroot %x li %x\n",
	    vfsp, li->li_realvfs, srootvp, realrootvp, li);
#endif
	return (0);
}

/*
 * Undo loopback mount
 */
static int
lo_unmount(struct vfs *vfsp, struct cred *cr)
{
	struct loinfo *li;

	if (!suser(cr))
		return (EPERM);

	li = vtoli(vfsp);
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_unmount(%x) li %x\n", vfsp, li);
#endif
	if (li->li_refct != 1 || li->li_rootvp->v_count != 1) {
#ifdef LODEBUG
		lo_dprint(lodebug, 4, "refct %d v_ct %d\n", li->li_refct,
		    li->li_rootvp->v_count);
#endif
		return (EBUSY);
	}
	VN_RELE(li->li_rootvp);
	kmem_free(li, sizeof (*li));
	module_keepcnt--;
	return (0);
}

/*
 * find root of lo
 */
static int
lo_root(struct vfs *vfsp, struct vnode **vpp)
{
	*vpp = (struct vnode *)vtoli(vfsp)->li_rootvp;
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_root(0x%x) = %x\n", vfsp, *vpp);
#endif
	VN_HOLD(*vpp);
	return (0);
}

/*
 * Get file system statistics.
 */
static int
lo_statvfs(register struct vfs *vfsp, struct statvfs *sbp)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lostatvfs %x\n", vfsp);
#endif
	return (VFS_STATVFS(lo_realvfs(vfsp), sbp));
}

/*
 * Flush any pending I/O.
 */
static int
lo_sync(struct vfs *vfsp,
	short flag,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_sync: %x\n", vfsp);
#endif
	if (vfsp != (struct vfs *)NULL)
		return (VFS_SYNC(lo_realvfs(vfsp), flag, cr));
	return (0);
}

/*
 * lo vfs operations vector.
 */
struct vfsops lo_vfsops = {
	lo_mount,
	lo_unmount,
	lo_root,
	lo_statvfs,
	lo_sync,
	fs_nosys,	/* vget */
	fs_nosys,	/* mountroot */
	fs_nosys	/* swapvp */
};
