/*
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	Copyright (c) 1986-1992,1994, by Sun Microsystems, Inc.
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989 AT&T.
 *	All rights reserved.
 */

#pragma ident	"@(#)nfs3_vnops.c	1.80	95/10/26 SMI"
/* SVr4.0 1.40 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/mman.h>
#include <sys/pathname.h>
#include <sys/dirent.h>
#include <sys/debug.h>
#include <sys/vmsystm.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/swap.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/pathconf.h>
#include <sys/dnlc.h>
#include <sys/acl.h>
#include <sys/systeminfo.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/nfs_acl.h>
#include <nfs/lm.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_kmem.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>

#include <fs/fs_subr.h>

static int	nfs3_rdwrlbn(vnode_t *, page_t *, u_int, u_int, int, cred_t *);
static int	nfs3write(vnode_t *, caddr_t, u_int, long, cred_t *,
			stable_how *);
static int	nfs3read(vnode_t *, caddr_t, u_int, long, long *, cred_t *,
			post_op_attr *);
static int	nfs3setattr(vnode_t *, struct vattr *, int, cred_t *);
static int	nfs3lookup(vnode_t *, char *, vnode_t **,
			struct pathname *, int, vnode_t *, cred_t *);
static int	nfs3create(vnode_t *, char *, struct vattr *, enum vcexcl,
			int, vnode_t **, cred_t *);
static int	nfs3mknod(vnode_t *, char *, struct vattr *, enum vcexcl,
			int, vnode_t **, cred_t *);
static int	nfs3rename(vnode_t *, char *, vnode_t *, char *, cred_t *);
static int	do_nfs3readdir(vnode_t *, rddir_cache *, cred_t *);
static void	nfs3readdir(vnode_t *, rddir_cache *, cred_t *);
static void	nfs3readdirplus(vnode_t *, rddir_cache *, cred_t *);
static int	nfs3_bio(struct buf *, stable_how *, cred_t *);
static int	nfs3_getapage(vnode_t *, u_int, u_int, u_int *,
			page_t *[], u_int, struct seg *, caddr_t,
			enum seg_rw, cred_t *);
static void	nfs3_readahead(vnode_t *, u_int, caddr_t, struct seg *,
			cred_t *);
static int	nfs3_sync_putapage(vnode_t *, page_t *, u_int, u_int,
			int, cred_t *);
static int	nfs3_sync_pageio(vnode_t *, page_t *, u_int, u_int,
			int, cred_t *);
static int	nfs3_commit(vnode_t *, offset3, count3, cred_t *);
static void	nfs3_set_mod(vnode_t *);
static void	nfs3_get_commit(vnode_t *);
#ifdef unused
#ifdef DEBUG
static int	nfs3_no_uncommitted_pages(vnode_t *);
#endif
#endif
static int	nfs3_putpage_commit(vnode_t *, offset_t, u_int, cred_t *);

/*
 * Error flags used to pass information about certain special errors
 * which need to be handled specially.
 */
#define	NFS_CACHEINVALERR	-99
#define	NFS_EOF			-98
#define	NFS_VERF_MISMATCH	-97

#define	ISVDEV(t) ((t == VBLK) || (t == VCHR) || (t == VFIFO))

/*
 * These are the vnode ops routines which implement the vnode interface to
 * the networked file system.  These routines just take their parameters,
 * make them look networkish by putting the right info into interface structs,
 * and then calling the appropriate remote routine(s) to do the work.
 *
 * Note on directory name lookup cacheing:  If we detect a stale fhandle,
 * we purge the directory cache relative to that vnode.  This way, the
 * user won't get burned by the cache repeatedly.  See <nfs/rnode.h> for
 * more details on rnode locking.
 */

static int	nfs3_open(vnode_t **, int, cred_t *);
static int	nfs3_close(vnode_t *, int, int, offset_t, cred_t *);
static int	nfs3_read(vnode_t *, struct uio *, int, cred_t *);
static int	nfs3_write(vnode_t *, struct uio *, int, cred_t *);
static int	nfs3_ioctl(vnode_t *, int, int, int, cred_t *, int *);
static int	nfs3_getattr(vnode_t *, struct vattr *, int, cred_t *);
static int	nfs3_setattr(vnode_t *, struct vattr *, int, cred_t *);
static int	nfs3_access(vnode_t *, int, int, cred_t *);
static int	nfs3_readlink(vnode_t *, struct uio *, cred_t *);
static int	nfs3_fsync(vnode_t *, int, cred_t *);
static void	nfs3_inactive(vnode_t *, cred_t *);
static int	nfs3_lookup(vnode_t *, char *, vnode_t **,
			struct pathname *, int, vnode_t *, cred_t *);
static int	nfs3_create(vnode_t *, char *, struct vattr *, enum vcexcl,
			int, vnode_t **, cred_t *);
static int	nfs3_remove(vnode_t *, char *, cred_t *);
static int	nfs3_link(vnode_t *, vnode_t *, char *, cred_t *);
static int	nfs3_rename(vnode_t *, char *, vnode_t *, char *, cred_t *);
static int	nfs3_mkdir(vnode_t *, char *, register struct vattr *,
			vnode_t **, cred_t *);
static int	nfs3_rmdir(vnode_t *, char *, vnode_t *, cred_t *);
static int	nfs3_symlink(vnode_t *, char *, struct vattr *, char *,
			cred_t *);
static int	nfs3_readdir(vnode_t *, struct uio *, cred_t *, int *);
static int	nfs3_fid(vnode_t *, fid_t *);
static void	nfs3_rwlock(vnode_t *, int);
static void	nfs3_rwunlock(vnode_t *, int);
static int	nfs3_seek(vnode_t *, offset_t, offset_t *);
static int	nfs3_getpage(vnode_t *, offset_t, u_int, u_int *,
			page_t *[], u_int, struct seg *, caddr_t,
			enum seg_rw, cred_t *);
static int	nfs3_putpage(vnode_t *, offset_t, u_int, int, cred_t *);
static int	nfs3_map(vnode_t *, offset_t, struct as *, caddr_t *,
			u_int, u_char, u_char, u_int, cred_t *);
static int	nfs3_addmap(vnode_t *, offset_t, struct as *, caddr_t,
			u_int, u_char, u_char, u_int, cred_t *);
static int	nfs3_cmp(vnode_t *, vnode_t *);
static int	nfs3_frlock(vnode_t *, int, struct flock *, int, offset_t,
			cred_t *);
static int	nfs3_space(vnode_t *, int, struct flock *, int, offset_t,
			cred_t *);
static int	nfs3_realvp(vnode_t *, vnode_t **);
static int	nfs3_delmap(vnode_t *, offset_t, struct as *, caddr_t,
			u_int, u_int, u_int, u_int, cred_t *);
static int	nfs3_pathconf(vnode_t *, int, u_long *, cred_t *);
static int	nfs3_pageio(vnode_t *, page_t *, u_int, u_int, int, cred_t *);
static void	nfs3_dispose(vnode_t *, page_t *, int, int, cred_t *);
static int	nfs3_setsecattr(vnode_t *, vsecattr_t *, int, cred_t *);
static int	nfs3_getsecattr(vnode_t *, vsecattr_t *, int, cred_t *);

struct vnodeops nfs3_vnodeops = {
	nfs3_open,
	nfs3_close,
	nfs3_read,
	nfs3_write,
	nfs3_ioctl,
	fs_setfl,
	nfs3_getattr,
	nfs3_setattr,
	nfs3_access,
	nfs3_lookup,
	nfs3_create,
	nfs3_remove,
	nfs3_link,
	nfs3_rename,
	nfs3_mkdir,
	nfs3_rmdir,
	nfs3_readdir,
	nfs3_symlink,
	nfs3_readlink,
	nfs3_fsync,
	nfs3_inactive,
	nfs3_fid,
	nfs3_rwlock,
	nfs3_rwunlock,
	nfs3_seek,
	nfs3_cmp,
	nfs3_frlock,
	nfs3_space,
	nfs3_realvp,
	nfs3_getpage,
	nfs3_putpage,
	nfs3_map,
	nfs3_addmap,
	nfs3_delmap,
	fs_poll,
	nfs_dump,
	nfs3_pathconf,
	nfs3_pageio,
	fs_nosys,	/* dumpctl */
	nfs3_dispose,
	nfs3_setsecattr,
	nfs3_getsecattr
};

/*
 * XXX:  This is referenced in modstubs.s
 */
struct vnodeops *
nfs3_getvnodeops(void)
{

	return (&nfs3_vnodeops);
}

/* ARGSUSED */
static int
nfs3_open(vnode_t **vpp, int flag, cred_t *cr)
{
	int error = 0;
	struct vattr va;
	rnode_t *rp;

	rp = VTOR(*vpp);
	mutex_enter(&rp->r_statelock);
	if (rp->r_cred == NULL) {
		crhold(cr);
		rp->r_cred = cr;
	}
	mutex_exit(&rp->r_statelock);

	/*
	 * If there is no cached data or if close-to-open
	 * consistancy checking is turned off, we can avoid
	 * the over the wire getattr.  Otherwise, force a
	 * call to the server to get fresh attributes and to
	 * check caches. This is required for close-to-open
	 * consistency.
	 *
	 * The check for ESTALE is done because we may have
	 * acquired the reference to this vnode via the DNLC
	 * instead of going to the server.  The file on the
	 * server may have been removed and created while we
	 * had an entry for the file cached and we are still
	 * in the attribute cache timeout for the directory.
	 * If we got ESTALE, then the entry for the file has
	 * has been rmoved from the DNLC.  We arrange to
	 * start the system call all over again and the next
	 * time through, we will get the correct vnode for
	 * the file that exists on the server.
	 */
	if (((*vpp)->v_pages != NULL || rp->r_dir != NULL) &&
	    !(VTOMI(*vpp)->mi_flags & MI_NOCTO)) {
		va.va_mask = AT_ALL;
		error = nfs3_getattr_otw(*vpp, &va, cr);
		if (error == ESTALE)
			ttolwp(curthread)->lwp_eosys = RESTARTSYS;
	}

	return (error);
}

