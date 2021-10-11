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

#pragma ident	"@(#)nfs_vnops.c	1.131	95/10/26 SMI"
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
#include <vm/seg_vn.h>
#include <vm/rm.h>

#include <fs/fs_subr.h>

static int	nfs_rdwrlbn(vnode_t *, page_t *, u_int, u_int, int, cred_t *);
static int	nfswrite(vnode_t *, caddr_t, u_int, long, cred_t *);
static int	nfsread(vnode_t *, caddr_t, u_int, long, long *, cred_t *,
			struct vattr *);
static int	nfssetattr(vnode_t *, struct vattr *, int, cred_t *);
static int	nfslookup(vnode_t *, char *, vnode_t **,
			struct pathname *, int, vnode_t *, cred_t *);
static int	nfsrename(vnode_t *, char *, vnode_t *, char *, cred_t *);
static int	nfsreaddir(vnode_t *, rddir_cache *, cred_t *);
static int	nfs_bio(struct buf *, cred_t *);
static int	nfs_getapage(vnode_t *, u_int, u_int, u_int *,
			page_t *[], u_int, struct seg *, caddr_t,
			enum seg_rw, cred_t *);
static void	nfs_readahead(vnode_t *, u_int, caddr_t, struct seg *,
			cred_t *);
static int	nfs_sync_putapage(vnode_t *, page_t *, u_int, u_int,
			int, cred_t *);
static int	nfs_sync_pageio(vnode_t *, page_t *, u_int, u_int,
			int, cred_t *);

/*
 * Error flags used to pass information about certain special errors
 * which need to be handled specially.
 */
#define	NFS_CACHEINVALERR	-99
#define	NFS_EOF			-98

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

static int	nfs_open(vnode_t **, int, cred_t *);
static int	nfs_close(vnode_t *, int, int, offset_t, cred_t *);
static int	nfs_read(vnode_t *, struct uio *, int, cred_t *);
static int	nfs_write(vnode_t *, struct uio *, int, cred_t *);
static int	nfs_ioctl(vnode_t *, int, int, int, cred_t *, int *);
static int	nfs_getattr(vnode_t *, struct vattr *, int, cred_t *);
static int	nfs_setattr(vnode_t *, register struct vattr *, int, cred_t *);
static int	nfs_access(vnode_t *, int, int, cred_t *);
static int	nfs_readlink(vnode_t *, struct uio *, cred_t *);
static int	nfs_fsync(vnode_t *, int, cred_t *);
static void	nfs_inactive(vnode_t *, cred_t *);
static int	nfs_lookup(vnode_t *, char *, vnode_t **,
			struct pathname *, int, vnode_t *, cred_t *);
static int	nfs_create(vnode_t *, char *, struct vattr *, enum vcexcl,
			int, vnode_t **, cred_t *);
static int	nfs_remove(vnode_t *, char *, cred_t *);
static int	nfs_link(vnode_t *, vnode_t *, char *, cred_t *);
static int	nfs_rename(vnode_t *, char *, vnode_t *, char *, cred_t *);
static int	nfs_mkdir(vnode_t *, char *, register struct vattr *,
			vnode_t **, cred_t *);
static int	nfs_rmdir(vnode_t *, char *, vnode_t *, cred_t *);
static int	nfs_symlink(vnode_t *, char *, struct vattr *, char *,
			cred_t *);
static int	nfs_readdir(vnode_t *, struct uio *, cred_t *, int *);
static int	nfs_fid(vnode_t *, fid_t *);
static void	nfs_rwlock(vnode_t *, int);
static void	nfs_rwunlock(vnode_t *, int);
static int	nfs_seek(vnode_t *, offset_t, offset_t *);
static int	nfs_getpage(vnode_t *, offset_t, u_int, u_int *,
			page_t *[], u_int, struct seg *, caddr_t,
			enum seg_rw, cred_t *);
static int	nfs_putpage(vnode_t *, offset_t, u_int, int, cred_t *);
static int	nfs_map(vnode_t *, offset_t, struct as *, caddr_t *,
			u_int, u_char, u_char, u_int, cred_t *);
static int	nfs_addmap(vnode_t *, offset_t, struct as *, caddr_t,
			u_int, u_char, u_char, u_int, cred_t *);
static int	nfs_cmp(vnode_t *, vnode_t *);
static int	nfs_frlock(vnode_t *, int, struct flock *, int, offset_t,
			cred_t *);
static int	nfs_space(vnode_t *, int, struct flock *, int, offset_t,
			cred_t *);
static int	nfs_realvp(vnode_t *, vnode_t **);
static int	nfs_delmap(vnode_t *, offset_t, struct as *, caddr_t,
			u_int, u_int, u_int, u_int, cred_t *);
static int	nfs_pathconf(vnode_t *, int, u_long *, cred_t *);
static int	nfs_pageio(vnode_t *, page_t *, u_int, u_int, int, cred_t *);
static int	nfs_setsecattr(vnode_t *, vsecattr_t *, int, cred_t *);
static int	nfs_getsecattr(vnode_t *, vsecattr_t *, int, cred_t *);

struct vnodeops nfs_vnodeops = {
	nfs_open,
	nfs_close,
	nfs_read,
	nfs_write,
	nfs_ioctl,
	fs_setfl,
	nfs_getattr,
	nfs_setattr,
	nfs_access,
	nfs_lookup,
	nfs_create,
	nfs_remove,
	nfs_link,
	nfs_rename,
	nfs_mkdir,
	nfs_rmdir,
	nfs_readdir,
	nfs_symlink,
	nfs_readlink,
	nfs_fsync,
	nfs_inactive,
	nfs_fid,
	nfs_rwlock,
	nfs_rwunlock,
	nfs_seek,
	nfs_cmp,
	nfs_frlock,
	nfs_space,
	nfs_realvp,
	nfs_getpage,
	nfs_putpage,
	nfs_map,
	nfs_addmap,
	nfs_delmap,
	fs_poll,
	nfs_dump,
	nfs_pathconf,
	nfs_pageio,
	fs_nosys,	/* dumpctl */
	fs_dispose,
	nfs_setsecattr,
	nfs_getsecattr
};

/*
 * XXX:  This is referenced in modstubs.s
 */
struct vnodeops *
nfs_getvnodeops(void)
{

	return (&nfs_vnodeops);
}

/* ARGSUSED */
static int
nfs_open(vnode_t **vpp, int flag, cred_t *cr)
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
		error = nfs_getattr_otw(*vpp, &va, cr);
		if (error == ESTALE)
			ttolwp(curthread)->lwp_eosys = RESTARTSYS;
	}

	return (error);
}

