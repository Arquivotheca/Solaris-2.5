/*
 * Copyright (c) 1987, 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)lofs_vnops.c	1.21	94/11/14 SMI"	/* SunOS-4.1.1 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/pathname.h>
#include <sys/debug.h>
#include <sys/fs/lofs_node.h>
#include <sys/fs/lofs_info.h>
#include <fs/fs_subr.h>
#include <vm/as.h>
#include <vm/seg.h>

extern vnode_t *makelonode(vnode_t *, struct loinfo *);
extern void freelonode(lnode_t *);

extern struct vnodeops lo_vnodeops;
#ifdef	LODEBUG
int lodebug;
#endif	LODEBUG

/*
 * These are the vnode ops routines which implement the vnode interface to
 * the looped-back file system.  These routines just take their parameters,
 * and then calling the appropriate real vnode routine(s) to do the work.
 */

static int
lo_open(register vnode_t **vpp,
	int flag,
	struct cred *cr)
{
	vnode_t *vp = *vpp;
	vnode_t *rvp;
	int error;

#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_open vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = rvp = realvp(vp);
	error = VOP_OPEN(&rvp, flag, cr);

	/*
	 * XXX: does this compute???
	 * XXX: what about reference counts for realvp, etc.
	 */
	if (!error && rvp != vp)
		*vpp = rvp;

	return (error);
}

static int
lo_close(vnode_t *vp,
	int flag,
	int count,
	offset_t offset,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_close vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_CLOSE(vp, flag, count, offset, cr));
}

static int
lo_read(vnode_t *vp,
	struct uio *uiop,
	int ioflag,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_read vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_READ(vp, uiop, ioflag, cr));
}

static int
lo_write(vnode_t *vp,
	struct uio *uiop,
	int ioflag,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_write vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_WRITE(vp, uiop, ioflag, cr));
}

static int
lo_ioctl(vnode_t *vp,
	int cmd,
	int arg,
	int flag,
	struct cred *cr,
	int *rvalp)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_ioctl vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_IOCTL(vp, cmd, arg, flag, cr, rvalp));
}

static int
lo_setfl(vnode_t *vp,
	int oflags,
	int nflags,
	cred_t *cr)
{
	vp = realvp(vp);
	return (VOP_SETFL(vp, oflags, nflags, cr));
}

static int
lo_getattr(vnode_t *vp,
	struct vattr *vap,
	int flags,
	struct cred *cr)
{
	vnode_t *xvp;
	int error;
	struct loinfo *lop;

#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_getattr vp %x realvp %x\n", vp, realvp(vp));
#endif
	/*
	 * If we are at the root of a mounted lofs filesystem
	 * and the underlying mount point is within the same
	 * filesystem, then return the attributes of the
	 * underlying mount point rather than the attributes
	 * of the mounted directory.  This prevents /bin/pwd
	 * and the C library function getcwd() from getting
	 * confused and returning failures.
	 */
	lop = (struct loinfo *)(vp->v_vfsp->vfs_data);
	if ((vp->v_flag & VROOT) &&
	    (xvp = vp->v_vfsp->vfs_vnodecovered) != NULL &&
	    vp->v_vfsp->vfs_dev == xvp->v_vfsp->vfs_dev)
		vp = xvp;
	else
		vp = realvp(vp);
	if (!(error = VOP_GETATTR(vp, vap, flags, cr))) {
		/*
		 * report lofs rdev instead of real vp's
		 */
		vap->va_rdev = lop->li_rdev;
	}
	return (error);
}

static int
lo_setattr(vnode_t *vp,
	struct vattr *vap,
	int flags,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_setattr vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_SETATTR(vp, vap, flags, cr));
}

static int
lo_access(vnode_t *vp,
	int mode,
	int flags,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_access vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_ACCESS(vp, mode, flags, cr));
}