static int
nfs3_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr)
{
	register rnode_t *rp;
	int error;

	/*
	 * If we are using local locking for this filesystem, then
	 * release all of the SYSV style record locks.  Otherwise,
	 * we are doing network locking and we need to release all
	 * of the network locks.  All of the locks held by this
	 * process on this file are released no matter what the
	 * incoming reference count is.
	 */
	if (VTOMI(vp)->mi_flags & MI_LLOCK)
		cleanlocks(vp, ttoproc(curthread)->p_pid, 0);
	else
		(void) nfs_lockrelease(vp, flag, offset, cr);

	if (count > 1)
		return (0);

	/*
	 * If not doing "no close to open" and the file was open
	 * for write, then flush all dirty pages and commit them
	 * on the server.
	 *
	 * If not doing "close to open" semantics, then just
	 * do nothing and no error occurs.
	 *
	 * In either case, return either the error that happened
	 * from the flush and commit operation or from any
	 * errors that may have occurred during async processing.
	 */
	rp = VTOR(vp);
	if (!(VTOMI(vp)->mi_flags & MI_NOCTO) && (flag & FWRITE)) {
		error = nfs3_putpage_commit(vp, (offset_t)0, 0, cr);
		if (!error)
			error = rp->r_error;
		else {
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
	} else
		error = rp->r_error;

	/*
	 * If an error occurred and there are multiple references
	 * to this vnode, then purge any references which may be
	 * due to DNLC entries.  This will hopefully force the
	 * nfs_inactive to be invoked as soon as possible.
	 */
	if ((error && vp->v_count > 1) || rp->r_unldvp != NULL)
		dnlc_purge_vp(vp);

	return (error);
}

/* ARGSUSED */
static int
nfs3_read(vnode_t *vp, struct uio *uiop, int ioflag, cred_t *cr)
{
	rnode_t *rp;
	u_int off;
	caddr_t base;
	u_int flags;
	int n;
	int on;
	int error;
	int diff;

	rp = VTOR(vp);

	ASSERT(RW_READ_HELD(&rp->r_rwlock));

	if (vp->v_type != VREG)
		return (EISDIR);

	if (uiop->uio_resid == 0)
		return (0);

	if (uiop->uio_loffset > MAXOFF_T)
		return (EFBIG);

	if (uiop->uio_offset < 0 || uiop->uio_offset + uiop->uio_resid < 0)
		return (EINVAL);

	do {
		off = uiop->uio_offset & MAXBMASK; /* mapping offset */
		on = uiop->uio_offset & MAXBOFFSET; /* Relative offset */
		n = MIN(MAXBSIZE - on, uiop->uio_resid);

		error = nfs3_validate_caches(vp, cr);
		if (error)
			break;

		diff = rp->r_size - uiop->uio_offset;
		if (diff <= 0)
			break;
		if (diff < n)
			n = diff;

		base = segmap_getmapflt(segkmap, vp, off + on, (u_int)n,
			1, S_READ);

		error = uiomove(base + on, n, UIO_READ, uiop);

		if (!error) {
			if (vp->v_flag & VNOCACHE)
				flags = SM_INVAL;
			else {
				/*
				 * If read a whole block or read to eof,
				 * won't need this buffer again soon.
				 */
				if (n + on == MAXBSIZE ||
				    uiop->uio_offset == rp->r_size)
					flags = SM_DONTNEED;
				else
					flags = 0;
			}
			error = segmap_release(segkmap, base, flags);
		} else
			(void) segmap_release(segkmap, base, 0);
	} while (!error && uiop->uio_resid > 0);

	return (error);
}

static int
nfs3_write(vnode_t *vp, struct uio *uiop, int ioflag, cred_t *cr)
{
	rnode_t *rp;
	u_int off;
	caddr_t base;
	u_int flags;
	int remainder;
	int n;
	int on;
	int error;
	int resid;
	long offset;
	mntinfo_t *mi;
	u_long bsize;

	rp = VTOR(vp);

	ASSERT(RW_WRITE_HELD(&rp->r_rwlock));

	if (vp->v_type != VREG)
		return (EISDIR);

	if (uiop->uio_resid == 0)
		return (0);

	if (ioflag & FAPPEND) {
		struct vattr va;

		va.va_mask = AT_SIZE;
		error = nfs3getattr(vp, &va, cr);
		if (error)
			return (error);
		uiop->uio_offset = va.va_size;
	} else if (uiop->uio_loffset > MAXOFF_T)
		return (EFBIG);

	offset = uiop->uio_offset + uiop->uio_resid;
	if (uiop->uio_offset < 0 || offset < 0)
		return (EINVAL);

	/*
	 * Check to make sure that the process will not exceed
	 * its limit on file size.  It is okay to write up to
	 * the limit, but not beyond.  Thus, the write which
	 * reaches the limit will be short and the next write
	 * will return an error.
	 */
	remainder = 0;
	if (offset > uiop->uio_limit) {
		remainder = offset - uiop->uio_limit;
		uiop->uio_resid = uiop->uio_limit - uiop->uio_offset;
		if (uiop->uio_resid <= 0) {
			uiop->uio_resid += remainder;
			psignal(ttoproc(curthread), SIGXFSZ);
			return (EFBIG);
		}
	}

	resid = uiop->uio_resid;
	offset = uiop->uio_offset;

	/*
	 * For file locking -- bypass VM to retain consistency.
	 */
	if (vp->v_flag & VNOCACHE) {
		size_t bufsize;
		long count;
		u_int org_offset;
		stable_how stab_comm;

		if (rp->r_flags & RDONTWRITE) {
			error = rp->r_error;
			goto bottom;
		}

		bufsize = MIN(uiop->uio_resid, PAGESIZE);
		base = (caddr_t) kmem_alloc(bufsize, KM_SLEEP);
		stab_comm = FILE_SYNC;

		do {
			count = MIN(uiop->uio_resid, PAGESIZE);
			org_offset = (u_int)uiop->uio_offset;
			error = uiomove(base, count, UIO_WRITE, uiop);
			if (!error) {
				error = nfs3write(vp, base, org_offset,
						count, cr, &stab_comm);
			}
		} while (!error && uiop->uio_resid > 0);

		kmem_free(base, bufsize);
		goto bottom;
	}

	mi = VTOMI(vp);

	bsize = vp->v_vfsp->vfs_bsize;

	do {
		off = uiop->uio_offset & MAXBMASK; /* mapping offset */
		on = uiop->uio_offset & MAXBOFFSET; /* Relative offset */
		n = MIN(MAXBSIZE - on, uiop->uio_resid);

		if (rp->r_flags & RDONTWRITE) {
			error = rp->r_error;
			break;
		}

		base = segmap_getmapflt(segkmap, vp, off + on, (u_int)n,
			0, S_READ);

		error = writerp(rp, base + on, n, uiop);

		if (!error) {
			if (mi->mi_flags & MI_NOAC)
				flags = SM_WRITE;
			else if ((uiop->uio_offset % bsize) == 0 ||
				    IS_SWAPVP(vp)) {
				/*
				 * Have written a whole block.
				 * Start an asynchronous write
				 * and mark the buffer to
				 * indicate that it won't be
				 * needed again soon.
				 */
				flags = SM_WRITE | SM_ASYNC | SM_DONTNEED;
			} else
				flags = 0;
			if ((ioflag & (FSYNC|FDSYNC)) ||
			    (rp->r_flags & ROUTOFSPACE)) {
				flags &= ~SM_ASYNC;
				flags |= SM_WRITE;
			}
			error = segmap_release(segkmap, base, flags);
		} else
			(void) segmap_release(segkmap, base, 0);
	} while (!error && uiop->uio_resid > 0);

bottom:
	if (error == EINTR && (ioflag & (FSYNC|FDSYNC))) {
		uiop->uio_resid = resid;
		uiop->uio_offset = offset;
	} else
		uiop->uio_resid += remainder;

	return (error);
}

/*
 * Flags are composed of {B_ASYNC, B_INVAL, B_FREE, B_DONTNEED}
 */
static int
nfs3_rdwrlbn(vnode_t *vp, page_t *pp, u_int off, u_int len,
	int flags, cred_t *cr)
{
	register struct buf *bp;
	int error;
	page_t *savepp;
	u_char fsdata;
	stable_how stab_comm;

	bp = pageio_setup(pp, len, vp, flags);
	ASSERT(bp != NULL);

	/*
	 * pageio_setup should have set b_addr to 0.  This
	 * is correct since we want to do I/O on a page
	 * boundary.  bp_mapin will use this addr to calculate
	 * an offset, and then set b_addr to the kernel virtual
	 * address it allocated for us.
	 */
	ASSERT(bp->b_un.b_addr == 0);

	bp->b_edev = 0;
	bp->b_dev = 0;
	bp->b_blkno = btodb(off);
	bp_mapin(bp);

	if ((flags & (B_WRITE|B_ASYNC)) == (B_WRITE|B_ASYNC))
		stab_comm = UNSTABLE;
	else
		stab_comm = FILE_SYNC;

	error = nfs3_bio(bp, &stab_comm, cr);

	bp_mapout(bp);
	pageio_done(bp);

	if (stab_comm == UNSTABLE)
		fsdata = C_DELAYCOMMIT;
	else
		fsdata = C_NOCOMMIT;

	savepp = pp;
	do {
		pp->p_fsdata = fsdata;
	} while ((pp = pp->p_next) != savepp);

	return (error);
}

/*
 * Write to file.  Writes to remote server in largest size
 * chunks that the server can handle.  Write is synchronous.
 */
static int
nfs3write(vnode_t *vp, caddr_t base, u_int offset, long count, cred_t *cr,
	stable_how *stab_comm)
{
	mntinfo_t *mi;
	WRITE3args args;
	WRITE3res res;
	int error;
	int tsize;
	int douprintf;
	stable_how stable;
	rnode_t *rp;

	rp = VTOR(vp);
	mi = VTOMI(vp);
	douprintf = 1;

	stable = *stab_comm;
	*stab_comm = FILE_SYNC;

	args.file = *VTOFH3(vp);
	args.stable = stable;

	do {
		tsize = MIN(mi->mi_curwrite, count);
		args.offset = (offset3)offset;
		args.count = (count3)tsize;
		args.data.data_len = (u_int)tsize;
		args.data.data_val = base;
		error = rfs3call(mi, NFSPROC3_WRITE,
				xdr_WRITE3args, (caddr_t)&args,
				xdr_WRITE3res, (caddr_t)&res,
				cr, &douprintf, &res.status);
		if (error == ENFS_TRYAGAIN) {
			error = 0;
			continue;
		}
		if (error)
			return (error);
		error = geterrno3(res.status);
		if (!error) {
			if ((int)res.resok.count > tsize) {
				cmn_err(CE_WARN,
				"nfs3write: server wrote %d, requested was %d",
					(int)res.resok.count, tsize);
				return (EIO);
			}
			if (res.resok.committed == UNSTABLE) {
				*stab_comm = UNSTABLE;
				if (args.stable == DATA_SYNC ||
				    args.stable == FILE_SYNC) {
					cmn_err(CE_WARN,
			"nfs3write: server %s did not commit to stable storage",
						mi->mi_hostname);
					return (EIO);
				}
			}
			tsize = (int)res.resok.count;
			count -= tsize;
			base += tsize;
			offset += tsize;
			mutex_enter(&rp->r_statelock);
			if (rp->r_flags & RHAVEVERF) {
				if (bcmp((caddr_t)&rp->r_verf,
				    (caddr_t)&res.resok.verf,
				    sizeof (writeverf3)) != 0) {
					bcopy((caddr_t)&res.resok.verf,
					    (caddr_t)&rp->r_verf,
					    sizeof (writeverf3));
					nfs3_set_mod(vp);
				}
			} else {
				bcopy((caddr_t)&res.resok.verf,
				    (caddr_t)&rp->r_verf, sizeof (writeverf3));
				rp->r_flags |= RHAVEVERF;
			}
			mutex_exit(&rp->r_statelock);
		}
	} while (!error && count);

	if (res.status == NFS3_OK) {
#ifdef notyet
		/*
		 * Oops, can't do this yet.  There are page locking
		 * issues...
		 */
		nfs3_cache_wcc_data(vp, &res.resok.file_wcc, cr);
#else
		if (res.resok.file_wcc.after.attributes) {
			nfs3_attrcache(vp, &res.resok.file_wcc.after.attr,
					rp->r_seq);
		} else
			PURGE_ATTRCACHE(vp);
#endif
	} else {
#ifdef notyet
		/*
		 * Oops, can't do this yet.  There are page locking
		 * issues...
		 */
		nfs3_cache_wcc_data(vp, &res.resfail.file_wcc, cr);
#else
		if (res.resfail.file_wcc.after.attributes) {
			nfs3_attrcache(vp, &res.resfail.file_wcc.after.attr,
					rp->r_seq);
		} else
			PURGE_ATTRCACHE(vp);
#endif
	}

	return (error);
}

/*
 * Read from a file.  Reads data in largest chunks our interface can handle.
 */
static int
nfs3read(vnode_t *vp, caddr_t base, u_int offset, long count, long *residp,
	cred_t *cr, post_op_attr *fap)
{
	mntinfo_t *mi;
	READ3args args;
	READ3res res;
	register int tsize;
	int error;
	int rpcerror;
	int douprintf;

	mi = VTOMI(vp);
	douprintf = 1;

	args.file = *VTOFH3(vp);

	do {
		do {
			tsize = MIN(mi->mi_curread, count);
			res.resok.data.data_val = base;
			res.resok.data.data_len = tsize;
			args.offset = (offset3)offset;
			args.count = (count3)tsize;
			rpcerror = rfs3call(mi, NFSPROC3_READ,
					xdr_READ3args, (caddr_t)&args,
					xdr_READ3res, (caddr_t)&res,
					cr, &douprintf, &res.status);
		} while (rpcerror == ENFS_TRYAGAIN);

		if (rpcerror)
			break;

		error = geterrno3(res.status);
		if (!error) {
			if (res.resok.count != res.resok.data.data_len) {
				cmn_err(CE_WARN,
				"nfs3read: server %s returned incorrect amount",
					mi->mi_hostname);
				error = EIO;
			} else {
				count -= res.resok.count;
				base += res.resok.count;
				offset += res.resok.count;
			}
		}
	} while (!error && count && !res.resok.eof);

	*residp = count;

	if (!rpcerror) {
		if (!error)
			*fap = res.resok.file_attributes;
	} else
		error = rpcerror;

	return (error);
}

/* ARGSUSED */
static int
nfs3_ioctl(vnode_t *vp, int com, int arg, int flag, cred_t *cr, int *rvalp)
{

	return (ENOTTY);
}

static int
nfs3_getattr(vnode_t *vp, struct vattr *vap, int flags, cred_t *cr)
{
	int error;
	rnode_t *rp;

	/*
	 * If we are just being asked for the size of the file
	 * and it has been specified that the return value will
	 * just be used as a hint, then just return the client's
	 * notion of the size of the file without checking to
	 * make sure that the attribute cache is up to date.
	 * The whole point is to avoid an over the wire GETATTR
	 * call.
	 */
	if (vap->va_mask == AT_SIZE && (flags & ATTR_HINT)) {
		vap->va_size = VTOR(vp)->r_size;
		return (0);
	}

	/*
	 * Only need to flush pages if asking for the mtime
	 * and if there any dirty pages or any outstanding
	 * asynchronous requests for this file.
	 */
	if (vap->va_mask & AT_MTIME) {
		rp = VTOR(vp);
		if ((rp->r_flags & RDIRTY) || rp->r_count > 0) {
			error = nfs3_putpage(vp, (offset_t)0, 0, 0, cr);
			if (error && (error == ENOSPC || error == EDQUOT)) {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
		}
	}

	return (nfs3getattr(vp, vap, cr));
}

static int
nfs3_setattr(vnode_t *vp, struct vattr *vap, int flags, cred_t *cr)
{
	int error;
	long mask;
	rnode_t *rp;

	mask = vap->va_mask;

	if (mask & AT_NOSET)
		return (EINVAL);

	rp = VTOR(vp);

	if (mask & (AT_UID | AT_GID)) {
		/*
		 * To change file ownership, a process must be the
		 * super-user if:
		 *
		 * If it is not the owner of the file, or
		 * if doing restricted chown semantics and
		 * either changing the ownership to someone else or
		 * changing the group to a group that we are not
		 * currently in.
		 */
		if (cr->cr_uid != rp->r_attr.va_uid ||
		    (rstchown &&
		    ((mask & AT_UID) && vap->va_uid != rp->r_attr.va_uid) ||
		    ((mask & AT_GID) && !groupmember(vap->va_gid, cr)))) {
			if (!suser(cr))
				return (EPERM);
		}
		/*
		 * In any case, clear the setuid and setgid bits on this
		 * file to eliminate a possible security problem.
		 */
		if (cr->cr_uid != 0) {
			if (!(mask & AT_MODE)) {
				vap->va_mask |= AT_MODE;
				vap->va_mode = rp->r_attr.va_mode;
			}
			vap->va_mode &= ~(VSUID | VSGID);
		}
	}
	if (mask & (AT_MTIME | AT_ATIME)) {
		/*
		 * To change either the access time or modified time
		 * on a file, a process must be either the owner of
		 * file, super-user, or have write permission on the
		 * file.  If it is neither the owner or super-user,
		 * then don't let it set the times to specific values
		 * as this may be used to mask security problems.
		 */
		if (cr->cr_uid != rp->r_attr.va_uid && cr->cr_uid != 0) {
			if (flags & ATTR_UTIME)
				return (EPERM);
			error = nfs3_access(vp, VWRITE, 0, cr);
			if (error)
				return (error);
		}
	}
	return (nfs3setattr(vp, vap, flags, cr));
}

static int
nfs3setattr(vnode_t *vp, struct vattr *vap, int flags, cred_t *cr)
{
	int error;
	long mask;
	SETATTR3args args;
	SETATTR3res res;
	int douprintf;
	rnode_t *rp;
	struct vattr va;

	mask = vap->va_mask;

	rp = VTOR(vp);

	/*
	 * Only need to flush pages if there are any pages and
	 * if the file is marked as dirty in some fashion.  The
	 * file must be flushed so that we can accurately
	 * determine the size of the file and the cached data
	 * after the SETATTR returns.  A file is considered to
	 * be dirty if it is either marked with RDIRTY, has
	 * outstanding i/o's active, or is mmap'd.  In this
	 * last case, we can't tell whether there are dirty
	 * pages, so we flush just to be sure.
	 */
	if (vp->v_pages != NULL &&
	    ((rp->r_flags & RDIRTY) ||
	    rp->r_count > 0 ||
	    rp->r_mapcnt > 0)) {
		ASSERT(vp->v_type != VCHR);
		error = nfs3_putpage(vp, (offset_t)0, 0, 0, cr);
		if (error && (error == ENOSPC || error == EDQUOT)) {
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
	}

	args.object = *RTOFH3(rp);
	vattr_to_sattr3(vap, &args.new_attributes);
	if ((mask & AT_ATIME) && !(flags & ATTR_UTIME))
		args.new_attributes.atime.set_it = SET_TO_SERVER_TIME;
	if ((mask & AT_MTIME) && !(flags & ATTR_UTIME))
		args.new_attributes.mtime.set_it = SET_TO_SERVER_TIME;

tryagain:
	if (mask & AT_SIZE) {
		args.guard.check = TRUE;
		args.guard.obj_ctime.seconds = rp->r_attr.va_ctime.tv_sec;
		args.guard.obj_ctime.nseconds = rp->r_attr.va_ctime.tv_nsec;
	} else
		args.guard.check = FALSE;

	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_SETATTR,
			xdr_SETATTR3args, (caddr_t)&args,
			xdr_SETATTR3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	/*
	 * Purge the access cache is changing either the owner
	 * of the file, the group owner, or the mode.  These
	 * may change the access permissions of the file, so
	 * purge old information and start over again.
	 */
	if ((mask & (AT_UID | AT_GID | AT_MODE)) && rp->r_acc != NULL)
		nfs_purge_access_cache(vp);

	if (error) {
		PURGE_ATTRCACHE(vp);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		/*
		 * If changing the size of the file, invalidate
		 * any local cached data which is no longer part
		 * of the file.  We also possibly invalidate the
		 * last page in the file.  We could use
		 * pvn_vpzero(), but this would mark the page as
		 * modified and require it to be written back to
		 * the server for no particularly good reason.
		 * This way, if we access it, then we bring it
		 * back in.  A read should be cheaper than a
		 * write.
		 */
		if (mask & AT_SIZE) {
			nfs_invalidate_pages(vp,
					(u_int)(vap->va_size & PAGEMASK), cr);
		}
		nfs3_cache_wcc_data(vp, &res.resok.obj_wcc, cr);
	} else {
		nfs3_cache_wcc_data(vp, &res.resfail.obj_wcc, cr);
		/*
		 * If we got back a "not synchronized" error, then
		 * we need to retry with a new guard value.  The
		 * guard value used is the change time.  If the
		 * server returned post_op_attr, then we can just
		 * retry because we have the latest attributes.
		 * Otherwise, we issue a GETATTR to get the latest
		 * attributes and then retry.  If we couldn't get
		 * the attributes this way either, then we give
		 * up because we can't complete the operation as
		 * required.
		 */
		if (res.status == NFS3ERR_NOT_SYNC) {
			if (res.resfail.obj_wcc.after.attributes)
				goto tryagain;
			va.va_mask = AT_CTIME;
			if (nfs3getattr(vp, &va, cr) == 0)
				goto tryagain;
		}
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

#ifdef DEBUG
static int nfs3_access_cache_hits = 0;
static int nfs3_access_cache_misses = 0;
#endif

/* ARGSUSED */
static int
nfs3_access(vnode_t *vp, int mode, int flags, cred_t *cr)
{
	int error;
	ACCESS3args args;
	ACCESS3res res;
	int douprintf;
	access_cache *acp;
	uint32 acc;
	rnode_t *rp;
	cred_t *cred;

	acc = 0;
	if (mode & VREAD)
		acc |= ACCESS3_READ;
	if (mode & VWRITE) {
		if ((vp->v_vfsp->vfs_flag & VFS_RDONLY) && !ISVDEV(vp->v_type))
			return (EROFS);
		if (vp->v_type == VDIR)
			acc |= ACCESS3_DELETE;
		acc |= ACCESS3_MODIFY | ACCESS3_EXTEND;
	}
	if (mode & VEXEC) {
		if (vp->v_type == VDIR)
			acc |= ACCESS3_LOOKUP;
		else
			acc |= ACCESS3_EXECUTE;
	}

	rp = VTOR(vp);
	if (rp->r_acc != NULL) {
		error = nfs3_validate_caches(vp, cr);
		if (error)
			return (error);
	}

	mutex_enter(&rp->r_statelock);
	for (acp = rp->r_acc; acp != NULL; acp = acp->next) {
		/*
		 * Look for an entry by comparing credentials.
		 */
		if (crcmp(acp->cred, cr) == 0) {
			if ((acp->known & acc) == acc) {
#ifdef DEBUG
				nfs3_access_cache_hits++;
#endif
				if ((acp->allowed & acc) == acc) {
					mutex_exit(&rp->r_statelock);
					return (0);
				}
				mutex_exit(&rp->r_statelock);
				return (EACCES);
			}
			break;
		}
	}
	mutex_exit(&rp->r_statelock);

#ifdef DEBUG
	nfs3_access_cache_misses++;
#endif

	args.object = *VTOFH3(vp);
	args.access = acc;

	cred = cr;
tryagain:
	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_ACCESS,
			xdr_ACCESS3args, (caddr_t)&args,
			xdr_ACCESS3res, (caddr_t)&res,
			cred, &douprintf, &res.status);

	if (error) {
		if (cred != cr)
			crfree(cred);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_post_op_attr(vp, &res.resok.obj_attributes, cr);
		if (args.access != res.resok.access) {
			/*
			 * The following code implements the semantic that
			 * a setuid root program has *at least* the
			 * permissions of the user that is running the
			 * program.  See rfs3call() for more portions
			 * of the implementation of this functionality.
			 */
			if (cred->cr_uid == 0 && cred->cr_ruid != 0) {
				cred = crdup(cr);
				cred->cr_uid = cred->cr_ruid;
				goto tryagain;
			}
			error = EACCES;
		}
		mutex_enter(&rp->r_statelock);
		for (acp = rp->r_acc; acp != NULL; acp = acp->next) {
			/*
			 * Look for an entry by comparing credentials.
			 */
			if (crcmp(acp->cred, cr) == 0)
				break;
		}
		if (acp != NULL) {
			acp->known |= args.access;
			acp->allowed &= ~args.access;
			acp->allowed |= res.resok.access;
		} else {
#ifdef DEBUG
			acp = access_cache_alloc(sizeof (*acp), KM_NOSLEEP);
#else
			acp = (access_cache *) kmem_alloc(sizeof (*acp),
							KM_NOSLEEP);
#endif
			if (acp != NULL) {
				acp->next = rp->r_acc;
				rp->r_acc = acp;
				crhold(cr);
				acp->cred = cr;
				acp->known = args.access;
				acp->allowed = res.resok.access;
			}
		}
		mutex_exit(&rp->r_statelock);
	} else {
		nfs3_cache_post_op_attr(vp, &res.resfail.obj_attributes, cr);
		PURGE_STALE_FH(error, vp, cr);
	}

	if (cred != cr)
		crfree(cred);

	return (error);
}

static int nfs3_do_symlink_cache = 1;

static int
nfs3_readlink(vnode_t *vp, struct uio *uiop, cred_t *cr)
{
	int error;
	READLINK3args args;
	READLINK3res res;
	rnode_t *rp;
	int douprintf;
	int len;

	/*
	 * Can't readlink anything other than a symbolic link.
	 */
	if (vp->v_type != VLNK)
		return (EINVAL);

	rp = VTOR(vp);
	if (nfs3_do_symlink_cache && rp->r_symlink.contents != NULL) {
		error = nfs3_validate_caches(vp, cr);
		if (error)
			return (error);
		mutex_enter(&rp->r_statelock);
		if (rp->r_symlink.contents != NULL) {
			error = uiomove(rp->r_symlink.contents,
					rp->r_symlink.len, UIO_READ, uiop);
			mutex_exit(&rp->r_statelock);
			return (error);
		}
		mutex_exit(&rp->r_statelock);
	}

	args.symlink = *VTOFH3(vp);
#ifdef DEBUG
	res.resok.data = symlink_cache_alloc(MAXPATHLEN, KM_SLEEP);
#else
	res.resok.data = (char *)kmem_alloc(MAXPATHLEN, KM_SLEEP);
#endif

	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_READLINK,
			xdr_READLINK3args, (caddr_t)&args,
			xdr_READLINK3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
#ifdef DEBUG
		symlink_cache_free((void *)res.resok.data, MAXPATHLEN);
#else
		kmem_free((caddr_t)res.resok.data, MAXPATHLEN);
#endif
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_post_op_attr(vp, &res.resok.symlink_attributes, cr);
		len = strlen(res.resok.data);
		error = uiomove(res.resok.data, len, UIO_READ, uiop);
		if (nfs3_do_symlink_cache && rp->r_symlink.contents == NULL) {
			mutex_enter(&rp->r_statelock);
			if (rp->r_symlink.contents == NULL) {
				rp->r_symlink.contents = res.resok.data;
				rp->r_symlink.len = len;
				rp->r_symlink.size = MAXPATHLEN;
				mutex_exit(&rp->r_statelock);
			} else {
				mutex_exit(&rp->r_statelock);
#ifdef DEBUG
				symlink_cache_free((void *)res.resok.data,
						    MAXPATHLEN);
#else
				kmem_free((caddr_t)res.resok.data, MAXPATHLEN);
#endif
			}
		} else {
#ifdef DEBUG
			symlink_cache_free((void *)res.resok.data, MAXPATHLEN);
#else
			kmem_free((caddr_t)res.resok.data, MAXPATHLEN);
#endif
		}
	} else {
		nfs3_cache_post_op_attr(vp, &res.resfail.symlink_attributes,
					cr);
		PURGE_STALE_FH(error, vp, cr);
#ifdef DEBUG
		symlink_cache_free((void *)res.resok.data, MAXPATHLEN);
#else
		kmem_free((caddr_t)res.resok.data, MAXPATHLEN);
#endif
	}

	/*
	 * The over the wire error for attempting to readlink something
	 * other than a symbolic link is ENXIO.  However, we need to
	 * return EINVAL instead of ENXIO, so we map it here.
	 */
	return (error == ENXIO ? EINVAL : error);
}

/*
 * Flush local dirty pages to stable storage on the server.
 *
 * If FNODSYNC is specified, then there is nothing to do because
 * metadata changes are not cached on the client before being
 * sent to the server.
 */
static int
nfs3_fsync(vnode_t *vp, int syncflag, cred_t *cr)
{
	int error;

	if ((syncflag & FNODSYNC) || IS_SWAPVP(vp))
		return (0);
	error = nfs3_putpage_commit(vp, (offset_t)0, 0, cr);
	if (!error)
		error = VTOR(vp)->r_error;
	return (error);
}

/*
 * Weirdness: if the file was removed or the target of a rename
 * operation while it was open, it got renamed instead.  Here we
 * remove the renamed file.
 */
static void
nfs3_inactive(vnode_t *vp, cred_t *cr)
{
	rnode_t *rp;

	ASSERT(vp != &nfs3_notfound);

	rp = VTOR(vp);
redo:
	mutex_enter(&nfs_rtable_lock);
	if (rp->r_unldvp != NULL) {
		/*
		 * Save the vnode pointer for the directory where the
		 * unlinked-open file got renamed, then set it to NULL
		 * to prevent another thread from getting here before
		 * we're done with the remove.  While we have the
		 * statelock, make local copies of the pertinent rnode
		 * fields.  If we weren't to do this in an atomic way, the
		 * the unl* fields could become inconsistent with respect
		 * to each other due to a race condition between this
		 * code and nfs_remove().  See bug report 1034328.
		 */
		mutex_enter(&rp->r_statelock);
		if (rp->r_unldvp != NULL) {
			register vnode_t *unldvp;
			register char *unlname;
			register cred_t *unlcred;
			REMOVE3args args;
			REMOVE3res res;
			int douprintf;
#ifdef notyet
			int error;
#endif

			unldvp = rp->r_unldvp;
			rp->r_unldvp = NULL;
			unlname = rp->r_unlname;
			rp->r_unlname = NULL;
			unlcred = rp->r_unlcred;
			rp->r_unlcred = NULL;
			mutex_exit(&rp->r_statelock);
			mutex_exit(&nfs_rtable_lock);

			/*
			 * Do the remove operation on the renamed file
			 */
			setdiropargs3(&args.object, unlname, unldvp);
			douprintf = 1;
#ifdef notyet
			Can't do this yet.  We may be being called from
			dnlc_purge_XXX while that routine is holding a
			mutex lock to the nc_rele list.  The calls to
			nfs3_cache_wcc_data may result in calls to
			dnlc_purge_XXX.  This will result in a deadlock.

			error = rfs3call(VTOMI(unldvp), NFSPROC3_REMOVE,
					xdr_REMOVE3args, (caddr_t)&args,
					xdr_REMOVE3res, (caddr_t)&res,
					unlcred, &douprintf, &res.status);

			if (error)
				PURGE_ATTRCACHE(unldvp);
			else {
				error = geterrno3(res.status);
				if (!error) {
					nfs3_cache_wcc_data(unldvp,
						&res.resok.dir_wcc, cr);
					if (VTOR(unldvp)->r_dir != NULL)
						nfs_purge_rddir_cache(unldvp);
				} else {
					nfs3_cache_wcc_data(unldvp,
						&res.resfail.dir_wcc, cr);
					PURGE_STALE_FH(error, unldvp, cr);
				}
			}
#else
			(void) rfs3call(VTOMI(unldvp), NFSPROC3_REMOVE,
					xdr_REMOVE3args, (caddr_t)&args,
					xdr_REMOVE3res, (caddr_t)&res,
					unlcred, &douprintf, &res.status);
			PURGE_ATTRCACHE(unldvp);
#endif

			/*
			 * Release stuff held for the remove
			 */
			VN_RELE(unldvp);
			kmem_free((caddr_t)unlname, MAXNAMELEN);
			crfree(unlcred);
			goto redo;
		}
		mutex_exit(&rp->r_statelock);
	}

	ASSERT(rp->r_mapcnt == 0);
	rp_addfree(rp, cr);
	mutex_exit(&nfs_rtable_lock);
}

/*
 * Remote file system operations having to do with directory manipulation.
 */

static int
nfs3_lookup(vnode_t *dvp, char *nm, vnode_t **vpp,
	struct pathname *pnp, int flags, vnode_t *rdir, cred_t *cr)
{
	int error;
	vnode_t *vp;

	error = nfs3lookup(dvp, nm, vpp, pnp, flags, rdir, cr);

	/*
	 * If vnode is a device, create special vnode.
	 */
	if (!error && ISVDEV((*vpp)->v_type)) {
		vp = *vpp;
		*vpp = specvp(vp, vp->v_rdev, vp->v_type, cr);
		VN_RELE(vp);
	}

	return (error);
}

static int nfs3_lookup_neg_cache = 0;

#ifdef DEBUG
static int nfs3_lookup_dnlc_hits = 0;
static int nfs3_lookup_dnlc_misses = 0;
static int nfs3_lookup_dnlc_neg_hits = 0;
static int nfs3_lookup_dnlc_disappears = 0;
static int nfs3_lookup_dnlc_lookups = 0;
#endif

/* ARGSUSED */
static int
nfs3lookup(vnode_t *dvp, char *nm, vnode_t **vpp,
	struct pathname *pnp, int flags, vnode_t *rdir, cred_t *cr)
{
	int error;
	LOOKUP3args args;
	LOOKUP3res res;
	int douprintf;
	struct vattr vattr;
	vnode_t *vp;

	/*
	 * If lookup is for "", just return dvp.  Don't need
	 * to send it over the wire, look it up in the dnlc,
	 * or perform any access checks.
	 */
	if (*nm == '\0') {
		VN_HOLD(dvp);
		*vpp = dvp;
		return (0);
	}

	/*
	 * Can't do lookups in non-directories.
	 */
	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	/*
	 * If lookup is for ".", just return dvp.  Don't need
	 * to send it over the wire or look it up in the dnlc,
	 * just need to check access.
	 */
	if (strcmp(nm, ".") == 0) {
		error = nfs3_access(dvp, VEXEC, 0, cr);
		if (error)
			return (error);
		VN_HOLD(dvp);
		*vpp = dvp;
		return (0);
	}

	/*
	 * Lookup this name in the DNLC.  If successful, then check
	 * access and then recheck the DNLC.  The DNLC is rechecked
	 * just in case this entry got invalidated during the call
	 * to nfs3_access.
	 *
	 * The assumption is made that nfs3_access invokes
	 * nfs3_validate_caches to make sure the access cache is valid.
	 * If the attributes were timed out, an ACCESS call is made to
	 * the server which returns new attributes.  When this happens,
	 * the attribute cache is updated and the DNLC will be purged
	 * if appropriate.
	 *
	 * Another assumption that is being made is that it is safe
	 * to say that a file exists which may not on the server.
	 * Any operations to the server will fail with ESTALE.
	 */
#ifdef DEBUG
	nfs3_lookup_dnlc_lookups++;
#endif
	vp = dnlc_lookup(dvp, nm, NOCRED);
	if (vp != NULL) {
		VN_RELE(vp);
		if (vp == &nfs3_notfound) {
			PURGE_ATTRCACHE(dvp);
			error = nfs3_validate_caches(dvp, cr);
			if (error)
				return (error);
		}
		error = nfs3_access(dvp, VEXEC, 0, cr);
		if (error)
			return (error);
		vp = dnlc_lookup(dvp, nm, NOCRED);
		if (vp != NULL) {
			if (vp == &nfs3_notfound) {
				VN_RELE(vp);
#ifdef DEBUG
				nfs3_lookup_dnlc_neg_hits++;
#endif
				return (ENOENT);
			}
			*vpp = vp;
#ifdef DEBUG
			nfs3_lookup_dnlc_hits++;
#endif
			return (0);
		}
#ifdef DEBUG
		nfs3_lookup_dnlc_disappears++;
#endif
	}
#ifdef DEBUG
	else
		nfs3_lookup_dnlc_misses++;
#endif

	setdiropargs3(&args.what, nm, dvp);

	douprintf = 1;
	error = rfs3call(VTOMI(dvp), NFSPROC3_LOOKUP,
			xdr_LOOKUP3args, (caddr_t)&args,
			xdr_LOOKUP3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_post_op_attr(dvp, &res.resok.dir_attributes, cr);
		if (res.resok.obj_attributes.attributes) {
			vp = makenfs3node(&res.resok.object,
					&res.resok.obj_attributes.attr,
					dvp->v_vfsp, cr);
		} else {
			vp = makenfs3node(&res.resok.object, NULL,
					dvp->v_vfsp, cr);
			if (vp->v_type == VNON) {
				vattr.va_mask = AT_TYPE;
				error = nfs3getattr(vp, &vattr, cr);
				if (error) {
					VN_RELE(vp);
					return (error);
				}
				vp->v_type = vattr.va_type;
			}
		}
		dnlc_enter(dvp, nm, vp, NOCRED);
		*vpp = vp;
	} else {
		nfs3_cache_post_op_attr(dvp, &res.resfail.dir_attributes, cr);
		PURGE_STALE_FH(error, dvp, cr);
		if (error == ENOENT && nfs3_lookup_neg_cache)
			dnlc_enter(dvp, nm, &nfs3_notfound, NOCRED);
	}

	return (error);
}

#ifdef DEBUG
static int nfs3_create_misses = 0;
#endif

static int
nfs3_create(vnode_t *dvp, char *nm, struct vattr *va,
	enum vcexcl exclusive, int mode, vnode_t **vpp, cred_t *cr)
{
	int error;
	vnode_t *vp;

top:
	error = nfs3_lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
	if (!error) {
		if (exclusive == EXCL)
			error = EEXIST;
		else if (vp->v_type == VDIR && (mode & VWRITE))
			error = EISDIR;
		else if (!(error = VOP_ACCESS(vp, mode, 0, cr))) {
			if ((va->va_mask & AT_SIZE) && vp->v_type == VREG) {
				va->va_mask = AT_SIZE;
				error = nfs3setattr(vp, va, 0, cr);
			}
		}
		if (error) {
			VN_RELE(vp);
			/*
			 * Check the comments above in nfs3_open() for
			 * the explanation for this.
			 */
			if (error == ESTALE)
				ttolwp(curthread)->lwp_eosys = RESTARTSYS;
		} else
			*vpp = vp;
		return (error);
	}

	dnlc_remove(dvp, nm);

	/*
	 * Decide what the group-id of the created file should be.
	 * Set it in attribute list as advisory...
	 */
	va->va_gid = setdirgid(dvp, cr);
	va->va_mask |= AT_GID;

	ASSERT(va->va_mask & AT_TYPE);
	if (va->va_type == VREG) {
		ASSERT(va->va_mask & AT_MODE);
		if (MANDMODE(va->va_mode))
			return (EACCES);
		error = nfs3create(dvp, nm, va, exclusive, mode, vpp, cr);
		/*
		 * If this is not an exclusive create, then the CREATE
		 * request will be made with the GUARDED mode set.  This
		 * means that the server will return EEXIST if the file
		 * exists.  The file could exist because of a retransmitted
		 * request.  In this case, we recover by starting over and
		 * checking to see whether the file exists.  This second
		 * time through it should and a CREATE request will not be
		 * sent.
		 *
		 * This handles the problem of a dangling CREATE request
		 * which contains attributes which indicate that the file
		 * should be truncated.  This retransmitted request could
		 * possibly truncate valid data in the file if not caught
		 * by the duplicate request mechanism on the server or if
		 * not caught by other means.  The scenario is:
		 *
		 * Client transmits CREATE request with size = 0
		 * Client times out, retransmits request.
		 * Response to the first request arrives from the server
		 *  and the client proceeds on.
		 * Client writes data to the file.
		 * The server now processes retransmitted CREATE request
		 *  and truncates file.
		 *
		 * The use of the GUARDED CREATE request prevents this from
		 * happening because the retransmitted CREATE would fail
		 * with EEXIST and would not truncate the file.
		 */
		if (error == EEXIST && exclusive == NONEXCL) {
#ifdef DEBUG
			nfs3_create_misses++;
#endif
			goto top;
		}
		return (error);
	}
	return (nfs3mknod(dvp, nm, va, exclusive, mode, vpp, cr));
}

/* ARGSUSED */
static int
nfs3create(vnode_t *dvp, char *nm, struct vattr *va,
	enum vcexcl exclusive, int mode, vnode_t **vpp, cred_t *cr)
{
	int error;
	CREATE3args args;
	CREATE3res res;
	int douprintf;
	vnode_t *vp;
	struct vattr vattr;
	timestruc_t *verfp;

	setdiropargs3(&args.where, nm, dvp);
	if (exclusive == EXCL) {
		args.how.mode = EXCLUSIVE;
		/*
		 * Construct the create verifier.  This verifier needs
		 * to be unique between different clients.  It also needs
		 * to vary for each exclusive create request generated
		 * from the client to the server.
		 *
		 * The first attempt is made to use the hostid and a
		 * unique number on the client.  If the hostid has not
		 * been set, the high resolution time that the exclusive
		 * create request is being made is used.  This will work
		 * unless two different clients, both with the hostid
		 * not set, attempt an exclusive create request on the
		 * same file, at exactly the same clock time.  The
		 * chances of this happening seem small enough to be
		 * reasonable.
		 */
		verfp = (timestruc_t *)&args.how.createhow3_u.verf;
		verfp->tv_sec = nfs_atoi(hw_serial);
		if (verfp->tv_sec != 0)
			verfp->tv_nsec = newnum();
		else {
			bcopy((caddr_t)&hrestime, (caddr_t)verfp,
			    sizeof (hrestime));
		}
	} else {
		/*
		 * Issue the non-exclusive create in guarded mode.  This
		 * may result in some false EEXIST responses for
		 * retransmitted requests, but these will be handled at
		 * a higher level.  By using GUARDED, duplicate requests
		 * to do file truncation and possible access problems
		 * can be avoided.
		 */
		args.how.mode = GUARDED;
		vattr_to_sattr3(va, &args.how.createhow3_u.obj_attributes);
	}

	douprintf = 1;
	error = rfs3call(VTOMI(dvp), NFSPROC3_CREATE,
			xdr_CREATE3args, (caddr_t)&args,
			xdr_CREATE3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		PURGE_ATTRCACHE(dvp);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_wcc_data(dvp, &res.resok.dir_wcc, cr);
		if (VTOR(dvp)->r_dir != NULL)
			nfs_purge_rddir_cache(dvp);

		if (!res.resok.obj.handle_follows) {
			error = nfs3lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
			if (error)
				return (error);
		} else {
			if (res.resok.obj_attributes.attributes) {
				vp = makenfs3node(&res.resok.obj.handle,
						&res.resok.obj_attributes.attr,
						dvp->v_vfsp, cr);
			} else {
				vp = makenfs3node(&res.resok.obj.handle, NULL,
						dvp->v_vfsp, cr);
				if (vp->v_type == VNON) {
					vattr.va_mask = AT_TYPE;
					error = nfs3getattr(vp, &vattr, cr);
					if (error) {
						VN_RELE(vp);
						return (error);
					}
					vp->v_type = vattr.va_type;
				}
			}
			dnlc_enter(dvp, nm, vp, NOCRED);
		}

		if (exclusive == EXCL) {
			/*
			 * If doing an exclusive create, then generate
			 * a SETATTR to set the initial attributes.
			 * Try to set the mtime and the atime to the
			 * server's current time.  It is somewhat
			 * expected that these fields will be used to
			 * store the exclusive create cookie.  If not,
			 * server implementors will need to know that
			 * a SETATTR will follow an exclusive create
			 * and the cookie should be destroyed if
			 * appropriate.
			 *
			 * The AT_GID and AT_SIZE bits are turned off
			 * so that the SETATTR request will not attempt
			 * to process these.  The gid will be set
			 * separately if appropriate.  The size is turned
			 * off because it is assumed that a new file will
			 * be created empty and if the file wasn't empty,
			 * then the exclusive create will have failed
			 * because the file must have existed already.
			 * Therefore, no truncate operation is needed.
			 */
			va->va_mask |= (AT_MTIME | AT_ATIME);
			va->va_mask &= ~(AT_GID | AT_SIZE);
			error = nfs3setattr(vp, va, 0, cr);
			if (error) {
				/*
				 * Couldn't correct the attributes of
				 * the newly created file and the
				 * attributes are wrong.  Remove the
				 * file and return an error to the
				 * application.
				 */
				VN_RELE(vp);
				(void) nfs3_remove(dvp, nm, cr);
				return (error);
			}
		}

		if (va->va_gid != VTOR(vp)->r_attr.va_gid) {
			/*
			 * If the gid on the file isn't right, then
			 * generate a SETATTR to attempt to change
			 * it.  This may or may not work, depending
			 * upon the server's semantics for allowing
			 * file ownership changes.
			 */
			va->va_mask = AT_GID;
			(void) nfs3setattr(vp, va, 0, cr);
		}

		/*
		 * If vnode is a device create special vnode
		 */
		if (ISVDEV(vp->v_type)) {
			*vpp = specvp(vp, vp->v_rdev, vp->v_type, cr);
			VN_RELE(vp);
		} else
			*vpp = vp;
	} else {
		nfs3_cache_wcc_data(dvp, &res.resfail.dir_wcc, cr);
		PURGE_STALE_FH(error, dvp, cr);
	}

	return (error);
}

/* ARGSUSED */
static int
nfs3mknod(vnode_t *dvp, char *nm, struct vattr *va,
	enum vcexcl exclusive, int mode, vnode_t **vpp, cred_t *cr)
{
	int error;
	MKNOD3args args;
	MKNOD3res res;
	int douprintf;
	vnode_t *vp;
	struct vattr vattr;

	switch ((int)va->va_type) {
	case VCHR:
		setdiropargs3(&args.where, nm, dvp);
		args.what.type = NF3CHR;
		vattr_to_sattr3(va,
				&args.what.mknoddata3_u.device.dev_attributes);
		args.what.mknoddata3_u.device.spec.specdata1 =
							getmajor(va->va_rdev);
		args.what.mknoddata3_u.device.spec.specdata2 =
							getminor(va->va_rdev);
		break;
	case VBLK:
		setdiropargs3(&args.where, nm, dvp);
		args.what.type = NF3BLK;
		vattr_to_sattr3(va,
				&args.what.mknoddata3_u.device.dev_attributes);
		args.what.mknoddata3_u.device.spec.specdata1 =
							getmajor(va->va_rdev);
		args.what.mknoddata3_u.device.spec.specdata2 =
							getminor(va->va_rdev);
		break;
	case VFIFO:
		setdiropargs3(&args.where, nm, dvp);
		args.what.type = NF3FIFO;
		vattr_to_sattr3(va, &args.what.mknoddata3_u.pipe_attributes);
		break;
	/*
	 * case VSOCK:
	 *	setdiropargs3(&args.where, nm, dvp);
	 *	args.what.type = NF3SOCK;
	 *	vattr_to_sattr3(va, &args.what.mknoddata3_u.pipe_attributes);
	 *	break;
	 */
	default:
		return (EINVAL);
	}

	douprintf = 1;
	error = rfs3call(VTOMI(dvp), NFSPROC3_MKNOD,
			xdr_MKNOD3args, (caddr_t)&args,
			xdr_MKNOD3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		PURGE_ATTRCACHE(dvp);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_wcc_data(dvp, &res.resok.dir_wcc, cr);
		if (VTOR(dvp)->r_dir != NULL)
			nfs_purge_rddir_cache(dvp);

		if (!res.resok.obj.handle_follows) {
			error = nfs3lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
			if (error)
				return (error);
		} else {
			if (res.resok.obj_attributes.attributes) {
				vp = makenfs3node(&res.resok.obj.handle,
						&res.resok.obj_attributes.attr,
						dvp->v_vfsp, cr);
			} else {
				vp = makenfs3node(&res.resok.obj.handle, NULL,
						    dvp->v_vfsp, cr);
				if (vp->v_type == VNON) {
					vattr.va_mask = AT_TYPE;
					error = nfs3getattr(vp, &vattr, cr);
					if (error) {
						VN_RELE(vp);
						return (error);
					}
					vp->v_type = vattr.va_type;
				}

			}
			dnlc_enter(dvp, nm, vp, NOCRED);
		}

		if (va->va_gid != VTOR(vp)->r_attr.va_gid) {
			va->va_mask = AT_GID;
			(void) nfs3setattr(vp, va, 0, cr);
		}

		/*
		 * If vnode is a device create special vnode
		 */
		if (ISVDEV(vp->v_type)) {
			*vpp = specvp(vp, vp->v_rdev, vp->v_type, cr);
			VN_RELE(vp);
		} else
			*vpp = vp;
	} else {
		nfs3_cache_wcc_data(dvp, &res.resfail.dir_wcc, cr);
		PURGE_STALE_FH(error, dvp, cr);
	}
	return (error);
}

/*
 * Weirdness: if the vnode to be removed is open
 * we rename it instead of removing it and nfs_inactive
 * will remove the new name.
 */
static int
nfs3_remove(vnode_t *dvp, char *nm, cred_t *cr)
{
	int error;
	REMOVE3args args;
	REMOVE3res res;
	vnode_t *vp;
	char *tmpname;
	int douprintf;
	rnode_t *rp;

	error = nfs3lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
	if (error)
		return (error);

	if (vp->v_type == VDIR && !suser(cr)) {
		VN_RELE(vp);
		return (EPERM);
	}

	/*
	 * We need to flush the name cache so we can
	 * check the real reference count on the vnode
	 */
	dnlc_purge_vp(vp);

	rp = VTOR(vp);
	if (vp->v_count > 1) {
		tmpname = newname();
		error = nfs3rename(dvp, nm, dvp, tmpname, cr);
		if (error)
			kmem_free((caddr_t)tmpname, MAXNAMELEN);
		else {
			mutex_enter(&rp->r_statelock);
			if (rp->r_unldvp == NULL) {
				VN_HOLD(dvp);
				rp->r_unldvp = dvp;
				if (rp->r_unlcred != NULL)
					crfree(rp->r_unlcred);
				crhold(cr);
				rp->r_unlcred = cr;
				rp->r_unlname = tmpname;
			} else {
				kmem_free((caddr_t)rp->r_unlname, MAXNAMELEN);
				rp->r_unlname = tmpname;
			}
			mutex_exit(&rp->r_statelock);
		}
	} else {
		/*
		 * We need to flush any dirty pages which happen to
		 * be hanging around before removing the file.  This
		 * shouldn't happen very often and mostly on file
		 * systems mounted "nocto".
		 */
		if (vp->v_pages != NULL &&
		    ((rp->r_flags & RDIRTY) || rp->r_count > 0)) {
			error = nfs3_putpage(vp, (offset_t)0, 0, 0, cr);
			if (error && (error == ENOSPC || error == EDQUOT)) {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
		}

		setdiropargs3(&args.object, nm, dvp);

		douprintf = 1;
		error = rfs3call(VTOMI(dvp), NFSPROC3_REMOVE,
				xdr_REMOVE3args, (caddr_t)&args,
				xdr_REMOVE3res, (caddr_t)&res,
				cr, &douprintf, &res.status);

		PURGE_ATTRCACHE(vp);

		if (error)
			PURGE_ATTRCACHE(dvp);
		else {
			error = geterrno3(res.status);
			if (!error) {
				nfs3_cache_wcc_data(dvp, &res.resok.dir_wcc,
						cr);
				if (VTOR(dvp)->r_dir != NULL)
					nfs_purge_rddir_cache(dvp);
			} else {
				nfs3_cache_wcc_data(dvp, &res.resfail.dir_wcc,
						cr);
				PURGE_STALE_FH(error, dvp, cr);
			}
		}
	}

	VN_RELE(vp);

	return (error);
}

static int
nfs3_link(vnode_t *tdvp, vnode_t *svp, char *tnm, cred_t *cr)
{
	int error;
	LINK3args args;
	LINK3res res;
	vnode_t *realvp;
	int douprintf;
	mntinfo_t *mi;

	if (VOP_REALVP(svp, &realvp) == 0)
		svp = realvp;

	mi = VTOMI(svp);

	if (!(mi->mi_flags & MI_LINK))
		return (EOPNOTSUPP);

	args.file = *VTOFH3(svp);
	setdiropargs3(&args.link, tnm, tdvp);

	dnlc_remove(tdvp, tnm);

	douprintf = 1;
	error = rfs3call(mi, NFSPROC3_LINK,
			xdr_LINK3args, (caddr_t)&args,
			xdr_LINK3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		PURGE_ATTRCACHE(tdvp);
		PURGE_ATTRCACHE(svp);
		return (error);
	}

	error = geterrno3(res.status);

	if (!error) {
		nfs3_cache_post_op_attr(svp, &res.resok.file_attributes, cr);
		nfs3_cache_wcc_data(tdvp, &res.resok.linkdir_wcc, cr);
		if (VTOR(tdvp)->r_dir != NULL)
			nfs_purge_rddir_cache(tdvp);
		dnlc_enter(tdvp, tnm, svp, NOCRED);
	} else {
		nfs3_cache_post_op_attr(svp, &res.resfail.file_attributes, cr);
		nfs3_cache_wcc_data(tdvp, &res.resfail.linkdir_wcc, cr);
		if (error == EOPNOTSUPP) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags &= ~MI_LINK;
			mutex_exit(&mi->mi_lock);
		}
	}

	return (error);
}

static int
nfs3_rename(vnode_t *odvp, char *onm, vnode_t *ndvp, char *nnm,
	cred_t *cr)
{
	vnode_t *realvp;

	if (VOP_REALVP(ndvp, &realvp) == 0)
		ndvp = realvp;

	return (nfs3rename(odvp, onm, ndvp, nnm, cr));
}

/*
 * nfs3rename does the real work of renaming in NFS Version 3.
 */
static int
nfs3rename(vnode_t *odvp, char *onm, vnode_t *ndvp, char *nnm,
	cred_t *cr)
{
	int error;
	RENAME3args args;
	RENAME3res res;
	int douprintf;
	vnode_t *nvp;
	vnode_t *ovp;
	char *tmpname;
	rnode_t *rp;

	if (strcmp(onm, ".") == 0 || strcmp(onm, "..") == 0 ||
	    strcmp(nnm, ".") == 0 || strcmp(nnm, "..") == 0)
		return (EINVAL);

	/*
	 * Lookup the target file.  If it exists, it needs to be
	 * checked to see whether it is a mount point and whether
	 * it is active (open).
	 */
	error = nfs3lookup(ndvp, nnm, &nvp, NULL, 0, NULL, cr);
	if (!error) {
		/*
		 * If this file has been mounted on, then just
		 * return busy because renaming to it would remove
		 * the mounted file system from the name space.
		 */
		if (nvp->v_vfsmountedhere != NULL) {
			VN_RELE(nvp);
			return (EBUSY);
		}

		/*
		 * Purge the name cache of all references to this vnode
		 * so that we can check the reference count to infer
		 * whether it is active or not.
		 */
		dnlc_purge_vp(nvp);

		/*
		 * If the vnode is active and is not a directory,
		 * arrange to rename it to a
		 * temporary file so that it will continue to be
		 * accessible.  This implements the "unlink-open-file"
		 * semantics for the target of a rename operation.
		 * Before doing this though, make sure that the
		 * source and target files are not already the same.
		 */
		if (nvp->v_count > 1 && nvp->v_type != VDIR) {

			/*
			 * Lookup the source name.
			 */
			error = nfs3lookup(odvp, onm, &ovp, NULL, 0, NULL, cr);

			/*
			 * The source name *should* already exist.
			 */
			if (error) {
				VN_RELE(nvp);
				return (error);
			}

			/*
			 * Compare the two vnodes.  If they are the same,
			 * just release all held vnodes and return success.
			 */
			if (ovp == nvp) {
				VN_RELE(ovp);
				VN_RELE(nvp);
				return (0);
			}

			/*
			 * Can't mix and match directories and non-
			 * directories in rename operations.  If
			 * they don't match, then return the error
			 * based on the type for source (ovp) file.
			 */
			if (ovp->v_type == VDIR) {
				VN_RELE(ovp);
				VN_RELE(nvp);
				return (ENOTDIR);
			}

			VN_RELE(ovp);

			/*
			 * The target file exists, is not the same as
			 * the source file, and is active.  Link it
			 * to a temporary filename to avoid having
			 * the server removing the file completely.
			 */
			tmpname = newname();
			error = nfs3_link(ndvp, nvp, tmpname, cr);
			if (error == EOPNOTSUPP) {
				error = nfs3_rename(ndvp, nnm, ndvp, tmpname,
						cr);
			}
			if (error) {
				kmem_free((caddr_t)tmpname, MAXNAMELEN);
				VN_RELE(nvp);
				return (error);
			}
			rp = VTOR(nvp);
			mutex_enter(&rp->r_statelock);
			if (rp->r_unldvp == NULL) {
				VN_HOLD(ndvp);
				rp->r_unldvp = ndvp;
				if (rp->r_unlcred != NULL)
					crfree(rp->r_unlcred);
				crhold(cr);
				rp->r_unlcred = cr;
				rp->r_unlname = tmpname;
			} else {
				kmem_free((caddr_t)rp->r_unlname, MAXNAMELEN);
				rp->r_unlname = tmpname;
			}
			mutex_exit(&rp->r_statelock);
		}

		VN_RELE(nvp);
	}

	dnlc_remove(odvp, onm);
	dnlc_remove(ndvp, nnm);

	setdiropargs3(&args.from, onm, odvp);
	setdiropargs3(&args.to, nnm, ndvp);

	douprintf = 1;
	error = rfs3call(VTOMI(odvp), NFSPROC3_RENAME,
			xdr_RENAME3args, (caddr_t)&args,
			xdr_RENAME3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		PURGE_ATTRCACHE(odvp);
		PURGE_ATTRCACHE(ndvp);
		return (error);
	}

	error = geterrno3(res.status);

	if (!error) {
		nfs3_cache_wcc_data(odvp, &res.resok.fromdir_wcc, cr);
		if (VTOR(odvp)->r_dir != NULL)
			nfs_purge_rddir_cache(odvp);
		nfs3_cache_wcc_data(ndvp, &res.resok.todir_wcc, cr);
		if (VTOR(ndvp)->r_dir != NULL)
			nfs_purge_rddir_cache(ndvp);
	} else {
		nfs3_cache_wcc_data(odvp, &res.resfail.fromdir_wcc, cr);
		nfs3_cache_wcc_data(ndvp, &res.resfail.todir_wcc, cr);
		/*
		 * System V defines rename to return EEXIST, not
		 * ENOTEMPTY if the target directory is not empty.
		 * Over the wire, the error is NFSERR_ENOTEMPTY
		 * which geterrno maps to ENOTEMPTY.
		 */
		if (error == ENOTEMPTY)
			error = EEXIST;
	}

	return (error);
}

static int
nfs3_mkdir(vnode_t *dvp, char *nm, struct vattr *va, vnode_t **vpp,
	cred_t *cr)
{
	int error;
	MKDIR3args args;
	MKDIR3res res;
	int douprintf;
	struct vattr vattr;
	vnode_t *vp;

	setdiropargs3(&args.where, nm, dvp);

	/*
	 * Decide what the group-id and set-gid bit of the created directory
	 * should be.  May have to do a setattr to get the gid right.
	 */
	va->va_gid = setdirgid(dvp, cr);
	va->va_mode = setdirmode(dvp, va->va_mode);
	va->va_mask |= AT_MODE|AT_GID;
	vattr_to_sattr3(va, &args.attributes);

	dnlc_remove(dvp, nm);

	douprintf = 1;
	error = rfs3call(VTOMI(dvp), NFSPROC3_MKDIR,
			xdr_MKDIR3args, (caddr_t)&args,
			xdr_MKDIR3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		PURGE_ATTRCACHE(dvp);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_wcc_data(dvp, &res.resok.dir_wcc, cr);
		if (VTOR(dvp)->r_dir != NULL)
			nfs_purge_rddir_cache(dvp);

		if (!res.resok.obj.handle_follows) {
			error = nfs3lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
			if (error)
				return (error);
		} else {
			if (res.resok.obj_attributes.attributes) {
				vp = makenfs3node(&res.resok.obj.handle,
						&res.resok.obj_attributes.attr,
						dvp->v_vfsp, cr);
			} else {
				vp = makenfs3node(&res.resok.obj.handle, NULL,
						    dvp->v_vfsp, cr);
				if (vp->v_type == VNON) {
					vattr.va_mask = AT_TYPE;
					error = nfs3getattr(vp, &vattr, cr);
					if (error) {
						VN_RELE(vp);
						return (error);
					}
					vp->v_type = vattr.va_type;
				}
			}
			dnlc_enter(dvp, nm, vp, NOCRED);
		}
		if (va->va_gid != VTOR(vp)->r_attr.va_gid) {
			va->va_mask = AT_GID;
			(void) nfs3setattr(vp, va, 0, cr);
		}
		*vpp = vp;
	} else {
		nfs3_cache_wcc_data(dvp, &res.resfail.dir_wcc, cr);
		PURGE_STALE_FH(error, dvp, cr);
	}

	return (error);
}

static int
nfs3_rmdir(vnode_t *dvp, char *nm, vnode_t *cdir, cred_t *cr)
{
	int error;
	RMDIR3args args;
	RMDIR3res res;
	vnode_t *vp;
	int douprintf;

	/*
	 * Attempt to prevent a rmdir(".") from succeeding.
	 */
	error = nfs3lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
	if (error)
		return (error);

	if (vp == cdir) {
		VN_RELE(vp);
		return (EINVAL);
	}

	setdiropargs3(&args.object, nm, dvp);

	douprintf = 1;
	error = rfs3call(VTOMI(dvp), NFSPROC3_RMDIR,
			xdr_RMDIR3args, (caddr_t)&args,
			xdr_RMDIR3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	PURGE_ATTRCACHE(vp);

	if (error) {
		PURGE_ATTRCACHE(dvp);
		VN_RELE(vp);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_wcc_data(dvp, &res.resok.dir_wcc, cr);
		if (VTOR(dvp)->r_dir != NULL)
			nfs_purge_rddir_cache(dvp);
		dnlc_purge_vp(vp);
		if (VTOR(vp)->r_dir != NULL)
			nfs_purge_rddir_cache(vp);
	} else {
		nfs3_cache_wcc_data(dvp, &res.resfail.dir_wcc, cr);
		PURGE_STALE_FH(error, dvp, cr);
		/*
		 * System V defines rmdir to return EEXIST, not
		 * ENOTEMPTY if the directory is not empty.  Over
		 * the wire, the error is NFSERR_ENOTEMPTY which
		 * geterrno maps to ENOTEMPTY.
		 */
		if (error == ENOTEMPTY)
			error = EEXIST;
	}

	VN_RELE(vp);

	return (error);
}

static int
nfs3_symlink(vnode_t *dvp, char *lnm, struct vattr *tva, char *tnm,
	cred_t *cr)
{
	int error;
	SYMLINK3args args;
	SYMLINK3res res;
	int douprintf;
	mntinfo_t *mi;
	vnode_t *vp;
	rnode_t *rp;
	char *contents;

	mi = VTOMI(dvp);

	if (!(mi->mi_flags & MI_SYMLINK))
		return (EOPNOTSUPP);

	setdiropargs3(&args.where, lnm, dvp);
	vattr_to_sattr3(tva, &args.symlink.symlink_attributes);
	args.symlink.symlink_data = tnm;

	dnlc_remove(dvp, lnm);

	douprintf = 1;
	error = rfs3call(mi, NFSPROC3_SYMLINK,
			xdr_SYMLINK3args, (caddr_t)&args,
			xdr_SYMLINK3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		PURGE_ATTRCACHE(dvp);
		return (error);
	}

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_wcc_data(dvp, &res.resok.dir_wcc, cr);
		if (VTOR(dvp)->r_dir != NULL)
			nfs_purge_rddir_cache(dvp);

		if (res.resok.obj.handle_follows) {
			if (res.resok.obj_attributes.attributes) {
				vp = makenfs3node(&res.resok.obj.handle,
						&res.resok.obj_attributes.attr,
						dvp->v_vfsp, cr);
			} else {
				vp = makenfs3node(&res.resok.obj.handle, NULL,
						dvp->v_vfsp, cr);
				vp->v_type = VLNK;
				vp->v_rdev = 0;
			}
			dnlc_enter(dvp, lnm, vp, NOCRED);
			rp = VTOR(vp);
			if (nfs3_do_symlink_cache &&
			    rp->r_symlink.contents == NULL) {
#ifdef DEBUG
				contents = symlink_cache_alloc(MAXPATHLEN,
								KM_NOSLEEP);
#else
				contents = (char *) kmem_alloc(MAXPATHLEN,
								KM_NOSLEEP);
#endif
				if (contents != NULL) {
					mutex_enter(&rp->r_statelock);
					if (rp->r_symlink.contents == NULL) {
						rp->r_symlink.len = strlen(tnm);
						bcopy(tnm, contents,
						    rp->r_symlink.len);
						rp->r_symlink.contents =
								    contents;
						rp->r_symlink.size = MAXPATHLEN;
						mutex_exit(&rp->r_statelock);
					} else {
						mutex_exit(&rp->r_statelock);
#ifdef DEBUG
						symlink_cache_free(
							(void *)contents,
							MAXPATHLEN);
#else
						kmem_free(contents, MAXPATHLEN);
#endif
					}
				}
			}
			VN_RELE(vp);
		}
	} else {
		nfs3_cache_wcc_data(dvp, &res.resfail.dir_wcc, cr);
		PURGE_STALE_FH(error, dvp, cr);
		if (error == EOPNOTSUPP) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags &= ~MI_SYMLINK;
			mutex_exit(&mi->mi_lock);
		}
	}

	return (error);
}

#ifdef DEBUG
static int nfs3_readdir_cache_hits = 0;
static int nfs3_readdir_cache_shorts = 0;
#endif

/*
 * Read directory entries.
 * There are some weird things to look out for here.  The uio_offset
 * field is either 0 or it is the offset returned from a previous
 * readdir.  It is an opaque value used by the server to find the
 * correct directory block to read. The count field is the number
 * of blocks to read on the server.  This is advisory only, the server
 * may return only one block's worth of entries.  Entries may be compressed
 * on the server.
 */
static int
nfs3_readdir(vnode_t *vp, struct uio *uiop, cred_t *cr, int *eofp)
{
	int error;
	u_int count;
	rnode_t *rp;
	rddir_cache *rdc;
	rddir_cache *nrdc;
	rddir_cache *rrdc;

	/*
	 * Make sure that the directory cache is valid.
	 */
	rp = VTOR(vp);
	if (rp->r_dir != NULL) {
		error = nfs3_validate_caches(vp, cr);
		if (error)
			return (error);
	}

	count = uiop->uio_iov->iov_len;

	nrdc = NULL;
top:
	/*
	 * Short circuit last readdir which always returns 0 bytes.
	 * This can be done after the directory has been read through
	 * completely at least once.  This will set r_direof which
	 * can be used to find the value of the last cookie.
	 */
	mutex_enter(&rp->r_statelock);
	if (rp->r_direof != NULL &&
	    uiop->uio_loffset == rp->r_direof->nfs3_ncookie) {
		mutex_exit(&rp->r_statelock);
#ifdef DEBUG
		nfs3_readdir_cache_shorts++;
#endif
		if (eofp)
			*eofp = 1;
		if (nrdc != NULL) {
#ifdef DEBUG
			rddir_cache_free((void *)nrdc, sizeof (*nrdc));
#else
			kmem_free((caddr_t)nrdc, sizeof (*nrdc));
#endif
		}
		return (0);
	}
	/*
	 * Look for a cache entry.  Cache entries are identified
	 * by the NFS cookie value and the byte count requested.
	 */
look:
	rdc = rp->r_dir;
	while (rdc != NULL) {
		/*
		 * To NFS 3, the cookie is an opaque 8 byte entity.  To
		 * the rest of the system, the cookie is really an
		 * offset.  Thus, NFS 3 stores the cookie in offset_t
		 * sized elements and compares them to offset_t offsets.
		 * This is valid as long as the client makes no other
		 * assumptions about the values of cookies.  The only
		 * valid tests are equal and not equal.
		 */
		if (rdc->nfs3_cookie == uiop->uio_loffset &&
		    rdc->buflen == count) {
			/*
			 * If the cache entry is in the process of being
			 * filled in, wait until this completes.  The
			 * RDDIRWAIT bit is set to indicate that someone
			 * is waiting and then the thread currently
			 * filling the entry is done, it should do a
			 * cv_broadcast to wakeup all of the threads
			 * waiting for it to finish.
			 */
			if (rdc->flags & RDDIR) {
				rdc->flags |= RDDIRWAIT;
				if (!cv_wait_sig(&rdc->cv, &rp->r_statelock)) {
					/*
					 * We got interrupted, probably
					 * the user typed ^C or an alarm
					 * fired.  We free the new entry
					 * if we allocated one.
					 */
					mutex_exit(&rp->r_statelock);
					if (nrdc != NULL) {
#ifdef DEBUG
						rddir_cache_free((void *)nrdc,
							    sizeof (*nrdc));
#else
						kmem_free((caddr_t)nrdc,
							    sizeof (*nrdc));
#endif
					}
					return (EINTR);
				}
				goto look;
			}
			/*
			 * Check to see if a readdir is required to
			 * fill the entry.  If so, mark this entry
			 * as being filled, remove our reference,
			 * and branch to the code to fill the entry.
			 */
			if (rdc->flags & RDDIRREQ) {
				rdc->flags &= ~RDDIRREQ;
				rdc->flags |= RDDIR;
				nrdc = rdc;
				mutex_exit(&rp->r_statelock);
				goto bottom;
			}
#ifdef DEBUG
			nfs3_readdir_cache_hits++;
#endif
			/*
			 * If an error occurred while attempting
			 * to fill the cache entry, just return it.
			 */
			if (rdc->error) {
				error = rdc->error;
				mutex_exit(&rp->r_statelock);
				if (nrdc != NULL) {
#ifdef DEBUG
					rddir_cache_free((void *)nrdc,
						    sizeof (*nrdc));
#else
					kmem_free((caddr_t)nrdc,
						    sizeof (*nrdc));
#endif
				}
				return (error);
			}
			/*
			 * The cache entry is complete and good,
			 * copyout the dirent structs to the calling
			 * thread.
			 */
			error = uiomove(rdc->entries, rdc->entlen,
					UIO_READ, uiop);
			/*
			 * If no error occurred during the copyout,
			 * update the offset in the uio struct to
			 * contain the value of the next NFS 3 cookie
			 * and set the eof value appropriately.
			 */
			if (!error) {
				uiop->uio_loffset = rdc->nfs3_ncookie;
				if (eofp)
					*eofp = rdc->eof;
			}
			/*
			 * Decide whether to do readahead.  Don't if
			 * have already read to the end of directory.
			 */
			if (rdc->eof) {
				mutex_exit(&rp->r_statelock);
				if (nrdc != NULL) {
#ifdef DEBUG
					rddir_cache_free((void *)nrdc,
						    sizeof (*nrdc));
#else
					kmem_free((caddr_t)nrdc,
						    sizeof (*nrdc));
#endif
				}
				return (error);
			}
			/*
			 * Now look for a readahead entry.
			 */
			rrdc = rp->r_dir;
			while (rrdc != NULL) {
				if (rrdc->nfs3_cookie == rdc->nfs3_ncookie &&
				    rrdc->buflen == count)
					break;
				rrdc = rrdc->next;
			}
			/*
			 * Check to see whether we found an entry
			 * for the readahead.  If so, we don't need
			 * to do anything further, so free the new
			 * entry if one was allocated.  Otherwise,
			 * allocate a new entry, add it to the cache,
			 * and then initiate an asynchronous readdir
			 * operation to fill it.
			 */
			if (rrdc != NULL) {
				if (nrdc != NULL) {
#ifdef DEBUG
					rddir_cache_free((void *)nrdc,
						    sizeof (*nrdc));
#else
					kmem_free((caddr_t)nrdc,
						    sizeof (*nrdc));
#endif
				}
			} else {
				if (nrdc != NULL)
					rrdc = nrdc;
				else {
#ifdef DEBUG
					rrdc = rddir_cache_alloc(sizeof (*rrdc),
								KM_NOSLEEP);
#else
					rrdc = (rddir_cache *)
					    kmem_alloc(sizeof (*rrdc),
							KM_NOSLEEP);
#endif
				}
				if (rrdc != NULL) {
					rrdc->nfs3_cookie = rdc->nfs3_ncookie;
					rrdc->buflen = count;
					rrdc->flags = RDDIR;
					cv_init(&rrdc->cv, "rddir_cache cv",
						CV_DEFAULT, NULL);
					rrdc->next = rp->r_dir;
					rp->r_dir = rrdc;
					mutex_exit(&rp->r_statelock);
					nfs_async_readdir(vp, rrdc, cr,
							do_nfs3readdir);
					return (error);
				}
			}
			mutex_exit(&rp->r_statelock);
			return (error);
		}
		rdc = rdc->next;
	}

	/*
	 * Didn't find an entry in the cache.  Construct a new empty
	 * entry and link it into the cache.  Other processes attempting
	 * to access this entry will need to wait until it is filled in.
	 *
	 * Since kmem_alloc may block, another pass through the cache
	 * will need to be taken to make sure that another process
	 * hasn't already added an entry to the cache for this request.
	 */
	if (nrdc == NULL) {
		mutex_exit(&rp->r_statelock);
#ifdef DEBUG
		nrdc = rddir_cache_alloc(sizeof (*nrdc), KM_SLEEP);
#else
		nrdc = (rddir_cache *)kmem_alloc(sizeof (*nrdc), KM_SLEEP);
#endif
		nrdc->nfs3_cookie = uiop->uio_loffset;
		nrdc->buflen = count;
		nrdc->flags = RDDIR;
		cv_init(&nrdc->cv, "rddir_cache cv", CV_DEFAULT, NULL);
		goto top;
	}

	/*
	 * Add this entry to the cache.
	 */
	nrdc->next = rp->r_dir;
	rp->r_dir = nrdc;
	mutex_exit(&rp->r_statelock);

bottom:
	/*
	 * Do the readdir.  This routine decides whether to use
	 * READDIR or READDIRPLUS.
	 */
	error = do_nfs3readdir(vp, nrdc, cr);

	/*
	 * If this operation failed, just return the error which occurred.
	 */
	if (error != 0)
		return (error);

	/*
	 * Since the RPC operation will have taken sometime and blocked
	 * this process, another pass through the cache will need to be
	 * taken to find the correct cache entry.  It is possible that
	 * the correct cache entry will not be there (although one was
	 * added) because the directory changed during the RPC operation
	 * and the readdir cache was flushed.  In this case, just start
	 * over.  It is hoped that this will not happen too often... :-)
	 */
	nrdc = NULL;
	goto top;
	/* NOTREACHED */
}

static int
do_nfs3readdir(vnode_t *vp, rddir_cache *rdc, cred_t *cr)
{
	int error;
	rnode_t *rp;
	mntinfo_t *mi;

	rp = VTOR(vp);
	mi = VTOMI(vp);

	/*
	 * Issue the proper request.  READDIRPLUS is used unless
	 * the server does not support READDIRPLUS or until the
	 * directory has been completely read once using READDIRPLUS.
	 * Once the directory has been completely read, the DNLC is
	 * assumed to be loaded and any further use of READDIRPLUS
	 * is just unnecessary overhead.  READDIR is used to retrieve
	 * directory entries which we do not find cached.  If the
	 * directory cache and the DNLC are flushed because the
	 * directory has changed, READDIRPLUS is used once again to
	 * repopulate the DNLC.
	 */
	if (!(mi->mi_flags & MI_READDIR) && !(rp->r_flags & REOF)) {
		nfs3readdirplus(vp, rdc, cr);
		/*
		 * Bugid 1218990: if we get an EIO from below, it
		 * may be because there is a large file in the
		 * directory; fall back to readdir to find out.
		 */
		if (rdc->error == EOPNOTSUPP || rdc->error == EIO)
			nfs3readdir(vp, rdc, cr);
	} else
		nfs3readdir(vp, rdc, cr);

	mutex_enter(&rp->r_statelock);
	rdc->flags &= ~RDDIR;
	if (rdc->flags & RDDIRWAIT) {
		rdc->flags &= ~RDDIRWAIT;
		cv_broadcast(&rdc->cv);
	}
	error = rdc->error;
	if (error)
		rdc->flags |= RDDIRREQ;
	mutex_exit(&rp->r_statelock);

	return (error);
}

static void
nfs3readdir(vnode_t *vp, rddir_cache *rdc, cred_t *cr)
{
	int error;
	READDIR3args args;
	READDIR3res res;
	rnode_t *rp;
	u_int count;
	int douprintf;

	count = rdc->buflen;

	rp = VTOR(vp);

	args.dir = *RTOFH3(rp);
	args.cookie = (cookie3)rdc->nfs3_cookie;
	bcopy((caddr_t)rp->r_cookieverf, (caddr_t)args.cookieverf,
	    sizeof (cookieverf3));
	args.count = count;

	res.resok.reply.entries = (entry3 *)kmem_alloc(count, KM_SLEEP);
	res.resok.size = count;
	res.resok.cookie = args.cookie;

	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_READDIR,
			xdr_READDIR3args, (caddr_t)&args,
			xdr_READDIR3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (!error) {
		error = geterrno3(res.status);
		if (!error) {
			nfs3_cache_post_op_attr(vp, &res.resok.dir_attributes,
						cr);
			rdc->nfs3_ncookie = (offset_t)res.resok.cookie;
			bcopy((caddr_t)res.resok.cookieverf,
			    (caddr_t)rp->r_cookieverf, sizeof (cookieverf3));
			rdc->entries = (char *)res.resok.reply.entries;
			if (res.resok.reply.eof) {
				rdc->eof = 1;
				mutex_enter(&rp->r_statelock);
				rp->r_flags |= REOF;
				rp->r_direof = rdc;
				mutex_exit(&rp->r_statelock);
			} else
				rdc->eof = 0;
			rdc->entlen = res.resok.size;
			rdc->error = 0;
		} else {
			nfs3_cache_post_op_attr(vp, &res.resfail.dir_attributes,
						cr);
			PURGE_STALE_FH(error, vp, cr);
		}
	}
	if (error) {
		kmem_free((caddr_t)res.resok.reply.entries, count);
		rdc->entries = NULL;
		rdc->error = error;
	}
}

#ifdef nextdp
#undef nextdp
#endif
#define	nextdp(dp)	((struct dirent *)((char *)(dp) + (dp)->d_reclen))
/*
 * Read directory entries.
 * There are some weird things to look out for here.  The uio_offset
 * field is either 0 or it is the offset returned from a previous
 * readdir.  It is an opaque value used by the server to find the
 * correct directory block to read. The count field is the number
 * of blocks to read on the server.  This is advisory only, the server
 * may return only one block's worth of entries.  Entries may be compressed
 * on the server.
 */
static void
nfs3readdirplus(vnode_t *vp, rddir_cache *rdc, cred_t *cr)
{
	int error;
	READDIRPLUS3args args;
	READDIRPLUS3res res;
	rnode_t *rp;
	mntinfo_t *mi;
	u_int count;
	int douprintf;
	char *buf;
	char *ibufp;
	struct dirent *idp;
	struct dirent *odp;
	post_op_attr *atp;
	post_op_fh3 *fhp;
	int isize;
	int osize;
	vnode_t *nvp;
	offset_t loff;

	count = rdc->buflen;

	rp = VTOR(vp);
	mi = VTOMI(vp);

	args.dir = *RTOFH3(rp);
	args.cookie = (cookie3)rdc->nfs3_cookie;
	bcopy((caddr_t)rp->r_cookieverf, (caddr_t)args.cookieverf,
	    sizeof (cookieverf3));
	args.dircount = count;
	args.maxcount = MAXBSIZE;

	res.resok.reply.entries = (entryplus3 *)kmem_alloc(MAXBSIZE, KM_SLEEP);
	res.resok.size = MAXBSIZE;

	loff = rdc->nfs3_cookie;

	douprintf = 1;
	error = rfs3call(mi, NFSPROC3_READDIRPLUS,
			xdr_READDIRPLUS3args, (caddr_t)&args,
			xdr_READDIRPLUS3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error) {
		rdc->entries = NULL;
		rdc->error = error;
		goto out;
	}

	error = geterrno3(res.status);
	if (error) {
		nfs3_cache_post_op_attr(vp, &res.resfail.dir_attributes, cr);
		PURGE_STALE_FH(error, vp, cr);
		if (error == EOPNOTSUPP) {
			mutex_enter(&mi->mi_lock);
			mi->mi_flags |= MI_READDIR;
			mutex_exit(&mi->mi_lock);
		}
		rdc->entries = NULL;
		rdc->error = error;
		goto out;
	}

	nfs3_cache_post_op_attr(vp, &res.resok.dir_attributes, cr);

	buf = (char *)kmem_alloc(count, KM_SLEEP);

	odp = (struct dirent *)buf;
	ibufp = (char *)res.resok.reply.entries;
	isize = res.resok.size;
	osize = 0;
	while (isize > 0) {
		idp = (struct dirent *)ibufp;
		if (osize + idp->d_reclen > count) {
			res.resok.reply.eof = FALSE;
			break;
		}
		bcopy((char *)idp, (char *)odp, idp->d_reclen);
		loff = (offset_t)idp->d_off;
		osize += odp->d_reclen;
		odp = nextdp(odp);
		isize -= idp->d_reclen;
		ibufp += idp->d_reclen;

		atp = (post_op_attr *)ibufp;
		if (!atp->attributes) {
			ibufp += sizeof (atp->attributes);
			isize -= sizeof (atp->attributes);
		} else {
			ibufp += sizeof (*atp);
			isize -= sizeof (*atp);
		}

		fhp = (post_op_fh3 *)ibufp;
		if (!fhp->handle_follows) {
			ibufp += sizeof (fhp->handle_follows);
			isize -= sizeof (fhp->handle_follows);
			continue;
		}
		ibufp += sizeof (*fhp);
		isize -= sizeof (*fhp);

		/*
		 * Add this entry to the DNLC if it is "." and
		 * we have attributes.  Otherwise, we end up
		 * polluting the DNLC with "." entries or not
		 * being able to determine what type of file
		 * this entry references.
		 */
		if (strcmp(idp->d_name, ".") != 0 &&
		    atp->attributes) {
			nvp = makenfs3node(&fhp->handle, &atp->attr,
					vp->v_vfsp, cr);
			dnlc_remove(vp, idp->d_name);
			dnlc_enter(vp, idp->d_name, nvp, NOCRED);
			VN_RELE(nvp);
		}
	}

	rdc->nfs3_ncookie = loff;
	bcopy((caddr_t)res.resok.cookieverf, (caddr_t)rp->r_cookieverf,
	    sizeof (cookieverf3));
	rdc->entries = buf;
	if (res.resok.reply.eof) {
		rdc->eof = 1;
		mutex_enter(&rp->r_statelock);
		rp->r_flags |= REOF;
		rp->r_direof = rdc;
		mutex_exit(&rp->r_statelock);
	} else
		rdc->eof = 0;
	rdc->entlen = osize;
	rdc->error = 0;

out:
	kmem_free((caddr_t)res.resok.reply.entries, MAXBSIZE);
}

#ifdef DEBUG
static int nfs3_bio_do_stop = 0;
#endif

static int
nfs3_bio(struct buf *bp, stable_how *stab_comm, cred_t *cr)
{
	register rnode_t *rp = VTOR(bp->b_vp);
	struct vattr va;
	post_op_attr fa;
	long count, rem;
	int error;
	int read = bp->b_flags & B_READ;
	cred_t *cred;
	u_int offset;
	long seq;

	offset = (u_int)dbtob(bp->b_blkno);
	if (read) {
		mutex_enter(&rp->r_statelock);
		if (rp->r_cred != NULL) {
			cred = rp->r_cred;
			crhold(cred);
		} else {
			rp->r_cred = cr;
			crhold(cr);
			cred = cr;
			crhold(cred);
		}
		mutex_exit(&rp->r_statelock);
	read_again:
		error = bp->b_error = nfs3read(bp->b_vp, bp->b_un.b_addr,
					    offset, (long)bp->b_bcount,
					    (long *)&bp->b_resid, cred, &fa);
		crfree(cred);
		if (!error) {
			if (bp->b_resid) {
				/*
				 * Didn't get it all because we hit EOF,
				 * zero all the memory beyond the EOF.
				 */
				/* bzero(rdaddr + */
				bzero(bp->b_un.b_addr +
				    (bp->b_bcount - bp->b_resid),
				    (u_int)bp->b_resid);
			}
			if (bp->b_resid == bp->b_bcount &&
			    offset >= rp->r_size) {
				/*
				 * We didn't read anything at all as we are
				 * past EOF.  Return an error indicator back
				 * but don't destroy the pages (yet).
				 */
				error = NFS_EOF;
			}
		} else if (error == EACCES) {
			mutex_enter(&rp->r_statelock);
			if (cred != cr) {
				if (rp->r_cred != NULL)
					crfree(rp->r_cred);
				rp->r_cred = cr;
				crhold(cr);
				cred = cr;
				crhold(cred);
				mutex_exit(&rp->r_statelock);
				goto read_again;
			}
			mutex_exit(&rp->r_statelock);
		}
	} else {
		if (!(rp->r_flags & RDONTWRITE)) {
			mutex_enter(&rp->r_statelock);
			if (rp->r_cred != NULL) {
				cred = rp->r_cred;
				crhold(cred);
			} else {
				rp->r_cred = cr;
				crhold(cr);
				cred = cr;
				crhold(cred);
			}
			mutex_exit(&rp->r_statelock);
		write_again:
			/*
			 * Can't use r_size in the MIN macro below because
			 * the MIN macro will cause r_size to be evaluated
			 * twice and there is no lock to prevent r_size from
			 * changing value in between.  Hence, just evaluate
			 * r_size once by storing it in a local variable.
			 * The load from r_size is assumed to be atomic.
			 */
			rem = rp->r_size - offset;
			count = MIN(bp->b_bcount, rem);
			if (count < 0)
				cmn_err(CE_PANIC, "nfs3_bio: write count < 0");
#ifdef DEBUG
			if (count == 0) {
				cmn_err(CE_WARN,
					"nfs3_bio: zero length write at %d",
					offset);
				nfs_printfhandle(&VTOR(bp->b_vp)->r_fh);
				if (nfs3_bio_do_stop)
					debug_enter("nfs3_bio");
			}
#endif
			error = nfs3write(bp->b_vp, bp->b_un.b_addr, offset,
					count, cred, stab_comm);
			if (error == EACCES) {
				mutex_enter(&rp->r_statelock);
				if (cred != cr) {
					if (rp->r_cred != NULL)
						crfree(rp->r_cred);
					rp->r_cred = cr;
					crhold(cr);
					crfree(cred);
					cred = cr;
					crhold(cred);
					mutex_exit(&rp->r_statelock);
					goto write_again;
				}
				mutex_exit(&rp->r_statelock);
			}
			bp->b_error = error;
			if (error && error != EINTR) {
				/*
				 * Don't print EDQUOT errors on the console.
				 * Don't print asynchronous EACCES errors.
				 * Print all other write errors.
				 */
				if (error != EDQUOT &&
				    (error != EACCES ||
				    !(bp->b_flags & B_ASYNC)))
					nfs_write_error(bp->b_vp, error, cred);
				/*
				 * Update r_error and r_flags as appropriate.
				 * If the error was ESTALE, then mark the
				 * rnode as not being writeable and save
				 * the error status.  Otherwise, save any
				 * errors which occur from asynchronous
				 * page invalidations.  Any errors occurring
				 * from other operations should be saved
				 * by the caller.
				 */
				mutex_enter(&rp->r_statelock);
				if (error == ESTALE) {
					rp->r_flags |= RDONTWRITE;
					if (!rp->r_error)
						rp->r_error = error;
				} else if (!rp->r_error &&
					    (bp->b_flags &
					    (B_INVAL|B_FORCE|B_ASYNC)) ==
					    (B_INVAL|B_FORCE|B_ASYNC)) {
					rp->r_error = error;
				}
				mutex_exit(&rp->r_statelock);
			}
			crfree(cred);
		} else
			error = rp->r_error;
	}

	if (!error && read) {
		if (fa.attributes) {
			fattr3_to_vattr(bp->b_vp, &fa.attr, &va);
			mutex_enter(&rp->r_statelock);
			seq = rp->r_seq;
			if (!CACHE_VALID(rp, va.va_mtime, va.va_size)) {
				mutex_exit(&rp->r_statelock);
				/*
				 * read, if cache is not valid, mark this bp
				 * with an error so it will be freed by
				 * pvn_done and return a special error,
				 * NFS_CACHEINVALERR, so caller can flush
				 * caches and re-do the operation.
				 */
				error = NFS_CACHEINVALERR;
				bp->b_error = EINVAL;
			} else
				mutex_exit(&rp->r_statelock);
			/*
			 * If we detect that the cache is invalid and this is
			 * an async I/O, don't update the rnode cached attrs,
			 * instead mark the attrs as timed out. This will
			 * force the cache to be purged the next time a cache
			 * check is done. Note that this is done only for the
			 * async case. This is because the async threads can't
			 * purge the cache because of deadlock possibilities.
			 * For the sync read case, it is correct to update the
			 * rnode attrs here, because nfs_getpage() will purge
			 * the cache upon getting NFS_CACHEINVALERR.
			 */
			if (error == NFS_CACHEINVALERR &&
			    (bp->b_flags & B_ASYNC)) {
				PURGE_ATTRCACHE(RTOV(rp));
			} else
				nfs_attrcache_va(RTOV(rp), &va, seq);
		}
	}

	if (error != 0 && error != NFS_EOF)
		bp->b_flags |= B_ERROR;

	return (error);
}

static int
nfs3_fid(vnode_t *vp, fid_t *fidp)
{
	rnode_t *rp;

	rp = VTOR(vp);

	if (fidp->fid_len < (u_short)rp->r_fh.fh_len) {
		fidp->fid_len = rp->r_fh.fh_len;
		return (ENOSPC);
	}
	fidp->fid_len = rp->r_fh.fh_len;
	bcopy(rp->r_fh.fh_buf, fidp->fid_data, fidp->fid_len);
	return (0);
}

static void
nfs3_rwlock(vnode_t *vp, int write_lock)
{
	rnode_t *rp = VTOR(vp);

	if (write_lock)
		rw_enter(&rp->r_rwlock, RW_WRITER);
	else
		rw_enter(&rp->r_rwlock, RW_READER);
}

/* ARGSUSED */
static void
nfs3_rwunlock(vnode_t *vp, int write_lock)
{
	rnode_t *rp = VTOR(vp);

	rw_exit(&rp->r_rwlock);
}

/* ARGSUSED */
static int
nfs3_seek(vnode_t *vp, offset_t ooff, offset_t *noffp)
{

	/*
	 * Because we stuff the readdir cookie into the offset field
	 * someone may attempt to do an lseek with the cookie which
	 * we want to succeed.
	 */
	if (vp->v_type == VDIR)
		return (0);
	return ((*noffp < 0 || *noffp > MAXOFF_T) ? EINVAL : 0);
}

static int nfs3_nra = 1;	/* number of pages to read ahead */
#ifdef DEBUG
static int nfs3_lostpage = 0;	/* number of times we lost original page */
#endif

/*
 * Return all the pages from [off..off+len) in file
 */
static int
nfs3_getpage(vnode_t *vp, offset_t off, u_int len, u_int *protp,
	page_t *pl[], u_int plsz, struct seg *seg, caddr_t addr,
	enum seg_rw rw, cred_t *cr)
{
	rnode_t *rp = VTOR(vp);
	int error;

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	if (protp != NULL)
		*protp = PROT_ALL;

	/*
	 * Now valididate that the caches are up to date.
	 */
	(void) nfs3_validate_caches(vp, cr);

retry:
	/*
	 * If we are getting called as a side effect of an nfs_write()
	 * operation the local file size might not be extended yet.
	 * In this case we want to be able to return pages of zeroes.
	 */
	if ((u_int)off + len > rp->r_size + PAGEOFFSET && seg != segkmap)
		return (EFAULT);		/* beyond EOF */

	if (len <= PAGESIZE) {
		error = nfs3_getapage(vp, (u_int)off, len, protp, pl, plsz,
		    seg, addr, rw, cr);
	} else {
		error = pvn_getpages(nfs3_getapage, vp, (u_int)off, len, protp,
		    pl, plsz, seg, addr, rw, cr);
	}

	switch (error) {
	case NFS_CACHEINVALERR:
	case NFS_EOF:
		nfs_purge_caches(vp, cr);
		goto retry;
	case ESTALE:
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

/*
 * Called from pvn_getpages or nfs3_getpage to get a particular page.
 */
/* ARGSUSED */
static int
nfs3_getapage(vnode_t *vp, u_int off, u_int len, u_int *protp,
	page_t *pl[], u_int plsz, struct seg *seg, caddr_t addr,
	enum seg_rw rw, cred_t *cr)
{
	register rnode_t *rp;
	register u_int bsize;
	struct buf *bp;
	page_t *pp;
	daddr_t lbn;
	u_int io_off, io_len;
	u_int blksize, blkoff;
	int error;
	int readahead;
	int pagefound;
	u_int rablkoff;
	page_t *savepp;

	rp = VTOR(vp);
	bsize = MAX(vp->v_vfsp->vfs_bsize, PAGESIZE);

reread:
	bp = NULL;
	pp = NULL;
	pagefound = 0;

	if (pl != NULL)
		pl[0] = NULL;

	error = 0;
	lbn = off / bsize;
	blkoff = lbn * bsize;

	/*
	 * Calculate the number of readaheads to do.
	 */
#ifdef notdef
	if (!(vp->v_flag & VNOCACHE) && (rp->r_nextr == off || off == 0))
		readahead = nfs3_nra;
	else
		readahead = 0;

	/*
	 * Queueing up the readahead before doing the synchronous read
	 * results in a significant increase in read throughput because
	 * of the increased parallelism between the async threads and
	 * the process context.
	 */
	if ((off & (MAXBSIZE - 1)) == 0 && rw != S_CREATE) {
		rablkoff = blkoff;
		while (readahead > 0 && rablkoff + bsize < rp->r_size) {
			nfs_async_readahead(vp, rablkoff + bsize,
					    addr + (rablkoff + bsize - off),
					    seg, cr, nfs3_readahead);
			readahead--;
			rablkoff += bsize;
		}
	}
#else
	/*
	 * Queueing up the readahead before doing the synchronous read
	 * results in a significant increase in read throughput because
	 * of the increased parallelism between the async threads and
	 * the process context.
	 */
	if ((off & (MAXBSIZE - 1)) == 0 &&
	    rw != S_CREATE &&
	    !(vp->v_flag & VNOCACHE)) {
		if (off == 0)
			readahead = 1;
		else if (rp->r_nextr == off)
			readahead = nfs3_nra;
		else
			readahead = 0;
		rablkoff = blkoff;
		while (readahead > 0 && rablkoff + bsize < rp->r_size) {
			nfs_async_readahead(vp, rablkoff + bsize,
					    addr + (rablkoff + bsize - off),
					    seg, cr, nfs3_readahead);
			readahead--;
			rablkoff += bsize;
		}
	}
#endif

again:
	if ((pagefound = page_exists(vp, off)) == 0) {
		if (pl == NULL) {
			nfs_async_readahead(vp, blkoff, addr, seg, cr,
					    nfs3_readahead);
		} else if (rw == S_CREATE) {
			/*
			 * Block for this page is not allocated, or the offset
			 * is beyond the current allocation size, or we're
			 * allocating a swap slot and the page was not found,
			 * so allocate it and return a zero page.
			 */
			if ((pp = page_create_va(vp, off,
			    PAGESIZE, PG_WAIT, seg->s_as, addr)) == NULL)
				cmn_err(CE_PANIC, "nfs3_getapage: page_create");
			io_len = PAGESIZE;
			rp->r_nextr = off + PAGESIZE;
		} else {
			/*
			 * Need to go to server to get a block
			 */
			mutex_enter(&rp->r_statelock);
			if (blkoff < rp->r_size &&
			    blkoff + bsize > rp->r_size) {
				/*
				 * If less than a block left in
				 * file read less than a block.
				 */
				if (rp->r_size <= off) {
					/*
					 * Trying to access beyond EOF,
					 * set up to get at least one page.
					 */
					blksize = off + PAGESIZE - blkoff;
				} else
					blksize = rp->r_size - blkoff;
			} else
				blksize = bsize;
			mutex_exit(&rp->r_statelock);

			pp = pvn_read_kluster(vp, off, seg, addr, &io_off,
			    &io_len, blkoff, blksize, 0);

			/*
			 * Some other thread has entered the page,
			 * so just use it.
			 */
			if (pp == NULL)
				goto again;

			/*
			 * Now round the request size up to page boundaries.
			 * This ensures that the entire page will be
			 * initialized to zeroes if EOF is encountered.
			 */
			io_len = ptob(btopr(io_len));

			bp = pageio_setup(pp, io_len, vp, B_READ);
			ASSERT(bp != NULL);

			/*
			 * pageio_setup should have set b_addr to 0.  This
			 * is correct since we want to do I/O on a page
			 * boundary.  bp_mapin will use this addr to calculate
			 * an offset, and then set b_addr to the kernel virtual
			 * address it allocated for us.
			 */
			ASSERT(bp->b_un.b_addr == 0);

			bp->b_edev = 0;
			bp->b_dev = 0;
			bp->b_blkno = btodb(io_off);
			bp_mapin(bp);

			/*
			 * If doing a write beyond what we believe is EOF,
			 * don't bother trying to read the pages from the
			 * server, we'll just zero the pages here.  We
			 * don't check that the rw flag is S_WRITE here
			 * because some implementations may attempt a
			 * read access to the buffer before copying data.
			 */
			if (io_off >= rp->r_size && seg == segkmap)
				bzero(bp->b_un.b_addr, io_len);
			else
				error = nfs3_bio(bp, NULL, cr);

			/*
			 * Unmap the buffer before freeing it.
			 */
			bp_mapout(bp);
			pageio_done(bp);

			savepp = pp;
			do {
				pp->p_fsdata = C_NOCOMMIT;
			} while ((pp = pp->p_next) != savepp);

			if (error == NFS_EOF) {
				/*
				 * If doing a write system call just return
				 * zeroed pages, else user tried to get pages
				 * beyond EOF, return error.  We don't check
				 * that the rw flag is S_WRITE here because
				 * some implementations may attempt a read
				 * access to the buffer before copying data.
				 */
				if (seg == segkmap)
					error = 0;
				else
					error = EFAULT;
			}

			rp->r_nextr = io_off + io_len;
		}
	}

out:
	if (pl == NULL)
		return (error);

	if (error) {
		if (pp != NULL)
			pvn_read_done(pp, B_ERROR);
		return (error);
	}

	if (pagefound != 0) {
		se_t se = (rw == S_CREATE ? SE_EXCL : SE_SHARED);

		/*
		 * Page exists in the cache, acquire the appropriate lock.
		 * If this fails, start all over again.
		 */
		if ((pp = page_lookup(vp, off, se)) == NULL) {
#ifdef DEBUG
			nfs3_lostpage++;
#endif
			goto reread;
		}
		pl[0] = pp;
		pl[1] = NULL;
		rp->r_nextr = off + PAGESIZE;
		return (0);
	}

	if (pp != NULL)
		pvn_plist_init(pp, pl, plsz, off, io_len, rw);

	return (error);
}

static void
nfs3_readahead(vnode_t *vp, u_int blkoff, caddr_t addr, struct seg *seg,
	cred_t *cr)
{
	int error;
	page_t *pp;
	u_int io_off, io_len;
	struct buf *bp;
	register u_int bsize, blksize;
	register rnode_t *rp = VTOR(vp);
	page_t *savepp;

	bsize = MAX(vp->v_vfsp->vfs_bsize, PAGESIZE);

	mutex_enter(&rp->r_statelock);
	if (blkoff < rp->r_size && blkoff + bsize > rp->r_size) {
		/*
		 * If less than a block left in file read less
		 * than a block.
		 */
		blksize = rp->r_size - blkoff;
	} else
		blksize = bsize;
	mutex_exit(&rp->r_statelock);

	pp = pvn_read_kluster(vp, blkoff, segkmap, addr, &io_off, &io_len,
				blkoff, blksize, 1);
	/*
	 * The isra flag passed to the kluster function is 1, we may have
	 * gotten a return value of NULL for a variety of reasons (# of free
	 * pages < minfree, someone entered the page on the vnode etc). In all
	 * cases, we want to punt on the readahead.
	 */
	if (pp == NULL)
		return;

	/*
	 * Now round the request size up to page boundaries.
	 * This ensures that the entire page will be
	 * initialized to zeroes if EOF is encountered.
	 */
	io_len = ptob(btopr(io_len));

	bp = pageio_setup(pp, io_len, vp, B_READ);
	ASSERT(bp != NULL);

	/*
	 * pageio_setup should have set b_addr to 0.  This is correct since
	 * we want to do I/O on a page boundary. bp_mapin() will use this addr
	 * to calculate an offset, and then set b_addr to the kernel virtual
	 * address it allocated for us.
	 */
	ASSERT(bp->b_un.b_addr == 0);

	bp->b_edev = 0;
	bp->b_dev = 0;
	bp->b_blkno = btodb(io_off);
	bp_mapin(bp);

	/*
	 * If doing a write beyond what we believe is EOF, don't bother trying
	 * to read the pages from the server, we'll just zero the pages here.
	 * We don't check that the rw flag is S_WRITE here because some
	 * implementations may attempt a read access to the buffer before
	 * copying data.
	 */
	if (io_off >= rp->r_size && seg == segkmap) {
		bzero(bp->b_un.b_addr, io_len);
		error = 0;
	} else {
		error = nfs3_bio(bp, NULL, cr);
		if (error == NFS_EOF)
			error = 0;
	}

	/*
	 * Unmap the buffer before freeing it.
	 */
	bp_mapout(bp);
	pageio_done(bp);

	savepp = pp;
	do {
		pp->p_fsdata = C_NOCOMMIT;
	} while ((pp = pp->p_next) != savepp);

	pvn_read_done(pp, error ? B_READ | B_ERROR : B_READ);
}

/*
 * Flags are composed of {B_INVAL, B_FREE, B_DONTNEED, B_FORCE}
 * If len == 0, do from off to EOF.
 *
 * The normal cases should be len == 0 & off == 0 (entire vp list),
 * len == MAXBSIZE (from segmap_release actions), and len == PAGESIZE
 * (from pageout).
 */
static int
nfs3_putpage(vnode_t *vp, offset_t off, u_int len, int flags, cred_t *cr)
{
	int error;
	rnode_t *rp;

	ASSERT(cr != NULL);

	/*
	 * XXX - Why should this check be made here?
	 */
	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	if (len == 0 && !(flags & B_INVAL) &&
	    (vp->v_vfsp->vfs_flag & VFS_RDONLY))
		return (0);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	rp->r_count++;
	mutex_exit(&rp->r_statelock);
	error = nfs_putpages(vp, off, len, flags, cr);
	mutex_enter(&rp->r_statelock);
	rp->r_count--;
	cv_signal(&rp->r_cv);
	mutex_exit(&rp->r_statelock);

	return (error);
}

/*
 * Write out a single page, possibly klustering adjacent dirty pages.
 */
int
nfs3_putapage(vnode_t *vp, page_t *pp, u_int *offp, u_int *lenp,
	int flags, cred_t *cr)
{
	u_int io_off, io_len;
	u_int lbn, lbn_off;
	u_int bsize;
	int error;
	rnode_t *rp;

	ASSERT(!(vp->v_vfsp->vfs_flag & VFS_RDONLY));
	ASSERT(pp != NULL);
	ASSERT(cr != NULL);

	rp = VTOR(vp);
	ASSERT(rp->r_count > 0);

	bsize = MAX(vp->v_vfsp->vfs_bsize, PAGESIZE);
	lbn = pp->p_offset / bsize;
	lbn_off = lbn * bsize;

	/*
	 * Find a kluster that fits in one block, or in
	 * one page if pages are bigger than blocks.  If
	 * there is less file space allocated than a whole
	 * page, we'll shorten the i/o request below.
	 */
	pp = pvn_write_kluster(vp, pp, &io_off, &io_len, lbn_off,
	    roundup(bsize, PAGESIZE), flags);

	/*
	 * pvn_write_kluster shouldn't have returned a page with offset
	 * behind the original page we were given.  Verify that.
	 */
	ASSERT((pp->p_offset / bsize) >= lbn);

	/*
	 * Now pp will have the list of kept dirty pages marked for
	 * write back.  It will also handle invalidation and freeing
	 * of pages that are not dirty.  Check for page length rounding
	 * problems.
	 */
	if (io_off + io_len > lbn_off + bsize) {
		ASSERT((io_off + io_len) - (lbn_off + bsize) < PAGESIZE);
		io_len = lbn_off + bsize - io_off;
	}
	/*
	 * The RMODINPROGRESS flag makes sure that nfs(3)_bio() sees a
	 * consistent value of r_size. r_size is being updated in writerp()
	 * without this handshaking, it is possible that nfs(3)_bio() picks
	 * up the value of r_size before writerp() has the chance to update
	 * it and this will result in the write being dropped.
	 * More precisely, there is a window between the time the uiomove()
	 * completes and the time the r_size is updated. If a VOP_PUTPAGE()
	 * operation intervenes in this window, the page will be picked up,
	 * because it is dirty (it will be unlocked, unless it was
	 * pagecreate'd). When the page is picked up as dirty, the dirty
	 * bit is reset (pvn_getdirty()). In nfs(3)write(), r_size is
	 * checked. This will still be the old size. Therefore the page will
	 * not be written out. When segmap_release() calls VOP_PUTPAGE(),
	 * the page will be found to be clean and the write will be dropped.
	 */
	if (rp->r_flags & RMODINPROGRESS) {
		mutex_enter(&rp->r_statelock);
		while ((rp->r_flags & RMODINPROGRESS) &&
		    rp->r_modaddr + MAXBSIZE > io_off &&
		    rp->r_modaddr < io_off + io_len) {
			/*
			 * 1) If this is an ASYNC request, remark the pages
			 * dirty and return EIO (so that the RDIRTY bit
			 * will get set again). We can't return 0 here,
			 * because RDIRTY won't get set, and there'll be a
			 * window where RDIRTY won't be set, yet there'll
			 * be dirty pages on the vnode.
			 * 2) If this a sync request, wait for the uimove()
			 * and the r_size update to finish and then proceed.
			 * 3) If the thread calling nfs(3)_putapage() is the
			 * same that is doing the uiomove(), then this has to
			 * be a result of a synchronous invalidation. In
			 * this case, it is quite safe to write out the
			 * pages synchronously. We determine this by
			 * checking if the owner of the rwlock (the thread
			 * that has obtained the "WRITE" version of the
			 * lock) is curthread.
			 * (Also look at bug 1218281).
			 * Special casing (3) is important. We can't make
			 * all threads that call nfs(3)_putapage() sync
			 * sleep on r_cv, if an overlap is detected,
			 * because the thread calling uiomove() may fault
			 * and then kick off an invalidation. Making that
			 * thread cv_wait() on r_cv would result in the
			 * thread deadlocking on itself.
			 */
			if (rw_owner(&rp->r_rwlock) == curthread) {
				/*
				 * The case where a synchronous invalidation
				 * is kicked off as a result from a fault
				 * from uiomove(). An obvious question to be
				 * answered here is - since the pages are
				 * being dirtied in writerp() anyway, why
				 * bother to write them out ? It was decided
				 * that it would be a good thing to write the
				 * pages out here anyway because in V3, the
				 * kluster size is 32K and the fact that we're
				 * write'ing to part of the kluster, shouldn't
				 * prevent other (dirty) pages in the kluster
				 * from getting written out and invalidated.
				 */
				break;
			}
			if (flags & B_ASYNC) {
				page_t *plist;

				mutex_exit(&rp->r_statelock);
				plist = pp;
				while (plist != NULL) {
					pp = plist;
					page_sub(&plist, pp);
					hat_setmod(pp);
					page_io_unlock(pp);
					page_unlock(pp);
				}
				if (offp)
					*offp = io_off;
				if (lenp)
					*lenp = io_len;
				return (EIO);
			}
			cv_wait(&rp->r_cv, &rp->r_statelock);
		}
		mutex_exit(&rp->r_statelock);
	}

	if (flags & B_ASYNC) {
		error = nfs_async_putapage(vp, pp, io_off, io_len, flags, cr,
					    nfs3_sync_putapage);
	} else
		error = nfs3_sync_putapage(vp, pp, io_off, io_len, flags, cr);

	if (offp)
		*offp = io_off;
	if (lenp)
		*lenp = io_len;
	return (error);
}

static int
nfs3_sync_putapage(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr)
{
	int error;
	rnode_t *rp;

	flags |= B_WRITE;

	error = nfs3_rdwrlbn(vp, pp, io_off, io_len, flags, cr);

	rp = VTOR(vp);

	if ((error == ENOSPC || error == EDQUOT) &&
	    (flags & (B_INVAL|B_FORCE)) != (B_INVAL|B_FORCE)) {
		if (!(rp->r_flags & ROUTOFSPACE)) {
			mutex_enter(&rp->r_statelock);
			rp->r_flags |= ROUTOFSPACE;
			mutex_exit(&rp->r_statelock);
		}
		flags |= B_ERROR;
		pvn_write_done(pp, flags);
		error = nfs3_putpage(vp, (offset_t)io_off, io_len,
				B_INVAL | B_FORCE | (flags & B_ASYNC), cr);
	} else {
		if (error)
			flags |= B_ERROR;
		else if (rp->r_flags & ROUTOFSPACE) {
			mutex_enter(&rp->r_statelock);
			rp->r_flags &= ~ROUTOFSPACE;
			mutex_exit(&rp->r_statelock);
		}
		pvn_write_done(pp, flags);
	}

	return (error);
}

static int
nfs3_map(vnode_t *vp, offset_t off, struct as *as, caddr_t *addrp,
	u_int len, u_char prot, u_char maxprot, u_int flags, cred_t *cr)
{
	struct segvn_crargs vn_a;
	int error;

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	if ((int)off < 0 || (int)(off + len) < 0)
		return (EINVAL);

	if (vp->v_type != VREG)
		return (ENODEV);

	/*
	 * Check to see if the vnode is currently marked as not cachable.
	 * This means portions of the file are locked (through VOP_FRLOCK).
	 * In this case the map request must be refused.
	 *
	 * XXX there is a race condition here.  What if a process is in
	 * in the process of locking a file, but not quite done yet?
	 */
	if (vp->v_flag & VNOCACHE)
		return (EAGAIN);

	as_rangelock(as);
	if (!(flags & MAP_FIXED)) {
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
	vn_a.prot = (u_char)prot;
	vn_a.maxprot = (u_char)maxprot;
	vn_a.flags = flags & ~MAP_TYPE;
	vn_a.cred = cr;
	vn_a.amp = NULL;

	error = as_map(as, *addrp, len, segvn_create, (caddr_t)&vn_a);
	as_rangeunlock(as);
	return (error);
}

/* ARGSUSED */
static int
nfs3_addmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
	u_int len, u_char prot, u_char maxprot, u_int flags, cred_t *cr)
{
	register rnode_t *rp;

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	rp->r_mapcnt += btopr(len);
	mutex_exit(&rp->r_statelock);
	return (0);
}

static int
nfs3_cmp(vnode_t *vp1, vnode_t *vp2)
{

	return (vp1 == vp2);
}

static int
nfs3_frlock(vnode_t *vp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr)
{
	netobj lm_fh3;
	int rc;
	u_long start, end;
	rnode_t *rp;
	int error;

	/* check for valid cmd parameter */
	if (cmd != F_GETLK && cmd != F_SETLK && cmd != F_SETLKW)
		return (EINVAL);

	/* Verify l_type. */
	switch (bfp->l_type) {
	case F_RDLCK:
		if (cmd != F_GETLK && !(flag & FREAD))
			return (EBADF);
		break;
	case F_WRLCK:
		if (cmd != F_GETLK && !(flag & FWRITE))
			return (EBADF);
		break;
	case F_UNLCK:
		break;

	default:
		return (EINVAL);
	}

	/* check the validity of the lock range */
	if (rc = flk_convert_lock_data(vp, bfp, &start, &end, offset))
		return (rc);
	if (rc = flk_check_lock_data(start, end))
		return (rc);

	/*
	 * If the file is currently mapped into user space, then don't
	 * allow the lock.
	 */
	rp = VTOR(vp);
	if (rp->r_mapcnt > 0)
		return (EAGAIN);

	/*
	 * If the filesystem is mounted using local locking, pass the
	 * request off to the local locking code.
	 */
	if (VTOMI(vp)->mi_flags & MI_LLOCK)
		return (fs_frlock(vp, cmd, bfp, flag, offset, cr));

	/*
	 * If currently dirty pages can't be flushed, then don't
	 * allow the lock.
	 */
	if (bfp->l_type != F_UNLCK && cmd != F_GETLK) {
		error = nfs3_putpage(vp, (offset_t)0, 0, B_INVAL, cr);
		if (error) {
			if (error == ENOSPC || error == EDQUOT) {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
			return (ENOLCK);
		}
	}

	lm_fh3.n_len = VTOFH3(vp)->fh3_length;
	lm_fh3.n_bytes = (char *) &(VTOFH3(vp)->fh3_u.data);

	/*
	 * Call the lock manager to do the real work of contacting
	 * the server and obtaining the lock.
	 */
	rc = lm4_frlock(vp, cmd, bfp, flag, offset, cr, &lm_fh3);

	if (rc == 0)
		nfs_lockcompletion(vp, cmd, bfp);

	return (rc);
}

/*
 * Free storage space associated with the specified vnode.  The portion
 * to be freed is specified by bfp->l_start and bfp->l_len (already
 * normalized to a "whence" of 0).
 *
 * This is an experimental facility whose continued existence is not
 * guaranteed.  Currently, we only support the special case
 * of l_len == 0, meaning free to end of file.
 */
/* ARGSUSED */
static int
nfs3_space(vnode_t *vp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr)
{
	int error;

	ASSERT(vp->v_type == VREG);
	if (cmd != F_FREESP)
		return (EINVAL);

	error = convoff(vp, bfp, 0, (off_t)offset);
	if (!error) {
		ASSERT(bfp->l_start >= 0);
		if (bfp->l_len == 0) {
			struct vattr va;

			va.va_mask = AT_SIZE;
			va.va_size = bfp->l_start;
			error = nfs3setattr(vp, &va, 0, cr);
		} else
			error = EINVAL;
	}

	return (error);
}

/* ARGSUSED */
static int
nfs3_realvp(vnode_t *vp, vnode_t **vpp)
{

	return (EINVAL);
}

/*
 * Remove some pages from an mmap'd vnode.  Just update the
 * count of pages.  If doing close-to-open, then flush and
 * commit all of the pages associated with this file.
 * Otherwise, just mark the rnode with RDIRTY to indicate
 * that a pass through the page list is required before
 * invalidating the pages.
 */
/* ARGSUSED */
static int
nfs3_delmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
	u_int len, u_int prot, u_int maxprot, u_int flags, cred_t *cr)
{
	int error;
	rnode_t *rp;

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	rp->r_mapcnt -= btopr(len);
	ASSERT(rp->r_mapcnt >= 0);
	mutex_exit(&rp->r_statelock);

	if (rp->r_mapcnt == 0 && vp->v_pages != NULL) {
		if (!(VTOMI(vp)->mi_flags & MI_NOCTO)) {
			error = nfs3_putpage_commit(vp, (offset_t)0, 0, cr);
			if (!error)
				error = rp->r_error;
			else {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
			/*
			 * If an error occurred and there are multiple
			 * references to this vnode, then purge any
			 * references which may be due to DNLC entries.
			 * This will hopefully force nfs3_inactive to be
			 * invoked as soon as possible.
			 */
			if (error && vp->v_count > 1)
				dnlc_purge_vp(vp);
			return (error);
		}
		mutex_enter(&rp->r_statelock);
		rp->r_flags |= RDIRTY;
		mutex_exit(&rp->r_statelock);
	}
	return (0);
}

static int
nfs3_pathconf(vnode_t *vp, int cmd, u_long *valp, cred_t *cr)
{
	int error;
	PATHCONF3args args;
	PATHCONF3res res;
	int douprintf;

	args.object = *VTOFH3(vp);

	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_PATHCONF,
			xdr_PATHCONF3args, (caddr_t)&args,
			xdr_PATHCONF3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno3(res.status);

	if (!error) {
		nfs3_cache_post_op_attr(vp, &res.resok.obj_attributes, cr);
		switch (cmd) {
		case _PC_LINK_MAX:
			*valp = res.resok.link_max;
			break;
		case _PC_NAME_MAX:
			*valp = res.resok.name_max;
			break;
		case _PC_PATH_MAX:
			*valp = MAXPATHLEN;
			break;
		case _PC_CHOWN_RESTRICTED:
			*valp = res.resok.chown_restricted;
			break;
		case _PC_NO_TRUNC:
			*valp = res.resok.no_trunc;
			break;
		default:
			return (EINVAL);
		}
	} else {
		nfs3_cache_post_op_attr(vp, &res.resfail.obj_attributes, cr);
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

/*
 * Called by async thread to do synchronous pageio. Do the i/o, wait
 * for it to complete, and cleanup the page list when done.
 */
static int
nfs3_sync_pageio(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr)
{
	int error;

	error = nfs3_rdwrlbn(vp, pp, io_off, io_len, flags, cr);
	if (flags & B_READ)
		pvn_read_done(pp, (error ? B_ERROR : 0) | flags);
	else
		pvn_write_done(pp, (error ? B_ERROR : 0) | flags);
	return (error);
}

static int
nfs3_pageio(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr)
{
	int error;
	rnode_t *rp;

	if (pp == NULL)
		return (EINVAL);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	rp->r_count++;
	mutex_exit(&rp->r_statelock);
	if (flags & B_ASYNC) {
		error = nfs_async_pageio(vp, pp, io_off, io_len, flags, cr,
					nfs3_sync_pageio);
	} else
		error = nfs3_rdwrlbn(vp, pp, io_off, io_len, flags, cr);
	mutex_enter(&rp->r_statelock);
	rp->r_count--;
	cv_signal(&rp->r_cv);
	mutex_exit(&rp->r_statelock);
	return (error);
}

static void
nfs3_dispose(vnode_t *vp, page_t *pp, int fl, int dn, cred_t *cr)
{
	int error;
	rnode_t *rp;
	page_t *plist;
	page_t *pptr;
	offset3 offset;
	count3 len;

	/*
	 * We should get called with fl equal to either B_FREE or
	 * B_INVAL.  Any other value is illegal.
	 *
	 * The page that we are either supposed to free or destroy
	 * should be exclusive locked and its io lock should not
	 * be held.
	 */
	ASSERT(fl == B_FREE || fl == B_INVAL);
	ASSERT((se_excl_assert(&pp->p_selock) &&
		!page_iolock_assert(pp)) || panicstr);

	rp = VTOR(vp);

	/*
	 * If the page doesn't need to be committed or we shouldn't
	 * even bother attempting to commit it, then just make sure
	 * that the p_fsdata byte is clear and then either free or
	 * destroy the page as appropriate.
	 */
	if (pp->p_fsdata == C_NOCOMMIT || (rp->r_flags & RDONTWRITE)) {
		pp->p_fsdata = C_NOCOMMIT;
		if (fl == B_FREE)
			page_free(pp, dn);
		else
			page_destroy(pp, dn);
		return;
	}

	/*
	 * If there is a page invalidation operation going on, then
	 * if this is one of the pages being destroyed, then just
	 * clear the p_fsdata byte and then either free or destroy
	 * the page as appropriate.
	 */
	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	if ((rp->r_flags & RTRUNCATE) && pp->p_offset >= rp->r_truncaddr) {
		mutex_exit(&rp->r_statelock);
		pp->p_fsdata = C_NOCOMMIT;
		if (fl == B_FREE)
			page_free(pp, dn);
		else
			page_destroy(pp, dn);
		return;
	}

	/*
	 * If we are freeing this page and someone else is already
	 * waiting to do a commit, then just unlock the page and
	 * return.  That other thread will take care of commiting
	 * this page.  The page can be freed sometime after the
	 * commit has finished.  Otherwise, if the page is marked
	 * as delay commit, then we may be getting called from
	 * pvn_write_done, one page at a time.   This could result
	 * in one commit per page, so we end up doing lots of small
	 * commits instead of fewer larger commits.  This is bad,
	 * we want do as few commits as possible.
	 */
	if (fl == B_FREE) {
		if (rp->r_flags & RCOMMITWAIT) {
			page_unlock(pp);
			mutex_exit(&rp->r_statelock);
			return;
		}
		if (pp->p_fsdata == C_DELAYCOMMIT) {
			pp->p_fsdata = C_COMMIT;
			page_unlock(pp);
			mutex_exit(&rp->r_statelock);
			return;
		}
	}

	/*
	 * We are starting to need to commit pages, so let's try
	 * to commit as many as possible at once to reduce the
	 * overhead.
	 *
	 * Set the `commit inprogress' state bit.  We must
	 * first wait until any current one finishes.  Then
	 * we initialize the c_pages list with this page.
	 */
	while (rp->r_flags & RCOMMIT) {
		rp->r_flags |= RCOMMITWAIT;
		cv_wait(&rp->r_commit.c_cv, &rp->r_statelock);
		rp->r_flags &= ~RCOMMITWAIT;
	}
	rp->r_flags |= RCOMMIT;
	mutex_exit(&rp->r_statelock);
	ASSERT(rp->r_commit.c_pages == NULL);
	rp->r_commit.c_pages = pp;
	rp->r_commit.c_commbase = (offset3)pp->p_offset;
	rp->r_commit.c_commlen = PAGESIZE;
	pp->p_fsdata = C_NOCOMMIT;

	/*
	 * Gather together all other pages which can be committed.
	 * They will all be chained off r_commit.c_pages.
	 */
	nfs3_get_commit(vp);

	/*
	 * Clear the `commit inprogress' status and disconnect
	 * the list of pages to be committed from the rnode.
	 * At this same time, we also save the starting offset
	 * and length of data to be committed on the server.
	 */
	plist = rp->r_commit.c_pages;
	rp->r_commit.c_pages = NULL;
	offset = rp->r_commit.c_commbase;
	len = rp->r_commit.c_commlen;
	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RCOMMIT;
	cv_broadcast(&rp->r_commit.c_cv);
	mutex_exit(&rp->r_statelock);

	/*
	 * Actually generate the COMMIT3 over the wire operation.
	 */
	error = nfs3_commit(vp, offset, len, cr);

	/*
	 * If we got a verifier mismatch error during the
	 * commit stage, just mark each page as modified
	 * and then unlock it.  The pages will get
	 * retransmitted to the server during a putpage
	 * operation.
	 */
	if (error == NFS_VERF_MISMATCH) {
		while (plist != NULL) {
			pptr = plist;
			page_sub(&plist, pptr);
			hat_setmod(pptr);
			page_unlock(pptr);
		}
		return;
	}

	/*
	 * We've tried as hard as we can to commit the
	 * data to stable storage on the server.  We
	 * just unlock the pages.  They will get freed later.
	 */
	while (plist != pp) {
		pptr = plist;
		page_sub(&plist, pptr);
		page_unlock(pptr);
	}

	/*
	 * Now, as appropriate, either free or destroy the page
	 * that we were called with.
	 */
	if (fl == B_FREE)
		page_free(pp, dn);
	else
		page_destroy(pp, dn);
}

static int
nfs3_commit(vnode_t *vp, offset3 offset, count3 count, cred_t *cr)
{
	int error;
	rnode_t *rp;
	COMMIT3args args;
	COMMIT3res res;
	int douprintf;
	cred_t *cred;

	rp = VTOR(vp);

	mutex_enter(&rp->r_statelock);
	if (rp->r_cred != NULL) {
		cred = rp->r_cred;
		crhold(cred);
	} else {
		rp->r_cred = cr;
		crhold(cr);
		cred = cr;
		crhold(cred);
	}
	mutex_exit(&rp->r_statelock);

	args.file = *VTOFH3(vp);
	args.offset = offset;
	args.count = count;

doitagain:
	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_COMMIT,
			xdr_COMMIT3args, (caddr_t)&args,
			xdr_COMMIT3res, (caddr_t)&res,
			cred, &douprintf, &res.status);

	crfree(cred);

	if (error)
		return (error);

	error = geterrno3(res.status);
	if (!error) {
		ASSERT(rp->r_flags & RHAVEVERF);
		mutex_enter(&rp->r_statelock);
		if (bcmp((caddr_t)&res.resok.verf, (caddr_t)&rp->r_verf,
		    sizeof (writeverf3)) == 0) {
			mutex_exit(&rp->r_statelock);
			return (0);
		}
		bcopy((caddr_t)&res.resok.verf, (caddr_t)&rp->r_verf,
		    sizeof (writeverf3));
		mutex_exit(&rp->r_statelock);
		error = NFS_VERF_MISMATCH;
	} else {
		if (error == EACCES) {
			mutex_enter(&rp->r_statelock);
			if (cred != cr) {
				if (rp->r_cred != NULL)
					crfree(rp->r_cred);
				rp->r_cred = cr;
				crhold(cr);
				cred = cr;
				crhold(cred);
				mutex_exit(&rp->r_statelock);
				goto doitagain;
			}
			mutex_exit(&rp->r_statelock);
		}
		/*
		 * Can't do a PURGE_STALE_FH here because this
		 * can cause a deadlock.  nfs3_commit can
		 * be called from nfs3_dispose which can be called
		 * indirectly via pvn_vplist_dirty.  PURGE_STALE_FH
		 * can call back to pvn_vplist_dirty.
		 */
		if (error == ESTALE) {
			mutex_enter(&rp->r_statelock);
			rp->r_flags |= RDONTWRITE;
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
			PURGE_ATTRCACHE(vp);
		} else {
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
	}

	return (error);
}

static void
nfs3_set_mod(vnode_t *vp)
{
	page_t *pp;
	kmutex_t *vphm;

	vphm = page_vnode_mutex(vp);
	mutex_enter(vphm);
	if ((pp = vp->v_pages) != NULL) {
		do {
			if (pp->p_fsdata != C_NOCOMMIT) {
				pp->p_fsdata = C_NOCOMMIT;
				hat_setmod(pp);
			}
		} while ((pp = pp->p_vpnext) != vp->v_pages);
	}
	mutex_exit(vphm);
}

/*
 * This routine is used to gather together a page list of the pages
 * which are to be committed on the server.  This routine must not
 * be called if the calling thread holds any locked pages.
 *
 * The calling thread must have set RCOMMIT.  This bit is used to
 * serialize access to the commit structure in the rnode.  As long
 * as the thread has set RCOMMIT, then it can manipulate the commit
 * structure without requiring any other locks.
 */
static void
nfs3_get_commit(vnode_t *vp)
{
	rnode_t *rp;
	page_t *pp;
	kmutex_t *vphm;

	rp = VTOR(vp);

	ASSERT(rp->r_flags & RCOMMIT);

	vphm = page_vnode_mutex(vp);
	mutex_enter(vphm);

	/*
	 * If there are no pages associated with this vnode, then
	 * just return.
	 */
	if ((pp = vp->v_pages) == NULL) {
		mutex_exit(vphm);
		return;
	}

	/*
	 * Step through all of the pages associated with this vnode
	 * looking for pages which need to be committed.
	 */
	do {
		/*
		 * If this page does not need to be committed or is
		 * modified, then just skip it.
		 */
		if (pp->p_fsdata == C_NOCOMMIT || PP_ISMOD(pp))
			continue;

		/*
		 * Attempt to lock the page.  If we can't, then
		 * someone else is messing with it and we will
		 * just skip it.
		 */
		if (!page_trylock(pp, SE_EXCL))
			continue;

		ASSERT(pp->p_free == 0);

		/*
		 * The page needs to be committed and we locked it.
		 * Update the base and length parameters and add it
		 * to r_pages.
		 */
		if (rp->r_commit.c_pages == NULL) {
			rp->r_commit.c_commbase = (offset3)pp->p_offset;
			rp->r_commit.c_commlen = PAGESIZE;
		} else if (pp->p_offset < rp->r_commit.c_commbase) {
			rp->r_commit.c_commlen = rp->r_commit.c_commbase -
						(offset3)pp->p_offset +
						rp->r_commit.c_commlen;
			rp->r_commit.c_commbase = (offset3)pp->p_offset;
		} else if ((rp->r_commit.c_commbase + rp->r_commit.c_commlen)
			    <= pp->p_offset) {
			rp->r_commit.c_commlen = (offset3)pp->p_offset -
						rp->r_commit.c_commbase +
						PAGESIZE;
		}
		pp->p_fsdata = C_NOCOMMIT;
		page_add(&rp->r_commit.c_pages, pp);
	} while ((pp = pp->p_vpnext) != vp->v_pages);

	mutex_exit(vphm);
}

#ifdef unused
#ifdef DEBUG
static int
nfs3_no_uncommitted_pages(vnode_t *vp)
{
	page_t *pp;
	kmutex_t *vphm;

	vphm = page_vnode_mutex(vp);
	mutex_enter(vphm);
	if ((pp = vp->v_pages) != NULL) {
		do {
			if (pp->p_fsdata != C_NOCOMMIT) {
				mutex_exit(vphm);
				return (0);
			}
		} while ((pp = pp->p_vpnext) != vp->v_pages);
	}
	mutex_exit(vphm);

	return (1);
}
#endif
#endif

static int
nfs3_putpage_commit(vnode_t *vp, offset_t poff, u_int plen, cred_t *cr)
{
	int error;
	rnode_t *rp;
	page_t *plist;
	page_t *pp;
	offset3 offset;
	count3 len;

	/*
	 * If this is a swap file or at least swap file like,
	 * Just return success and don't do anything else.
	 */
	if (IS_SWAPVP(vp))
		return (0);

	rp = VTOR(vp);

	/*
	 * Flush the data portion of the file and then commit any
	 * portions which need to be committed.  This may need to
	 * be done twice if the server has changed state since
	 * data was last written.  The data will need to be
	 * rewritten to the server and then a new commit done.
	 *
	 * In fact, this may need to be done several times if the
	 * server is having problems and crashing while we are
	 * attempting to do this.
	 */

top:
	/*
	 * Do a flush based on the poff and plen arguments.  This
	 * will synchronously write out any modified pages in the
	 * range specified by (poff, plen) and wait until all of
	 * the asynchronous i/o's in that range are done as well.
	 */
	error = nfs3_putpage(vp, poff, plen, 0, cr);

	/*
	 * Set the `commit inprogress' state bit.  We must
	 * first wait until any current one finishes.
	 */
	mutex_enter(&rp->r_statelock);
	while (rp->r_flags & RCOMMIT) {
		rp->r_flags |= RCOMMITWAIT;
		cv_wait(&rp->r_commit.c_cv, &rp->r_statelock);
		rp->r_flags &= ~RCOMMITWAIT;
	}
	rp->r_flags |= RCOMMIT;
	mutex_exit(&rp->r_statelock);

	/*
	 * Gather together all of the pages which need to be
	 * committed.
	 */
	if (!error)
		nfs3_get_commit(vp);

	/*
	 * Clear the `commit inprogress' bit and disconnect the
	 * page list which was gathered together in nfs3_get_commit.
	 */
	plist = rp->r_commit.c_pages;
	rp->r_commit.c_pages = NULL;
	offset = rp->r_commit.c_commbase;
	len = rp->r_commit.c_commlen;
	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RCOMMIT;
	cv_broadcast(&rp->r_commit.c_cv);
	mutex_exit(&rp->r_statelock);

	/*
	 * If any pages need to be committed, commit them and
	 * then unlock them so that they can be freed some
	 * time later.
	 */
	if (plist != NULL) {
		ASSERT(!error);

		/*
		 * No error occurred during the flush portion
		 * of this operation, so now attempt to commit
		 * the data to stable storage on the server.
		 */
		error = nfs3_commit(vp, offset, len, cr);

		/*
		 * If we got a verifier mismatch error during the
		 * commit stage, just mark each page as modified
		 * and then unlock it.  The pages will get
		 * retransmitted to the server during the putpage
		 * operation.
		 */
		if (error == NFS_VERF_MISMATCH) {
			while (plist != NULL) {
				pp = plist;
				page_sub(&plist, pp);
				hat_setmod(pp);
				page_unlock(pp);
			}
			goto top;
		}
		/*
		 * We've tried as hard as we can to commit the
		 * data to stable storage on the server.  We
		 * just unlock the pages.  They will get freed later.
		 */
		while (plist != NULL) {
			pp = plist;
			page_sub(&plist, pp);
			page_unlock(pp);
		}
	}

	return (error);
}

static int
nfs3_setsecattr(vnode_t *vp, vsecattr_t *vsecattr, int flag, cred_t *cr)
{
	int error;
	mntinfo_t *mi;

	mi = VTOMI(vp);

	if (mi->mi_flags & MI_ACL) {
		error = acl_setacl3(vp, vsecattr, flag, cr);
		if (mi->mi_flags & MI_ACL)
			return (error);
	}

	return (ENOSYS);
}

static int
nfs3_getsecattr(vnode_t *vp, vsecattr_t *vsecattr, int flag, cred_t *cr)
{
	int error;
	mntinfo_t *mi;

	mi = VTOMI(vp);

	if (mi->mi_flags & MI_ACL) {
		error = acl_getacl3(vp, vsecattr, flag, cr);
		if (mi->mi_flags & MI_ACL)
			return (error);
	}

	return (fs_fab_acl(vp, vsecattr, flag, cr));
}