static int
nfs_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr)
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
	 * for write, then flush all dirty pages to the server.
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
		error = nfs_putpage(vp, (offset_t)0, 0, 0, cr);
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
nfs_read(vnode_t *vp, struct uio *uiop, int ioflag, cred_t *cr)
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

		error = nfs_validate_caches(vp, cr);
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
nfs_write(vnode_t *vp, struct uio *uiop, int ioflag, cred_t *cr)
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

	rp = VTOR(vp);

	ASSERT(RW_WRITE_HELD(&rp->r_rwlock));

	if (vp->v_type != VREG)
		return (EISDIR);

	if (uiop->uio_resid == 0)
		return (0);

	if (ioflag & FAPPEND) {
		struct vattr va;

		va.va_mask = AT_SIZE;
		error = nfsgetattr(vp, &va, cr);
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

		if (rp->r_flags & RDONTWRITE) {
			error = rp->r_error;
			goto bottom;
		}

		bufsize = MIN(uiop->uio_resid, PAGESIZE);
		base = (caddr_t) kmem_alloc(bufsize, KM_SLEEP);

		do {
			count = MIN(uiop->uio_resid, PAGESIZE);
			org_offset = (u_int)uiop->uio_offset;
			error = uiomove(base, count, UIO_WRITE, uiop);
			if (!error) {
				error = nfswrite(vp, base, org_offset,
						count, cr);
			}
		} while (!error && uiop->uio_resid > 0);

		kmem_free(base, bufsize);
		goto bottom;
	}

	mi = VTOMI(vp);

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
			else if (n + on == MAXBSIZE || IS_SWAPVP(vp)) {
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
nfs_rdwrlbn(vnode_t *vp, page_t *pp, u_int off, u_int len,
	int flags, cred_t *cr)
{
	register struct buf *bp;
	int error;

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

	error = nfs_bio(bp, cr);

	bp_mapout(bp);
	pageio_done(bp);

	return (error);
}

/*
 * Write to file.  Writes to remote server in largest size
 * chunks that the server can handle.  Write is synchronous.
 */
static int
nfswrite(vnode_t *vp, caddr_t base, u_int offset, long count, cred_t *cr)
{
	mntinfo_t *mi;
	struct nfswriteargs wa;
	struct nfsattrstat ns;
	int error;
	int tsize;
	int douprintf;

	mi = VTOMI(vp);
	douprintf = 1;

	wa.wa_args = &wa.wa_args_buf;
	wa.wa_fhandle = *VTOFH(vp);

	do {
		tsize = MIN(mi->mi_curwrite, count);
		wa.wa_data = base;
		wa.wa_begoff = offset;
		wa.wa_totcount = tsize;
		wa.wa_count = tsize;
		wa.wa_offset = offset;
		error = rfs2call(mi, RFS_WRITE,
				xdr_writeargs, (caddr_t)&wa,
				xdr_attrstat, (caddr_t)&ns,
				cr, &douprintf, &ns.ns_status);
		if (error == ENFS_TRYAGAIN) {
			error = 0;
			continue;
		}
		if (!error) {
			error = geterrno(ns.ns_status);
			/*
			 * Can't check for stale fhandle and purge caches
			 * here because pages are held by nfs_getpage.
			 */
		}
		count -= tsize;
		base += tsize;
		offset += tsize;
	} while (!error && count);

	if (!error) {
		nfs_attrcache(vp, &ns.ns_attr, VTOR(vp)->r_seq);
		/*
		 * If ACL is supported on a server, then attribute
		 * returned by server will have minimal permissions
		 * denying sometimes access to users having proper
		 * ACL set up for them. So in this case, always force
		 * to get the attributes by NFS_ACL GETATTR2. We have
		 * to pay this performance penalty to buy ACL support.
		 */
		if (mi->mi_flags & MI_ACL)
			PURGE_ATTRCACHE(vp);
	}

	return (error);
}

/*
 * Read from a file.  Reads data in largest chunks our interface can handle.
 */
static int
nfsread(vnode_t *vp, caddr_t base, u_int offset, long count, long *residp,
	cred_t *cr, struct vattr *vap)
{
	mntinfo_t *mi;
	struct nfsreadargs ra;
	struct nfsrdresult rr;
	register int tsize;
	int error;
	int douprintf;

	mi = VTOMI(vp);
	douprintf = 1;

	ra.ra_fhandle = *VTOFH(vp);

	do {
		do {
			tsize = MIN(mi->mi_curread, count);
			rr.rr_data = base;
			ra.ra_offset = offset;
			ra.ra_totcount = tsize;
			ra.ra_count = tsize;
			error = rfs2call(mi, RFS_READ,
					xdr_readargs, (caddr_t)&ra,
					xdr_rdresult, (caddr_t)&rr,
					cr, &douprintf, &rr.rr_status);
		} while (error == ENFS_TRYAGAIN);

		if (!error) {
			error = geterrno(rr.rr_status);
			/*
			 * Can't purge caches here because pages are held by
			 * nfs_getpage.
			 */
		}
		if (!error) {
			count -= rr.rr_count;
			base += rr.rr_count;
			offset += rr.rr_count;
		}
	} while (!error && count && rr.rr_count == tsize);

	*residp = count;

	if (!error)
		nattr_to_vattr(vp, &rr.rr_attr, vap);

	return (error);
}

/* ARGSUSED */
static int
nfs_ioctl(vnode_t *vp, int com, int arg, int flag, cred_t *cr, int *rvalp)
{

	return (ENOTTY);
}

static int
nfs_getattr(vnode_t *vp, struct vattr *vap, int flags, cred_t *cr)
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
			error = nfs_putpage(vp, (offset_t)0, 0, 0, cr);
			if (error && (error == ENOSPC || error == EDQUOT)) {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
		}
	}

	return (nfsgetattr(vp, vap, cr));
}

static int
nfs_setattr(vnode_t *vp, struct vattr *vap, int flags, cred_t *cr)
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
			error = nfs_access(vp, VWRITE, 0, cr);
			if (error)
				return (error);
		}
	}
	return (nfssetattr(vp, vap, flags, cr));
}

static int
nfssetattr(vnode_t *vp, struct vattr *vap, int flags, cred_t *cr)
{
	int error;
	long mask;
	struct nfssaargs args;
	struct nfsattrstat ns;
	int douprintf;
	rnode_t *rp;

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
		error = nfs_putpage(vp, (offset_t)0, 0, 0, cr);
		if (error && (error == ENOSPC || error == EDQUOT)) {
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
	}

	/*
	 * If the system call was utime(2) or utimes(2) and the
	 * application did not specify the times, then set the
	 * mtime nanosecond field to 1 billion.  This will get
	 * translated from 1 billion nanoseconds to 1 million
	 * microseconds in the over the wire request.  The
	 * server will use 1 million in the microsecond field
	 * to tell whether both the mtime and atime should be
	 * set to the server's current time.
	 *
	 * This is an overload of the protocol and should be
	 * documented in the NFS Version 2 protocol specification.
	 */
	if ((mask & AT_MTIME) && !(flags & ATTR_UTIME))
		vap->va_mtime.tv_nsec = 1000000000;

	vattr_to_sattr(vap, &args.saa_sa);
	args.saa_fh = *VTOFH(vp);
	douprintf = 1;
	error = rfs2call(VTOMI(vp), RFS_SETATTR,
			xdr_saargs, (caddr_t)&args,
			xdr_attrstat, (caddr_t)&ns,
			cr, &douprintf, &ns.ns_status);
	if (!error) {
		error = geterrno(ns.ns_status);
		if (!error) {
			timestruc_t ctime;
			timestruc_t mtime;
			long seq;

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
			ctime.tv_sec = ns.ns_attr.na_ctime.tv_sec;
			ctime.tv_nsec = ns.ns_attr.na_ctime.tv_usec * 1000;
			mtime.tv_sec = ns.ns_attr.na_mtime.tv_sec;
			mtime.tv_nsec = ns.ns_attr.na_mtime.tv_usec * 1000;
			nfs_cache_check(vp, ctime, mtime, ns.ns_attr.na_size,
					&seq, cr);
			nfs_attrcache(vp, &ns.ns_attr, seq);
			/*
			 * If ACL is supported on a server, then attribute
			 * returned by server will have minimal permissions
			 * denying sometimes access to users having proper
			 * ACL set up for them. So in this case, always force
			 * to get the attributes by NFS_ACL GETATTR2. We have
			 * to pay this performance penalty to buy ACL support.
			 */
			if (VTOMI(vp)->mi_flags & MI_ACL)
				PURGE_ATTRCACHE(vp);

			/*
			 * This next check attempts to deal with NFS
			 * servers which can not handle increasing
			 * the size of the file via setattr.  Most
			 * of these servers do not return an error,
			 * but do not change the size of the file.
			 * Hence, this check and then attempt to set
			 * the file size by writing 1 byte at the
			 * offset of the end of the file that we need.
			 */
			if ((mask & AT_SIZE) &&
			    ns.ns_attr.na_size < vap->va_size) {
				char zb = '\0';

				error = nfswrite(vp, &zb,
						vap->va_size - sizeof (zb),
						(long) sizeof (zb), cr);
			}
		} else {
			PURGE_ATTRCACHE(vp);
			PURGE_STALE_FH(error, vp, cr);
		}
	} else
		PURGE_ATTRCACHE(vp);

	return (error);
}

