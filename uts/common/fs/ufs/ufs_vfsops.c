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
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */
#pragma	ident	"@(#)ufs_vfsops.c	2.130	95/10/20 SMI"

/* From  "ufs_vfsops.c 2.55     90/01/08 SMI"  */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitmap.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/buf.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/dnlc.h>
#include <sys/kstat.h>
#include <sys/acl.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_mount.h>
#include <sys/fs/ufs_acl.h>
#include <sys/fs/ufs_panic.h>
#ifdef QUOTA
#include <sys/fs/ufs_quota.h>
#endif
#undef NFS
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/swap.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include "fs/fs_subr.h"
#include <sys/cmn_err.h>
#include <sys/dnlc.h>

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct vfsops	ufs_vfsops;
extern int		ufsinit();
static int		mountfs();
extern int		highbit();
extern char		*strcpy();
extern char		*strncpy();

#if defined(DEBUG)
extern int	ufs_debug;
#endif /* DEBUG */

struct  dquot *dquot, *dquotNDQUOT;

static struct vfssw vfw = {
	"ufs",
	ufsinit,
	&ufs_vfsops,
	0
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_fsops;

static struct modlfs modlfs = {
	&mod_fsops, "filesystem for ufs", &vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

/*
 * An attempt has been made to make this module unloadable.  In order to
 * test it, we need a system in which the root fs is NOT ufs.  THIS HAS NOT
 * BEEN DONE
 */

char _depends_on[] = "fs/specfs strmod/rpcmod";

static int module_keepcnt = 0;	/* ==0 means the module is unloadable */

extern kstat_t *ufs_inode_kstat;

_init()
{
	return (mod_install(&modlinkage));
}

_fini()
{
	/* Make UFS un-unloadable */
	return (EBUSY);

	/*
	 * If we ever want to do this right, we need code that cleans
	 * up the kstat stuff, ufs threads, and other consumers of
	 * resources. Also, we would need to end this with:
	 *
	 *	return (mod_remove(&modlinkage));
	 */
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

struct vnode *makespecvp();

extern int ufsfstype;

static int mountfs(struct vfs *, enum whymountroot, dev_t, char *,
		struct cred *, int, void *, int);

/*
 * XXX - this appears only to be used by the VM code to handle the case where
 * UNIX is running off the mini-root.  That probably wants to be done
 * differently.
 */
struct vnode *rootvp;

static int
ufs_mount(vfsp, mvp, uap, cr)
	register struct vfs *vfsp;
	struct vnode *mvp;
	struct mounta *uap;
	struct cred *cr;
{
	char *data = uap->dataptr;
	int datalen = uap->datalen;
	register dev_t dev;
	struct vnode *bvp;
	struct pathname dpn;
	register int error;
	enum whymountroot why;
	union {
		struct ufs_args		_a;
		struct ufs_args_fop	_fa;
	} args;

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


	if ((uap->flags & MS_REMOUNT) == 0 &&
	    (uap->flags & MS_OVERLAY) == 0 &&
		(mvp->v_count != 1 || (mvp->v_flag & VROOT))) {
			mutex_exit(&mvp->v_lock);
			module_keepcnt--;
			return (EBUSY);
	}
	mutex_exit(&mvp->v_lock);

	/*
	 * Get arguments
	 */
	if (data && datalen) {
		long flagmask;

		flagmask = UFSMNT_NOINTR | UFSMNT_SYNCDIR | UFSMNT_NOSETSEC;
		switch (datalen) {
		case sizeof (struct ufs_args_fop):
			flagmask |= UFSMNT_ONERROR_FLGMASK;
			/* FALLTHROUGH */

		case sizeof (struct ufs_args):
			if (copyin(data, (caddr_t)&args, datalen)) {
				module_keepcnt--;
				return (EFAULT);
			}
			if (args._a.flags & ~flagmask) {
				module_keepcnt--;
				return (EINVAL);
			}
			break;

		default:
			module_keepcnt--;
			return (EINVAL);
		}
	} else {
		datalen = 0;
	}

	if (error = pn_get(uap->dir, UIO_USERSPACE, &dpn)) {
		module_keepcnt--;
		return (error);
	}

	/*
	 * Resolve path name of special file being mounted.
	 */
	if (error = lookupname(uap->spec, UIO_USERSPACE, FOLLOW, NULLVPP,
	    &bvp)) {
		pn_free(&dpn);
		module_keepcnt--;
		return (error);
	}
	if (bvp->v_type != VBLK) {
		VN_RELE(bvp);
		pn_free(&dpn);
		module_keepcnt--;
		return (ENOTBLK);
	}
	dev = bvp->v_rdev;
	VN_RELE(bvp);
	/*
	 * Ensure that this device isn't already mounted,
	 * unless this is a REMOUNT request
	 */
	if (vfs_devsearch(dev) != NULL) {
		if (uap->flags & MS_REMOUNT)
			why = ROOT_REMOUNT;
		else {
			pn_free(&dpn);
			module_keepcnt--;
			return (EBUSY);
		}
	} else {
		why = ROOT_INIT;
	}
	if (getmajor(dev) >= devcnt) {
		pn_free(&dpn);
		module_keepcnt--;
		return (ENXIO);
	}

	/*
	 * If the device is a tape, mount it read only
	 */
	if (devopsp[getmajor(dev)]->devo_cb_ops->cb_flag & D_TAPE)
		vfsp->vfs_flag |= VFS_RDONLY;

	if (uap->flags & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;

	/*
	 * Mount the filesystem.
	 */
	error = mountfs(vfsp, why, dev, dpn.pn_path, cr, 0, &args, datalen);
	pn_free(&dpn);
	if (error)
		module_keepcnt--;
	return (error);
}

/*
 * Mount root file system.
 * "why" is ROOT_INIT on initial call, ROOT_REMOUNT if called to
 * remount the root file system, and ROOT_UNMOUNT if called to
 * unmount the root (e.g., as part of a system shutdown).
 *
 * XXX - this may be partially machine-dependent; it, along with the VFS_SWAPVP
 * operation, goes along with auto-configuration.  A mechanism should be
 * provided by which machine-INdependent code in the kernel can say "get me the
 * right root file system" and "get me the right initial swap area", and have
 * that done in what may well be a machine-dependent fashion.
 * Unfortunately, it is also file-system-type dependent (NFS gets it via
 * bootparams calls, UFS gets it from various and sundry machine-dependent
 * mechanisms, as SPECFS does for swap).
 */
static int
ufs_mountroot(vfsp, why)
	struct vfs *vfsp;
	enum whymountroot why;
{
	register struct fs *fsp;
	register int error;
	static int ufsrootdone = 0;
	dev_t rootdev;
	extern dev_t getrootdev();
	struct vnode *vp;
	struct vnode *devvp = 0;
	int ovflags;
	int doclkset;
	register struct ufsvfs *ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;

	if (why == ROOT_INIT) {
		if (ufsrootdone++)
			return (EBUSY);
		rootdev = getrootdev();
		if (rootdev == (dev_t)NODEV)
			return (ENODEV);
		vfsp->vfs_dev = rootdev;
		module_keepcnt++;
#ifdef notneeded
		if ((boothowto & RB_WRITABLE) == 0) {
			/*
			 * We mount a ufs root file system read-only to
			 * avoid problems during fsck.   After fsck runs,
			 * we remount it read-write.
			 */
			vfsp->vfs_flag |= VFS_RDONLY;
		}
#endif
		vfsp->vfs_flag |= VFS_RDONLY;
	} else if (why == ROOT_REMOUNT) {
		vp = ((struct ufsvfs *)vfsp->vfs_data)->vfs_devvp;
		(void) dnlc_purge_vfsp(vfsp, 0);
		(void) VOP_PUTPAGE(vp, (offset_t)0, (u_int)0, B_INVAL, CRED());
		binval(vfsp->vfs_dev);
		fsp = getfs(vfsp);

		ovflags = vfsp->vfs_flag;
		vfsp->vfs_flag &= ~VFS_RDONLY;
		vfsp->vfs_flag |= VFS_REMOUNT;
		rootdev = vfsp->vfs_dev;
	} else if (why == ROOT_UNMOUNT) {
		/*
		 * root on metatrans not supported
		 */
		ASSERT(!(TRANS_ISTRANS(ufsvfsp)));
		ufs_update(0);
		return (0);
	}
	error = vfs_lock(vfsp);
	if (error) {
		if (why == ROOT_INIT)
			module_keepcnt--;
		return (error);
	}
	/* If RO media, don't call clkset() (see below) */
	doclkset = 1;
	if (why == ROOT_INIT) {
		devvp = makespecvp(rootdev, VBLK);
		error = VOP_OPEN(&devvp, FREAD|FWRITE, CRED());
		if (error == 0) {
			(void) VOP_CLOSE(devvp, FREAD|FWRITE, 1,
				(offset_t)0, CRED());
		} else {
			doclkset = 0;
		}
		VN_RELE(devvp);
	}

	error = mountfs(vfsp, why, rootdev, "/", CRED(), 1, NULL, 0);
	/*
	 * XXX - assumes root device is not indirect, because we don't set
	 * rootvp.  Is rootvp used for anything?  If so, make another arg
	 * to mountfs (in S5 case too?)
	 */
	if (error) {
		vfs_unlock(vfsp);
		if (why == ROOT_REMOUNT)
			vfsp->vfs_flag = ovflags;
		if (rootvp) {
			VN_RELE(rootvp);
			rootvp = (struct vnode *)0;
		}
		if (why == ROOT_INIT)
			module_keepcnt--;
		return (error);
	}
	if (why == ROOT_INIT)
		vfs_add((struct vnode *)0, vfsp,
		    (vfsp->vfs_flag & VFS_RDONLY) ? MS_RDONLY : 0);
	vfs_unlock(vfsp);
	fsp = getfs(vfsp);
	clkset(doclkset ? fsp->fs_time : -1);
	return (0);
}

static int
remountfs(struct vfs *vfsp, dev_t dev, void *raw_argsp, int args_len)
{
	struct ufsvfs *ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	struct ulockfs *ulp = &ufsvfsp->vfs_ulockfs;
	struct buf *bp = ufsvfsp->vfs_bufp;
	struct fs *fsp = (struct fs *)bp->b_un.b_addr;
	struct fs *fspt;
	struct buf *tpt = 0;
	int error = 0;
	int flags;
	long toosoon = 0;

	if (args_len == sizeof (struct ufs_args) && raw_argsp) {
		flags = ((struct ufs_args *) raw_argsp)->flags;
	} else if (args_len == sizeof (struct ufs_args_fop) && raw_argsp) {
		flags = ((struct ufs_args_fop *) raw_argsp)->flags;
		toosoon = ((struct ufs_args_fop *) raw_argsp)->toosoon;
	} else {
		flags = 0;
	}

	/* cannot remount to RDONLY */
	if (vfsp->vfs_flag & VFS_RDONLY)
		return (EINVAL);

	/* woops, wrong dev */
	if (vfsp->vfs_dev != dev)
		return (EINVAL);

	/*
	 * synchronize w/ufs ioctls
	 */
	mutex_enter(&ulp->ul_lock);

	/*
	 * reset options
	 */
	ufsvfsp->vfs_nointr  = flags & UFSMNT_NOINTR;
	ufsvfsp->vfs_syncdir = flags & UFSMNT_SYNCDIR;
	ufsvfsp->vfs_nosetsec = flags & UFSMNT_NOSETSEC;

	/*
	 * read/write to read/write; all done
	 */
	if (fsp->fs_ronly == 0)
		goto remounterr;

	/*
	 * fix-on-panic assumes RO->RW remount implies system-critical fs
	 * if it is shortly after boot; so, don't attempt to lock and fix
	 * (unless the user explicitly asked for another action on error)
	 * XXX UFSMNT_ONERROR_RDONLY rather than UFSMNT_ONERROR_PANIC
	 */
#define	BOOT_TIME_LIMIT	(180*HZ)
	if (!(flags & UFSMNT_ONERROR_FLGMASK) && lbolt < BOOT_TIME_LIMIT) {
		cmn_err(CE_WARN, "%s is required to be mounted onerror=%s",
			ufsvfsp->vfs_fs->fs_fsmnt, UFSMNT_ONERROR_PANIC_STR);
		flags |= UFSMNT_ONERROR_PANIC;
	}

	if ((error = ufsfx_mount(ufsvfsp, flags, toosoon)) != 0)
		goto remounterr;

	/*
	 * Lock the file system and flush stuff from memory
	 */
	error = ufs_quiesce(ulp);
	if (error)
		goto remounterr;
	error = ufs_flush(vfsp);
	if (error)
		goto remounterr;

	tpt = bread(vfsp->vfs_dev, SBLOCK, SBSIZE);
	if (tpt->b_flags & B_ERROR) {
		error = EIO;
		goto remounterr;
	}
	fspt = (struct fs *)tpt->b_un.b_addr;
	if (fspt->fs_magic != FS_MAGIC || fspt->fs_bsize > MAXBSIZE ||
	    fspt->fs_frag > MAXFRAG ||
	    fspt->fs_bsize < sizeof (struct fs) ||
	    fspt->fs_bsize < PAGESIZE) {
		tpt->b_flags |= B_STALE | B_AGE;
		error = EINVAL;
		goto remounterr;
	}
	/*
	 * check for transaction device
	 */
	ufsvfsp->vfs_trans = ufs_trans_get(dev, vfsp);
	if (TRANS_ISERROR(ufsvfsp))
		goto remounterr;
	TRANS_DOMATAMAP(ufsvfsp);

	if ((fspt->fs_state + (long)fspt->fs_time == FSOKAY) &&
	    fspt->fs_clean == FSLOG && !TRANS_ISTRANS(ufsvfsp)) {
		ufsvfsp->vfs_trans = NULL;
		ufsvfsp->vfs_domatamap = 0;
		error = ENOSPC;
		goto remounterr;
	}

	if (fspt->fs_state + (long)fspt->fs_time == FSOKAY &&
	    (fspt->fs_clean == FSCLEAN ||
	    fspt->fs_clean == FSSTABLE ||
	    fspt->fs_clean == FSLOG)) {

		if (error = ufs_getsummaryinfo(vfsp->vfs_dev, ufsvfsp, fspt))
			goto remounterr;

		/* preserve mount name */
		strncpy(fspt->fs_fsmnt, fsp->fs_fsmnt, MAXMNTLEN);
		/* free the old cg space */
		kmem_free((caddr_t)fsp->fs_csp[0], (u_int)fsp->fs_cssize);
		/* switch in the new superblock */
		bcopy((caddr_t)tpt->b_un.b_addr,
			(caddr_t)bp->b_un.b_addr,
			(u_int) fspt->fs_sbsize);

		fsp->fs_clean = FSSTABLE;
	} /* superblock updated in memory */
	tpt->b_flags |= B_STALE | B_AGE;
	brelse(tpt);
	tpt = 0;

	if (fsp->fs_clean != FSSTABLE) {
		error = ENOSPC;
		goto remounterr;
	}

	if (TRANS_ISTRANS(ufsvfsp)) {
		fsp->fs_clean = FSLOG;
		ufsvfsp->vfs_dio = 0;
	} else
		if (ufsvfsp->vfs_dio)
			fsp->fs_clean = FSSUSPEND;

	TRANS_MATA_MOUNT(ufsvfsp);

	fsp->fs_fmod = 0;
	fsp->fs_ronly = 0;

	cv_broadcast(&ulp->ul_cv);
	mutex_exit(&ulp->ul_lock);

	if (TRANS_ISTRANS(ufsvfsp)) {

		/*
		 * start the delete thread
		 */
		ufs_thread_start(&ufsvfsp->vfs_delete, ufs_thread_delete, vfsp);

		/*
		 * start the reclaim thread
		 */
		if (fsp->fs_reclaim & (FS_RECLAIM|FS_RECLAIMING)) {
			fsp->fs_reclaim &= ~FS_RECLAIM;
			fsp->fs_reclaim |=  FS_RECLAIMING;
			ufs_thread_start(&ufsvfsp->vfs_reclaim,
				ufs_thread_reclaim, vfsp);
		}
	}

	TRANS_SBWRITE(ufsvfsp, TOP_MOUNT);

	return (0);

remounterr:
	if (tpt)
		brelse(tpt);
	cv_broadcast(&ulp->ul_cv);
	mutex_exit(&ulp->ul_lock);
	return (error);
}

static int
mountfs(vfsp, why, dev, path, cr, isroot, raw_argsp, args_len)
	struct vfs *vfsp;
	enum whymountroot why;
	dev_t dev;
	char *path;
	struct cred *cr;
	int isroot;
	void *raw_argsp;
	int args_len;
{
	struct vnode *devvp = 0;
	register struct fs *fsp;
	register struct ufsvfs *ufsvfsp = 0;
	register struct buf *bp = 0;
	struct buf *tp = 0;
	int error = 0;
	size_t len;
	int needclose = 0;
	int needtrans = 0;
	struct inode *rip;
	struct vnode *rvp;
	int flags;
	long toosoon = 0;

	if (args_len == sizeof (struct ufs_args) && raw_argsp) {
		flags = ((struct ufs_args *) raw_argsp)->flags;
	} else if (args_len == sizeof (struct ufs_args_fop) && raw_argsp) {
		flags = ((struct ufs_args_fop *) raw_argsp)->flags;
		toosoon = ((struct ufs_args_fop *) raw_argsp)->toosoon;
	} else {
		flags = 0;
	}

	ASSERT(MUTEX_HELD(&vfsp->vfs_reflock));

	if (why == ROOT_INIT) {
		/*
		 * Open the device.
		 */
		devvp = makespecvp(dev, VBLK);

		/*
		 * Open block device mounted on.
		 * When bio is fixed for vnodes this can all be vnode
		 * operations.
		 */
		error = VOP_OPEN(&devvp,
		    (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE, cr);
		if (error)
			goto out;
		needclose = 1;

		/*
		 * Refuse to go any further if this
		 * device is being used for swapping.
		 */
		if (IS_SWAPVP(devvp)) {
			error = EBUSY;
			goto out;
		}
	}

	/*
	 * check for dev already mounted on
	 */
	if (vfsp->vfs_flag & VFS_REMOUNT)
		return (remountfs(vfsp, dev, raw_argsp, args_len));

	ASSERT(devvp != 0);

	/*
	 * Flush back any dirty pages on the block device to
	 * try and keep the buffer cache in sync with the page
	 * cache if someone is trying to use block devices when
	 * they really should be using the raw device.
	 */
	(void) VOP_PUTPAGE(devvp, (offset_t)0, (u_int)0, B_INVAL, cr);

	/*
	 * read in superblock
	 */

	tp = bread(dev, SBLOCK, SBSIZE);
	if (tp->b_flags & B_ERROR) {
		goto out;
	}
	fsp = (struct fs *)tp->b_un.b_addr;
	if (fsp->fs_magic != FS_MAGIC || fsp->fs_bsize > MAXBSIZE ||
	    fsp->fs_frag > MAXFRAG ||
	    fsp->fs_bsize < sizeof (struct fs) || fsp->fs_bsize < PAGESIZE) {
		error = EINVAL;	/* also needs translation */
		goto out;
	}
	/*
	 * Allocate VFS private data.
	 */
	ufsvfsp = kmem_zalloc(sizeof (struct ufsvfs), KM_SLEEP);

	vfsp->vfs_bcount = 0;
	vfsp->vfs_data = (caddr_t) ufsvfsp;
	vfsp->vfs_fstype = ufsfstype;
	vfsp->vfs_dev = dev;
	vfsp->vfs_flag |= VFS_NOTRUNC;
	vfsp->vfs_fsid.val[0] = dev;
	vfsp->vfs_fsid.val[1] = ufsfstype;
	ufsvfsp->vfs_devvp = devvp;

	ufsvfsp->vfs_maxwrite = (fsp->fs_bsize * fsp->fs_nindir);
	ufsvfsp->vfs_smallwrite = SMALL_WRITE_SIZE(fsp);
	ufsvfsp->vfs_dirsize = INODESIZE + (4 * ALLOCSIZE) + fsp->fs_fsize;
	/*
	 * Initialize threads
	 */
	ufs_thread_init(&ufsvfsp->vfs_delete, 1);
	ufs_thread_init(&ufsvfsp->vfs_reclaim, 0);

	/*
	 * Copy the super block into a buffer in its native size.
	 * Use ngeteblk to allocate the buffer
	 */
	bp = ngeteblk(fsp->fs_bsize);
	ufsvfsp->vfs_bufp = bp;
	bp->b_edev = dev;
	bp->b_dev = cmpdev(dev);
	bp->b_blkno = SBLOCK;
	bp->b_bcount = fsp->fs_sbsize;
	bcopy((caddr_t)tp->b_un.b_addr, (caddr_t)bp->b_un.b_addr,
		(u_int)fsp->fs_sbsize);
	tp->b_flags |= B_STALE | B_AGE;
	brelse(tp);
	tp = 0;

	fsp = (struct fs *)bp->b_un.b_addr;

	if (vfsp->vfs_flag & VFS_RDONLY) {
		fsp->fs_ronly = 1;
		fsp->fs_fmod = 0;
		if (((fsp->fs_state + (long)fsp->fs_time) == FSOKAY) &&
		    (fsp->fs_clean == FSCLEAN ||
		    fsp->fs_clean == FSSTABLE ||
		    fsp->fs_clean == FSLOG))
			fsp->fs_clean = FSSTABLE;
		else
			fsp->fs_clean = FSBAD;
	} else {

		fsp->fs_fmod = 0;
		fsp->fs_ronly = 0;

		ufsvfsp->vfs_trans = ufs_trans_get(dev, vfsp);
		TRANS_DOMATAMAP(ufsvfsp);

		if ((TRANS_ISERROR(ufsvfsp)) ||
		    (((fsp->fs_state + (long)fsp->fs_time) == FSOKAY) &&
		    fsp->fs_clean == FSLOG && !TRANS_ISTRANS(ufsvfsp))) {
			ufsvfsp->vfs_trans = NULL;
			ufsvfsp->vfs_domatamap = 0;
			error = ENOSPC;
			goto out;
		}

		if (((fsp->fs_state + (long)fsp->fs_time) == FSOKAY) &&
		    (fsp->fs_clean == FSCLEAN ||
		    fsp->fs_clean == FSSTABLE ||
		    fsp->fs_clean == FSLOG))
			fsp->fs_clean = FSSTABLE;
		else {
			if (isroot) {
				/*
				 * allow root partition to be mounted even
				 * when fs_state is not ok
				 * will be fixed later by a remount root
				 */
				fsp->fs_clean = FSBAD;
				ufsvfsp->vfs_trans = NULL;
				ufsvfsp->vfs_domatamap = 0;
			} else {
				error = ENOSPC;
				goto out;
			}
		}

		if (fsp->fs_clean == FSSTABLE && TRANS_ISTRANS(ufsvfsp))
			fsp->fs_clean = FSLOG;
	}
	TRANS_MATA_MOUNT(ufsvfsp);
	needtrans = 1;

	vfsp->vfs_bsize = fsp->fs_bsize;

	/*
	 * Read in summary info
	 */
	if (error = ufs_getsummaryinfo(dev, ufsvfsp, fsp))
		goto out;

	mutex_init(&ufsvfsp->vfs_lock, "fs_lock", MUTEX_DEFAULT, NULL);
	copystr(path, fsp->fs_fsmnt, sizeof (fsp->fs_fsmnt) - 1, &len);
	bzero(fsp->fs_fsmnt + len, sizeof (fsp->fs_fsmnt) - len);

	/*
	 * Sanity checks for old file systems
	 */
	ufsvfsp->vfs_npsect = MAX(fsp->fs_npsect, fsp->fs_nsect);
	ufsvfsp->vfs_interleave = MAX(fsp->fs_interleave, 1);
	if (fsp->fs_postblformat == FS_42POSTBLFMT)
		ufsvfsp->vfs_nrpos = 8;
	else
		ufsvfsp->vfs_nrpos = fsp->fs_nrpos;

	/*
	 * Initialize lockfs structure to support file system locking
	 */
	bzero((caddr_t) &ufsvfsp->vfs_ulockfs.ul_lockfs,
		sizeof (struct lockfs));
	ufsvfsp->vfs_ulockfs.ul_fs_lock = ULOCKFS_ULOCK;
	mutex_init(&ufsvfsp->vfs_ulockfs.ul_lock, "ufs_lockfs_lock",
	    MUTEX_DEFAULT, NULL);
	cv_init(&ufsvfsp->vfs_ulockfs.ul_cv, "ufs_lockfs_cv", CV_DEFAULT, NULL);

	if (error = ufs_iget(vfsp, UFSROOTINO, &rip, cr))
		goto out;
	rvp = ITOV(rip);
	mutex_enter(&rvp->v_lock);
	rvp->v_flag |= VROOT;
	mutex_exit(&rvp->v_lock);
	ufsvfsp->vfs_root = rvp;

	/* options */
	ufsvfsp->vfs_nosetsec = flags & UFSMNT_NOSETSEC;
	ufsvfsp->vfs_nointr  = flags & UFSMNT_NOINTR;
	ufsvfsp->vfs_syncdir = flags & UFSMNT_SYNCDIR;

	ufsvfsp->vfs_nindiroffset = fsp->fs_nindir - 1;
	ufsvfsp->vfs_nindirshift = highbit(ufsvfsp->vfs_nindiroffset);
	ufsvfsp->vfs_rdclustsz = ufsvfsp->vfs_wrclustsz =
	    fsp->fs_bsize * fsp->fs_maxcontig;

	if (TRANS_ISTRANS(ufsvfsp)) {
		/*
		 * start the delete thread
		 */
		ufs_thread_start(&ufsvfsp->vfs_delete, ufs_thread_delete, vfsp);

		/*
		 * start reclaim thread
		 */
		if (fsp->fs_reclaim & (FS_RECLAIM|FS_RECLAIMING)) {
			fsp->fs_reclaim &= ~FS_RECLAIM;
			fsp->fs_reclaim |=  FS_RECLAIMING;
			ufs_thread_start(&ufsvfsp->vfs_reclaim,
				ufs_thread_reclaim, vfsp);
		}
	}
	if (!fsp->fs_ronly)
		TRANS_SBWRITE(ufsvfsp, TOP_MOUNT);

	/* fix-on-panic initialization */
	if (isroot && !(flags & UFSMNT_ONERROR_FLGMASK))
		flags |= UFSMNT_ONERROR_PANIC;	/* XXX ..._RDONLY */

	if ((error = ufsfx_mount(ufsvfsp, flags, toosoon)) != 0)
		goto out;

#ifdef DEBUG
	/*
	 * now check root inode, since it couldn't be checked
	 * from iget, since the fs wasn't yet mounted
	 */
	rw_enter(&rip->i_contents, RW_READER);
	if (!ufs_ivalid(rip)) {
		cmn_err(CE_WARN, "mountfs: root inode of fs %s is bad",
					ufsvfsp->vfs_fs->fs_fsmnt);
	}
	rw_exit(&rip->i_contents);
#endif /* DEBUG */

	if (why == ROOT_INIT) {
		if (isroot)
			rootvp = devvp;
	}
	return (0);
out:
	if (error == 0)
		error = EIO;
	if (bp) {
		bp->b_flags |= (B_STALE|B_AGE);
		brelse(bp);
	}
	if (tp) {
		tp->b_flags |= (B_STALE|B_AGE);
		brelse(tp);
	}
	if (needtrans) {
		TRANS_MATA_UMOUNT(ufsvfsp);
	}
	if (ufsvfsp) {
		ufs_thread_exit(&ufsvfsp->vfs_delete);
		ufs_thread_exit(&ufsvfsp->vfs_reclaim);
		kmem_free((caddr_t)ufsvfsp, sizeof (struct ufsvfs));
	}
	if (needclose) {
		(void) VOP_CLOSE(devvp, (vfsp->vfs_flag & VFS_RDONLY) ?
			FREAD : FREAD|FWRITE, 1, (offset_t)0, cr);
		bflush(dev);
		binval(dev);
	}
	VN_RELE(devvp);
	return (error);
}

/*
 * vfs operations
 */
static int
ufs_unmount(struct vfs *vfsp, struct cred *cr)
{
	dev_t 		dev		= vfsp->vfs_dev;
	struct ufsvfs	*ufsvfsp	= (struct ufsvfs *)vfsp->vfs_data;
	struct fs	*fs		= ufsvfsp->vfs_fs;
	struct ulockfs	*ulp		= &ufsvfsp->vfs_ulockfs;
	struct vnode 	*bvp, *vp;
	struct buf	*bp;
	struct inode	*ip, *inext, *rip;
	union ihead	*ih;
	int 		error, flag, i;
	int		istrans;

	ASSERT(MUTEX_HELD(&vfsp->vfs_reflock));

	if (!suser(cr))
		return (EPERM);

	UFS_DEBUG_ICACHE_CHECK();

	/* coordinate with global hlock thread */
	istrans = (int)TRANS_ISTRANS(ufsvfsp);
	if (istrans && ufs_trans_put(dev))
		return (EAGAIN);

	/* kill the reclaim thread */
	ufs_thread_exit(&ufsvfsp->vfs_reclaim);

	/* suspend the delete thread */
	ufs_thread_suspend(&ufsvfsp->vfs_delete);

	/*
	 * drain the delete and idle queues
	 */
	ufs_delete_drain(vfsp, 0, 1);
	ufs_idle_drain(vfsp);

	/*
	 * use the lockfs protocol to prevent new ops from starting
	 */
	mutex_enter(&ulp->ul_lock);

	/*
	 * if the file system is busy; return EBUSY
	 */
	if (ulp->ul_vnops_cnt || ULOCKFS_IS_SLOCK(ulp)) {
		error = EBUSY;
		goto out;
	}

	/*
	 * if this is not a forced unmount (!hard/error locked), then
	 * get rid of every inode except the root and quota inodes
	 * also, commit any outstanding transactions
	 */
	if (!ULOCKFS_IS_HLOCK(ulp) && !ULOCKFS_IS_ELOCK(ulp))
		if (error = ufs_flush(vfsp))
			goto out;

	/*
	 * ignore inodes in the cache if fs is hard locked or error locked
	 */
	if (!ULOCKFS_IS_HLOCK(ulp) && !ULOCKFS_IS_ELOCK(ulp)) {
		/*
		 * Otherwise, only the quota and root inodes are in the cache
		 */
		rip = VTOI(ufsvfsp->vfs_root);
		for (i = 0, ih = ihead; i < inohsz; i++, ih++) {
			mutex_enter(&ih_lock[i]);
			for (ip = ih->ih_chain[0];
					ip != (struct inode *)ih;
					ip = ip->i_forw) {
				if (ip->i_ufsvfs != ufsvfsp)
					continue;
				if (ip == ufsvfsp->vfs_qinod)
					continue;
				if (ip == rip && ITOV(ip)->v_count == 1)
					continue;
				mutex_exit(&ih_lock[i]);
				error = EBUSY;
				goto out;
			}
			mutex_exit(&ih_lock[i]);
		}
	}
#ifdef QUOTA
	/*
	 * close the quota file
	 */
	(void) closedq(ufsvfsp, cr);
	/*
	 * drain the delete and idle queues
	 */
	ufs_delete_drain(vfsp, 0, 0);
	ufs_idle_drain(vfsp);
#endif QUOTA

	/*
	 * discard the inodes for this fs (including root, shadow, and quota)
	 */
	for (i = 0, ih = ihead; i < inohsz; i++, ih++) {
		mutex_enter(&ih_lock[i]);
		for (inext = 0, ip = ih->ih_chain[0];
				ip != (struct inode *)ih;
				ip = inext) {
			inext = ip->i_forw;
			if (ip->i_ufsvfs != ufsvfsp)
				continue;
			vp = ITOV(ip);
			VN_HOLD(vp)
			remque(ip);
			UFS_DEBUG_ICACHE_DEC(i);
			ufs_rmidle(ip);
			ufs_si_del(ip);
			ip->i_ufsvfs = NULL;
			vp->v_vfsp = NULL;
			vp->v_type = VBAD;
			VN_RELE(vp);
		}
		mutex_exit(&ih_lock[i]);
	}
	ufs_si_cache_flush(dev);

	/*
	 * kill the delete thread and drain the idle queue
	 */
	ufs_thread_exit(&ufsvfsp->vfs_delete);
	ufs_idle_drain(vfsp);

	bp = ufsvfsp->vfs_bufp;
	bvp = ufsvfsp->vfs_devvp;
	flag = !fs->fs_ronly;
	if (flag) {
		bflush(dev);
		if (fs->fs_clean != FSBAD) {
			if (fs->fs_clean != FSLOG)
				fs->fs_clean = FSCLEAN;
			fs->fs_reclaim &= ~FS_RECLAIM;
		}
		TRANS_SBUPDATE(ufsvfsp, vfsp, TOP_SBUPDATE_UNMOUNT);
		/*
		 * push this last transaction
		 */
		curthread->t_flag |= T_DONTBLOCK;
		TRANS_BEGIN_SYNC(ufsvfsp, TOP_COMMIT_UNMOUNT, TOP_COMMIT_SIZE);
		TRANS_END_SYNC(ufsvfsp, error, TOP_COMMIT_UNMOUNT,
				TOP_COMMIT_SIZE);
		curthread->t_flag &= ~T_DONTBLOCK;
	}

	TRANS_MATA_UMOUNT(ufsvfsp);
	ufsfx_unmount(ufsvfsp);		/* fix-on-panic bookkeeping */
	kmem_free((caddr_t)fs->fs_csp[0], (u_int)fs->fs_cssize);

	bp->b_flags |= B_STALE|B_AGE;
	ufsvfsp->vfs_bufp = NULL;	/* don't point at free'd buf */
	brelse(bp);			/* free the superblock buf */

	(void) VOP_PUTPAGE(bvp, (offset_t)0, (u_int)0, B_INVAL, cr);
	(void) VOP_CLOSE(bvp, flag, 1, (offset_t)0, cr);
	bflush(dev);
	binval(dev);
	VN_RELE(bvp);

	/* free up lockfs comment structure, if any */
	if (ulp->ul_lockfs.lf_comlen && ulp->ul_lockfs.lf_comment)
		kmem_free(ulp->ul_lockfs.lf_comment,
			(u_int) ulp->ul_lockfs.lf_comlen);
	/*
	 * For a forcible unmount, threads may be asleep in
	 * ufs_lockfs_begin/ufs_check_lockfs.  These threads will need
	 * the ufsvfs structure so we don't free it, yet.  ufs_update
	 * will free it up after awhile.
	 */
	if (ULOCKFS_IS_HLOCK(ulp) || ULOCKFS_IS_ELOCK(ulp)) {
		extern kmutex_t		ufsvfs_mutex;
		extern struct ufsvfs	*ufsvfslist;

		mutex_enter(&ufsvfs_mutex);
		ufsvfsp->vfs_next = ufsvfslist;
		ufsvfslist = ufsvfsp;
		mutex_exit(&ufsvfs_mutex);
		/* wakeup any suspended threads */
		cv_broadcast(&ulp->ul_cv);
		mutex_exit(&ulp->ul_lock);
	} else {
		kmem_free((caddr_t)ufsvfsp, sizeof (struct ufsvfs));
		module_keepcnt--;
	}
	UFS_DEBUG_ICACHE_CHECK();

	return (0);
out:

	/* open the fs to new ops */
	cv_broadcast(&ulp->ul_cv);
	mutex_exit(&ulp->ul_lock);

	if (istrans) {
		/* allow the delete thread to continue */
		ufs_thread_continue(&ufsvfsp->vfs_delete);
		/* restart the reclaim thread */
		ufs_thread_start(&ufsvfsp->vfs_reclaim, ufs_thread_reclaim,
				vfsp);
		/* coordinate with global hlock thread */
		(void) ufs_trans_get(dev, vfsp);
		/* check for trans errors during umount */
		ufs_trans_onerror();
	}
	UFS_DEBUG_ICACHE_CHECK();

	return (error);
}

static int
ufs_root(vfsp, vpp)
	struct vfs *vfsp;
	struct vnode **vpp;
{
	struct ufsvfs *ufsvfsp;
	struct vnode *vp;

	if (!vfsp)
		return (EIO);

	ufsvfsp = (struct ufsvfs *) vfsp->vfs_data;
	if (!ufsvfsp || !ufsvfsp->vfs_root)
		return (EIO);	/* forced unmount */

	vp = ufsvfsp->vfs_root;
	VN_HOLD(vp);
	*vpp = vp;
	return (0);
}

/*
 * Get file system statistics.
 */
static int
ufs_statvfs(vfsp, sp)
	register struct vfs *vfsp;
	struct statvfs *sp;
{
	register struct fs *fsp;
	int blk, i;
	long max_avail, used;

	fsp = getfs(vfsp);
	if (fsp->fs_magic != FS_MAGIC)
		return (EINVAL);
	(void) bzero((caddr_t)sp, (int)sizeof (*sp));
	sp->f_bsize = fsp->fs_bsize;
	sp->f_frsize = fsp->fs_fsize;
	sp->f_blocks = fsp->fs_dsize;
	sp->f_bfree = fsp->fs_cstotal.cs_nbfree * fsp->fs_frag +
	    fsp->fs_cstotal.cs_nffree;
	/*
	 * avail = MAX(max_avail - used, 0)
	 */
	max_avail = fsp->fs_dsize - (fsp->fs_dsize / 100) * fsp->fs_minfree;

	used = (fsp->fs_dsize - sp->f_bfree);


	if (max_avail > used)
		sp->f_bavail = max_avail - used;
	else
		sp->f_bavail = 0;
	/*
	 * inodes
	 */
	sp->f_files =  fsp->fs_ncg * fsp->fs_ipg;
	sp->f_ffree = sp->f_favail = fsp->fs_cstotal.cs_nifree;
	sp->f_fsid = vfsp->vfs_dev;
	strcpy(sp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name);
	sp->f_flag = vf_to_stf(vfsp->vfs_flag);
	sp->f_namemax = MAXNAMLEN;
	if (fsp->fs_cpc == 0) {
		bzero(sp->f_fstr, 14);
		return (0);
	}
	blk = fsp->fs_spc * fsp->fs_cpc / NSPF(fsp);
	for (i = 0; i < blk; i += fsp->fs_frag) /* CSTYLED */
		/* void */;
	i -= fsp->fs_frag;
	blk = i / fsp->fs_frag;
/*	bcopy((char *)&(fsp->fs_rotbl[blk]), sp->f_fstr, 14);	*/
	bcopy((char *)&(fs_rotbl(fsp)[blk]), sp->f_fstr, 14);
	return (0);
}

/*
 * Flush any pending I/O to file system vfsp.
 * The ufs_update() routine will only flush *all* ufs files.
 * If vfsp is non-NULL, only sync this ufs (in preparation
 * for a umount).
 */
/*ARGSUSED*/
static int
ufs_sync(struct vfs *vfsp, short flag, struct cred *cr)
{
	extern struct vfsops ufs_vfsops;
	struct ufsvfs *ufsvfsp;
	struct fs *fs;
	int cheap = flag & SYNC_ATTR;
	int error;

	if (vfsp == NULL) {
		ufs_update(flag);
		return (0);
	}

	/* Flush a single ufs */
	if (vfsp->vfs_op != &ufs_vfsops || vfs_lock(vfsp) != 0)
		return (0);

	ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	if (!ufsvfsp)
		return (EIO);
	fs = ufsvfsp->vfs_fs;
	mutex_enter(&ufsvfsp->vfs_lock);

	/* Write back modified superblock */
	if (fs->fs_fmod == 0) {
		mutex_exit(&ufsvfsp->vfs_lock);
	} else {
		if (fs->fs_ronly != 0) {
			mutex_exit(&ufsvfsp->vfs_lock);
			vfs_unlock(vfsp);
			return (ufs_fault(ufsvfsp->vfs_root,
					    "fs = %s update: ro fs mod\n",
					    fs->fs_fsmnt));
		}
		fs->fs_fmod = 0;
		mutex_exit(&ufsvfsp->vfs_lock);

		TRANS_SBUPDATE(ufsvfsp, vfsp, TOP_SBUPDATE_UPDATE);
	}
	vfs_unlock(vfsp);

	(void) ufs_scan_inodes(1, ufs_sync_inode, (void *)cheap);
	bflush((dev_t)vfsp->vfs_dev);

	/*
	 * commit any outstanding async transactions
	 */
	curthread->t_flag |= T_DONTBLOCK;
	TRANS_BEGIN_SYNC(ufsvfsp, TOP_COMMIT_UPDATE, TOP_COMMIT_SIZE);
	TRANS_END_SYNC(ufsvfsp, error, TOP_COMMIT_UPDATE, TOP_COMMIT_SIZE);
	curthread->t_flag &= ~T_DONTBLOCK;

	return (0);
}


void
sbupdate(struct vfs *vfsp)
{
	struct ufsvfs *ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	struct fs *fs = ufsvfsp->vfs_fs;
	struct buf *bp;
	int blks;
	caddr_t space;
	int i;
	long size;

	/*
	 * for ulockfs processing, limit the superblock writes
	 */
	if ((ufsvfsp->vfs_ulockfs.ul_sbowner) &&
	    (curthread != ufsvfsp->vfs_ulockfs.ul_sbowner)) {
		/* process later */
		fs->fs_fmod = 1;
		return;
	}
	ULOCKFS_SET_MOD((&ufsvfsp->vfs_ulockfs));

	if (TRANS_ISTRANS(ufsvfsp)) {
		mutex_enter(&ufsvfsp->vfs_lock);
		ufs_sbwrite(ufsvfsp);
		mutex_exit(&ufsvfsp->vfs_lock);
		return;
	}

	blks = howmany(fs->fs_cssize, fs->fs_fsize);
	space = (caddr_t)fs->fs_csp[0];
	for (i = 0; i < blks; i += fs->fs_frag) {
		size = fs->fs_bsize;
		if (i + fs->fs_frag > blks)
			size = (blks - i) * fs->fs_fsize;
		bp = getblk(vfsp->vfs_dev,
			(daddr_t)(fsbtodb(fs, fs->fs_csaddr + i)),
			fs->fs_bsize);
		bcopy(space, bp->b_un.b_addr, (u_int)size);
		space += size;
		bp->b_bcount = size;
		bwrite(bp);
	}
	mutex_enter(&ufsvfsp->vfs_lock);
	ufs_sbwrite(ufsvfsp);
	mutex_exit(&ufsvfsp->vfs_lock);
}

static int
ufs_vget(vfsp, vpp, fidp)
	struct vfs *vfsp;
	struct vnode **vpp;
	struct fid *fidp;
{
	struct ufid *ufid;
	struct inode *ip;

	ufid = (struct ufid *)fidp;
	if ((vfsp->vfs_data == NULL) ||
	    ufs_iget(vfsp, ufid->ufid_ino, &ip, CRED())) {
		*vpp = NULL;
		return (0);
	}
	if (ip->i_gen != ufid->ufid_gen || ip->i_mode == 0) {
		VN_RELE(ITOV(ip));
		*vpp = NULL;
		return (0);
	}
	*vpp = ITOV(ip);
	return (0);
}

/* ARGSUSED */
static int
ufs_swapvp(vfsp, vpp, nm)
	struct vfs *vfsp;
	struct vnode **vpp;
	char *nm;
{
	return (ENOSYS);
}

/*
 * ufs vfs operations.
 */
struct vfsops ufs_vfsops = {
	ufs_mount,
	ufs_unmount,
	ufs_root,
	ufs_statvfs,
	ufs_sync,
	ufs_vget,
	ufs_mountroot,
	ufs_swapvp
};