static int
lo_fsync(vnode_t *vp,
	int syncflag,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_fsync vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_FSYNC(vp, syncflag, cr));
}

/*ARGSUSED*/
static void
lo_inactive(vnode_t *vp, struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_inactive %x, realvp %x\n", vp, realvp(vp));
#endif
	freelonode(vtol(vp));
}

/* ARGSUSED */
static int
lo_fid(vnode_t *vp, struct fid *fidp)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_fid %x, realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_FID(vp, fidp));
}

static int
lo_lookup(vnode_t *dvp,
	char *nm,
	vnode_t **vpp,
	struct pathname *pnp,
	int flags,
	vnode_t *rdir,
	struct cred *cr)
{
	vnode_t *vp = NULL, *tvp;
	int error;
	vnode_t *realdvp = realvp(dvp);
	struct vfs *vfsp;

	*vpp = NULL;	/* default(error) case */

	/*
	 * Handle ".." out of mounted filesystem
	 */
	while ((realdvp->v_flag & VROOT) && strcmp(nm, "..") == 0)
		realdvp = realdvp->v_vfsp->vfs_vnodecovered;


	/*
	 * Do the normal lookup
	 */
	if (error = VOP_LOOKUP(realdvp, nm, &vp, pnp, flags, rdir, cr))
		goto out;

	/*
	 * If this vnode is mounted on, then we
	 * transparently indirect to the vnode which
	 * is the root of the mounted file system.
	 * Before we do this we must check that an unmount is not
	 * in progress on this vnode. This maintains the fs status
	 * quo while a possibly lengthy unmount is going on.
	 */
	mutex_enter(&vp->v_lock);
	while ((vfsp = vp->v_vfsmountedhere) != 0) {
		/*
		 * Don't traverse a loopback mountpoint unless its
		 * going to the next higher depth
		 * This prevents loops to ourselves either directly or not

		 */
		if (vfsp->vfs_op == &lo_vfsops &&
		    vtoli(vfsp)->li_depth != vtoli(dvp->v_vfsp)->li_depth+1) {
			break;
		}
		if (vp->v_flag & VVFSLOCK) {
			vp->v_flag |= VVFSWAIT;
			if (!cv_wait_sig(&vp->v_cv, &vp->v_lock)) {
				mutex_exit(&vp->v_lock);
				return (EINTR);
			}
			continue;
		}
		error = VFS_ROOT(vfsp, &tvp);
		mutex_exit(&vp->v_lock);
		VN_RELE(vp);
		if (error)
			goto out;
		vp = tvp;
		mutex_enter(&vp->v_lock);
	}

	mutex_exit(&vp->v_lock);
	/*
	 * Now make our loop vnode for the real vnode
	 * But we only have to do it if the real vnode is a directory
	 * We can't do it for shared text without hacking on distpte
	 */
	if (vp->v_type == VDIR)
		*vpp = makelonode(vp, vtoli(dvp->v_vfsp));
	else
		*vpp = vp;

out:
#ifdef LODEBUG
	lo_dprint(lodebug, 4,
	"lo_lookup dvp %x realdvp %x nm '%s' newvp %x real vp %x error %d\n",
		dvp, realvp(dvp), nm, *vpp, vp, error);
#endif
	return (error);
}

/*ARGSUSED*/
static int
lo_create(vnode_t *dvp,
	char *nm,
	struct vattr *va,
	enum vcexcl exclusive,
	int mode,
	vnode_t **vpp,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_create vp %x realvp %x\n", dvp, realvp(dvp));
#endif
	dvp = realvp(dvp);
	return (VOP_CREATE(dvp, nm, va, exclusive, mode, vpp, cr));
}

static int
lo_remove(vnode_t *dvp,
	char *nm,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_remove vp %x realvp %x\n", dvp, realvp(dvp));
#endif
	dvp = realvp(dvp);
	return (VOP_REMOVE(dvp, nm, cr));
}