static int
nfs_access(vnode_t *vp, int mode, int flags, cred_t *cr)
{
	struct vattr va;
	gid_t *gp;
	int error;
	mntinfo_t *mi;

	mi = VTOMI(vp);

	if (mi->mi_flags & MI_ACL) {
		error = acl_access2(vp, mode, flags, cr);
		if (mi->mi_flags & MI_ACL)
			return (error);
	}

	va.va_mask = AT_MODE | AT_UID | AT_GID;
	error = nfsgetattr(vp, &va, cr);
	if (error)
		return (error);

	/*
	 * Disallow write attempts on read-only
	 * file systems, unless the file is a
	 * device node.
	 */
	if ((mode & VWRITE) &&
	    (vp->v_vfsp->vfs_flag & VFS_RDONLY) &&
	    !ISVDEV(vp->v_type))
		return (EROFS);

	/*
	 * Disallow attempts to access mandatory lock files.
	 */
	if ((mode & (VWRITE | VREAD | VEXEC)) &&
	    MANDLOCK(vp, va.va_mode))
		return (EACCES);

	/*
	 * If you're the super-user,
	 * you always get access.
	 */
	if (cr->cr_uid == 0)
		return (0);

	/*
	 * Access check is based on only
	 * one of owner, group, public.
	 * If not owner, then check group.
	 * If not a member of the group,
	 * then check public access.
	 */
	if (cr->cr_uid != va.va_uid) {
		mode >>= 3;
		if (cr->cr_gid == va.va_gid)
			goto found;
		gp = cr->cr_groups;
		for (; gp < &cr->cr_groups[cr->cr_ngroups]; gp++)
			if (va.va_gid == *gp)
				goto found;
		mode >>= 3;
	}
found:
	if ((va.va_mode & mode) == mode)
		return (0);

	return (EACCES);
}

static int nfs_do_symlink_cache = 1;

