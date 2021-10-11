/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)prvfsops.c	1.27	95/03/24 SMI"	/* SVr4.0 1.25	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/mode.h>

#include <sys/signal.h>
#include <sys/user.h>
#include <sys/mount.h>

#include <fs/fs_subr.h>

#include <fs/proc/prdata.h>

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct vfsops prvfsops;
extern int prinit();

static struct vfssw vfw = {
	"proc",
	prinit,
	&prvfsops,
	0
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_fsops;

static struct modlfs modlfs = {
	&mod_fsops, "filesystem for proc", &vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

_init()
{
	return (mod_install(&modlinkage));
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * N.B.
 * No _fini routine. The module cannot be unloaded once loaded.
 * The NO_UNLOAD_STUB in modstubs.s must change if this module
 * is ever modified to become unloadable.
 */

int procfstype = 0;
int prmounted = 0;		/* Set to 1 if /proc is mounted. */
struct vfs *procvfs;		/* Points to /proc vfs entry. */
dev_t procdev;

/*
 * /proc VFS operations vector.
 */
static int	prmount(), prunmount(), prroot(), prstatvfs();

struct vfsops prvfsops = {
	prmount,
	prunmount,
	prroot,
	prstatvfs,
	fs_sync,
	fs_nosys,	/* vget */
	fs_nosys,	/* mountroot */
	fs_nosys	/* swapvp */
};

/* ARGSUSED */
static int
prmount(vfsp, mvp, uap, cr)
	struct vfs *vfsp;
	struct vnode *mvp;
	struct mounta *uap;
	struct cred *cr;
{
	register struct vnode *vp;
	register struct prnode *pnp;
	register int error;

	if (!suser(cr)) {
		error = EPERM;
		goto err;
	}
	if (mvp->v_type != VDIR) {
		error = ENOTDIR;
		goto err;
	}
	mutex_enter(&pr_mount_lock);
	mutex_enter(&mvp->v_lock);
	if ((uap->flags & MS_OVERLAY) == 0 &&
	    (mvp->v_count > 1 || (mvp->v_flag & VROOT))) {
		mutex_exit(&mvp->v_lock);
		mutex_exit(&pr_mount_lock);
		error = EBUSY;
		goto err;
	}
	mutex_exit(&mvp->v_lock);
	/*
	 * Prevent duplicate mount.
	 */
	if (prmounted) {
		mutex_exit(&pr_mount_lock);
		error = EBUSY;
		goto err;
	}
	pnp = &prrootnode;
	bzero((caddr_t)pnp, sizeof (*pnp));
	vp = &pnp->pr_vnode;
	vp->v_vfsp = vfsp;
	vp->v_op = &prvnodeops;
	vp->v_count = 1;
	vp->v_type = VDIR;
	vp->v_data = (caddr_t) pnp;
	vp->v_flag |= VROOT;
	pnp->pr_mode = 0555;	/* read and search permissions */
	vfsp->vfs_fstype = procfstype;
	vfsp->vfs_data = NULL;
	vfsp->vfs_dev = procdev;
	vfsp->vfs_fsid.val[0] = procdev;
	vfsp->vfs_fsid.val[1] = procfstype;
	vfsp->vfs_bsize = 1024;
	procvfs = vfsp;
	prmounted = 1;
	mutex_exit(&pr_mount_lock);
	return (0);

err:
	return (error);
}

/* ARGSUSED */
static int
prunmount(vfsp, cr)
	struct vfs *vfsp;
	struct cred *cr;
{
	register proc_t *p;
	register struct vnode *vp = &prrootnode.pr_vnode;

	if (!suser(cr))
		return (EPERM);

	/*
	 * Ensure that no /proc vnodes are in use.
	 * Lock pidlock while we do this to keep the practive list stable.
	 * pidlock is also used to ensure that no new /proc vnodes are
	 * allocated in prlookup()/prget() after prmounted is set to 0.
	 * Individual p->p_lock's protect the p->p_plist's.
	 */
	mutex_enter(&vp->v_lock);
	mutex_enter(&pidlock);		/* protect practive */

	if (vp->v_count > 1)
		goto busy;

	for (p = practive; p != NULL; p = p->p_next) {
		mutex_enter(&p->p_lock);
		if (p->p_plist != NULL) {
			mutex_exit(&p->p_lock);
			goto busy;
		}
		mutex_exit(&p->p_lock);
	}

	prmounted = 0;
	procvfs = NULL;

	mutex_exit(&pidlock);
	mutex_exit(&vp->v_lock);
	VN_RELE(vp);
	return (0);

busy:
	mutex_exit(&pidlock);
	mutex_exit(&vp->v_lock);
	return (EBUSY);
}

/* ARGSUSED */
static int
prroot(vfsp, vpp)
	struct vfs *vfsp;
	struct vnode **vpp;
{
	struct vnode *vp = &prrootnode.pr_vnode;

	VN_HOLD(vp);
	*vpp = vp;
	return (0);
}

static int
prstatvfs(vfsp, sp)
	struct vfs *vfsp;
	register struct statvfs *sp;
{
	register int i, n;

	mutex_enter(&pidlock);
	for (n = v.v_proc, i = 0; i < v.v_proc; i++)
		if (pid_entry(i) != NULL)
			n--;
	mutex_exit(&pidlock);

	bzero((caddr_t)sp, sizeof (*sp));
	sp->f_bsize	= 1024;
	sp->f_frsize	= 1024;
	sp->f_blocks	= 0;
	sp->f_bfree	= 0;
	sp->f_bavail	= 0;
	sp->f_files	= v.v_proc + 2;
	sp->f_ffree	= n;
	sp->f_favail	= n;
	sp->f_fsid	= vfsp->vfs_dev;
	strcpy(sp->f_basetype, vfssw[procfstype].vsw_name);
	sp->f_flag = vf_to_stf(vfsp->vfs_flag);
	sp->f_namemax = PNSIZ;
	strcpy(sp->f_fstr, "/proc");
	strcpy(&sp->f_fstr[6], "/proc");
	return (0);
}
