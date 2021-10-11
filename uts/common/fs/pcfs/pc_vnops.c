/*
 * Copyright (c) 1990, 1992-1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)pc_vnops.c 1.33     95/07/26 SMI"

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/dirent.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include <sys/mman.h>
#include <sys/pathname.h>
#include <sys/vmsystm.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/statvfs.h>
#include <sys/unistd.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/flock.h>

#include <vm/seg.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>

#include <fs/fs_subr.h>

static int pcfs_open(struct vnode **, int, struct cred *);
static int pcfs_close(struct vnode *, int, int, offset_t, struct cred *);
static int pcfs_read(struct vnode *, struct uio *, int, struct cred *);
static int pcfs_write(struct vnode *, struct uio *, int, struct cred *);
static int pcfs_getattr(struct vnode *, struct vattr *, int, struct cred *);
static int pcfs_setattr(struct vnode *, struct vattr *, int, struct cred *);
static int pcfs_access(struct vnode *, int, int, struct cred *);
static int pcfs_lookup(struct vnode *, char *, struct vnode **,
	struct pathname *, int, struct vnode *, struct cred *);
static int pcfs_create(struct vnode *, char *, struct vattr *,
	enum vcexcl, int mode, struct vnode **, struct cred *);
static int pcfs_remove(struct vnode *, char *, struct cred *);
static int pcfs_rename(struct vnode *, char *, struct vnode *, char *,
	struct cred *);
static int pcfs_mkdir(struct vnode *, char *, struct vattr *, struct vnode **,
	struct cred *);
static int pcfs_rmdir(struct vnode *, char *, struct vnode *, struct cred *);
static int pcfs_readdir(struct vnode *, struct uio *, struct cred *, int *);
static int pcfs_fsync(struct vnode *, int, struct cred *);
static void pcfs_inactive(struct vnode *, struct cred *);
static int pcfs_space(struct vnode *, int, struct flock *, int,
	offset_t, struct cred *);
static int pcfs_getpage(struct vnode *, offset_t, u_int, u_int *, page_t *[],
	u_int, struct seg *, caddr_t, enum seg_rw, struct cred *);
static int pcfs_getapage(struct vnode *, u_int, u_int, u_int *, page_t *[],
	u_int, struct seg *, caddr_t, enum seg_rw, struct cred *);
static int pcfs_putpage(struct vnode *, offset_t, u_int, int, struct cred *);
static int pcfs_map(struct vnode *, offset_t, struct as *, caddr_t *, u_int,
	u_char, u_char, u_int, struct cred *);
static int pcfs_addmap(struct vnode *, offset_t, struct as *, caddr_t,
	uint, u_char, u_char, uint, struct cred *);
static int pcfs_delmap(struct vnode *, offset_t, struct as *, caddr_t,
	u_int, u_int, u_int, u_int, struct cred *);
static int pcfs_seek(struct vnode *, offset_t, offset_t *);
static int pcfs_pathconf(struct vnode *, int, u_long *, struct cred *);

int pcfs_putapage(struct vnode *, page_t *, u_int *, u_int *, int,
	struct cred *);
static int rwpcp(struct pcnode *, struct uio *, enum uio_rw, int);

extern krwlock_t pcnodes_lock;

/*
 * vnode op vectors for files, directories, and invalid files.
 */
struct vnodeops pcfs_fvnodeops = {
	pcfs_open,
	pcfs_close,
	pcfs_read,
	pcfs_write,
	fs_nosys,
	fs_setfl,
	pcfs_getattr,
	pcfs_setattr,
	pcfs_access,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	pcfs_fsync,
	pcfs_inactive,
	fs_nosys,		/* VOP_FID */
	fs_rwlock,
	fs_rwunlock,
	pcfs_seek,
	fs_cmp,
	fs_nosys,
	pcfs_space,
	fs_nosys,
	pcfs_getpage,
	pcfs_putpage,
	pcfs_map,
	pcfs_addmap,
	pcfs_delmap,
	fs_poll,
	fs_nosys,		/* dump */
	pcfs_pathconf,
	fs_nosys,		/* pageio */
	fs_nosys,		/* dumpctl */
	fs_dispose,
	fs_nosys,		/* setsecattr */
	fs_fab_acl		/* getsecattr */
};

struct vnodeops pcfs_dvnodeops = {
	pcfs_open,
	pcfs_close,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_setfl,
	pcfs_getattr,
	fs_nosys,	/* can't we support this op? one problem is to */
	pcfs_access,  /* make attr of '.', '..' and the real dir the same. */
	pcfs_lookup,
	pcfs_create,
	pcfs_remove,
	fs_nosys,
	pcfs_rename,
	pcfs_mkdir,
	pcfs_rmdir,
	pcfs_readdir,
	fs_nosys,
	fs_nosys,
	pcfs_fsync,
	pcfs_inactive,
	fs_nosys,		/* VOP_FID */
	fs_rwlock,
	fs_rwunlock,
	pcfs_seek,
	fs_cmp,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys,
	fs_nosys_map,
	fs_nosys_addmap,
	fs_nosys,
	fs_poll,
	fs_nosys,		/* dump */
	pcfs_pathconf,
	fs_nosys,		/* pageio */
	fs_nosys,		/* dumpctl */
	fs_dispose,
	fs_nosys,		/* setsecattr */
	fs_fab_acl		/* getsecattr */
};


/*ARGSUSED*/
static int
pcfs_open(
	struct vnode **vpp,
	int flag,
	struct cred *cr)
{
	return (0);
}

/*
 * files are sync'ed on close to keep floppy up to date
 */

/*ARGSUSED*/
static int
pcfs_close(
	struct vnode *vp,
	int flag,
	int count,
	offset_t offset,
	struct cred *cr)
{
	return (0);
}

/*ARGSUSED*/
static int
pcfs_read(
	struct vnode *vp,
	struct uio *uiop,
	int ioflag,
	struct cred *cr)
{
	register struct pcfs *fsp;
	register struct pcnode *pcp;
	int error;