static int
nfs_readlink(vnode_t *vp, struct uio *uiop, cred_t *cr)
{
	int error;
	struct nfsrdlnres rl;
	rnode_t *rp;
	int douprintf;

	/*
	 * We want to be consistent with UFS semantics so we will return
	 * EINVAL instead of ENXIO. This violates the XNFS spec and
	 * the RFC 1094, which are wrong any way. BUGID 1138002.
	 */
	if (vp->v_type != VLNK)
		return (EINVAL);

	rp = VTOR(vp);
	if (nfs_do_symlink_cache && rp->r_symlink.contents != NULL) {
		error = nfs_validate_caches(vp, cr);
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

#ifdef DEBUG
	rl.rl_data = symlink_cache_alloc(NFS_MAXPATHLEN, KM_SLEEP);
#else
	rl.rl_data = (char *)kmem_alloc(NFS_MAXPATHLEN, KM_SLEEP);
#endif

	douprintf = 1;
	error = rfs2call(VTOMI(vp), RFS_READLINK,
			xdr_fhandle, (caddr_t)VTOFH(vp),
			xdr_rdlnres, (caddr_t)&rl,
			cr, &douprintf, &rl.rl_status);

	if (error) {
#ifdef DEBUG
		symlink_cache_free((void *)rl.rl_data, NFS_MAXPATHLEN);
#else
		kmem_free((caddr_t)rl.rl_data, NFS_MAXPATHLEN);
#endif
		return (error);
	}

	error = geterrno(rl.rl_status);
	if (!error) {
		error = uiomove(rl.rl_data, (int)rl.rl_count, UIO_READ, uiop);
		if (nfs_do_symlink_cache && rp->r_symlink.contents == NULL) {
			mutex_enter(&rp->r_statelock);
			if (rp->r_symlink.contents == NULL) {
				rp->r_symlink.contents = rl.rl_data;
				rp->r_symlink.len = (int)rl.rl_count;
				rp->r_symlink.size = NFS_MAXPATHLEN;
				mutex_exit(&rp->r_statelock);
			} else {
				mutex_exit(&rp->r_statelock);
#ifdef DEBUG
				symlink_cache_free((void *)rl.rl_data,
						NFS_MAXPATHLEN);
#else
				kmem_free((caddr_t)rl.rl_data, NFS_MAXPATHLEN);
#endif
			}
		} else {
#ifdef DEBUG
			symlink_cache_free((void *)rl.rl_data, NFS_MAXPATHLEN);
#else
			kmem_free((caddr_t)rl.rl_data, NFS_MAXPATHLEN);
#endif
		}
	} else {
		PURGE_STALE_FH(error, vp, cr);
#ifdef DEBUG
		symlink_cache_free((void *)rl.rl_data, NFS_MAXPATHLEN);
#else
		kmem_free((caddr_t)rl.rl_data, NFS_MAXPATHLEN);
#endif
	}

	/*
	 * Conform to UFS semantics (see comment above)
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
nfs_fsync(vnode_t *vp, int syncflag, cred_t *cr)
{
	int error;

	if ((syncflag & FNODSYNC) || IS_SWAPVP(vp))
		return (0);
	error = nfs_putpage(vp, (offset_t)0, 0, 0, cr);
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
nfs_inactive(vnode_t *vp, cred_t *cr)
{
	rnode_t *rp;

	ASSERT(vp != &nfs_notfound);

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
			struct nfsdiropargs da;
			enum nfsstat status;
			int douprintf;

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
			setdiropargs(&da, unlname, unldvp);
			douprintf = 1;
			(void) rfs2call(VTOMI(unldvp), RFS_REMOVE,
					xdr_diropargs, (caddr_t)&da,
					xdr_enum, (caddr_t)&status,
					unlcred, &douprintf, &status);
			PURGE_ATTRCACHE(unldvp);

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
nfs_lookup(vnode_t *dvp, char *nm, vnode_t **vpp,
	struct pathname *pnp, int flags, vnode_t *rdir, cred_t *cr)
{
	int error;
	vnode_t *vp;

	error = nfslookup(dvp, nm, vpp, pnp, flags, rdir, cr);

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

static int nfs_lookup_neg_cache = 0;

#ifdef DEBUG
static int nfs_lookup_dnlc_hits = 0;
static int nfs_lookup_dnlc_misses = 0;
static int nfs_lookup_dnlc_neg_hits = 0;
static int nfs_lookup_dnlc_disappears = 0;
static int nfs_lookup_dnlc_lookups = 0;
#endif

/* ARGSUSED */
static int
nfslookup(vnode_t *dvp, char *nm, vnode_t **vpp,
	struct pathname *pnp, int flags, vnode_t *rdir, cred_t *cr)
{
	int error;
	struct nfsdiropargs da;
	struct nfsdiropres dr;
	int douprintf;
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
		error = nfs_access(dvp, VEXEC, 0, cr);
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
	 * to nfs_access.
	 *
	 * The assumption is made that nfs_access invokes nfsgetattr
	 * to fetch the attributes.  If the attributes were timed
	 * out, new attributes are gotten from the server.  When this
	 * happens, the attribute cache is updated and the DNLC will
	 * be purged if appropriate.
	 *
	 * Another assumption that is being made is that it is safe
	 * to say that a file exists which may not on the server.
	 * Any operations to the server will fail with ESTALE.
	 */
#ifdef DEBUG
	nfs_lookup_dnlc_lookups++;
#endif
	vp = dnlc_lookup(dvp, nm, NOCRED);
	if (vp != NULL) {
		VN_RELE(vp);
		if (vp == &nfs_notfound)
			PURGE_ATTRCACHE(dvp);
		error = nfs_access(dvp, VEXEC, 0, cr);
		if (error)
			return (error);
		vp = dnlc_lookup(dvp, nm, NOCRED);
		if (vp != NULL) {
			if (vp == &nfs_notfound) {
				VN_RELE(vp);
#ifdef DEBUG
				nfs_lookup_dnlc_neg_hits++;
#endif
				return (ENOENT);
			}
			*vpp = vp;
#ifdef DEBUG
			nfs_lookup_dnlc_hits++;
#endif
			return (0);
		}
#ifdef DEBUG
		nfs_lookup_dnlc_disappears++;
#endif
	}
#ifdef DEBUG
	else
		nfs_lookup_dnlc_misses++;
#endif

	setdiropargs(&da, nm, dvp);

	douprintf = 1;
	error = rfs2call(VTOMI(dvp), RFS_LOOKUP,
			xdr_diropargs, (caddr_t)&da,
			xdr_diropres, (caddr_t)&dr,
			cr, &douprintf, &dr.dr_status);

	if (!error) {
		error = geterrno(dr.dr_status);
		if (!error) {
			*vpp = makenfsnode(&dr.dr_fhandle, &dr.dr_attr,
					    dvp->v_vfsp, cr);
			/*
			 * If ACL is supported on a server, then attribute
			 * returned by server will have minimal permissions
			 * denying sometimes access to users having proper
			 * ACL set up for them. So in this case, always force
			 * to get the attributes by NFS_ACL GETATTR2. We have
			 * to pay this performance penalty to buy ACL support.
			 */
			if (VTOMI(*vpp)->mi_flags & MI_ACL)
				PURGE_ATTRCACHE(*vpp);
			dnlc_enter(dvp, nm, *vpp, NOCRED);
		} else {
			PURGE_STALE_FH(error, dvp, cr);
			if (error == ENOENT && nfs_lookup_neg_cache)
				dnlc_enter(dvp, nm, &nfs_notfound, NOCRED);
		}
	}

	return (error);
}

static int
nfs_create(vnode_t *dvp, char *nm, struct vattr *va,
	enum vcexcl exclusive, int mode, vnode_t **vpp, cred_t *cr)
{
	int error;
	struct nfscreatargs args;
	struct nfsdiropres dr;
	int douprintf;
	vnode_t *vp;
	rnode_t *rp;

	error = nfs_lookup(dvp, nm, &vp, NULL, 0, NULL, cr);
	if (!error) {
		if (exclusive == EXCL)
			error = EEXIST;
		else if (vp->v_type == VDIR && (mode & VWRITE))
			error = EISDIR;
		else if (!(error = VOP_ACCESS(vp, mode, 0, cr))) {
			if ((va->va_mask & AT_SIZE) && vp->v_type == VREG) {
				va->va_mask = AT_SIZE;
				error = nfssetattr(vp, va, 0, cr);
			}
		}
		if (error) {
			VN_RELE(vp);
			/*
			 * Check the comments above in nfs_open() for
			 * the explanation for this.
			 */
			if (error == ESTALE)
				ttolwp(curthread)->lwp_eosys = RESTARTSYS;
		} else
			*vpp = vp;
		return (error);
	}

	ASSERT(va->va_mask & AT_TYPE);
	if (va->va_type == VREG) {
		ASSERT(va->va_mask & AT_MODE);
		if (MANDMODE(va->va_mode))
			return (EACCES);
	}

	dnlc_remove(dvp, nm);

	setdiropargs(&args.ca_da, nm, dvp);

	/*
	 * Decide what the group-id of the created file should be.
	 * Set it in attribute list as advisory...then do a setattr
	 * if the server didn't get it right the first time.
	 */
	va->va_gid = setdirgid(dvp, cr);
	va->va_mask |= AT_GID;

	/*
	 * This is a completely gross hack to make mknod
	 * work over the wire until we can wack the protocol
	 */
#define	IFCHR		0020000		/* character special */
#define	IFBLK		0060000		/* block special */
#define	IFSOCK		0140000		/* socket */

	/*
	 * Bug 1107325: dev_t is u_long in 5.x and short in 4.x. Both 4.x
	 * supports 8 bit majors. 5.x supports 14 bit majors. 5.x supports 18
	 * bits in the minor number where 4.x supports 8 bits.  If the 5.x
	 * minor/major numbers <= 8 bits long, compress the device
	 * number before sending it. Otherwise, the 4.x server will not
	 * create the device with the correct device number and nothing can be
	 * done about this.
	 */
	if (va->va_type == VCHR || va->va_type == VBLK) {
		if (va->va_type == VCHR)
			va->va_mode |= IFCHR;
		else
			va->va_mode |= IFBLK;
		if (va->va_rdev & ~((SO4_MAXMAJ<<L_BITSMINOR) | SO4_MAXMIN))
			va->va_size = (u_long)va->va_rdev;
		else
			va->va_size = (u_long)nfsv2_cmpdev(va->va_rdev);
		va->va_mask |= AT_MODE|AT_SIZE;
	} else if (va->va_type == VFIFO) {
		va->va_mode |= IFCHR;		/* xtra kludge for namedpipe */
		va->va_size = (u_long)NFS_FIFO_DEV;	/* blech */
		va->va_mask |= AT_MODE|AT_SIZE;
	}

	args.ca_sa = &args.ca_sa_buf;
	vattr_to_sattr(va, args.ca_sa);

	douprintf = 1;
	error = rfs2call(VTOMI(dvp), RFS_CREATE,
			xdr_creatargs, (caddr_t)&args,
			xdr_diropres, (caddr_t)&dr,
			cr, &douprintf, &dr.dr_status);

	PURGE_ATTRCACHE(dvp);	/* mod time changed */

	if (!error) {
		error = geterrno(dr.dr_status);
		if (!error) {
			if (VTOR(dvp)->r_dir != NULL)
				nfs_purge_rddir_cache(dvp);
			vp = makenfsnode(&dr.dr_fhandle, &dr.dr_attr,
					dvp->v_vfsp, cr);
			/*
			 * If ACL is supported on a server, then attribute
			 * returned by server will have minimal permissions
			 * denying sometimes access to users having proper
			 * ACL set up for them. So in this case, always force
			 * to get the attributes by NFS_ACL GETATTR2. We have
			 * to pay this performance penalty to buy ACL support.
			 */
			if (VTOMI(vp)->mi_flags & MI_ACL)
				PURGE_ATTRCACHE(vp);
			rp = VTOR(vp);
			if (va->va_size == 0) {
				rp->r_size = 0;
				if (vp->v_pages != NULL) {
					ASSERT(vp->v_type != VCHR);
					nfs_invalidate_pages(vp, 0, cr);
				}
			}

			/*
			 * Make sure the gid was set correctly.
			 * If not, try to set it (but don't lose
			 * any sleep over it).
			 */
			if (va->va_gid != rp->r_attr.va_gid) {
				va->va_mask = AT_GID;
				(void) nfssetattr(vp, va, 0, cr);
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
			PURGE_STALE_FH(error, dvp, cr);
		}
	}
	return (error);
}

/*
 * Weirdness: if the vnode to be removed is open
 * we rename it instead of removing it and nfs_inactive
 * will remove the new name.
 */
static int
nfs_remove(vnode_t *dvp, char *nm, cred_t *cr)
{
	int error;
	struct nfsdiropargs da;
	enum nfsstat status;
	vnode_t *vp;
	char *tmpname;
	int douprintf;
	rnode_t *rp;

	error = nfslookup(dvp, nm, &vp, NULL, 0, NULL, cr);
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
		error = nfsrename(dvp, nm, dvp, tmpname, cr);
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
			error = nfs_putpage(vp, (offset_t)0, 0, 0, cr);
			if (error && (error == ENOSPC || error == EDQUOT)) {
				mutex_enter(&rp->r_statelock);
				if (!rp->r_error)
					rp->r_error = error;
				mutex_exit(&rp->r_statelock);
			}
		}

		setdiropargs(&da, nm, dvp);

		douprintf = 1;
		error = rfs2call(VTOMI(dvp), RFS_REMOVE,
				xdr_diropargs, (caddr_t)&da,
				xdr_enum, (caddr_t)&status,
				cr, &douprintf, &status);

		PURGE_ATTRCACHE(dvp);	/* mod time changed */
		PURGE_ATTRCACHE(vp);	/* link count changed */

		if (!error) {
			error = geterrno(status);
			if (!error) {
				if (VTOR(dvp)->r_dir != NULL)
					nfs_purge_rddir_cache(dvp);
			} else {
				PURGE_STALE_FH(error, dvp, cr);
			}
		}
	}

	VN_RELE(vp);

	return (error);
}

static int
nfs_link(vnode_t *tdvp, vnode_t *svp, char *tnm, cred_t *cr)
{
	register int error;
	struct nfslinkargs args;
	enum nfsstat status;
	vnode_t *realvp;
	int douprintf;

	if (VOP_REALVP(svp, &realvp) == 0)
		svp = realvp;

	args.la_from = VTOFH(svp);
	setdiropargs(&args.la_to, tnm, tdvp);

	dnlc_remove(tdvp, tnm);

	douprintf = 1;
	error = rfs2call(VTOMI(svp), RFS_LINK,
			xdr_linkargs, (caddr_t)&args,
			xdr_enum, (caddr_t)&status,
			cr, &douprintf, &status);

	PURGE_ATTRCACHE(tdvp);	/* mod time changed */
	PURGE_ATTRCACHE(svp);	/* link count changed */

	if (!error) {
		error = geterrno(status);
		if (!error) {
			if (VTOR(tdvp)->r_dir != NULL)
				nfs_purge_rddir_cache(tdvp);
		}
	}
	return (error);
}

static int
nfs_rename(vnode_t *odvp, char *onm, vnode_t *ndvp, char *nnm,
	cred_t *cr)
{
	vnode_t *realvp;

	if (VOP_REALVP(ndvp, &realvp) == 0)
		ndvp = realvp;

	return (nfsrename(odvp, onm, ndvp, nnm, cr));
}

/*
 * nfsrename does the real work of renaming in NFS Version 2.
 */
static int
nfsrename(vnode_t *odvp, char *onm, vnode_t *ndvp, char *nnm,
	cred_t *cr)
{
	int error;
	enum nfsstat status;
	struct nfsrnmargs args;
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
	error = nfslookup(ndvp, nnm, &nvp, NULL, 0, NULL, cr);
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
			error = nfslookup(odvp, onm, &ovp, NULL, 0, NULL, cr);

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
			error = nfs_link(ndvp, nvp, tmpname, cr);
			if (error == EOPNOTSUPP) {
				error = nfs_rename(ndvp, nnm, ndvp, tmpname,
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

	setdiropargs(&args.rna_from, onm, odvp);
	setdiropargs(&args.rna_to, nnm, ndvp);

	douprintf = 1;
	error = rfs2call(VTOMI(odvp), RFS_RENAME,
			xdr_rnmargs, (caddr_t)&args,
			xdr_enum, (caddr_t)&status,
			cr, &douprintf, &status);

	PURGE_ATTRCACHE(odvp);	/* mod time changed */
	PURGE_ATTRCACHE(ndvp);	/* mod time changed */

	if (!error) {
		error = geterrno(status);
		if (!error) {
			if (VTOR(odvp)->r_dir != NULL)
				nfs_purge_rddir_cache(odvp);
			if (VTOR(ndvp)->r_dir != NULL)
				nfs_purge_rddir_cache(ndvp);
		} else {
			/*
			 * System V defines rename to return EEXIST, not
			 * ENOTEMPTY if the target directory is not empty.
			 * Over the wire, the error is NFSERR_ENOTEMPTY
			 * which geterrno maps to ENOTEMPTY.
			 */
			if (error == ENOTEMPTY)
				error = EEXIST;
		}
	}

	return (error);
}

static int
nfs_mkdir(vnode_t *dvp, char *nm, struct vattr *va, vnode_t **vpp,
	cred_t *cr)
{
	int error;
	struct nfscreatargs args;
	struct nfsdiropres dr;
	int douprintf;

	setdiropargs(&args.ca_da, nm, dvp);

	/*
	 * Decide what the group-id and set-gid bit of the created directory
	 * should be.  May have to do a setattr to get the gid right.
	 */
	va->va_gid = setdirgid(dvp, cr);
	va->va_mode = setdirmode(dvp, va->va_mode);
	va->va_mask |= AT_MODE|AT_GID;

	args.ca_sa = &args.ca_sa_buf;
	vattr_to_sattr(va, args.ca_sa);

	dnlc_remove(dvp, nm);

	douprintf = 1;
	error = rfs2call(VTOMI(dvp), RFS_MKDIR,
			xdr_creatargs, (caddr_t)&args,
			xdr_diropres, (caddr_t)&dr,
			cr, &douprintf, &dr.dr_status);

	PURGE_ATTRCACHE(dvp);	/* mod time changed */

	if (!error) {
		error = geterrno(dr.dr_status);
		if (!error) {
			if (VTOR(dvp)->r_dir != NULL)
				nfs_purge_rddir_cache(dvp);
			/*
			 * The attributes returned by RFS_MKDIR can not
			 * be depended upon, so mark the attribute cache
			 * as purged.  A subsequent GETATTR will get the
			 * correct attributes from the server.
			 */
			*vpp = makenfsnode(&dr.dr_fhandle, &dr.dr_attr,
					    dvp->v_vfsp, cr);
			PURGE_ATTRCACHE(*vpp);

			/*
			 * Make sure the gid was set correctly.
			 * If not, try to set it (but don't lose
			 * any sleep over it).
			 */
			if (va->va_gid != VTOR(*vpp)->r_attr.va_gid) {
				va->va_mask = AT_GID;
				(void) nfssetattr(*vpp, va, 0, cr);
			}
		} else {
			PURGE_STALE_FH(error, dvp, cr);
		}
	}

	return (error);
}

static int
nfs_rmdir(vnode_t *dvp, char *nm, vnode_t *cdir, cred_t *cr)
{
	int error;
	enum nfsstat status;
	struct nfsdiropargs da;
	vnode_t *vp;
	int douprintf;

	/*
	 * Attempt to prevent a rmdir(".") from succeeding.
	 */
	error = nfslookup(dvp, nm, &vp, NULL, 0, NULL, cr);
	if (error)
		return (error);

	if (vp == cdir) {
		VN_RELE(vp);
		return (EINVAL);
	}

	setdiropargs(&da, nm, dvp);

	douprintf = 1;
	error = rfs2call(VTOMI(dvp), RFS_RMDIR,
			xdr_diropargs, (caddr_t)&da,
			xdr_enum, (caddr_t)&status,
			cr, &douprintf, &status);

	PURGE_ATTRCACHE(dvp);	/* mod time changed */

	if (error) {
		VN_RELE(vp);
		return (error);
	}

	error = geterrno(status);
	if (!error) {
		if (VTOR(dvp)->r_dir != NULL)
			nfs_purge_rddir_cache(dvp);
		dnlc_purge_vp(vp);
		if (VTOR(vp)->r_dir != NULL)
			nfs_purge_rddir_cache(vp);
	} else {
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
nfs_symlink(vnode_t *dvp, char *lnm, struct vattr *tva, char *tnm,
	cred_t *cr)
{
	int error;
	struct nfsslargs args;
	enum nfsstat status;
	int douprintf;

	setdiropargs(&args.sla_from, lnm, dvp);
	args.sla_sa = &args.sla_sa_buf;
	vattr_to_sattr(tva, args.sla_sa);
	args.sla_tnm = tnm;

	dnlc_remove(dvp, lnm);

	douprintf = 1;
	error = rfs2call(VTOMI(dvp), RFS_SYMLINK,
			xdr_slargs, (caddr_t)&args,
			xdr_enum, (caddr_t)&status,
			cr, &douprintf, &status);

	PURGE_ATTRCACHE(dvp);	/* mod time changed */

	if (!error) {
		error = geterrno(status);
		if (!error) {
			if (VTOR(dvp)->r_dir != NULL)
				nfs_purge_rddir_cache(dvp);
		} else {
			PURGE_STALE_FH(error, dvp, cr);
		}
	}

	return (error);
}

#ifdef DEBUG
static int nfs_readdir_cache_hits = 0;
static int nfs_readdir_cache_shorts = 0;
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
nfs_readdir(vnode_t *vp, struct uio *uiop, cred_t *cr, int *eofp)
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
		error = nfs_validate_caches(vp, cr);
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
	    uiop->uio_offset == rp->r_direof->nfs_ncookie) {
		mutex_exit(&rp->r_statelock);
#ifdef DEBUG
		nfs_readdir_cache_shorts++;
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
		 * To NFS, the cookie is an opaque 4 byte entity.  To
		 * the rest of the system, the cookie is really an
		 * offset.  Thus, NFS stores the cookie in off_t
		 * sized elements and compares them to off_t offsets.
		 * This is valid as long as the client makes no other
		 * assumptions about the values of cookies.  The only
		 * valid tests are equal and not equal.
		 */
		if (rdc->nfs_cookie == uiop->uio_offset &&
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
			nfs_readdir_cache_hits++;
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
			 * contain the value of the next NFS cookie
			 * and set the eof value appropriately.
			 */
			if (!error) {
				uiop->uio_offset = rdc->nfs_ncookie;
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
				if (rrdc->nfs_cookie == rdc->nfs_ncookie &&
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
					rrdc->nfs_cookie = rdc->nfs_ncookie;
					rrdc->buflen = count;
					rrdc->flags = RDDIR;
					cv_init(&rrdc->cv, "rddir_cache cv",
						CV_DEFAULT, NULL);
					rrdc->next = rp->r_dir;
					rp->r_dir = rrdc;
					mutex_exit(&rp->r_statelock);
					nfs_async_readdir(vp, rrdc, cr,
							nfsreaddir);
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
		nrdc->nfs_cookie = uiop->uio_offset;
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
	 * Do the readdir.
	 */
	error = nfsreaddir(vp, nrdc, cr);

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

static int nfs_shrinkreaddir = 0;

static int
nfsreaddir(vnode_t *vp, rddir_cache *rdc, cred_t *cr)
{
	int error;
	struct nfsrddirargs rda;
	struct nfsrddirres rd;
	rnode_t *rp;
	mntinfo_t *mi;
	u_int count;
	int douprintf;

	count = rdc->buflen;

	rp = VTOR(vp);
	mi = VTOMI(vp);

	/*
	 * UGLINESS: SunOS 3.2 servers apparently cannot always handle an
	 * RFS_READDIR request with rda_count set to more than 0x400. So
	 * we reduce the request size here purely for compatibility.
	 *
	 * In general, this is no longer required.  However, if a server
	 * is discovered which can not handle requests larger than 1024,
	 * nfs_shrinkreaddir can be set to 1 to enable this backwards
	 * compatibility.
	 */
	if (nfs_shrinkreaddir && count > 0x400)
		count = 0x400;

	rda.rda_fh = *VTOFH(vp);
	rda.rda_offset = rdc->nfs_cookie;

	rd.rd_entries = (struct dirent *)kmem_alloc(rdc->buflen, KM_SLEEP);
	rd.rd_size = count;
	rd.rd_offset = rda.rda_offset;

	douprintf = 1;
	do {
		rda.rda_count = MIN(count, mi->mi_curread);
		error = rfs2call(mi, RFS_READDIR,
				xdr_rddirargs, (caddr_t)&rda,
				xdr_getrddirres, (caddr_t)&rd,
				cr, &douprintf, &rd.rd_status);
	} while (error == ENFS_TRYAGAIN);

	/*
	 * Since we are actually doing a READDIR RPC, we must have
	 * exclusive access to the cache entry being filled.  Thus,
	 * it is safe to update all fields except for the flags
	 * field.  The r_statelock in the rnode must be held to
	 * prevent two different threads from simultaneously
	 * attempting to update the flags field.  This can happen
	 * if we are turning off RDDIR and the other thread is
	 * trying to set RDDIRWAIT.
	 */
	ASSERT(rdc->flags & RDDIR);
	if (!error) {
		error = geterrno(rd.rd_status);
		if (!error) {
			rdc->nfs_ncookie = rd.rd_offset;
			rdc->entries = (char *)rd.rd_entries;
			if (rd.rd_eof) {
				rdc->eof = 1;
				rp->r_direof = rdc;
			} else
				rdc->eof = 0;
			rdc->entlen = rd.rd_size;
			rdc->error = 0;
		} else {
			PURGE_STALE_FH(error, vp, cr);
		}
	}
	if (error) {
		kmem_free((caddr_t)rd.rd_entries, rdc->buflen);
		rdc->entries = NULL;
		rdc->error = error;
	}

	mutex_enter(&rp->r_statelock);
	rdc->flags &= ~RDDIR;
	if (rdc->flags & RDDIRWAIT) {
		rdc->flags &= ~RDDIRWAIT;
		cv_broadcast(&rdc->cv);
	}
	if (error)
		rdc->flags |= RDDIRREQ;
	mutex_exit(&rp->r_statelock);

	return (error);
}

#ifdef DEBUG
static int nfs_bio_do_stop = 0;
#endif

static int
nfs_bio(struct buf *bp, cred_t *cr)
{
	register rnode_t *rp = VTOR(bp->b_vp);
	struct vattr va;
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
		error = bp->b_error = nfsread(bp->b_vp, bp->b_un.b_addr,
					offset, (long)bp->b_bcount,
					(long *)&bp->b_resid, cred, &va);
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
				cmn_err(CE_PANIC, "nfs_bio: write count < 0");
#ifdef DEBUG
			if (count == 0) {
				cmn_err(CE_WARN,
					"nfs_bio: zero length write at %d",
					offset);
				nfs_printfhandle(&VTOR(bp->b_vp)->r_fh);
				if (nfs_bio_do_stop)
					debug_enter("nfs_bio");
			}
#endif
			error = nfswrite(bp->b_vp, bp->b_un.b_addr, offset,
					count, cred);
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
		mutex_enter(&rp->r_statelock);
		seq = rp->r_seq;
		if (!CACHE_VALID(rp, va.va_mtime, va.va_size)) {
			mutex_exit(&rp->r_statelock);
			/*
			 * read, if cache is not valid mark this bp
			 * with an error so it will be freed by pvn_done
			 * and return a special error, NFS_CACHEINVALERR,
			 * so caller can flush caches and re-do the operation.
			 */
			error = NFS_CACHEINVALERR;
			bp->b_error = EINVAL;
		} else
			mutex_exit(&rp->r_statelock);
		/*
		 * If we detect that the cache is invalid and this is an
		 * async I/O, don't update the rnode cached attrs, instead
		 * mark the attrs as timed out. This will force the cache
		 * to be purged the next time a cache check is done.
		 * Note that this is done only for the async case. This is
		 * because the async threads can't purge the cache because of
		 * deadlock possibilities.
		 * For the sync read case, it is correct to update the rnode
		 * attrs here, because nfs_getpage() will purge the cache
		 * upon getting NFS_CACHEINVALERR.
		 */
		if (error == NFS_CACHEINVALERR && (bp->b_flags & B_ASYNC)) {
			PURGE_ATTRCACHE(RTOV(rp));
		} else {
			nfs_attrcache_va(RTOV(rp), &va, seq);
			/*
			 * If ACL is supported on a server, then attribute
			 * returned by server will have minimal permissions
			 * denying sometimes access to users having proper
			 * ACL set up for them. So in this case, always force
			 * to get the attributes by NFS_ACL GETATTR2. We have
			 * to pay this performance penalty to buy ACL support.
			 */
			if (VTOMI(RTOV(rp))->mi_flags & MI_ACL)
				PURGE_ATTRCACHE(RTOV(rp));
		}
	}

	if (error != 0 && error != NFS_EOF)
		bp->b_flags |= B_ERROR;

	return (error);
}

static int
nfs_fid(vnode_t *vp, fid_t *fidp)
{
	register struct nfs_fid *fp;
	register rnode_t *rp = VTOR(vp);

	if (fidp->fid_len < (sizeof (struct nfs_fid) - sizeof (short))) {
		fidp->fid_len = sizeof (struct nfs_fid) - sizeof (short);
		return (ENOSPC);
	}
	fp = (struct nfs_fid *)fidp;
	fp->nf_pad = 0;
	fp->nf_len = sizeof (struct nfs_fid) - sizeof (short);
	bcopy(rp->r_fh.fh_buf, fp->nf_data, NFS_FHSIZE);
	return (0);
}

static void
nfs_rwlock(vnode_t *vp, int write_lock)
{
	rnode_t *rp = VTOR(vp);

	if (write_lock)
		rw_enter(&rp->r_rwlock, RW_WRITER);
	else
		rw_enter(&rp->r_rwlock, RW_READER);
}

/* ARGSUSED */
static void
nfs_rwunlock(vnode_t *vp, int write_lock)
{
	rnode_t *rp = VTOR(vp);

	rw_exit(&rp->r_rwlock);
}

/* ARGSUSED */
static int
nfs_seek(vnode_t *vp, offset_t ooff, offset_t *noffp)
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

static int nfs_nra = 1;	/* number of pages to read ahead */
#ifdef DEBUG
static int nfs_lostpage = 0;	/* number of times we lost original page */
#endif

/*
 * Return all the pages from [off..off+len) in file
 */
static int
nfs_getpage(vnode_t *vp, offset_t off, u_int len, u_int *protp,
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
	(void) nfs_validate_caches(vp, cr);

retry:
	/*
	 * If we are getting called as a side effect of an nfs_write()
	 * operation the local file size might not be extended yet.
	 * In this case we want to be able to return pages of zeroes.
	 */
	if ((u_int)off + len > rp->r_size + PAGEOFFSET && seg != segkmap)
		return (EFAULT);		/* beyond EOF */

	if (len <= PAGESIZE) {
		error = nfs_getapage(vp, (u_int)off, len, protp, pl, plsz,
		    seg, addr, rw, cr);
	} else {
		error = pvn_getpages(nfs_getapage, vp, (u_int)off, len, protp,
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
 * Called from pvn_getpages or nfs_getpage to get a particular page.
 */
/* ARGSUSED */
static int
nfs_getapage(vnode_t *vp, u_int off, u_int len, u_int *protp,
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
		readahead = nfs_nra;
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
					    seg, cr, nfs_readahead);
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
			readahead = nfs_nra;
		else
			readahead = 0;
		rablkoff = blkoff;
		while (readahead > 0 && rablkoff + bsize < rp->r_size) {
			nfs_async_readahead(vp, rablkoff + bsize,
					    addr + (rablkoff + bsize - off),
					    seg, cr, nfs_readahead);
			readahead--;
			rablkoff += bsize;
		}
	}
#endif

again:
	if ((pagefound = page_exists(vp, off)) == 0) {
		if (pl == NULL) {
			nfs_async_readahead(vp, blkoff, addr, seg, cr,
					    nfs_readahead);
		} else if (rw == S_CREATE) {
			/*
			 * Block for this page is not allocated, or the offset
			 * is beyond the current allocation size, or we're
			 * allocating a swap slot and the page was not found,
			 * so allocate it and return a zero page.
			 */
			if ((pp = page_create_va(vp, off,
			    PAGESIZE, PG_WAIT, seg->s_as, addr)) == NULL)
				cmn_err(CE_PANIC, "nfs_getapage: page_create");
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
				error = nfs_bio(bp, cr);

			/*
			 * Unmap the buffer before freeing it.
			 */
			bp_mapout(bp);
			pageio_done(bp);

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
			nfs_lostpage++;
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
nfs_readahead(vnode_t *vp, u_int blkoff, caddr_t addr, struct seg *seg,
	cred_t *cr)
{
	int error;
	page_t *pp;
	u_int io_off, io_len;
	struct buf *bp;
	register u_int bsize, blksize;
	register rnode_t *rp = VTOR(vp);

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
		error = nfs_bio(bp, cr);
		if (error == NFS_EOF)
			error = 0;
	}

	/*
	 * Unmap the buffer before freeing it.
	 */
	bp_mapout(bp);
	pageio_done(bp);

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
nfs_putpage(vnode_t *vp, offset_t off, u_int len, int flags, cred_t *cr)
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
nfs_putapage(vnode_t *vp, page_t *pp, u_int *offp, u_int *lenp,
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
				 * And V2 does the same thing for symmetry.
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
					    nfs_sync_putapage);
	} else
		error = nfs_sync_putapage(vp, pp, io_off, io_len, flags, cr);

	if (offp)
		*offp = io_off;
	if (lenp)
		*lenp = io_len;
	return (error);
}

static int
nfs_sync_putapage(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr)
{
	int error;
	rnode_t *rp;

	flags |= B_WRITE;

	error = nfs_rdwrlbn(vp, pp, io_off, io_len, flags, cr);

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
		error = nfs_putpage(vp, (offset_t)io_off, io_len,
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
nfs_map(vnode_t *vp, offset_t off, struct as *as, caddr_t *addrp,
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
nfs_addmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
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
nfs_cmp(vnode_t *vp1, vnode_t *vp2)
{

	return (vp1 == vp2);
}

static int
nfs_frlock(vnode_t *vp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr)
{
	netobj lm_fh;
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
		error = nfs_putpage(vp, (offset_t)0, 0, B_INVAL, cr);
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

	lm_fh.n_len = sizeof (fhandle_t);
	lm_fh.n_bytes = (char *) VTOFH(vp);

	/*
	 * Call the lock manager to do the real work of contacting
	 * the server and obtaining the lock.
	 */
	rc = lm_frlock(vp, cmd, bfp, flag, offset, cr, &lm_fh);

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
nfs_space(vnode_t *vp, int cmd, struct flock *bfp, int flag,
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
			error = nfssetattr(vp, &va, 0, cr);
		} else
			error = EINVAL;
	}

	return (error);
}

/* ARGSUSED */
static int
nfs_realvp(vnode_t *vp, vnode_t **vpp)
{

	return (EINVAL);
}

/*
 * Remove some pages from an mmap'd vnode.  Just update the
 * count of pages.  If doing close-to-open, then flush all
 * of the pages associated with this file.  Otherwise, just
 * mark the rnode with RDIRTY to indicate that a pass through
 * the page list is required before invalidating the pages.
 */
/* ARGSUSED */
static int
nfs_delmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
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
			error = nfs_putpage(vp, (offset_t)0, (u_int)0, 0, cr);
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
			 * This will hopefully force nfs_inactive to be
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

/* ARGSUSED */
static int
nfs_pathconf(vnode_t *vp, int cmd, u_long *valp, cred_t *cr)
{
	register int error = 0;

	/*
	 * This looks a little weird because it's written in a general
	 * manner but we make use of only one case.  If cntl() ever gets
	 * widely used, the outer switch will make more sense.
	 */

	switch (cmd) {

	case _PC_LINK_MAX:
	case _PC_NAME_MAX:
	case _PC_PATH_MAX:
	case _PC_CHOWN_RESTRICTED:
	case _PC_NO_TRUNC: {
		mntinfo_t *mi;
		struct pathcnf *pc;

		if ((mi = VTOMI(vp)) == NULL || (pc = mi->mi_pathconf) == NULL)
			return (EINVAL);
		error = _PC_ISSET(cmd, pc->pc_mask);    /* error or bool */
		switch (cmd) {
		case _PC_LINK_MAX:
			*valp = pc->pc_link_max;
			break;
		case _PC_NAME_MAX:
			*valp = pc->pc_name_max;
			break;
		case _PC_PATH_MAX:
			*valp = pc->pc_path_max;
			break;
		case _PC_CHOWN_RESTRICTED:
			/*
			 * if we got here, error is really a boolean which
			 * indicates whether cmd is set or not.
			 */
			*valp = error ? 1 : 0;	/* see above */
			error = 0;
			break;
		case _PC_NO_TRUNC:
			/*
			 * if we got here, error is really a boolean which
			 * indicates whether cmd is set or not.
			 */
			*valp = error ? 1 : 0;	/* see above */
			error = 0;
			break;
		}
		return (error ? EINVAL : 0);
	    }
	default:
		return (EINVAL);
	}
}

/*
 * Called by async thread to do synchronous pageio. Do the i/o, wait
 * for it to complete, and cleanup the page list when done.
 */
static int
nfs_sync_pageio(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr)
{
	int error;

	error = nfs_rdwrlbn(vp, pp, io_off, io_len, flags, cr);
	if (flags & B_READ)
		pvn_read_done(pp, (error ? B_ERROR : 0) | flags);
	else
		pvn_write_done(pp, (error ? B_ERROR : 0) | flags);
	return (error);
}

static int
nfs_pageio(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
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
					nfs_sync_pageio);
	} else
		error = nfs_rdwrlbn(vp, pp, io_off, io_len, flags, cr);
	mutex_enter(&rp->r_statelock);
	rp->r_count--;
	cv_signal(&rp->r_cv);
	mutex_exit(&rp->r_statelock);
	return (error);
}

static int
nfs_setsecattr(vnode_t *vp, vsecattr_t *vsecattr, int flag, cred_t *cr)
{
	int error;
	mntinfo_t *mi;

	mi = VTOMI(vp);

	if (mi->mi_flags & MI_ACL) {
		error = acl_setacl2(vp, vsecattr, flag, cr);
		if (mi->mi_flags & MI_ACL)
			return (error);
	}

	return (ENOSYS);
}

static int
nfs_getsecattr(vnode_t *vp, vsecattr_t *vsecattr, int flag, cred_t *cr)
{
	int error;
	mntinfo_t *mi;

	mi = VTOMI(vp);

	if (mi->mi_flags & MI_ACL) {
		error = acl_getacl2(vp, vsecattr, flag, cr);
		if (mi->mi_flags & MI_ACL)
			return (error);
	}

	return (fs_fab_acl(vp, vsecattr, flag, cr));
}