static int
lo_link(vnode_t *tdvp,
	vnode_t *vp,
	char *tnm,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_link vp %x realvp %x\n", vp, realvp(vp));
#endif
	while (vp->v_op == &lo_vnodeops)
		vp = realvp(vp);
	while (tdvp->v_op == &lo_vnodeops)
		tdvp = realvp(tdvp);
	if (vp->v_vfsp != tdvp->v_vfsp)
		return (EXDEV);
	return (VOP_LINK(tdvp, vp, tnm, cr));
}

static int
lo_rename(vnode_t *odvp,
	char *onm,
	vnode_t *ndvp,
	char *nnm,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4,
		"lo_rename vp %x realvp %x\n", odvp, realvp(odvp));
#endif
	while (odvp->v_op == &lo_vnodeops)
		odvp = realvp(odvp);
	while (ndvp->v_op == &lo_vnodeops)
		ndvp = realvp(ndvp);
	if (odvp->v_vfsp != ndvp->v_vfsp)
		return (EXDEV);
	return (VOP_RENAME(odvp, onm, ndvp, nnm, cr));
}

static int
lo_mkdir(vnode_t *dvp,
	char *nm,
	register struct vattr *va,
	vnode_t **vpp,
	struct cred *cr)
{
	vnode_t *vp;
	int error;

#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_mkdir vp %x realvp %x\n", dvp, realvp(dvp));
#endif
	error = VOP_MKDIR(realvp(dvp), nm, va, &vp, cr);
	if (!error)
		*vpp = makelonode(vp, vtoli(dvp->v_vfsp));
	return (error);
}

static int
lo_rmdir(vnode_t *dvp,
	char *nm,
	vnode_t *cdir,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_rmdir vp %x realvp %x\n", dvp, realvp(dvp));
#endif
	dvp = realvp(dvp);
	return (VOP_RMDIR(dvp, nm, cdir, cr));
}

static int
lo_symlink(vnode_t *dvp,
	char *lnm,
	struct vattr *tva,
	char *tnm,
	struct cred *cr)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_symlink vp %x realvp %x\n", dvp, realvp(dvp));
#endif
	dvp = realvp(dvp);
	return (VOP_SYMLINK(dvp, lnm, tva, tnm, cr));
}

static int
lo_readlink(vnode_t *vp,
	struct uio *uiop,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_READLINK(vp, uiop, cr));
}

static int
lo_readdir(vnode_t *vp,
	register struct uio *uiop,
	struct cred *cr,
	int *eofp)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_readdir vp %x realvp %x\n", vp, realvp(vp));
#endif
	vp = realvp(vp);
	return (VOP_READDIR(vp, uiop, cr, eofp));
}

static void
lo_rwlock(vnode_t *vp, int write_lock)
{
	vp = realvp(vp);
	VOP_RWLOCK(vp, write_lock);
}

static void
lo_rwunlock(vnode_t *vp, int write_lock)
{
	vp = realvp(vp);
	VOP_RWUNLOCK(vp, write_lock);
}

static int
lo_seek(vnode_t *vp, offset_t ooff, offset_t *noffp)
{
	vp = realvp(vp);
	return (VOP_SEEK(vp, ooff, noffp));
}

static int
lo_cmp(vnode_t *vp1, vnode_t *vp2)
{
	while (vp1->v_op == &lo_vnodeops)
		vp1 = realvp(vp1);
	while (vp2->v_op == &lo_vnodeops)
		vp2 = realvp(vp2);
	return (VOP_CMP(vp1, vp2));
}

static int
lo_frlock(vnode_t *vp,
	int cmd,
	struct flock *bfp,
	int flag,
	offset_t offset,
	cred_t *cr)
{
	vp = realvp(vp);
	return (VOP_FRLOCK(vp, cmd, bfp, flag, offset, cr));
}