	fsp = VFSTOPCFS(vp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((pcp = VTOPC(vp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	error = rwpcp(pcp, uiop, UIO_READ, ioflag);
	pc_unlockfs(fsp);
	if (error) {
PCFSDEBUG(1)
prom_printf("pcfs_read: io error = %d\n", error);
	}
	return (error);
}

/*ARGSUSED*/
static int
pcfs_write(
	struct vnode *vp,
	struct uio *uiop,
	int ioflag,
	struct cred *cr)
{
	register struct pcfs *fsp;
	register struct pcnode *pcp;
	int error;

	fsp = VFSTOPCFS(vp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((pcp = VTOPC(vp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	if (ioflag & FAPPEND) {
		/*
		 * in append mode start at end of file.
		 */
		uiop->uio_offset = pcp->pc_size;
	}
	error = rwpcp(pcp, uiop, UIO_WRITE, ioflag);
	pcp->pc_flags |= PC_MOD;
	pc_mark(pcp);
	if (ioflag & (FSYNC|FDSYNC))
		(void) pc_nodeupdate(pcp);

	pc_unlockfs(fsp);
	if (error) {
PCFSDEBUG(1)
prom_printf("pcfs_write: io error = %d\n", error);
	}
	return (error);
}

/*
 * read or write a vnode
 */
static int
rwpcp(
	register struct pcnode *pcp,
	register struct uio *uio,
	enum uio_rw rw,
	int ioflag)
{
	struct vnode *vp = PCTOV(pcp);
	struct pcfs *fsp;
	daddr_t bn;			/* phys block number */
	register int n;
	register u_int off;
	caddr_t base;
	int mapon, pagecreate;
	int error = 0;

PCFSDEBUG(5)
prom_printf("rwpcp pcp=%x off=%d resid=%d size=%d\n",
pcp, uio->uio_offset, uio->uio_resid, pcp->pc_size);

	ASSERT(rw == UIO_READ || rw == UIO_WRITE);
	ASSERT(vp->v_type == VREG);

	if (uio->uio_offset < 0 || (uio->uio_offset + uio->uio_resid) < 0)
		return (EINVAL);
	if (uio->uio_resid == 0)
		return (0);
	fsp = VFSTOPCFS(vp->v_vfsp);

	do {
		off = uio->uio_offset & MAXBMASK;
		mapon = uio->uio_offset & MAXBOFFSET;
		n = MIN(MAXBSIZE - mapon, uio->uio_resid);
		if (rw == UIO_READ) {
			int diff;

			diff = pcp->pc_size - uio->uio_offset;
			if (diff <= 0)
				return (0);
			if (diff < n)
				n = diff;
		}
		base = segmap_getmap(segkmap, vp, off);
		pagecreate = 0;
		if (rw == UIO_WRITE) {
			/*
			 * If PAGESIZE < MAXBSIZE, perhaps we ought to deal
			 * with one page at a time, instead of one MAXBSIZE
			 * at a time, so we can fully explore pagecreate
			 * optimization??
			 */
			if (uio->uio_offset + n > pcp->pc_size) {
				u_int ncl, lcn;

				ncl = howmany(pcp->pc_size, fsp->pcfs_clsize);
				if (uio->uio_offset > pcp->pc_size &&
				    ncl < howmany(uio->uio_offset,
				    fsp->pcfs_clsize)) {
					/*
					 * Allocate and zerofill skipped
					 * clusters. This may not be worth the
					 * effort since a small lseek beyond
					 * eof but still within the cluster
					 * will not be zeroed out.
					 */
					lcn = pc_lblkno(fsp, uio->uio_offset);
					error = pc_balloc(pcp, (daddr_t) lcn,
					    1, &bn);
					ncl = lcn + 1;
				}
				if (!error &&
				    ncl < howmany(uio->uio_offset + n,
				    fsp->pcfs_clsize))
					/*
					 * allocate clusters w/o zerofill
					 */
					error = pc_balloc(pcp,
					    (daddr_t) pc_lblkno(fsp,
					    uio->uio_offset + n - 1),
					    0, &bn);

				pcp->pc_flags |= PC_CHG;

				if (error) {
					/* figure out new file size */
					pcp->pc_size = fsp->pcfs_clsize *
					    pc_fileclsize(fsp,
						pcp->pc_scluster);

					if (error == ENOSPC &&
					    (pcp->pc_size - uio->uio_offset)
						> 0) {
PCFSDEBUG(2)
prom_printf("rwpcp ENOSPC off=%d n=%d size=%d\n",
uio->uio_offset, n, pcp->pc_size);
						n = pcp->pc_size -
						    uio->uio_offset;
					} else {
PCFSDEBUG(1)
prom_printf("rwpcp error1=%d\n", error);
						(void) segmap_release(segkmap,
						    base, 0);
						break;
					}
				} else {
					pcp->pc_size = uio->uio_offset + n;
				}
				if (mapon == 0) {
					segmap_pagecreate(segkmap, base,
						(u_int)n, 0);
					pagecreate = 1;
				}
			} else if (n == MAXBSIZE) {
				segmap_pagecreate(segkmap, base, (u_int)n, 0);
				pagecreate = 1;
			}
		}
		error = uiomove(base + mapon, (long)n, rw, uio);

		if (pagecreate && uio->uio_offset <
			roundup(off + mapon + n, PAGESIZE)) {
			int nzero, nmoved;

			nmoved = uio->uio_offset - (off + mapon);
			nzero = roundup(mapon + n, PAGESIZE) - nmoved;
			(void) kzero(base + mapon + nmoved, (u_int)nzero);
		}

		if (error) {
PCFSDEBUG(1)
prom_printf("rwpcp error2=%d\n", error);
			/*
			 * If we failed on a write, we may have already
			 * allocated file blocks as well as pages.  It's hard
			 * to undo the block allocation, but we must be sure
			 * to invalidate any pages that may have been
			 * allocated.
			 */
			if (rw == UIO_WRITE)
				(void) segmap_release(segkmap, base, SM_INVAL);
			else
				(void) segmap_release(segkmap, base, 0);
		} else {
			u_int flags = 0;

			if (rw == UIO_READ) {
				if (n + mapon == MAXBSIZE ||
				    uio->uio_offset == pcp->pc_size)
					flags = SM_DONTNEED;
			} else if (ioflag & (FSYNC|FDSYNC)) {
				flags = SM_WRITE;
			} else if (n + mapon == MAXBSIZE) {
				flags = SM_WRITE|SM_ASYNC|SM_DONTNEED;
			}
			error = segmap_release(segkmap, base, flags);
		}

	} while (error == 0 && uio->uio_resid > 0 && n != 0);

	return (error);
}

/*ARGSUSED*/
static int
pcfs_getattr(
	struct vnode *vp,
	register struct vattr *vap,
	int flags,
	struct cred *cr)
{
	register struct pcnode *pcp;
	register struct pcfs *fsp;
	int error;
	char attr;

PCFSDEBUG(8)
prom_printf("pcfs_getattr: vp=%x\n", vp);

	fsp = VFSTOPCFS(vp->v_vfsp);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((pcp = VTOPC(vp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	/*
	 * Copy from pcnode.
	 */
	vap->va_type = vp->v_type;
	attr = pcp->pc_entry.pcd_attr;
	if (attr & (PCA_HIDDEN|PCA_SYSTEM))
		vap->va_mode = 0;
	else if (attr & PCA_LABEL)
		vap->va_mode = 0444;
	else if (attr & PCA_RDONLY)
		vap->va_mode = 0555;
	else
		vap->va_mode = 0777;
	if (attr & PCA_DIR)
		vap->va_mode |= S_IFDIR;
	else
		vap->va_mode |= S_IFREG;
	vap->va_uid = cr->cr_uid;
	vap->va_gid = cr->cr_gid;
	vap->va_fsid = vp->v_vfsp->vfs_dev;
	vap->va_nodeid =
	    pc_makenodeid(pcp->pc_eblkno, pcp->pc_eoffset, &pcp->pc_entry,
		fsp->pcfs_entps);
	vap->va_nlink = 1;
	vap->va_size = pcp->pc_size;
	pc_pcttotv(&pcp->pc_entry.pcd_mtime, &vap->va_mtime);
	vap->va_ctime = vap->va_atime = vap->va_mtime;
	vap->va_rdev = 0;
	vap->va_nblocks = howmany(pcp->pc_size, DEV_BSIZE);
	vap->va_blksize = fsp->pcfs_clsize;
	pc_unlockfs(fsp);
	return (0);
}


/*ARGSUSED*/
static int
pcfs_setattr(
	struct vnode *vp,
	register struct vattr *vap,
	int flags,
	struct cred *cr)
{
	register struct pcnode *pcp;
	register long int mask = vap->va_mask;
	int error;

PCFSDEBUG(6)
prom_printf("pcfs_setattr: vp=%x mask=%x\n", vp, mask);
	/*
	 * cannot set these attributes
	 */
	if (mask & (AT_NOSET | AT_UID | AT_GID))
		return (EINVAL);

	error = pc_lockfs(VFSTOPCFS(vp->v_vfsp));
	if (error)
		return (error);
	if ((pcp = VTOPC(vp)) == NULL) {
		pc_unlockfs(VFSTOPCFS(vp -> v_vfsp));
		return (EIO);
	}
	/*
	 * Change file access modes.
	 * If nobody has write permision, file is marked readonly.
	 * Otherwise file is writeable by anyone.
	 */
	if ((mask & AT_MODE) && (vap->va_mode != (u_short)-1)) {
		if ((vap->va_mode & 0222) == 0)
			pcp->pc_entry.pcd_attr |= PCA_RDONLY;
		else
			pcp->pc_entry.pcd_attr &= ~PCA_RDONLY;
		pcp->pc_flags |= PC_CHG;
	}
	/*
	 * Truncate file. Must have write permission.
	 */
	if ((mask & AT_SIZE) && (vap->va_size != (u_long)-1)) {
		if (pcp->pc_entry.pcd_attr & PCA_RDONLY) {
			error = EACCES;
			goto out;
		}
		error = pc_truncate(pcp, (long)vap->va_size);
		if (error)
			goto out;
	}
	/*
	 * Change file modified times.
	 */
	if ((mask & (AT_MTIME | AT_CTIME)) && (vap->va_mtime.tv_sec != -1)) {
		/*
		 * If SysV-compatible option to set access and
		 * modified times if root, owner, or write access,
		 * use current time rather than va_mtime.
		 *
		 * XXX - va_mtime.tv_usec == -1 flags this.
		 */
		pc_tvtopct((vap->va_mtime.tv_sec == -1) ? &hrestime:
		    &vap->va_mtime, &pcp->pc_entry.pcd_mtime);
		pcp->pc_flags |= PC_CHG;
	}
out:
	pc_unlockfs(VFSTOPCFS(vp->v_vfsp));
	return (error);
}


/*ARGSUSED*/
static int
pcfs_access(
	struct vnode *vp,
	int mode,
	int flags,
	struct cred *cr)
{
	struct pcnode * pcp;

	if ((pcp = VTOPC(vp)) == NULL)
		return (EIO);
	if ((mode & VWRITE) && (pcp->pc_entry.pcd_attr & PCA_RDONLY))
		return (EACCES);
	else
		return (0);
}


/*ARGSUSED*/
static int
pcfs_fsync(
	struct vnode *vp,
	int syncflag,
	struct cred *cr)
{
	struct pcfs *fsp;
	struct pcnode * pcp;
	int error;

	fsp = VFSTOPCFS(vp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((pcp = VTOPC(vp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	rw_enter(&pcnodes_lock, RW_WRITER);
	error = pc_nodesync(pcp);
	rw_exit(&pcnodes_lock);
	pc_unlockfs(fsp);
	return (error);
}


/*ARGSUSED*/
static void
pcfs_inactive(
	struct vnode *vp,
	struct cred *cr)
{
	register struct pcnode *pcp;
	struct pcfs * fsp;

	fsp = VFSTOPCFS(vp->v_vfsp);
	(void) pc_lockfs(fsp);

	mutex_enter(&vp->v_lock);
	ASSERT(vp->v_count >= 1);
	if (vp->v_count > 1) {
		vp->v_count--;  /* release our hold from vn_rele */
		mutex_exit(&vp->v_lock);
		pc_unlockfs(fsp);
		return;
	}
	mutex_exit(&vp->v_lock);

	/*
	 * Check again to confirm that no intervening I/O error
	 * with a subsequent pc_diskchanged() call has released
	 * the pcnode.  If it has then release the vnode as above.
	 */
	if ((pcp = VTOPC(vp)) == NULL) {
		kmem_free((caddr_t) vp, sizeof (struct vnode));
	} else {
		(void) pc_rele(pcp);
	}
	pc_unlockfs(fsp);
}

/*ARGSUSED*/
static int
pcfs_lookup(
	struct vnode *dvp,
	char *nm,
	struct vnode **vpp,
	struct pathname *pnp,
	int flags,
	struct vnode *rdir,
	struct cred *cr)
{
	struct pcfs *fsp;
	struct pcnode *pcp;
	register int error;

	/*
	 * verify that the dvp is still valid on the disk
	 */
	fsp = VFSTOPCFS(dvp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if (VTOPC(dvp) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	/*
	 * Null component name is a synonym for directory being searched.
	 */
	if (*nm == '\0') {
		VN_HOLD(dvp);
		*vpp = dvp;
		pc_unlockfs(fsp);
		return (0);
	}

	error = pc_dirlook(VTOPC(dvp), nm, &pcp);
	if (!error) {
		*vpp = PCTOV(pcp);
		pcp->pc_flags |= PC_EXTERNAL;
	}
	pc_unlockfs(fsp);
	return (error);
}


/*ARGSUSED*/
static int
pcfs_create(
	struct vnode *dvp,
	char *nm,
	struct vattr *vap,
	enum vcexcl exclusive,
	int mode,
	struct vnode **vpp,
	struct cred *cr)
{
	register int error;
	struct pcnode *pcp;
	struct vnode * vp;

	/*
	 * can't create directories. use pcfs_mkdir.
	 */
	if (vap->va_type == VDIR)
		return (EISDIR);
	pcp = (struct pcnode *) 0;
	error = pc_lockfs(VFSTOPCFS(dvp->v_vfsp));
	if (error)
		return (error);
	if (VTOPC(dvp) == NULL) {
		pc_unlockfs(VFSTOPCFS(dvp -> v_vfsp));
		return (EIO);
	}
	if (*nm == '\0') {
		/*
		 * Null component name refers to the directory itself.
		 */
		VN_HOLD(dvp);
		pcp = VTOPC(dvp);
		error = EEXIST;
	} else {
		error = pc_direnter(VTOPC(dvp), nm, vap, &pcp);
	}
	/*
	 * if file exists and this is a nonexclusive create,
	 * check for access permissions
	 */
	if (error == EEXIST) {
		vp = PCTOV(pcp);
		if (exclusive == NONEXCL) {
			if (vp->v_type == VDIR) {
				error = EISDIR;
			} else if (mode) {
				error = pcfs_access(PCTOV(pcp), mode, 0,
					(struct cred *) 0);
			} else {
				error = 0;
			}
		}
		if (error) {
			VN_RELE(PCTOV(pcp));
		} else if ((vp->v_type == VREG) && (vap->va_mask & AT_SIZE) &&
			(vap->va_size == 0)) {
			error = pc_truncate(pcp, 0L);
			if (error)
				VN_RELE(PCTOV(pcp));
		}
	}
	if (error) {
		pc_unlockfs(VFSTOPCFS(dvp->v_vfsp));
		return (error);
	}
	*vpp = PCTOV(pcp);
	pcp->pc_flags |= PC_EXTERNAL;
	pc_unlockfs(VFSTOPCFS(dvp->v_vfsp));
	return (error);
}

/*ARGSUSED*/
static int
pcfs_remove(
	struct vnode *vp,
	char *nm,
	struct cred *cr)
{
	struct pcfs *fsp;
	struct pcnode * pcp;
	register int error;

	fsp = VFSTOPCFS(vp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((pcp = VTOPC(vp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	error = pc_dirremove(pcp, nm, (struct vnode *)0, VREG);
	pc_unlockfs(fsp);
	return (error);
}

/*
 * Rename a file or directory
 * This rename is restricted to only rename files within a directory.
 * XX should make rename more general
 */
/*ARGSUSED*/
static int
pcfs_rename(
	struct vnode *sdvp,		/* old (source) parent vnode */
	char *snm,			/* old (source) entry name */
	struct vnode *tdvp,		/* new (target) parent vnode */
	char *tnm,			/* new (target) entry name */
	struct cred *cr)
{
	struct pcfs *fsp;
	register struct pcnode *dp;	/* parent pcnode */
	register int error;

	fsp = VFSTOPCFS(sdvp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	if (((dp = VTOPC(sdvp)) == NULL) || (VTOPC(tdvp) == NULL)) {
		return (EIO);
	}
	/*
	 * make sure source and target directories are the same
	 */
	if (dp->pc_scluster != VTOPC(tdvp)->pc_scluster)
		return (EXDEV);		/* XXX */
	/*
	 * make sure we can muck with this directory.
	 */
	error = pcfs_access(sdvp, VWRITE, 0, (struct cred *) 0);
	if (error) {
		return (error);
	}
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((VTOPC(sdvp) == NULL) || (VTOPC(tdvp) == NULL)) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	error = pc_rename(dp, snm, tnm);
	pc_unlockfs(fsp);
	return (error);
}

/*ARGSUSED*/
static int
pcfs_mkdir(
	struct vnode *dvp,
	char *nm,
	register struct vattr *vap,
	struct vnode **vpp,
	struct cred *cr)
{
	struct pcfs *fsp;
	struct pcnode *pcp;
	register int error;

	fsp = VFSTOPCFS(dvp->v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if (VTOPC(dvp) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	error = pc_direnter(VTOPC(dvp), nm, vap, &pcp);

	if (!error) {
		pcp -> pc_flags |= PC_EXTERNAL;
		*vpp = PCTOV(pcp);
	} else if (error == EEXIST) {
		VN_RELE(PCTOV(pcp));
	}
	pc_unlockfs(fsp);
	return (error);
}

/*ARGSUSED*/
static int
pcfs_rmdir(
	struct vnode *dvp,
	char *nm,
	struct vnode *cdir,
	struct cred *cr)
{
	struct pcfs * fsp;
	struct pcnode * pcp;
	register int error;

	fsp = VFSTOPCFS(dvp -> v_vfsp);
	if (error = pc_verify(fsp))
		return (error);
	if (error = pc_lockfs(fsp))
		return (error);

	if ((pcp = VTOPC(dvp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	error = pc_dirremove(pcp, nm, cdir, VDIR);
	pc_unlockfs(fsp);
	return (error);
}

struct pc_dirent {
	ino_t		d_ino;		/* "inode number" of entry */
	off_t		d_off;		/* offset of disk directory entry */
	unsigned short	d_reclen;	/* length of this record */
	char		d_name[PCFNAMESIZE+PCFEXTSIZE+2]; /* name of file */
};

/*
 * read entries in a directory.
 * we must convert pc format to unix format
 */

/*ARGSUSED*/
static int
pcfs_readdir(
	struct vnode *dvp,
	register struct uio *uiop,
	struct cred *cr,
	int *eofp)
{
	register char *fp, *tp;
	register struct pcnode *pcp;
	register struct pcfs *fsp;
	struct pcdir *ep;
	struct buf *bp = NULL;
	register char c;
	register int i;
	int offset;
	register int n;
	register int boff;
	struct pc_dirent d;
	int error;

#define	PC_DIRENTSIZ sizeof (struct pc_dirent)

	if ((uiop->uio_iovcnt != 1) || (uiop->uio_offset % PC_DIRENTSIZ))
		return (EINVAL);
	fsp = VFSTOPCFS(dvp->v_vfsp);
	/*
	 * verify that the dp is still valid on the disk
	 */
	if (error = pc_verify(fsp))
		return (error);
	error = pc_lockfs(fsp);
	if (error)
		return (error);
	if ((pcp = VTOPC(dvp)) == NULL) {
		pc_unlockfs(fsp);
		return (EIO);
	}
	n = uiop->uio_resid / PC_DIRENTSIZ;
	offset = (uiop->uio_offset / PC_DIRENTSIZ) * sizeof (struct pcdir);
	d.d_reclen = PC_DIRENTSIZ;

PCFSDEBUG(5)
prom_printf("pcfs_readdir: resid=%d offset=%d n=%d\n", uiop->uio_resid,
offset, n);
	if (dvp->v_flag & VROOT) {
		/*
		 * kludge up entries for "." and ".." in the root.
		 */
		if (offset == 0 && n) {
			d.d_off = uiop->uio_offset + d.d_reclen;
			d.d_ino = (ino_t) -1;
			strcpy(d.d_name, ".");
			(void) uiomove((caddr_t)&d,
			    PC_DIRENTSIZ, UIO_READ, uiop);
			offset = sizeof (struct pcdir);
			n--;
		}
		if (offset == sizeof (struct pcdir) && n) {
			d.d_off = uiop->uio_offset + d.d_reclen;
			d.d_ino = (ino_t) -1;
			strcpy(d.d_name, "..");
			(void) uiomove((caddr_t)&d,
				PC_DIRENTSIZ, UIO_READ, uiop);
			offset = 2 * sizeof (struct pcdir);
			n--;
		}
		offset -= 2 * sizeof (struct pcdir);
		/* offset now has the real offset value into directory file */
	}

	/* LINTED lint thinks ep may be used before set */
	for (; n; ep++, offset += sizeof (struct pcdir)) {
PCFSDEBUG(9)
prom_printf("offset=%d n=%d\n", offset, n);
		boff = pc_blkoff(fsp, offset);
		if (boff == 0 || bp == NULL || boff >= bp->b_bcount) {
			if (bp != NULL) {
				brelse(bp);
				bp = NULL;
			}
			error = pc_blkatoff(pcp, offset, &bp, &ep);
			if (error) {
PCFSDEBUG(3)
prom_printf("pcfs_readdir: error=%d\n", error);
				if (error == ENOENT) {
					error = 0;
					if (eofp)
						*eofp = 1;
				}
				break;
			}
		}
		if (ep->pcd_filename[0] == PCD_UNUSED) {
			if (eofp)
				*eofp = 1;
			break;
		}
		/*
		 * Don't display label because it may contain funny characters.
		 */
		if ((ep->pcd_filename[0] == PCD_ERASED) ||
		    (ep->pcd_attr & (PCA_HIDDEN | PCA_SYSTEM | PCA_LABEL))) {
			uiop->uio_offset += PC_DIRENTSIZ;
			continue;
		}
		d.d_ino = pc_makenodeid(pc_daddrdb(fsp, bp->b_blkno),
		    boff, ep, fsp->pcfs_entps);
		tp = &d.d_name[0];
		fp = &ep->pcd_filename[0];
		i = PCFNAMESIZE;
		while (i-- && ((c = *fp) != ' ')) {
			if (!(c == '.' || pc_validchar(c))) {
				tp = &d.d_name[0];
				break;
			}
			*tp++ = tolower(c);
			fp++;
		}
		fp = &ep->pcd_ext[0];
		if (tp != &d.d_name[0] && *fp != ' ') {
			*tp++ = '.';
			i = PCFEXTSIZE;
			while (i-- && ((c = *fp) != ' ')) {
				if (!pc_validchar(c)) {
					tp = &d.d_name[0];
					break;
				}
				*tp++ = tolower(c);
				fp++;
			}
		}
		*tp = 0;
	/*	d.d_namlen = tp - &d.d_name[0]; */
		d.d_off = uiop->uio_offset + d.d_reclen;
		if (tp != &d.d_name[0]) {
			(void) uiomove((caddr_t)&d,
			    PC_DIRENTSIZ, UIO_READ, uiop);
		} else {
			uiop->uio_offset += PC_DIRENTSIZ;
		}

		/* an entry was copied, now decrement the count */
		n--;
	}
	if (bp)
		brelse(bp);
	pc_unlockfs(fsp);
	return (error);
}


/*
 * Called from pvn_getpages or pcfs_getpage to get a particular page.
 * When we are called the pcfs is already locked.
 */
/*ARGSUSED*/
static int
pcfs_getapage(
	struct vnode *vp,
	register u_int off,
	u_int len,
	u_int *protp,
	page_t *pl[],		/* NULL if async IO is requested */
	u_int plsz,
	struct seg *seg,
	caddr_t addr,
	enum seg_rw rw,
	struct cred *cr)
{
	register struct pcnode * pcp;
	register struct pcfs *fsp = VFSTOPCFS(vp->v_vfsp);
	struct vnode *devvp;
	page_t *pp;
	int err, pagefound;

PCFSDEBUG(5)
prom_printf("pcfs_getapage: vp=%x off=%d len=%d\n", vp, off, len);

	if ((pcp = VTOPC(vp)) == NULL)
		return (EIO);
	devvp = fsp->pcfs_devvp;

	/* pcfs doesn't do readaheads */
	if (pl == NULL)
		return (0);

	pl[0] = NULL;
	err = 0;
reread:
	if ((pagefound = page_exists(vp, off)) == 0) {
		/*
		 * Need to really do disk IO to get the page(s).
		 */
		struct buf *bp;
		daddr_t lbn, bn;
		u_int io_off, io_len;
		u_int lbnoff, pgoff, xferoffset;
		u_int xfersize;
		int err1;

		lbn = pc_lblkno(fsp, off);
		lbnoff = off & ~(fsp->pcfs_clsize -1);
		xferoffset = off & ~(fsp->pcfs_secsize -1);

		pp = pvn_read_kluster(vp, off, seg, addr, &io_off, &io_len,
		    lbnoff, min(pc_blksize(fsp, pcp, off), PAGESIZE), 0);
		if (pp == NULL)
			/*
			 * XXX - If pcfs is made MT-hot, this should go
			 * back to reread.
			 */
			panic("pcfs_getapage pvn_read_kluster");

		for (pgoff = 0; pgoff < PAGESIZE && xferoffset < pcp->pc_size;
		    pgoff += xfersize,
		    lbn +=  howmany(xfersize, fsp->pcfs_clsize),
		    /* LINTED lint thinks xfersize may be used before set */
		    lbnoff += xfersize, xferoffset += xfersize) {
			/*
			 * read as many contiguous blocks as possible to
			 * fill this page
			 */
			xfersize = PAGESIZE - pgoff;
			err1 = pc_bmap(pcp, lbn, &bn, &xfersize);
			if (err1) {
PCFSDEBUG(1)
prom_printf("pc_getapage err=%d", err1);
				err = err1;
				goto out;
			}
			bp = pageio_setup(pp, xfersize, devvp, B_READ);
			bp->b_edev = devvp->v_rdev;
			bp->b_dev = cmpdev(devvp->v_rdev);
			bp->b_blkno = bn +
			    /* add a sector offset within the cluster */
			    /* when the clustersize > PAGESIZE */
			    (xferoffset - lbnoff) / fsp->pcfs_secsize;
			bp->b_un.b_addr = (caddr_t)pgoff;
			bdev_strategy(bp);

			if (err == 0)
				err = biowait(bp);
			else
				(void) biowait(bp);
			pageio_done(bp);
			if (err)
				goto out;
		}
		if (pgoff < PAGESIZE) {
			pagezero(pp->p_prev, pgoff, PAGESIZE - pgoff);
		}
		pvn_plist_init(pp, pl, plsz, off, io_len, rw);
	}
out:
	if (err) {
		if (pp != NULL)
			pvn_read_done(pp, B_ERROR);
		return (err);
	}

	if (pagefound) {
		/*
		 * Page exists in the cache, acquire the "shared"
		 * lock.  If this fails, go back to reread.
		 */
		if ((pp = page_lookup(vp, off, SE_SHARED)) == NULL) {
			goto reread;
		}
		pl[0] = pp;
		pl[1] = NULL;
	}
	return (err);
}

/*
 * Return all the pages from [off..off+len] in given file
 */
static int
pcfs_getpage(
	struct vnode *vp,
	offset_t off,
	u_int len,
	u_int *protp,
	page_t *pl[],
	u_int plsz,
	struct seg *seg,
	caddr_t addr,
	enum seg_rw rw,
	struct cred *cr)
{
	register struct pcfs *fsp = VFSTOPCFS(vp->v_vfsp);
	int err;

PCFSDEBUG(6)
prom_printf("pcfs_getpage\n");
	if (err = pc_verify(fsp))
		return (err);
	if (vp->v_flag & VNOMAP)
		return (ENOSYS);
	err = pc_lockfs(fsp);
	if (err)
		return (err);
	if (protp != NULL)
		*protp = PROT_ALL;

	ASSERT((off & PAGEOFFSET) == 0);
	if (len <= PAGESIZE) {
		err = pcfs_getapage(vp, (u_int)off, len, protp, pl,
		    plsz, seg, addr, rw, cr);
	} else {
		err = pvn_getpages(pcfs_getapage, vp, (u_int)off,
		    len, protp, pl, plsz, seg, addr, rw, cr);
	}
	pc_unlockfs(fsp);
	return (err);
}


/*
 * Flags are composed of {B_INVAL, B_FREE, B_DONTNEED, B_FORCE}
 * If len == 0, do from off to EOF.
 *
 * The normal cases should be len == 0 & off == 0 (entire vp list),
 * len == MAXBSIZE (from segmap_release actions), and len == PAGESIZE
 * (from pageout).
 *
 */
/*ARGSUSED*/
static int
pcfs_putpage(
	register struct vnode *vp,
	offset_t off,
	u_int len,
	int flags,
	struct cred *cr)
{
	register struct pcnode *pcp;
	page_t *pp;
	register struct pcfs *fsp;
	u_int io_off, io_len, eoff;
	int err;

PCFSDEBUG(6)
prom_printf("pcfs_putpage vp=0x%x\n", vp);
	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	fsp = VFSTOPCFS(vp->v_vfsp);

	if (err = pc_verify(fsp))
		return (err);
	if ((pcp = VTOPC(vp)) == NULL) {
PCFSDEBUG(3)
prom_printf("pcfs_putpage NULL vp=0x%x\n", vp);
		return (EIO);
	}

	if (NOMEMWAIT()) {
		/*
		 * XXX - This is a quick hack to avoid blocking
		 * pageout. Also to avoid pcfs_getapage deadlocking
		 * with putpage when memory is running out,
		 * since we only have one global lock and we don't
		 * support async putpage.
		 * It should be fixed someday.
		 */
		return (ENOMEM);
	}

	flags &= ~B_ASYNC;	/* XXX should fix this later */

	err = pc_lockfs(fsp);
	if (err)
		return (err);
	if (vp->v_pages == NULL || off >= pcp->pc_size) {
		pc_unlockfs(fsp);
		return (0);
	}

	if (len == 0) {
		/*
		 * Search the entire vp list for pages >= off
		 */
		err = pvn_vplist_dirty(vp, (u_int)off,
		    pcfs_putapage, flags, cr);
	} else {
		eoff = (u_int)off + len;

		for (io_off = (u_int)off; io_off < eoff &&
		    /* LINTED lint thinkgs that io_len may be used before set */
		    io_off < pcp->pc_size; io_off += io_len) {
			/*
			 * If we are not invalidating, synchronously
			 * freeing or writing pages use the routine
			 * page_lookup_nowait() to prevent reclaiming
			 * them from the free list.
			 */
			if ((flags & B_INVAL) || ((flags & B_ASYNC) == 0)) {
				pp = page_lookup(vp, io_off,
					(flags & (B_INVAL | B_FREE)) ?
					    SE_EXCL : SE_SHARED);
			} else {
				pp = page_lookup_nowait(vp, io_off,
					(flags & B_FREE) ? SE_EXCL : SE_SHARED);
			}

			if (pp == NULL || pvn_getdirty(pp, flags) == 0)
				io_len = PAGESIZE;
			else {
				err = pcfs_putapage(vp, pp, &io_off, &io_len,
					flags, cr);
				if (err != 0)
					break;
				/*
				 * "io_off" and "io_len" are returned as
				 * the range of pages we actually wrote.
				 * This allows us to skip ahead more quickly
				 * since several pages may've been dealt
				 * with by this iteration of the loop.
				 */
			}
		}
	}
	if (err == 0 && (flags & B_INVAL) &&
	    off == 0 && len == 0 && vp->v_pages != NULL) {
		/*
		 * If doing "invalidation", make sure that
		 * all pages on the vnode list are actually
		 * gone.
		 */
		cmn_err(CE_PANIC,
			"pcfs_putpage: B_INVAL, pages not gone");
	} else if (err) {
PCFSDEBUG(1)
prom_printf("pcfs_putpage err=%d\n", err);
	}
	pc_unlockfs(fsp);
	return (err);
}

/*
 * Write out a single page, possibly klustering adjacent dirty pages.
 */
/*ARGSUSED*/
int
pcfs_putapage(
	struct vnode *vp,
	page_t *pp,
	u_int *offp,
	u_int *lenp,
	int flags,
	struct cred *cr)
{
	register struct pcnode *pcp;
	register struct pcfs *fsp;
	struct vnode *devvp;
	u_int io_off, io_len;
	daddr_t bn;
	u_int lbn, lbnoff, xferoffset;
	u_int pgoff, xfersize;
	int err = 0;

	pcp = VTOPC(vp);
	fsp = VFSTOPCFS(vp->v_vfsp);
	devvp = fsp->pcfs_devvp;

	/*
	 * If the modified time on the inode has not already been
	 * set elsewhere (e.g. for write/setattr) and this is not
	 * a call from msync (B_FORCE) we set the time now.
	 * This gives us approximate modified times for mmap'ed files
	 * which are modified via stores in the user address space.
	 */
	if ((pcp->pc_flags & PC_MOD) == 0 || (flags & B_FORCE)) {
		pcp->pc_flags |= PC_MOD;
		pc_mark(pcp);
	}
	pp = pvn_write_kluster(vp, pp, &io_off, &io_len, pp->p_offset,
	    PAGESIZE, flags);
PCFSDEBUG(7)
prom_printf("pc_putpage writing dirty page off=%d\n", io_off);

	lbn = pc_lblkno(fsp, io_off);
	lbnoff = io_off & ~(fsp->pcfs_clsize - 1);
	xferoffset = io_off & ~(fsp->pcfs_secsize - 1);

	for (pgoff = 0; pgoff < io_len && xferoffset < pcp->pc_size;
	    pgoff += xfersize,
	    lbn += howmany(xfersize, fsp->pcfs_clsize),
	    /* LINTED lint thinks xfersize may be used before set */
	    lbnoff += xfersize, xferoffset += xfersize) {

		struct buf *bp;
		int err1;

		/*
		 * write as many contiguous blocks as possible from this page
		 */
		xfersize = io_len - pgoff;
		err1 = pc_bmap(pcp, (daddr_t) lbn, &bn, &xfersize);
		if (err1) {
			err = err1;
			goto out;
		}
		bp = pageio_setup(pp, xfersize, devvp, B_WRITE | flags);
		bp->b_edev = devvp->v_rdev;
		bp->b_dev = cmpdev(devvp->v_rdev);
		bp->b_blkno = bn +
		    /* add a sector offset within the cluster */
		    /* when the clustersize > PAGESIZE */
		    (xferoffset - lbnoff) / fsp->pcfs_secsize;
		bp->b_un.b_addr = (caddr_t)pgoff;
		bdev_strategy(bp);

		if (err == 0)
			err = biowait(bp);
		else
			(void) biowait(bp);
		pageio_done(bp);
	}
	pvn_write_done(pp, ((err) ? B_ERROR : 0) | B_WRITE | flags);
	pp = NULL;

out:
	if (err != 0 && pp != NULL)
		pvn_write_done(pp, B_ERROR | B_WRITE | flags);
	if (offp)
		*offp = io_off;
	if (lenp)
		*lenp = io_len;
PCFSDEBUG(4)
prom_printf("pcfs_putapage: vp=%x pp=%x off=%d len=%d\n", vp, pp, io_off,
io_len);
	if (err) {
PCFSDEBUG(1)
prom_printf("pcfs_putapage err=%d", err);
	}
	return (err);
}

/*ARGSUSED*/
static int
pcfs_map(
	struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t *addrp,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	struct cred *cr)
{
	struct segvn_crargs vn_a;
	int error;

PCFSDEBUG(6)
prom_printf("pcfs_map\n");
	if (vp->v_flag & VNOMAP)
		return (ENOSYS);
	if ((int)off < 0 || (int)(off + len) < 0)
		return (EINVAL);

	as_rangelock(as);
	if ((flags & MAP_FIXED) == 0) {
		map_addr(addrp, len, (off_t)off, 1);
		if (*addrp == NULL) {
			as_rangeunlock(as);
			return (ENOMEM);
		}
	} else {
		/*
		 * User specified address - blow away any previous mappings
		 */
		(void) as_unmap(as, *addrp, len);
	}

	vn_a.vp = vp;
	vn_a.offset = (u_int)off;
	vn_a.type = flags & MAP_TYPE;
	vn_a.prot = prot;
	vn_a.maxprot = maxprot;
	vn_a.flags = flags & ~MAP_TYPE;
	vn_a.cred = cr;
	vn_a.amp = NULL;

	error = as_map(as, *addrp, len, segvn_create, (caddr_t)&vn_a);
	as_rangeunlock(as);
	return (error);
}

/* ARGSUSED */
static int
pcfs_seek(
	struct vnode *vp,
	offset_t ooff,
	offset_t *noffp)
{
	return ((*noffp < 0 || *noffp > MAXOFF_T) ? EINVAL : 0);
}

/* ARGSUSED */
static int
pcfs_addmap(
	struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	uint len,
	u_char prot,
	u_char maxprot,
	uint flags,
	struct cred *cr)
{
	if (vp->v_flag & VNOMAP)
		return (ENOSYS);
	return (0);
}

/*ARGSUSED*/
static int
pcfs_delmap(
	struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	u_int len,
	u_int prot,
	u_int maxprot,
	u_int flags,
	struct cred *cr)
{
	if (vp->v_flag & VNOMAP)
		return (ENOSYS);
	return (0);
}

/*
 * POSIX pathconf() support.
 */
/* ARGSUSED */
static int
pcfs_pathconf(
	struct vnode *vp,
	int cmd,
	u_long *valp,
	struct cred *cr)
{
	register u_long val;
	register int error = 0;
	struct statvfs vfsbuf;

	switch (cmd) {

	case _PC_LINK_MAX:
		val = 1;
		break;

	case _PC_MAX_CANON:
		val = MAX_CANON;
		break;

	case _PC_MAX_INPUT:
		val = MAX_INPUT;
		break;

	case _PC_NAME_MAX:
		struct_zero((caddr_t)&vfsbuf, sizeof (vfsbuf));
		if (error = VFS_STATVFS(vp->v_vfsp, &vfsbuf))
			break;
		val = vfsbuf.f_namemax;
		break;

	case _PC_PATH_MAX:
		val = PCMAXPATHLEN;
		break;

	case _PC_PIPE_BUF:
		val = PIPE_BUF;
		break;

	case _PC_NO_TRUNC:
		val = (u_long)-1; 	/* Will truncate long file name */
		break;

	case _PC_VDISABLE:
		val = _POSIX_VDISABLE;
		break;

	case _PC_CHOWN_RESTRICTED:
		if (rstchown)
			val = rstchown;		/* chown restricted enabled */
		else
			val = (u_long)-1;
		break;

	default:
		error = EINVAL;
		break;
	}

	if (error == 0)
		*valp = val;
	return (error);
}

/* ARGSUSED */
static int
pcfs_space(
	struct vnode *vp,
	int cmd,
	struct flock *bfp,
	int flag,
	offset_t offset,
	struct cred *cr)
{
	struct vattr vattr;
	int error;

	if (cmd != F_FREESP)
		return (EINVAL);
	if ((error = convoff(vp, bfp, 0, (off_t)offset)) == 0) {
		/*
		 * we only support the special case of l_len == 0,
		 * meaning free to end of file at this moment.
		 */
		if (bfp->l_len != 0)
			return (EINVAL);
		vattr.va_mask = AT_SIZE;
		vattr.va_size = bfp->l_start;
		error = VOP_SETATTR(vp, &vattr, 0, cr);
	}
	return (error);
}