static int
lo_space(vnode_t *vp,
	int cmd,
	struct flock *bfp,
	int flag,
	offset_t offset,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_SPACE(vp, cmd, bfp, flag, offset, cr));
}

static int
lo_realvp(vnode_t *vp, vnode_t **vpp)
{
#ifdef LODEBUG
	lo_dprint(lodebug, 4, "lo_realvp %x\n", vp);
#endif
	while (vp->v_op == &lo_vnodeops)
		vp = realvp(vp);

	if (VOP_REALVP(vp, vpp) != 0)
		*vpp = vp;
	return (0);
}

static int
lo_getpage(vnode_t *vp,
	offset_t off,
	u_int len,
	u_int *prot,
	struct page *parr[],
	u_int psz,
	struct seg *seg,
	caddr_t addr,
	enum seg_rw rw,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_GETPAGE(vp, off, len, prot, parr, psz, seg, addr, rw, cr));
}

static int
lo_putpage(vnode_t *vp,
	offset_t off,
	u_int len,
	int flags,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_PUTPAGE(vp, off, len, flags, cr));
}

static int
lo_map(vnode_t *vp,
	offset_t off,
	struct as *as,
	caddr_t *addrp,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_MAP(vp, off, as, addrp, len, prot, maxprot, flags, cr));
}

static int
lo_addmap(vnode_t *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_ADDMAP(vp, off, as, addr, len, prot, maxprot, flags, cr));
}

static int
lo_delmap(vnode_t *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	u_int len,
	u_int prot,
	u_int maxprot,
	u_int flags,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_DELMAP(vp, off, as, addr, len, prot, maxprot, flags, cr));
}

static int
lo_poll(vnode_t *vp,
	short events,
	int anyyet,
	short *reventsp,
	struct pollhead **phpp)
{
	vp = realvp(vp);
	return (VOP_POLL(vp, events, anyyet, reventsp, phpp));
}

static int
lo_dump(vnode_t *vp,
	caddr_t addr,
	int bn,
	int count)
{
	vp = realvp(vp);
	return (VOP_DUMP(vp, addr, bn, count));
}

static int
lo_pathconf(vnode_t *vp,
	int cmd,
	u_long *valp,
	struct cred *cr)
{
	vp = realvp(vp);
	return (VOP_PATHCONF(vp, cmd, valp, cr));
}

static int
lo_pageio(vnode_t *vp,
	struct page *pp,
	u_int io_off,
	u_int io_len,
	int flags,
	cred_t *cr)
{
	vp = realvp(vp);
	return (VOP_PAGEIO(vp, pp, io_off, io_len, flags, cr));
}

static void
lo_dispose(vnode_t *vp, page_t *pp, int fl, int dn, cred_t *cr)
{
	extern struct vnode kvp;

	vp = realvp(vp);
	if (vp != NULL && vp != &kvp)
		VOP_DISPOSE(vp, pp, fl, dn, cr);
}

/*
 * Loopback vnode operations vector.
 */
struct vnodeops lo_vnodeops = {
	lo_open,
	lo_close,
	lo_read,
	lo_write,
	lo_ioctl,
	lo_setfl,
	lo_getattr,
	lo_setattr,
	lo_access,
	lo_lookup,
	lo_create,
	lo_remove,
	lo_link,
	lo_rename,
	lo_mkdir,
	lo_rmdir,
	lo_readdir,
	lo_symlink,
	lo_readlink,
	lo_fsync,
	lo_inactive,
	lo_fid,
	lo_rwlock,
	lo_rwunlock,
	lo_seek,
	lo_cmp,
	lo_frlock,
	lo_space,
	lo_realvp,
	lo_getpage,
	lo_putpage,
	lo_map,
	lo_addmap,
	lo_delmap,
	lo_poll,
	lo_dump,
	lo_pathconf,
	lo_pageio,
	fs_nosys,	/* dumpctl */
	lo_dispose,
	fs_nosys,
	fs_fab_acl
};
