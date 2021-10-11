/*
 * Copyright (c) 1992,1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_vnops.c 1.181     95/08/28 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/mman.h>
#include <sys/tiuser.h>
#include <sys/pathname.h>
#include <sys/dirent.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/vmsystm.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/swap.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/disp.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/vtrace.h>
#include <sys/mount.h>
#include <sys/bootconf.h>
#include <sys/dnlc.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <sys/fs/cachefs_fs.h>
#include <sys/fs/cachefs_log.h>
#include "fs/fs_subr.h"

extern	int		basyncnt;
extern kmutex_t cachefs_cnode_freelist_lock;

static	int	cachefs_open(register struct vnode **, int, cred_t *);
static	int	cachefs_close(struct vnode *, int, int, offset_t, cred_t *);
static	int	cachefs_read(struct vnode *, struct uio *, int, cred_t *);
static	int	cachefs_write(struct vnode *, struct uio *, int, cred_t *);
static	int	cachefs_ioctl(struct vnode *, int, int, int, cred_t *, int *);
static	int	cachefs_getattr(struct vnode *, struct vattr *, int, cred_t *);
static	int	cachefs_setattr(register struct vnode *, struct vattr *,
			int, cred_t *);
static	int	cachefs_access(struct vnode *, int, int, cred_t *);
static	int	cachefs_lookup(struct vnode *, char *, struct vnode **,
			struct pathname *, int, struct vnode *, cred_t *);
static	int	cachefs_create(struct vnode *, char *, struct vattr *,
			enum vcexcl, int, struct vnode **, cred_t *);
static	int	cachefs_remove(struct vnode *, char *, cred_t *);
static	int	cachefs_link(struct vnode *, struct vnode *, char *, cred_t *);
static	int	cachefs_rename(struct vnode *, char *, struct vnode *,
			char *, cred_t *);
static	int	cachefs_mkdir(struct vnode *, char *, register struct vattr *,
			struct vnode **, cred_t *);
static	int	cachefs_rmdir(struct vnode *, char *, struct vnode *, cred_t *);
static	int	cachefs_readdir(struct vnode *, register struct uio *,
			cred_t *, int *);
static	int	cachefs_symlink(struct vnode *, char *, struct vattr *, char *,
			cred_t *);
static	int	cachefs_readlink(struct vnode *, struct uio *, cred_t *);
	int	cachefs_fsync(struct vnode *, int, cred_t *);
static	void	cachefs_inactive_queue(register struct vnode *, cred_t *);
static	int	cachefs_fid(struct vnode *, struct fid *);
static	void	cachefs_rwlock(struct vnode *, int);
static	void	cachefs_rwunlock(struct vnode *, int);
static	int	cachefs_seek(struct vnode *, offset_t, offset_t *);
static	int	cachefs_cmp(struct vnode *, struct vnode *);
static	int	cachefs_frlock(struct vnode *, int, struct flock *,
			int, offset_t, cred_t *);
static	int	cachefs_space(struct vnode *, int, struct flock *, int,
			offset_t, cred_t *);
static	int	cachefs_realvp(struct vnode *, struct vnode **);
static	int	cachefs_getpage(struct vnode *, offset_t, u_int, u_int *,
			struct page *[], u_int, struct seg *, caddr_t,
			enum seg_rw, cred_t *);
static	int	cachefs_getapage(struct vnode *, u_int, u_int, u_int *,
			struct page *[], u_int, struct seg *, caddr_t,
			enum seg_rw, cred_t *);
static	int	cachefs_putpage(struct vnode *, offset_t, u_int, int, cred_t *);
	int	cachefs_putapage(struct vnode *, page_t *, u_int *, u_int *,
			int, cred_t *);
	void	cachefs_async_stop(register struct vfs *);
static	int	cachefs_map(struct vnode *, offset_t, struct as *, caddr_t *,
			u_int, u_char, u_char, u_int, cred_t *);
static	int	cachefs_addmap(struct vnode *, offset_t, struct as *, caddr_t,
			u_int, u_char, u_char, u_int, cred_t *);
static	int	cachefs_delmap(struct vnode *, offset_t, struct as *, caddr_t,
			u_int, u_int, u_int, u_int, cred_t *);

static int	cachefs_dump(struct vnode *, caddr_t, int, int);
static int	cachefs_pageio(struct vnode *, page_t *,
		    u_int, u_int, int, cred_t *);
static int	cachefs_setsecattr(vnode_t *, vsecattr_t *, int, cred_t *);
static int	cachefs_getsecattr(vnode_t *, vsecattr_t *, int, cred_t *);
static int	cachefs_writepage(struct vnode *vp, caddr_t base,
		    int tcount, struct uio *uiop);
static int	cachefs_pin(struct vnode *vp, cred_t *cr);
static int	cachefs_pin_locked(struct vnode *vp, cred_t *cr);
static int	cachefs_unpin(struct vnode *vp, cred_t *cr);
static int	cachefs_convert_mount(struct fscache *fscp,
    struct cachefs_cnvt_mnt *ccmp);

struct vnodeops cachefs_vnodeops = {
	cachefs_open,
	cachefs_close,
	cachefs_read,
	cachefs_write,
	cachefs_ioctl,
	fs_setfl,
	cachefs_getattr,
	cachefs_setattr,
	cachefs_access,
	cachefs_lookup,
	cachefs_create,
	cachefs_remove,
	cachefs_link,
	cachefs_rename,
	cachefs_mkdir,
	cachefs_rmdir,
	cachefs_readdir,
	cachefs_symlink,
	cachefs_readlink,
	cachefs_fsync,
	cachefs_inactive_queue,
	cachefs_fid,
	cachefs_rwlock,
	cachefs_rwunlock,
	cachefs_seek,
	cachefs_cmp,
	cachefs_frlock,
	cachefs_space,
	cachefs_realvp,
	cachefs_getpage,
	cachefs_putpage,
	cachefs_map,
	cachefs_addmap,
	cachefs_delmap,
	fs_poll,
	cachefs_dump,
	fs_pathconf,
	cachefs_pageio,
	fs_nosys,
	fs_dispose,
	cachefs_setsecattr,
	cachefs_getsecattr
};

/* forward declarations of pure statics */
static int cachefs_readdir_back(struct cnode *, struct uio *, cred_t *, int *);

struct vnodeops *
cachefs_getvnodeops(void)
{
	return (&cachefs_vnodeops);
}

static int
cachefs_open(register struct vnode **vpp, int flag, cred_t *cr)
{
	int error = 0;
	cnode_t *cp = VTOC(*vpp);
	fscache_t *fscp = C_TO_FSCACHE(cp);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_open: ENTER vpp %x flag %x \n",
		    (int) vpp, flag);
#endif

	rw_enter(&cp->c_statelock, RW_WRITER);
	if ((flag & FWRITE) &&
	    ((*vpp)->v_type == VDIR || (*vpp)->v_type == VLNK)) {
		error = EISDIR;
		goto out;
	}

	if ((cp->c_backvp == NULL) && (flag & FWRITE)) {
		error = cachefs_getbackvp(fscp,
		    &cp->c_metadata.md_cookie, cp);
		if (error)
			goto out;
	}
	if ((flag & FWRITE) && (C_ISFS_WRITE_AROUND(fscp)))
		cachefs_nocache(cp);

	/*
	 * note: open() -> vn_open() should call VOP_ACCESS() or
	 * VOP_CREATE() before we get here.  in cachefs_access(), we
	 * get the backvp unless local_access is turned on.
	 */

	if (cp->c_backvp != NULL) {
		error = VOP_OPEN(&cp->c_backvp, flag, cr);
		if (error)
			goto out;
	}
	/*
	 * Necessary to check for consistency here ?
	 */
	error = CFSOP_CHECK_COBJECT(fscp, cp, C_BACK_CHECK, RW_WRITER, cr);
out:
	rw_exit(&cp->c_statelock);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_open: EXIT vpp %x error %d \n",
		    (int) vpp, error);
#endif
	return (error);
}

/* ARGSUSED */
static int
cachefs_close(struct vnode *vp, int flag, int count, offset_t offset,
	cred_t *cr)
{
	int error = 0;
	cnode_t *cp = VTOC(vp);
	struct flock ld;
	vnode_t *backvp = NULL;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_close: ENTER vp %x \n", (int) vp);
#endif

	rw_enter(&cp->c_statelock, RW_READER);
	backvp = cp->c_backvp;
	if (backvp != NULL)
		VN_HOLD(backvp);
	rw_exit(&cp->c_statelock);

	if (MANDLOCK(vp, cp->c_attr.va_mode)) {
		/*
		 * release all file/record locks.
		 */
		if (vp->v_filocks) {
			ld.l_type = F_UNLCK;
			ld.l_whence = 0;
			ld.l_start = 0;
			ld.l_len = 0;
			fs_frlock(vp, F_SETLK, &ld, flag, offset, cr);
		}
		cleanlocks(vp, ttoproc(curthread)->p_pid, 0);
	}

	if (count > 1) {
		if (backvp != NULL)
			error = VOP_CLOSE(backvp, flag, count,
			    offset, cr);
		goto out;
	}

	if (vp->v_type == VREG && backvp) {
		error = VOP_FSYNC(vp, FSYNC, cr);
		if (cp->c_error) {
			(void) cachefs_putpage(vp, (offset_t) 0, 0,
				B_INVAL | B_FORCE, cr);
			(void) VOP_CLOSE(backvp, flag, count, offset, cr);
#ifdef CFSDEBUG
			if (vp->v_pages) {
				printf("cachefs_close: %x has pages\n",
				    (int) cp);
			}
#endif
			dnlc_purge_vp(vp);
			rw_enter(&cp->c_statelock, RW_WRITER);
			error = cp->c_error;
			cp->c_error = 0;
			rw_exit(&cp->c_statelock);
		} else {
			(void) VOP_CLOSE(backvp, flag, count, offset, cr);
		}
	}

out:
	if (backvp != NULL)
		VN_RELE(backvp);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_close: EXIT vp %x \n", (int) vp);
#endif
	return (error);
}

/*ARGSUSED*/
static int
cachefs_read(struct vnode *vp, struct uio *uiop, int ioflag, cred_t *cr)
{
	struct cnode *cp = VTOC(vp);
	register u_int off;
	register int mapoff;
	register caddr_t base;
	int n;
	u_int flags = 0;
	int error = 0;
	int type = RW_READER;
	fscache_t *fscp = C_TO_FSCACHE(cp);

	/*
	 * Do a quick check for easy cases (i.e. EOF, zero count)
	 */
	if (vp->v_type != VREG)
		return (EISDIR);
	if (uiop->uio_offset < 0)
		return (EINVAL);
	if ((uiop->uio_resid == 0) || (uiop->uio_offset >= cp->c_size))
		return (0);
	if (MANDLOCK(vp, cp->c_attr.va_mode)) {
		error = chklock(vp, FREAD, uiop->uio_offset,
			    uiop->uio_resid, uiop->uio_fmode);
		if (error)
			return (error);
	}
	for (;;) {
		rw_enter(&cp->c_statelock, type);
		error = CFSOP_CHECK_COBJECT(fscp, cp, C_VERIFY_ATTRS, type, cr);
		if (error != EAGAIN)
			break;
		rw_exit(&cp->c_statelock);
		type = RW_WRITER;
	}
	rw_exit(&cp->c_statelock);
	if (error)
		return (error);
	/*
	 * Sit in a loop and transfer (uiomove) the data in up to
	 * MAXBSIZE chunks. Each chunk is mapped into the kernel's
	 * address space as needed and then released.
	 */
	do {
		/*
		*	off	Offset of current MAXBSIZE chunk
		*	mapoff	Offset within the current chunk
		*	n	Number of bytes to move from this chunk
		*	base	kernel address of mapped in chunk
		*/
		off = uiop->uio_offset & MAXBMASK;
		mapoff = uiop->uio_offset & MAXBOFFSET;
		n = MIN(MAXBSIZE - mapoff, uiop->uio_resid);
		n = MIN((cp->c_size - uiop->uio_offset), n);
		if (n == 0)
			break;
		base = segmap_getmap(segkmap, vp, off);
		error = segmap_fault(kas.a_hat, segkmap, base, n,
			F_SOFTLOCK, S_READ);
		if (error) {
			(void) segmap_release(segkmap, base, 0);
			if (FC_CODE(error) == FC_OBJERR)
				error =  FC_ERRNO(error);
			else
				error = EIO;
			break;
		}
		error = uiomove(base+mapoff, n, UIO_READ, uiop);
		(void) segmap_fault(kas.a_hat, segkmap, base, n,
				F_SOFTUNLOCK, S_READ);
		if (error == 0) {
		/*
		* if we read a whole page(s), or to eof,
		*  we won't need this page(s) again soon.
		*/
			if (n + mapoff == MAXBSIZE ||
				uiop->uio_offset == cp->c_size)
				flags |= SM_DONTNEED;
		}
		(void) segmap_release(segkmap, base, flags);
	} while (error == 0 && uiop->uio_resid > 0);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_read: EXIT error %d resid %d \n", error,
			uiop->uio_resid);
#endif
	return (error);
}

/*ARGSUSED*/
static int
cachefs_write(struct vnode *vp, struct uio *uiop, int ioflag, cred_t *cr)
{
	struct cnode *cp = VTOC(vp);
	int error = 0;
	u_int off;
	caddr_t base;
	u_int flags;
	int n, on;
	fscache_t *fscp = C_TO_FSCACHE(cp);
	rlim_t limit = uiop->uio_limit;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf(
		"cachefs_write: ENTER vp %x offset %lu count %d cflags %x\n",
			(int) vp, uiop->uio_offset, uiop->uio_resid,
			cp->c_flags);
#endif

	ASSERT(RW_WRITE_HELD(&cp->c_rwlock));

	if (vp->v_type != VREG) {
		error = EISDIR;
		goto out;
	}
	if (MANDLOCK(vp, cp->c_attr.va_mode)) {
		error = chklock(vp, FWRITE, uiop->uio_offset,
			    uiop->uio_resid, uiop->uio_fmode);
		if (error)
			return (error);
	}
	if (uiop->uio_resid == 0) {
		goto out;
	}

	ASSERT(RW_WRITE_HELD(&cp->c_rwlock));
	rw_enter(&cp->c_statelock, RW_WRITER);
	if (cp->c_cred != NULL)
		crfree(cp->c_cred);
	cp->c_cred = cr;
	crhold(cr);
	error = cachefs_initstate(cp, RW_WRITER, 1, cr);
	if (error) {
		rw_exit(&cp->c_statelock);
		goto out;
	}
	CFSOP_CHECK_COBJECT(fscp, cp, C_VERIFY_ATTRS, RW_WRITER, cr);
	if (ioflag & FAPPEND)
		uiop->uio_offset = cp->c_size;
	rw_exit(&cp->c_statelock);
	do {
		off = uiop->uio_offset & MAXBMASK;  /* mapping offset */
		on = uiop->uio_offset & MAXBOFFSET; /* Relative offset */
		n = MIN(MAXBSIZE - on, uiop->uio_resid);

		if (vp->v_type == VREG && ((uiop->uio_offset + n) >= limit)) {
			if (uiop->uio_offset >= limit) {
				psignal(ttoproc(curthread), SIGXFSZ);
				error = EFBIG;
				goto out;
			}
			n = limit - uiop->uio_offset;
		}

		base = segmap_getmap(segkmap, vp, off);
		error = cachefs_writepage(vp, (base + on), n, uiop);
		if (error == 0) {
			flags = 0;
			/*
			* Have written a whole block.Start an
			* asynchronous write and mark the buffer to
			* indicate that it won't be needed again
			* soon.
			*/
			if (n + on == MAXBSIZE) {
				flags = SM_WRITE |SM_ASYNC |SM_DONTNEED;
			}
			if ((ioflag & (FSYNC|FDSYNC)) ||
				(cp->c_backvp && cp->c_backvp->v_filocks)) {
				flags &= ~SM_ASYNC;
				flags |= SM_WRITE;
			}
			error = segmap_release(segkmap, base, flags);
		} else {
			(void) segmap_release(segkmap, base, 0);
		}
	} while (error == 0 && uiop->uio_resid > 0);


out:
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_write: EXIT error %d\n", error);
#endif
	return (error);
}

/*
 * see if we've charged ourselves for frontfile data at
 * the given offset.  If not, allocate a block for it now.
 */
static int
cachefs_charge_page(struct cnode *cp, u_int offset)
{
	u_int blockoff;
	int error;
	int inc;

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	ASSERT(PAGESIZE <= MAXBSIZE);

	error = 0;
	blockoff = offset & MAXBMASK;

	if (cachefs_check_allocmap(cp, blockoff))
		return (0);

	for (inc = PAGESIZE; inc < MAXBSIZE; inc += PAGESIZE)
		if (cachefs_check_allocmap(cp, blockoff+inc))
			return (0);

	error = cachefs_allocblocks(C_TO_FSCACHE(cp)->fs_cache, 1, kcred);
	if (error == 0) {
		cp->c_metadata.md_frontblks++;
		cp->c_flags |= CN_UPDATED;
	} else {
		cachefs_nocache(cp);
	}

	return (error);
}

static int
cachefs_writepage(struct vnode *vp, caddr_t base, int tcount, struct uio *uiop)
/* base   - base address kernel addr space */
/* tcount - Total bytes to move - < MAXBSIZE */
{
	struct cnode *cp =  VTOC(vp);
	register int n;
	register int offset;
	int error = 0;
	extern struct as kas;
	int lastpage_off;
	int pagecreate = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf(
		    "cachefs_writepage: ENTER vp %x offset %lu len %d\\\n",
		    (int) vp, uiop->uio_offset, uiop->uio_resid);
#endif

	/*
	 * Move bytes in PAGESIZE chunks. We must avoid spanning pages in
	 * uiomove() because page faults may cause the cache to be invalidated
	 * out from under us.
	 */

	do {
		offset = uiop->uio_offset;
		lastpage_off = cp->c_size & PAGEMASK;

		/*
		* n is the number of bytes required to satisfy the request
		* or the number of bytes to fill out the page.
		*/
		n = MIN((PAGESIZE - ((u_int)base & PAGEOFFSET)), tcount);

		/*
		* Check to see if we can skip reading in the page
		* and just allocate the memory.  We can do this
		* if we are going to rewrite the entire mapping
		* or if we are going to write to or beyond the current
		* end of file from the beginning of the mapping.
		*/
		if ((offset > (lastpage_off + PAGEOFFSET)) ||
			((cp->c_size == 0) && (offset < PAGESIZE)) ||
			((u_int)base & PAGEOFFSET) == 0 && (n == PAGESIZE ||
			((offset + n) >= cp->c_size))) {
			pagecreate = 1;
			segmap_pagecreate(segkmap,
				(caddr_t)((u_int)base & PAGEMASK), PAGESIZE, 0);
			(void) kzero((caddr_t)((u_int)base & PAGEMASK),
					(u_int)PAGESIZE);
			error = uiomove(base, n, UIO_WRITE, uiop);
		} else {
			/*
			* KLUDGE ! Use segmap_fault instead of faulting and
			* using as_fault() to avoid a recursive readers lock
			* on kas.
			*/
			error = segmap_fault(kas.a_hat, segkmap,
				(caddr_t)((u_int)base & PAGEMASK),
				PAGESIZE, F_SOFTLOCK, S_WRITE);
			if (error) {
				if (FC_CODE(error) == FC_OBJERR)
					error =  FC_ERRNO(error);
				else
					error = EIO;
				break;
			}
			error = uiomove(base, n, UIO_WRITE, uiop);
			(void) segmap_fault(kas.a_hat, segkmap,
					(caddr_t)((u_int)base & PAGEMASK),
					PAGESIZE, F_SOFTUNLOCK, S_WRITE);
		}
		n = uiop->uio_offset - offset; /* n = # of bytes written */
		base += n;
		tcount -= n;
		/*
		* cp->c_attr.va_size is the maximum number of
		* bytes known to be in the file.
		* Make sure it is at least as high as the
		* last byte we just wrote into the buffer.
		*/
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (cp->c_size < uiop->uio_offset) {
			cp->c_size = uiop->uio_offset;
		}
		if (cp->c_size != cp->c_attr.va_size) {
			cp->c_attr.va_size = cp->c_size;
			cp->c_flags |= CN_UPDATED;
		}
		if (error == 0) {
			cp->c_flags |= CDIRTY;
			if (pagecreate && (cp->c_flags & CN_NOCACHE) == 0) {
				/*
				 * if we're not in NOCACHE mode
				 * (i.e., single-writer), we update the
				 * allocmap here rather than waiting until
				 * cachefspush is called.  This prevents
				 * getpage from clustering up pages from
				 * the backfile and stomping over the changes
				 * we make here.
				 */
				if (cachefs_charge_page(cp, offset) == 0) {
					cachefs_update_allocmap(cp,
					    offset&PAGEMASK, PAGESIZE);
				}
			}
		}
		rw_exit(&cp->c_statelock);
	} while (tcount > 0 && error == 0);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_writepage: EXIT error %d\n", error);
#endif

	return (error);
}

cachefspush(struct vnode *vp, struct page *pp, u_int *offp, u_int *lenp,
	u_int flags, cred_t *cr)
{
	struct cnode *cp = VTOC(vp);
	struct buf *bp;
	int err = 0;
	fscache_t *fscp = C_TO_FSCACHE(cp);
	u_int iooff, iolen;
	u_int lbn, lbn_off;
	u_int bsize;
	int blocks = 0;
	int resid;
	u_int popoff;

	ASSERT((flags & B_ASYNC) == 0);
	ASSERT(!(vp->v_vfsp->vfs_flag & VFS_RDONLY));
	ASSERT(pp != NULL);
	ASSERT(cr != NULL);

	if (cp->c_cred != NULL)
		cr = cp->c_cred;

	bsize = MAXBSIZE;
	lbn = pp->p_offset / bsize;
	lbn_off = lbn * bsize;

	/*
	 * Find a kluster that fits in one block, or in
	 * one page if pages are bigger than blocks.  If
	 * there is less file space allocated than a whole
	 * page, we'll shorten the i/o request below.
	 */

	pp = pvn_write_kluster(vp, pp, &iooff, &iolen, lbn_off,
			MAXBSIZE, flags);

	if (cp->c_backvp == NULL) {
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (cp->c_backvp == NULL) {
			err = cachefs_getbackvp(fscp,
			    &cp->c_metadata.md_cookie, cp);
			if (err != 0) {
				rw_exit(&cp->c_statelock);
				err = ESTALE;
				goto writedone;
			}
		}
		rw_exit(&cp->c_statelock);
	}

	/*
	 * Set the pages up for pageout.
	 */
	bp = pageio_setup(pp, iolen, CTOV(cp), B_WRITE | flags);
	if (bp == NULL) {
		return (ENOMEM);
	}
	/*
	 * pageio_setup should have set b_addr to 0.  This
	 * is correct since we want to do I/O on a page
	 * boundary.  bp_mapin will use this addr to calculate
	 * an offset, and then set b_addr to the kernel virtual
	 * address it allocated for us.
	 */
	bp->b_edev = 0;
	bp->b_dev = 0;
	bp->b_blkno = btodb(iooff);
	bp_mapin(bp);

	/*
	 * If we need to write the pages out to the back file system,
	 * make sure we have the backvp first !
	 */
	iolen  = MIN(bp->b_bcount, cp->c_size - dbtob(bp->b_blkno));
again:
	rw_enter(&cp->c_statelock, RW_WRITER);
	if (C_ISFS_SINGLE(fscp) && ((cp->c_flags & CN_NOCACHE) == 0) &&
	    (cp->c_frontvp != NULL)) {

		for (popoff = iooff; popoff < (iooff + iolen);
		    popoff += MAXBSIZE) {
			if (cachefs_charge_page(cp, popoff) == 0) {
				blocks++;
			} else {
				blocks = 0;
				rw_exit(&cp->c_statelock);
				goto again;
			}
		}
		rw_exit(&cp->c_statelock);
		ASSERT(cp->c_frontvp != NULL);
		err = bp->b_error =
			vn_rdwr(UIO_WRITE, cp->c_frontvp, bp->b_un.b_addr,
				(int)iolen, iooff, UIO_SYSSPACE, 0,
				RLIM_INFINITY, cr, &resid);
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (err) {
			cachefs_nocache(cp);
			err = 0;
		} else {
			(void) cachefs_update_allocmap(cp, iooff, iolen);
			cp->c_flags |= (CN_UPDATED | CN_NEED_FRONT_SYNC |
					CN_POPULATION_PENDING);
		}

	}
	rw_exit(&cp->c_statelock);
	if ((C_ISFS_WRITE_AROUND(fscp) || C_ISFS_SINGLE(fscp))) {
		ASSERT(cp->c_backvp != NULL);
		err = bp->b_error =
			vn_rdwr(UIO_WRITE, cp->c_backvp, bp->b_un.b_addr,
			(int)iolen, iooff, UIO_SYSSPACE, FSYNC,
			RLIM_INFINITY, cr, &resid);
	}
	if (err) {
#ifdef CFSDEBUG
		printf("cachefspush: error %d cr %x\n", err, (int) cr);
		if (cr == kcred) {
			printf("cachefspush: cred is kcred\n");
		} else {
			printf("cachefspush: cred is not kcred -- uid = %ld\n",
			    cr->cr_uid);
		}
#endif
		bp->b_flags |= B_ERROR;
	}
	bp_mapout(bp);
	pageio_done(bp);

writedone:

	pvn_write_done(pp, ((err) ? B_ERROR : 0) | B_WRITE | flags);
	if (offp)
		*offp = iooff;
	if (lenp)
		*lenp = iolen;

	rw_enter(&cp->c_statelock, RW_WRITER);
	if (err) {
		CFSOP_INVALIDATE_COBJECT(fscp, cp, cr);
	} else {
		CFSOP_MODIFY_COBJECT(fscp, cp, cr);
	}
	cp->c_error = err;
	rw_exit(&cp->c_statelock);

	return (err);
}

/*ARGSUSED*/
static int
cachefs_dump(struct vnode *vp, caddr_t foo1, int foo2, int foo3)
{
	return (ENOSYS); /* should we panic if we get here? */
}

/*ARGSUSED*/
static int
cachefs_ioctl(struct vnode *vp, int cmd, int arg, int flag, cred_t *cred,
	int *rvalp)
{
	int error;
	struct cnode *cp = VTOC(vp);
	struct fscache *fscp = C_TO_FSCACHE(cp);
	struct vnode *iovp;
	struct cachefscache *cachep;
	extern kmutex_t cachefs_cachelock;
	extern cachefscache_t *cachefs_cachelist;
	struct cachefs_cnvt_mnt ccm;
	struct cachefs_boinfo cboi;
	struct bootobj *bootobjp;
	char *fname, *s;
	int len;

	switch (cmd) {
	case _FIOPIN:
		if (!suser(cred))
			error = EPERM;
		else
			error = cachefs_pin(vp, cred);
		break;

	case _FIOUNPIN:
		if (!suser(cred))
			error = EPERM;
		else
			error = cachefs_unpin(vp, cred);
		break;

	case _FIOCOD:
		if (!suser(cred)) {
			error = EPERM;
			break;
		}

		error = EBUSY;
		if (arg) {
			/* non-zero arg means do all filesystems */
			mutex_enter(&cachefs_cachelock);
			for (cachep = cachefs_cachelist; cachep != NULL;
			    cachep = cachep->c_next) {
				mutex_enter(&cachep->c_fslistlock);
				for (fscp = cachep->c_fslist;
				    fscp != NULL;
				    fscp = fscp->fs_next) {
					if (C_ISFS_CODCONST(fscp)) {
						fscp->fs_cod_time =
						    hrestime;
						error = 0;
					}
				}
				mutex_exit(&cachep->c_fslistlock);
			}
			mutex_exit(&cachefs_cachelock);
		} else {
			if (C_ISFS_CODCONST(fscp)) {
				fscp->fs_cod_time = hrestime;
				error = 0;
			}
		}
		break;

	case _FIOCNVTMNT:
		if (!suser(cred)) {
			error = EPERM;
			break;
		}

		/* Now copyin the convert structure */
		error = copyin((caddr_t)arg, (caddr_t)&ccm,
				sizeof (struct cachefs_cnvt_mnt));

		if (error)
			break;

		if (ccm.cm_op != CFS_CM_BACK && ccm.cm_op != CFS_CM_FRONT) {
			error = EINVAL;
			break;
		}

		if (ccm.cm_namelen == 0) {
			error = EINVAL;
			break;
		}

		/* Now copyin the name */
		fname = (caddr_t)cachefs_kmem_alloc(ccm.cm_namelen, KM_SLEEP);
		if (copyin(ccm.cm_name, fname, ccm.cm_namelen)) {
			cachefs_kmem_free(fname, ccm.cm_namelen);
			error = EINVAL;
			break;
		}
		ccm.cm_name = fname;
		error = cachefs_convert_mount(fscp, &ccm);
		cachefs_kmem_free(fname, ccm.cm_namelen);
		break;

	case _FIOBOINFO:
		if (!suser(cred)) {
			error = EPERM;
			break;
		}

		/* Now copyin the bootobj info structure */
		error = copyin((caddr_t)arg, (caddr_t)&cboi,
				sizeof (struct cachefs_boinfo));
		if (error)
			break;

		/* get the piece we're interested in and copy it back out */
		error = 0;
		switch (cboi.boi_which) {
		case CFS_BOI_ROOTFS:
			bootobjp = &rootfs;
			break;
		case CFS_BOI_FRONTFS:
			bootobjp = &frontfs;
			break;
		case CFS_BOI_BACKFS:
			bootobjp = &backfs;
			break;
		default:
			error = EINVAL;
			break;
		}
		if (error)
			break;

		s = cboi.boi_device ? bootobjp->bo_name : bootobjp->bo_fstype;
		len = strlen(s) + 1;
		len = MIN(len, cboi.boi_len);
		error = copyout((caddr_t)s, (caddr_t)cboi.boi_value, len);
		break;

	default:
		if (cp->c_backvp == NULL) {
			rw_enter(&cp->c_statelock, RW_WRITER);
			error = cachefs_getbackvp(fscp,
				&cp->c_metadata.md_cookie, cp);
			rw_exit(&cp->c_statelock);
			if (error)
				break;
		}
		iovp = cp->c_backvp;
		error = VOP_IOCTL(iovp, cmd, arg, flag, cred, rvalp);
	}

	/* return the result */
	return (error);
}

/*ARGSUSED*/
static int
cachefs_getattr(struct vnode *vp, struct vattr *vap, int flags, cred_t *cr)
{
	struct cnode *cp = VTOC(vp);
	fscache_t *fscp = C_TO_FSCACHE(cp);
	int error = EAGAIN;
	int type = RW_READER;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_getattr: ENTER vp %x\n", (int) vp);
#endif

#if MAXFIDSZ < 32
	panic("FID SIZE IS WRONG COMPILE WITH NEW vfs.h");
#endif

	while (error == EAGAIN) {
		rw_enter(&cp->c_statelock, type);
		error = CFSOP_CHECK_COBJECT(fscp, cp, C_VERIFY_ATTRS, type, cr);
		if (error != EAGAIN)
			break;
		rw_exit(&cp->c_statelock);
		type = RW_WRITER;
	}
	if (error == 0) {
		*vap = cp->c_attr;
		if (cp->c_size > vap->va_size)
			vap->va_size = cp->c_size;
	}
	rw_exit(&cp->c_statelock);
	vap->va_fsid = vp->v_vfsp->vfs_dev;

out:
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_getattr: EXIT error = %d\n", error);
#endif
	return (error);
}

static int
cachefs_setattr(register struct vnode *vp, register struct vattr *vap,
	int flags, cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	fscache_t *fscp = C_TO_FSCACHE(cp);
	cachefscache_t *cachep = fscp->fs_cache;
	register long int mask = vap->va_mask;
	int error = 0;
	u_int bcnt;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_setattr: ENTER vp %x\n", (int) vp);
#endif
	/*
	 * Cannot set these attributes.
	 */
	if (mask & AT_NOSET)
		return (EINVAL);
	/*
	 * Truncate file.  Must have write permission and not be a directory.
	 */
	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR) {
			if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_TRUNCATE))
				cachefs_log_truncate(cachep, EISDIR,
				    fscp->fs_cfsvfsp,
				    &cp->c_metadata.md_cookie, cp->c_fileno,
				    cr->cr_uid, vap->va_size);
			return (EISDIR);
		}
	}

	/*
	 * Gotta deal with one special case here, where we're setting the
	 * size of the file. First, we zero out part of the page after the
	 * new size of the file. Then we toss (not write) all pages after
	 * page in which the new offset occurs. Note that the NULL passed
	 * in instead of a putapage() fn parameter is correct, since
	 * no dirty pages will be found (B_TRUNC | B_INVAL).
	 */
	rw_enter(&cp->c_rwlock, RW_WRITER);

	/* sync dirty pages */
	(void) cachefs_putpage(vp, (offset_t)0, 0, 0, cr);

	rw_enter(&cp->c_statelock, RW_WRITER);
	if (cp->c_backvp == NULL) {
		error = cachefs_getbackvp(fscp, &cp->c_metadata.md_cookie, cp);
		if (error) {
			rw_exit(&cp->c_statelock);
			goto out;
		}
	}
	error = VOP_SETATTR(cp->c_backvp, vap, flags, cr);
	if (error) {
		rw_exit(&cp->c_statelock);
		goto out;
	}
	if (mask & AT_SIZE) {
		cp->c_size = vap->va_size;
		cachefs_inval_object(cp, cr);
	}
	cp->c_flags |= CN_UPDATED;
	cp->c_attr.va_mask = AT_ALL;
	error = VOP_GETATTR(cp->c_backvp, &cp->c_attr, 0, cr);
	if (error) {
		rw_exit(&cp->c_statelock);
		goto out;
	}
	/* ASSERT(cp->c_fileno == cp->c_attr.va_nodeid); */
	cp->c_attr.va_size = MAX(cp->c_attr.va_size, cp->c_size);
	cp->c_size = cp->c_attr.va_size;
	/*
	 * Attr's have changed, so we need to call the CFSOP_MODIFY_COBJECT
	 * function to notify the consistency interface of this.
	 */
	CFSOP_MODIFY_COBJECT(fscp, cp, cr);
	rw_exit(&cp->c_statelock);

	/*
	 * If the file size has been changed then
	 * toss whole pages beyond the end of the file and zero
	 * the portion of the last page that is beyond the end of the file.
	 */
	if (mask & AT_SIZE) {
		bcnt = cp->c_size & PAGEOFFSET;
		if (bcnt)
			pvn_vpzero(vp, cp->c_size, PAGESIZE - bcnt);
		pvn_vplist_dirty(vp, cp->c_size, cachefspush,
			B_TRUNC | B_INVAL, cr);
	}

out:
	rw_exit(&cp->c_rwlock);

	if ((mask & AT_SIZE) &&
	    (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_TRUNCATE)))
		cachefs_log_truncate(cachep, error, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno, cr->cr_uid,
		    vap->va_size);


#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_setattr: EXIT vp %x error %d \n",
		    (int) vp, error);
#endif
	return (error);
}

int
cachefsaccess(cnode_t *cp, fscache_t *fscp, int mode, int type, cred_t *cr)
{
	struct vattr *vap = &cp->c_attr;
	int error = 0;

	/*
	 * Make sure the cnode attrs are valid first.
	 */
	error = CFSOP_CHECK_COBJECT(fscp, cp, C_VERIFY_ATTRS, type, cr);
	if (error)
		goto out;
	if ((fscp->fs_options.opt_flags & CFS_ACCESS_BACKFS) == 0) {
		/*
		 * Disallow write attempt on read-only
		 * file systems, except for device/special files
		 */
		if (mode & VWRITE) {
			struct vnode *vp = CTOV(cp);
			if (vp->v_vfsp->vfs_flag & VFS_RDONLY) {
				if (!C_ISVDEV(vp->v_type)) {
					return (EROFS);
				}
			}
		}

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
		*
		*/
		if (cr->cr_uid != vap->va_uid) {
			mode >>= 3;
			if (!groupmember(vap->va_gid, cr))
				mode >>= 3;
		}
		if ((vap->va_mode & mode) == mode)
			error = 0;
		else
			error = EACCES;

	} else {
		if (cp->c_backvp == NULL) {
			error = cachefs_getbackvp(fscp,
					&cp->c_metadata.md_cookie, cp);
			if (error)
				goto out;
		}
		ASSERT(cp->c_backvp != NULL);
		error = VOP_ACCESS(cp->c_backvp, mode, 0, cr);
	}
out:
	return (error);
}

/* ARGSUSED */
static int
cachefs_access(struct vnode *vp, int mode, int flags, cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	fscache_t *fscp = C_TO_FSCACHE(cp);
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_access: ENTER vp %x\n", (int) vp);
#endif

	rw_enter(&cp->c_statelock, RW_WRITER);
	if ((fscp->fs_options.opt_flags & CFS_ACCESS_BACKFS) != 0)
		error = cachefs_initstate(cp, RW_WRITER, 0, cr);
	if (!error)
		error = cachefsaccess(cp, fscp, mode, RW_WRITER, cr);
	rw_exit(&cp->c_statelock);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_access: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * CFS has a fastsymlink scheme. If the size of the link is < C_FSL_SIZE, then
 * the link is placed in the metadata itself (no front file is allocated).
 */
static int
cachefs_readlink(struct vnode *vp, struct uio *uiop, cred_t *cr)
{
	int error = 0;
	cnode_t *cp = VTOC(vp);
	fscache_t *fscp = C_TO_FSCACHE(cp);
	cachefscache_t *cachep = fscp->fs_cache;
	int readcache = 0;
	int len = 0;

	if (vp->v_type != VLNK) /* XXX don't bother logging? */
		return (ENXIO);

	readcache = 1;
	rw_enter(&cp->c_rwlock, RW_WRITER);
	rw_enter(&cp->c_statelock, RW_WRITER);
	if ((cp->c_metadata.md_flags & (MD_POPULATED|MD_FASTSYMLNK)) == 0) {
		/*
		* symlink data is not cached yet.
		* First, make sure we have a backvp.
		*/
		if (cp->c_backvp == NULL) {
			error = cachefs_getbackvp(fscp,
					&cp->c_metadata.md_cookie, cp);
			if (error)
				goto out;
		}
		/*
		* If possible, call cachefs_cachesymlink() to cache the
		* data -- either as a fast symlink or in a frontfile.
		*/
		if ((cp->c_flags & CN_NOCACHE) == 0 &&
			(fscp->fs_flags & CFS_FS_WRITE)) {
			error = cachefs_cachesymlink(cp, cr);
			if (error) {
				readcache = 0;
				cachefs_nocache(cp);
				error = 0;
			}
		} else {
			readcache = 0;
		}
	} else if (cp->c_metadata.md_flags & MD_POPULATED) {
		/*
		 * symlink data is cached in a file (as opposed to in
		 * the metadata as a "fast symlink").  Make sure
		 * we've got the vp for the file.
		 */
		ASSERT(cp->c_metadata.md_flags & MD_FILE);
		if (cp->c_frontvp == NULL) {
			error = cachefs_getfrontfile(cp,
					(struct vattr *)NULL, cr);
			if (error) {
				readcache = 0;
				cachefs_nocache(cp);
				error = 0;
			}
		}
	}
	rw_downgrade(&cp->c_statelock);
	rw_downgrade(&cp->c_rwlock);

	if (readcache == 0) {
		ASSERT(cp->c_backvp != NULL);
		error = VOP_READLINK(cp->c_backvp, uiop, cr);
		len = uiop->uio_resid;
	} else if (cp->c_metadata.md_flags & MD_FASTSYMLNK) {
		error = uiomove((caddr_t)cp->c_metadata.md_allocinfo,
			len = (int)MIN(cp->c_size, uiop->uio_resid),
			UIO_READ, uiop);
	} else {
		ASSERT(cp->c_frontvp != NULL);
		ASSERT((cp->c_metadata.md_flags & (MD_FILE|MD_POPULATED)) ==
			(MD_FILE|MD_POPULATED));
		/*
		 * Read symlink data from frontfile
		 */
		uiop->uio_offset = 0;
		VOP_RWLOCK(cp->c_frontvp, 0);
		error = VOP_READ(cp->c_frontvp, uiop, 0, cr);
		VOP_RWUNLOCK(cp->c_frontvp, 0);
		len = uiop->uio_resid;
	}
out:
	if (error == 0) {
		if (readcache)
			fscp->fs_stats.st_hits++;
		else
			fscp->fs_stats.st_misses++;
	}
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_READLINK))
		cachefs_log_readlink(cachep, error, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno, cr->cr_uid, len);

	rw_exit(&cp->c_statelock);
	rw_exit(&cp->c_rwlock);
	return (error);
}

int
cachefs_fsync(struct vnode *vp, int syncflag, cred_t *cr)
{
	struct cnode *cp = VTOC(vp);
	int error = 0;
	fscache_t *fscp = C_TO_FSCACHE(cp);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_fsync: ENTER vp %x \n", (int) vp);
#endif

	if (fscp->fs_backvfsp->vfs_flag & VFS_RDONLY)
		goto out;

	/*
	 * Clean the cachefs pages synchronously
	 */
	error = cachefs_putpage(vp, (offset_t)0, 0, 0, cr);
	if (error)
		goto out;
	if (C_ISFS_WRITE_AROUND(fscp)) {
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (cp->c_backvp == NULL) {
			/*
			 * believe it or not, this can happen.
			 * The SVVS suite has a test that hits
			 * this case for a FIFO
			 */
			error = cachefs_getbackvp(fscp,
					&cp->c_metadata.md_cookie, cp);
		}
		if (error == 0) {
			error = VOP_FSYNC(cp->c_backvp, syncflag, cr);
			if (error)
				cp->c_error = error;
		}
		if (error == 0)
			cp->c_flags &= ~CDIRTY;
		rw_exit(&cp->c_statelock);
	}
out:

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_fsync: EXIT vp %x \n", (int) vp);
#endif
	return (error);
}

/*
 * Called from cachefs_inactive(), to make sure all the data goes out to disk.
 */
int
cachefs_sync_metadata(cnode_t *cp, cred_t *cr)
{
	int error = 0;
	struct filegrp *fgp;
	struct vattr va;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("c_sync_metadata: ENTER cp %x  cflag %x\n",
			(int) cp, cp->c_flags);
#endif

	rw_enter(&cp->c_statelock, RW_WRITER);
	if ((cp->c_flags & CN_UPDATED) == 0)
		goto out;
	if (cp->c_flags & CN_STALE)
		goto out;
	fgp = cp->c_filegrp;
	if ((fgp->fg_flags & CFS_FG_WRITE) == 0)
		goto out;

	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR) {
		rw_exit(&cp->c_statelock);
		error = filegrp_allocattr(fgp);
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (error) {
			error = 0;
			goto out;
		}
	}

	if (cp->c_flags & CN_ALLOC_PENDING) {
		error = filegrp_create_metadata(fgp, &cp->c_metadata,
		    cp->c_fileno);
		if (error)
			goto out;
		cp->c_flags &= ~CN_ALLOC_PENDING;
	}

	if (cp->c_flags & CN_NEED_FRONT_SYNC) {
		if (cp->c_frontvp != NULL) {
			error = VOP_FSYNC(cp->c_frontvp, FSYNC, cr);
			if (error) {
				cp->c_metadata.md_timestamp.tv_sec = 0;
			} else {
				va.va_mask = AT_MTIME;
				error = VOP_GETATTR(cp->c_frontvp, &va, 0, cr);
				if (error)
					goto out;
				cp->c_metadata.md_timestamp = va.va_mtime;
				cp->c_flags &=
				~(CN_NEED_FRONT_SYNC | CN_POPULATION_PENDING);
			}
		} else {
			cp->c_flags &=
				~(CN_NEED_FRONT_SYNC | CN_POPULATION_PENDING);
		}
	}

	if ((cp->c_flags & CN_ALLOC_PENDING) == 0 &&
			(cp->c_flags & CN_UPDATED)) {
		error = filegrp_write_metadata(fgp, cp->c_fileno,
				&cp->c_metadata);
		if (error)
			goto out;
	}
out:
	if (error) {
		ASSERT((cp->c_flags & CN_LRU) == 0);
		if (cp->c_metadata.md_lruno) {
			cachefs_removefrontfile(&cp->c_metadata,
			    cp->c_fileno, fgp);
			cachefs_active_remove(C_TO_FSCACHE(cp)->fs_cache,
			    cp->c_metadata.md_lruno);
			cachefs_lru_free(C_TO_FSCACHE(cp)->fs_cache,
			    cp->c_metadata.md_lruno);
			cp->c_metadata.md_lruno = 0;
			if (cp->c_frontvp) {
				VN_RELE(cp->c_frontvp);
				cp->c_frontvp = NULL;
			}
		}
		(void) filegrp_destroy_metadata(fgp, cp->c_fileno);
		cp->c_flags |= CN_ALLOC_PENDING;
		cachefs_nocache(cp);
	}
	/*
	 * we clear the updated bit even on errors because a retry
	 * will probably fail also.
	 */
	cp->c_flags &= ~CN_UPDATED;
	rw_exit(&cp->c_statelock);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("c_sync_metadata: EXIT cp %x cflag %x\n",
			(int) cp, cp->c_flags);
#endif

	return (error);
}

extern int maxcnodes;

/*
 * This is the vop entry point for inactivating a vnode.
 * It just queues the request for the async thread which
 * calls cachefs_inactive.
 */
static void
cachefs_inactive_queue(register struct vnode *vp, cred_t *cr)
{
	cnode_t *cp;
	struct cachefs_req *rp;
	struct vnode *backvp;
	int error;
	struct cachefscache *cachep;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_inactive_queue: ENTER vp %x \n", (int) vp);
#endif

	cp = VTOC(vp);

	ASSERT((cp->c_flags & CN_INACTIVE) == 0);

	/* vn_rele() set the v_count == 1 */

	if ((cp->c_flags & CN_DESTROY) && (cp->c_backvp)) {
		if (vp->v_pages) {
			(void) pvn_vplist_dirty(vp, (u_int) 0,
			    (int (*)())NULL, B_INVAL|B_TRUNC, cr);
		}
		if (vp->v_count == 1) {
			backvp = cp->c_backvp;
			cp->c_backvp = NULL;
			VN_RELE(backvp);
		}
	}

	cp->c_ipending = 1;

	rp = (struct cachefs_req *)cachefs_kmem_zalloc(
		/*LINTED alignment okay*/
		sizeof (struct cachefs_req), KM_SLEEP);
	mutex_init(&rp->cfs_req_lock, "CFS Request Mutex", MUTEX_DEFAULT, NULL);
	rp->cfs_cmd = CFS_INACTIVE;
	rp->cfs_cr = cr;
	crhold(rp->cfs_cr);
	rp->cfs_req_u.cu_inactive.ci_vp = vp;
	error = cachefs_addqueue(rp, &(C_TO_FSCACHE(cp)->fs_workq));
	if (error) {
		cachep = C_TO_FSCACHE(cp)->fs_cache;
		error = cachefs_addqueue(rp, &cachep->c_workq);
		ASSERT(error == 0);
	}

out:; /* semicolon so we have something before close-brace ifndef CFSDEBUG */

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_inactive_queue: EXIT vp %x \n", (int) vp);
#endif
}

/*
 * This routine does the real work of inactivating a vnode.
 */
void
cachefs_inactive(register struct vnode *vp, cred_t *cr)
{
	cnode_t *cp;
	struct fscache *fscp;
	int error;
	int decvnoderef = 0;
	struct filegrp *fgp;
	cachefscache_t *cachep;
	struct cachefs_metadata *mdp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_inactive: ENTER vp %x \n", (int) vp);
#endif

	cp = VTOC(vp);
	ASSERT((cp->c_flags & CN_INACTIVE) == 0);

	fscp = C_TO_FSCACHE(cp);
	cachep = fscp->fs_cache;
	ASSERT(cachep != NULL);
	fgp = cp->c_filegrp;
	mdp = &cp->c_metadata;

	if ((cp->c_flags & CN_NOCACHE) && (cp->c_metadata.md_flags & MD_FILE)) {
		rw_enter(&cp->c_statelock, RW_WRITER);
		cachefs_inval_object(cp, cr);
		rw_exit(&cp->c_statelock);
	}

	/* lock the file group against action by lookup or gc */
	filegrp_hold(fgp);
	mutex_enter(&fgp->fg_gc_mutex);

	for (;;) {
		/* see if vnode is really inactive */
		mutex_enter(&vp->v_lock);
		ASSERT(vp->v_count > 0);
		if (vp->v_count > 1) {
			cp->c_ipending = 0;
			vp->v_count--;	/* release our hold from vn_rele */
			mutex_exit(&vp->v_lock);
			goto out;
		}
		mutex_exit(&vp->v_lock);

		error = 0;
		if (vp->v_pages) {
			error = cachefs_putpage(vp, (offset_t)0, 0, B_FREE, cr);
			if (vp->v_pages && cp->c_error) {
				(void) cachefs_putpage(vp, (offset_t) 0, 0,
					B_INVAL | B_FORCE, cr);
				if (vp->v_pages) {
					printf("inactive: cp %x still"
						" has pages\n", (int) cp);
					error = ESTALE;
				} else {
					error = 0;
				}
			}
		}

		/* see if vnode is really inactive */
		mutex_enter(&vp->v_lock);
		ASSERT(vp->v_count > 0);
		if (error || (vp->v_count > 1)) {
			cp->c_ipending = 0;
			vp->v_count--;	/* release our hold from vn_rele */
			mutex_exit(&vp->v_lock);
#ifdef CFSDEBUG
			if (error) {
				printf("cachefs_inactive: putpage error %d "
					"vp %x\n", error, (int) vp);
			}
#endif
			goto out;
		}
		mutex_exit(&vp->v_lock);

		/* if need to sync metadata */
		if ((cp->c_flags & (CN_UPDATED | CN_DESTROY)) == CN_UPDATED) {
			(void) cachefs_sync_metadata(cp, cr);
			continue;
		}
		break;
	}

	/* if we need to get rid of this cnode */
	if ((cp->c_flags & (CN_DESTROY | CN_STALE)) ||
			(cachefs_cnode_cnt(0) > maxcnodes)) {
		/* remove the cnode from the hash list */
		mutex_enter(&fscp->fs_cnodelock);
		cachefs_remhash(cp);
		mutex_exit(&fscp->fs_cnodelock);

		/* get rid of any pages */
		(void) cachefs_putpage(vp, (offset_t)0, 0, B_INVAL, cr);
		if (vp->v_pages)
			(void) cachefs_putpage(vp, (offset_t)0, 0,
					B_TRUNC | B_INVAL, cr);
		ASSERT(vp->v_pages == NULL);
		ASSERT(vp->v_count == 1);

		if (cp->c_cred != NULL) {
			crfree(cp->c_cred);
			cp->c_cred = NULL;
		}

		/* if we can (and should) destory the front file and metadata */
		if (((cp->c_flags & (CN_DESTROY | CN_STALE)) == CN_DESTROY) &&
		    (fgp->fg_flags & CFS_FG_WRITE)) {
			if (mdp->md_lruno) {
				cachefs_removefrontfile(mdp, cp->c_fileno, fgp);
				if (cp->c_flags & CN_LRU) {
					cachefs_lru_remove(cachep,
						mdp->md_lruno);
				} else {
					cachefs_active_remove(cachep,
						mdp->md_lruno);
				}
				cachefs_lru_free(cachep, mdp->md_lruno);
			}
			(void) filegrp_destroy_metadata(fgp, cp->c_fileno);
		}

		/* else make sure the front file is on the lru list */
		else if (mdp->md_lruno &&
		    ((mdp->md_flags & MD_PINNED) == 0) &&
		    ((cp->c_flags & CN_LRU) == 0)) {
			ASSERT((cachep->c_flags & CACHE_NOFILL) == 0);
			cachefs_active_remove(cachep, mdp->md_lruno);
			cachefs_lru_add(cachep, mdp->md_lruno);
		}

		if (cp->c_frontvp)
			VN_RELE(cp->c_frontvp);

		if (cp->c_backvp)
			VN_RELE(cp->c_backvp);

		bzero((caddr_t)cp, sizeof (cnode_t));
		cachefs_kmem_free((caddr_t)cp, sizeof (cnode_t));
		filegrp_rele(fgp);
		(void) cachefs_cnode_cnt(-1);
		decvnoderef++;
	} else {
		/* else make it inactive and put it on the cnode free list */
		/*
		 * Get anyone (like sync) that looks at the cnode hash
		 * list to ignore this cnode.
		 */
		mutex_enter(&fscp->fs_cnodelock);
		cp->c_flags |= CN_HASHSKIP;
		mutex_exit(&fscp->fs_cnodelock);

		/* release our "hold" from vn_rele */
		mutex_enter(&vp->v_lock);
		ASSERT(vp->v_count == 1);
		decvnoderef++;
		vp->v_count--;
		mutex_exit(&vp->v_lock);

		/* put on lru list if necessary */
		if (mdp->md_lruno &&
		    ((cachep->c_flags & CACHE_NOFILL) == 0) &&
		    ((mdp->md_flags & MD_PINNED) == 0)) {
			if (cp->c_flags & CN_LRU) {
				cachefs_lru_remove(cachep, mdp->md_lruno);
			} else {
				cachefs_active_remove(cachep, mdp->md_lruno);
			}
			cachefs_lru_add(cachep, mdp->md_lruno);
			cp->c_flags |= CN_LRU;
		}

		/* release front and back files */
		if (cp->c_frontvp) {
			VN_RELE(cp->c_frontvp);
			cp->c_frontvp = NULL;
		}
		if (cp->c_backvp) {
			VN_RELE(cp->c_backvp);
			cp->c_backvp = NULL;
		}
		if (cp->c_cred != NULL) {
			crfree(cp->c_cred);
			cp->c_cred = NULL;
		}
		cp->c_error = 0;

		/*
		* leave ALLOC_PENDING alone here.  It could be ON
		* when we are in NOCACHE mode.  Then we need to have
		* ALLOC_PENDING on so that the right things happen
		* later when a remount enables caching
		*/

		if ((cachep->c_flags & CACHE_NOCACHE) == 0)
			cp->c_flags &= ~CN_NOCACHE;

		mutex_enter(&fscp->fs_cnodelock);
		cp->c_flags &= ~CN_HASHSKIP;
		mutex_exit(&fscp->fs_cnodelock);

		/*
		* Put on free list and mark as inactive.
		*/
		mutex_enter(&cachefs_cnode_freelist_lock);
		cachefs_addfree(cp);
		cp->c_flags |= CN_INACTIVE;
		cp->c_filegrp = NULL;
		ASSERT((cp->c_flags & CN_UPDATED) == 0);
		mutex_exit(&cachefs_cnode_freelist_lock);

		filegrp_rele(fgp);
	}
out:
	mutex_exit(&fgp->fg_gc_mutex);
	filegrp_rele(fgp);

	if (decvnoderef) {
		fscache_rele(fscp);
	}

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_inactive: EXIT vp %x \n", (int) vp);
#endif
}

int cachefs_dnlc = 1;	/* use dnlc */

int
cachefs_initstate(cnode_t *cp, int type, int alloc, cred_t *cr)
{
	int error = 0;
	fscache_t *fscp = C_TO_FSCACHE(cp);

	ASSERT(type == RW_READER || type == RW_WRITER);
	if ((cp->c_frontvp || (cp->c_flags & CN_NOCACHE)) && cp->c_backvp)
		return (0);
	if (type != RW_WRITER)
		if (rw_tryupgrade(&cp->c_statelock) != 1)
			return (EAGAIN);
	if ((cp->c_flags & CN_NOCACHE) == 0 && cp->c_frontvp == NULL &&
			((cp->c_metadata.md_flags & MD_FILE) || alloc)) {
		error = cachefs_getfrontfile(cp, (struct vattr *)NULL, cr);
		if (error) {
			cachefs_nocache(cp);
			error = 0;
		}
	}
	if (cp->c_backvp == NULL)
		error = cachefs_getbackvp(fscp, &cp->c_metadata.md_cookie, cp);
	if (type == RW_READER)
		rw_downgrade(&cp->c_statelock);
	return (error);
}

/*
 * Remote file system operations having to do with directory manipulation.
 */

/* ARGSUSED */
static int
cachefs_lookup(struct vnode *dvp, char *nm, struct vnode **vpp,
	struct pathname *pnp, int flags, struct vnode *rdir, cred_t *cr)
{
	int error = 0;
	cnode_t *cp, *dcp = VTOC(dvp);
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	struct fid cookie;
	u_int d_offset;
	ino_t fileno;
	u_int flag;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_lookup: ENTER dvp %x nm %s\n", (int) dvp, nm);
#endif

	/*
	 * If lookup is for ".", just return dvp.  Don't need
	 * to send it over the wire or look it up in the dnlc.
	 * Null component is a synonym for current directory.
	 */
	if (strcmp(nm, ".") == 0 || *nm == '\0') {
		VN_HOLD(dvp);
		*vpp = dvp;
		goto out1;
	}
	/* VOP_ACCESS will verify consistency for us */
	error = VOP_ACCESS(dvp, VEXEC, 0, cr);
	if (error)
		goto out1;
	/*
	 * Read lock the cnode before starting the search.
	 */
	rw_enter(&dcp->c_rwlock, RW_READER);

	*vpp = (vnode_t *)dnlc_lookup(dvp, nm, cr);
	if (*vpp)
		goto out;
	/*
	 * Didn't get a dnlc hit. We have to search the directory.
	 */
	rw_enter(&dcp->c_statelock, RW_WRITER);
	if ((dcp->c_flags & CN_NOCACHE) == 0 && dcp->c_frontvp == NULL) {
		error = cachefs_getfrontfile(dcp, (struct vattr *)NULL, cr);
		if (error) {
			cachefs_nocache(dcp);
			rw_exit(&dcp->c_statelock);
			error = cachefs_lookup_back(dvp, nm, vpp, 0, cr);
			goto out;
		}
		ASSERT(dcp->c_frontvp != NULL);
	}
	dcp->c_usage++;
	if ((dcp->c_flags & CN_NOCACHE) ||
	    ((dcp->c_filegrp->fg_flags & CFS_FG_READ) == 0)) {
		rw_exit(&dcp->c_statelock);
		error = cachefs_lookup_back(dvp, nm, vpp, 0, cr);
		goto out;
	}

	if ((dcp->c_metadata.md_flags & MD_POPULATED) == 0) {
		if ((dcp->c_filegrp->fg_flags & CFS_FG_WRITE) == 0) {
			rw_exit(&dcp->c_statelock);
			error = cachefs_lookup_back(dvp, nm, vpp, 0, cr);
			goto out;
		}
		if (dcp->c_backvp == NULL) {
			error = cachefs_getbackvp(fscp,
					&dcp->c_metadata.md_cookie, dcp);
			if (error) {
				rw_exit(&dcp->c_statelock);
				goto out;
			}
		}
		error = cachefs_filldir(dcp, RW_WRITER, cr);
		if (error) {
			rw_exit(&dcp->c_statelock);
			error = cachefs_lookup_back(dvp, nm, vpp, 0, cr);
			goto out;
		}
	}

	/*
	 * By now we have a valid cached front file that we can search.
	 */
	error = cachefs_dirlook(dcp, nm, &cookie, &flag,
			&d_offset, &fileno, cr);
	rw_exit(&dcp->c_statelock);
	if (error == EINVAL) {
		error = cachefs_lookup_back(dvp, nm, vpp, d_offset, cr);
	} else if (error == 0) {
		/*
		* We make a cnode here and return it.
		*/
		error = makecachefsnode(fileno, fscp, &cookie,
					(vnode_t *)NULL, cr, 0, &cp);
		if (error == ESTALE) {
			rw_enter(&dcp->c_statelock, RW_WRITER);
			cachefs_nocache(dcp);
			rw_exit(&dcp->c_statelock);
			error = cachefs_lookup_back(dvp, nm, vpp, 0, cr);
		} else if (error == 0) {
			if (cachefs_dnlc)
				dnlc_enter(CTOV(dcp), nm, CTOV(cp), cr);
			*vpp = CTOV(cp);
		}
	} else if (error != ENOENT) {
		rw_enter(&dcp->c_statelock, RW_WRITER);
		cachefs_nocache(dcp);
		rw_exit(&dcp->c_statelock);
		error = cachefs_lookup_back(dvp, nm, vpp, 0, cr);
	}

out:
	rw_exit(&dcp->c_rwlock);
out1:
	if (error == 0 && C_ISVDEV((*vpp)->v_type)) {
		struct vnode *newvp;

		newvp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp);
		if (newvp == NULL) {
			error = ENOSYS;
		} else {
			*vpp = newvp;
		}
	}

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_lookup: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * Called from cachefs_lookup when the back file system needs to be
 * examined to perform the lookup.
 */
int
cachefs_lookup_back(struct vnode *dvp, char *nm, struct vnode **vpp,
    u_int d_offset, cred_t *cr)
{
	int error = 0;
	cnode_t *cp, *dcp = VTOC(dvp);
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	vnode_t *backvp = NULL;
	struct vattr va;
	struct fid cookie;

	rw_enter(&dcp->c_statelock, RW_WRITER);

	/*
	 * Do a lookup on the back FS to get the back vnode.
	 */
	if (dcp->c_backvp == NULL) {
		error = cachefs_getbackvp(fscp, &dcp->c_metadata.md_cookie,
				dcp);
		if (error)
			goto out;
	}
	error = VOP_LOOKUP(dcp->c_backvp, nm, &backvp, (struct pathname *)NULL,
				0, (vnode_t *)NULL, cr);
	if (error)
		goto out;
	if (C_ISVDEV(backvp->v_type)) {
		struct vnode *devvp = backvp;

		if (VOP_REALVP(devvp, &backvp) == 0) {
			VN_HOLD(backvp);
			VN_RELE(devvp);
		}
	}
	/*
	 * Get the cookie from the backvp.
	 */
	error = cachefs_getcookie(backvp, &cookie, &va, cr);
	if (error)
		goto out;
	/*
	 * If the directory entry was incomplete, we can complete it now.
	 */
	if ((dcp->c_metadata.md_flags & MD_POPULATED) &&
	    ((dcp->c_flags & CN_NOCACHE) == 0) &&
	    (dcp->c_filegrp->fg_flags & CFS_FG_WRITE)) {
		cachefs_dirent_mod(dcp, d_offset, &cookie, &va.va_nodeid);
	}

out:
	rw_exit(&dcp->c_statelock);

	/*
	 * Create the cnode.
	 */
	if (error == 0) {
		error = makecachefsnode(va.va_nodeid, fscp, &cookie, backvp,
			cr, 0, &cp);
		if (error == 0) {
			if (cachefs_dnlc)
				dnlc_enter(CTOV(dcp), nm, CTOV(cp), cr);
			*vpp = CTOV(cp);
		}
	}

	if (backvp)
		VN_RELE(backvp);

	return (error);
}

static int
cachefs_create_non_local(cnode_t *dcp, char *nm, struct vattr *vap,
	enum vcexcl exclusive, int mode,
	cnode_t **newcpp, cred_t *cr)
{
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	int error = 0;
	vnode_t *tvp = NULL;
	struct vnode *devvp = NULL;
	struct fid cookie;
	struct vattr va;



#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_create_non_local: ENTER dcp %x excl %d \n",
			(int) dcp, exclusive);
#endif

	rw_enter(&dcp->c_statelock, RW_WRITER);
	error = cachefs_initstate(dcp, RW_WRITER, 0, cr);
	rw_exit(&dcp->c_statelock);
	if (error)
		goto out;
	if (C_ISFS_SINGLE(fscp)) {
		struct cnode *tcp;

		error = cachefs_lookup(CTOV(dcp), nm, &tvp, NULL, 0, NULL, cr);
		if (error == 0) {
			if (tvp->v_type == VREG) {
				tcp = VTOC(tvp);
				mutex_enter(&tcp->c_iomutex);
				while (tcp->c_ioflags & CIO_PUTPAGES) {
					error = cv_wait_sig(&tcp->c_iocv,
							&tcp->c_iomutex);
					if (error) {
						mutex_exit(&tcp->c_iomutex);
						goto out;
					}
				}
				mutex_exit(&tcp->c_iomutex);
			}
			VN_RELE(tvp);
			tvp = NULL;
		}
	}
	*newcpp = NULL;
	error = VOP_CREATE(dcp->c_backvp, nm, vap, exclusive, mode, &devvp, cr);
	if (error)
		goto out;
	if (VOP_REALVP(devvp, &tvp) == 0) {
		VN_HOLD(tvp);
		VN_RELE(devvp);
	} else {
		tvp = devvp;
	}
	error = cachefs_getcookie(tvp, &cookie, &va, cr);
	if (error)
		goto out;
	error = makecachefsnode(va.va_nodeid, fscp, &cookie,
			tvp, cr, 0, newcpp);
	if (error)
		goto out;
	if ((CTOV(*newcpp))->v_type == VREG &&
		(vap->va_mask & AT_SIZE) && vap->va_size == 0) {
		(void) pvn_vplist_dirty(CTOV(*newcpp), (u_int) 0,
		    (int (*)())NULL, B_INVAL|B_TRUNC, cr);
	}
	rw_enter(&(*newcpp)->c_statelock, RW_WRITER);
	(*newcpp)->c_attr = va;
	ASSERT((*newcpp)->c_fileno == (*newcpp)->c_attr.va_nodeid);
	(*newcpp)->c_size = va.va_size;
	(*newcpp)->c_flags |= CN_UPDATED;
	rw_exit(&(*newcpp)->c_statelock);

	/*
	 * Finally, we enter the created file in the parent directory.
	 * If the mount is a strict consistency mount, there's no need
	 * to do a cachefs_dirent(), since the CFSOP_MODIFY op will invalidate
	 * the cache anyway.
	 */
	rw_enter(&dcp->c_rwlock, RW_WRITER);
	rw_enter(&dcp->c_statelock, RW_WRITER);
	if (!C_ISFS_STRICT(fscp) && ((dcp->c_flags & CN_NOCACHE) == 0) &&
		(dcp->c_metadata.md_flags & MD_POPULATED)) {
		u_int eoffset;

		error = cachefs_dirlook(dcp, nm, NULL, NULL,
				&eoffset, NULL, cr);
		if (error == ENOENT) {
			error = cachefs_direnter(dcp, nm, &cookie, 0,
					va.va_nodeid, -1, cr, SM_ASYNC);
			if (error) {
				/*
				* We got some error from direnter().
				* Put the directory
				* in nocache mode from now on.
				*/
				cachefs_nocache(dcp);
				error = 0;
			}
			dnlc_enter(CTOV(dcp), nm, CTOV((*newcpp)), cr);
		} else if (error == EINVAL) {
			cachefs_dirent_mod(dcp, eoffset,
			    &cookie, &va.va_nodeid);
			error = 0;
		} else if (error != 0) {
			cachefs_nocache(dcp);
			error = 0;
		}
	}
	CFSOP_MODIFY_COBJECT(fscp, dcp, cr);
	rw_exit(&dcp->c_statelock);
	rw_exit(&dcp->c_rwlock);

out:
	if (tvp != NULL)
		VN_RELE(tvp);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_create_non_local: EXIT dcp %x error %d\n",
			(int) dcp, error);
#endif
	return (error);
}

static int
cachefs_create(struct vnode *dvp, char *nm, struct vattr *vap,
	enum vcexcl exclusive, int mode, struct vnode **vpp, cred_t *cr)
{
	cnode_t *cp = NULL, *dcp = VTOC(dvp);
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_create: ENTER dvp %x excl %d\n",
			(int) dvp, exclusive);
#endif

	/*
	 * Acquire the writer's lock on the cnode to eliminate any other naughty
	 * activity that might be happening on this directory.
	 */
	error = cachefs_create_non_local(dcp, nm, vap, exclusive,
			mode, &cp, cr);
	if (error)
		goto out;
	*vpp = CTOV(cp);
out:

	if (error == 0 && C_ISVDEV((*vpp)->v_type)) {
		struct vnode *newvp;

		newvp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp);
		if (newvp == NULL) {
			error = ENOSYS;
		} else {
			*vpp = newvp;
		}
	}

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_CREATE)) {
		fid_t *fidp = NULL;
		ino_t fileno = 0;

		if (cp != NULL) {
			fidp = &cp->c_metadata.md_cookie;
			fileno = cp->c_fileno;
		}
		cachefs_log_create(cachep, error, fscp->fs_cfsvfsp,
		    fidp, fileno, cr->cr_uid);
	}

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_create: EXIT dvp %x error %d\n",
		    (int) dvp, error);
#endif

	return (error);
}
struct cnode *boguscp = NULL;

static int
cachefs_remove(struct vnode *dvp, char *nm, cred_t *cr)
{
	cnode_t *cp, *dcp = VTOC(dvp);
	vnode_t *vp = NULL;
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;
	int error = 0;
	struct vnode *origvp = NULL;
	int quiet;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_remove: ENTER dvp %x name %s \n",
		    (int) dvp, nm);
#endif

	if (fscp->fs_cache->c_flags & (CACHE_NOFILL | CACHE_NOCACHE)) {
		ASSERT(dcp->c_flags & CN_NOCACHE);
	}

	error = cachefs_lookup(dvp, nm, &origvp, (struct pathname *)NULL, 0,
				(vnode_t *)NULL, cr);
	if (error) {
		if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_REMOVE)) {
			struct fid foo;

			bzero((caddr_t) &foo, sizeof (foo));
			cachefs_log_remove(cachep, error, fscp->fs_cfsvfsp,
			    &foo, 0, cr->cr_uid);
		}
		return (error);
	}

	if (VOP_REALVP(origvp, &vp) != 0)
		vp = origvp;

	cp = VTOC(vp);

	/*
	 * Acquire the rwlock (WRITER) on the directory to prevent other
	 * activity on the directory.
	 */
	rw_enter(&dcp->c_rwlock, RW_WRITER);
	rw_enter(&dcp->c_statelock, RW_WRITER);
	rw_enter(&cp->c_statelock, RW_WRITER);
	if (dcp->c_backvp == NULL) {
		error = cachefs_getbackvp(fscp, &dcp->c_metadata.md_cookie,
				dcp);
		if (error) {
			rw_exit(&cp->c_statelock);
			goto out;
		}
	}

	/* determine if the cnode is about to go inactive */
	dnlc_purge_vp(vp);
	quiet = (vp->v_count == 1) || ((vp->v_count == 2) && cp->c_ipending);

#ifdef QUIET_TEST
	if (!quiet) {	/* TEST */
		printf("cachefs_remove: not quiet: cp %x  v_count %d"
			"  ipending %d\n",
			(int) cp, (int) vp->v_count, cp->c_ipending);
	}
#endif

	if (quiet && cp->c_backvp) {
		if (vp->v_pages) {
			rw_exit(&cp->c_statelock);
			(void) pvn_vplist_dirty(vp, (u_int) 0,
			    (int (*)())NULL, B_INVAL|B_TRUNC, cr);
			rw_enter(&cp->c_statelock, RW_WRITER);
		}
		if (vp->v_pages == NULL) {
			VN_RELE(cp->c_backvp);
			cp->c_backvp = NULL;
		}
	} else if (!quiet && cp->c_backvp == NULL) {
		/*
		* Must get ref to the back vnode so I/O after
		* remove works.
		*/
		error = cachefs_getbackvp(fscp, &cp->c_metadata.md_cookie, cp);
		if (error) {
			rw_exit(&cp->c_statelock);
			goto out;
		}
	}

	/*
	 * Call VOP_REMOVE(BackFS). Remove the directory entry from the
	 * cached directory.
	 */
	error = VOP_REMOVE(dcp->c_backvp, nm, cr);
	if (error) {
		rw_exit(&cp->c_statelock);
		goto out;
	}

	dnlc_purge_vp(dvp);
	if (cp->c_attr.va_nlink == 1)
		cp->c_flags |= CN_DESTROY;
	else
		cp->c_flags |= CN_UPDATED;

	cp->c_attr.va_nlink--;
	CFSOP_MODIFY_COBJECT(fscp, cp, cr);
	rw_exit(&cp->c_statelock);
	/*
	 * The directory has been modified, so inform the consistency module
	 */
	CFSOP_MODIFY_COBJECT(fscp, dcp, cr);
	dcp->c_flags |= CN_UPDATED;
	/*
	 * If the mount is a strict consistency mount, there's no need
	 * to do a cachefs_rmdirent(), since the CFSOP_MODIFY op will invalidate
	 * the cache anyway.
	 */
	if (!(dcp->c_flags & CN_NOCACHE) && (!C_ISFS_STRICT(fscp))) {
		error = cachefs_rmdirent(dcp, nm, cr);
		if (error) {
			cachefs_nocache(dcp);
			error = 0;
		}
	}

out:
	rw_exit(&dcp->c_statelock);
	rw_exit(&dcp->c_rwlock);
	VN_RELE(origvp);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_REMOVE))
		cachefs_log_remove(cachep, error, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno, cr->cr_uid);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_remove: EXIT dvp %x\n", (int) dvp);
#endif
	return (error);
}

static int
cachefs_link(struct vnode *tdvp, struct vnode *svp, char *tnm, cred_t *cr)
{
	cnode_t *cp = VTOC(svp), *tdcp = VTOC(tdvp);
	fscache_t *fscp = VFS_TO_FSCACHE(tdvp->v_vfsp);
	int error = 0;
	struct vnode *realvp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_link: ENTER svp %x tdvp %x tnm %s \n",
			(int) svp, (int) tdvp, tnm);
#endif

	if (fscp->fs_cache->c_flags & (CACHE_NOFILL | CACHE_NOCACHE))
		ASSERT(tdcp->c_flags & CN_NOCACHE);

	if (VOP_REALVP(svp, &realvp) == 0) {
		svp = realvp;
		cp = VTOC(svp);
	}

	rw_enter(&tdcp->c_rwlock, RW_WRITER);
	rw_enter(&tdcp->c_statelock, RW_WRITER);
	error = cachefs_initstate(tdcp, RW_WRITER, 1, cr);
	if (error)
		goto out;
	error = CFSOP_CHECK_COBJECT(fscp, tdcp, 0, RW_WRITER, cr);
	if (error)
		goto out;
	if (tdcp != cp)
		rw_enter(&cp->c_statelock, RW_WRITER);
	error = cachefs_initstate(cp, RW_WRITER, 1, cr);
	if (error) {
		if (tdcp != cp)
			rw_exit(&cp->c_statelock);
		goto out;
	}

	error = VOP_LINK(tdcp->c_backvp, cp->c_backvp, tnm, cr);
	if (error) {
		if (tdcp != cp)
			rw_exit(&cp->c_statelock);
		goto out;
	}
	cp->c_flags |= CN_UPDATED;
	/*
	 * If the mount is a strict consistency mount, there's no need
	 * to do a cachefs_dirent(), since the CFSOP_MODIFY op will invalidate
	 * the cache anyway.
	 */
	if (!C_ISFS_STRICT(fscp) && ((tdcp->c_flags & CN_NOCACHE) == 0) &&
		(tdcp->c_metadata.md_flags & MD_POPULATED)) {
		error = cachefs_direnter(tdcp, tnm, &cp->c_cookie, 0,
					cp->c_fileno, -1, cr, SM_ASYNC);
		if (error) {
			cachefs_nocache(tdcp);
			error = 0;
		}
		tdcp->c_flags |= CN_UPDATED;
	}

	CFSOP_MODIFY_COBJECT(fscp, cp, cr);
	CFSOP_MODIFY_COBJECT(fscp, tdcp, cr);
	cp->c_attr.va_mask = AT_ALL;
	error = VOP_GETATTR(cp->c_backvp, &cp->c_attr, 0, cr);
	ASSERT(cp->c_fileno == cp->c_attr.va_nodeid);
	if (tdcp != cp)
		rw_exit(&cp->c_statelock);
out:
	rw_exit(&tdcp->c_statelock);
	rw_exit(&tdcp->c_rwlock);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_link: EXIT svp %x tdvp %x tnm %s \n",
			(int) svp, (int) tdvp, tnm);
#endif

	return (error);
}

/*
 * Serialize all renames in CFS, to avoid deadlocks - We have to hold two
 * cnodes atomically.
 */
kmutex_t cachefs_rename_lock;

static int
cachefs_rename(struct vnode *odvp, char *onm, struct vnode *ndvp,
	char *nnm, cred_t *cr)
{
	cnode_t *odcp = VTOC(odvp);
	cnode_t *ndcp = VTOC(ndvp);
	fscache_t *fscp = C_TO_FSCACHE(odcp);
	cachefscache_t *cachep = fscp->fs_cache;
	int error = 0;
	struct vnode *replvp = NULL;
	struct cnode *replcp;
	struct fid replcookie;
	struct vattr replva;
	int dodelete = 0;

	if (fscp->fs_cache->c_flags & (CACHE_NOFILL | CACHE_NOCACHE))
		ASSERT((odcp->c_flags & CN_NOCACHE) &&
		    (ndcp->c_flags & CN_NOCACHE));

	/*
	 * To avoid deadlock, we acquire this global rename lock before
	 * we try to get the locks for the source and target directories.
	 */
	mutex_enter(&cachefs_rename_lock);
	rw_enter(&odcp->c_rwlock, RW_WRITER);
	if (odcp != ndcp) {
		rw_enter(&ndcp->c_rwlock, RW_WRITER);
	}
	mutex_exit(&cachefs_rename_lock);

	rw_enter(&odcp->c_statelock, RW_WRITER);
	error = cachefs_initstate(odcp, RW_WRITER, 1, cr);
	if (error) {
		rw_exit(&odcp->c_statelock);
		goto out;
	}
	error = CFSOP_CHECK_COBJECT(fscp, odcp, 0, RW_WRITER, cr);
	if (error) {
		rw_exit(&odcp->c_statelock);
		goto out;
	}
	rw_exit(&odcp->c_statelock);

	if (odcp != ndcp) {
		rw_enter(&ndcp->c_statelock, RW_WRITER);
		error = cachefs_initstate(ndcp, RW_WRITER, 1, cr);
		if (error) {
			rw_exit(&ndcp->c_statelock);
			goto out;
		}
		error = CFSOP_CHECK_COBJECT(fscp, ndcp, 0, RW_WRITER, cr);
		if (error) {
			rw_exit(&ndcp->c_statelock);
			goto out;
		}
		rw_exit(&ndcp->c_statelock);
	}

	/*
	 * see if the rename is deleting a file that previously
	 * existed in the target directory.
	 */
	replcp = NULL;
	replvp = NULL;
	error = VOP_LOOKUP(ndcp->c_backvp, nnm, &replvp,
			(struct pathname *) NULL, 0, (vnode_t *)NULL, cr);
	if (error == 0) {
		error = cachefs_getcookie(replvp, &replcookie, &replva, cr);
		if (error == 0) {
			dodelete = 1;
		} else {
			VN_RELE(replvp);
			replvp = NULL;
		}
	}

	if (dodelete) {
		/*
		* gotta make sure that existing cnodes for this thing
		* don't hang around...
		*/
		error = makecachefsnode(replva.va_nodeid, fscp, &replcookie,
			replvp, cr, 0, &replcp);
		if (error) {
			VN_RELE(replvp);
			replvp = NULL;
			dodelete = 0;
		} else {
			/*
			 * if the target of the rename already exists and is
			 * a directory, we check to see if it is currently
			 * mounted-on.  If so, we fail the rename with EBUSY.
			 * If not, we hold the vn_vfslock across the rename
			 * operation so that no mounts can come in while the
			 * rename is happening.
			 */
			struct vnode *rdvp = CTOV(replcp);
			if (rdvp->v_type == VDIR) {
				if (vn_vfslock(rdvp)) {
					error = EBUSY;
					VN_RELE(replvp);
					replvp = NULL;
					VN_RELE(rdvp);
					goto out;
				}
				if (rdvp->v_vfsmountedhere) {
					vn_vfsunlock(rdvp);
					error = EBUSY;
					VN_RELE(replvp);
					replvp = NULL;
					VN_RELE(rdvp);
					goto out;
				}
			}
			if ((replcp->c_attr.va_nlink != 1) &&
			    (replcp->c_backvp == NULL)) {
				/*
				 * Must get ref to the back vnode so I/O after
				 * remove works.
				 */
				rw_enter(&replcp->c_statelock, RW_WRITER);
				error = cachefs_getbackvp(fscp,
				    &replcp->c_metadata.md_cookie, replcp);
				rw_exit(&replcp->c_statelock);
			}
		}
	}

	error = VOP_RENAME(odcp->c_backvp, onm, ndcp->c_backvp, nnm, cr);
	if (error) {
		if (dodelete) {
			struct vnode *rvp = CTOV(replcp);

			VN_RELE(replvp);
			replvp = NULL;
			if (rvp->v_type == VDIR)
				vn_vfsunlock(rvp);
			VN_RELE(rvp);
		}
		goto out;
	}
	dnlc_purge_vp(odvp);
	dnlc_purge_vp(ndvp);

	if (dodelete) {
		rw_enter(&replcp->c_statelock, RW_WRITER);
		if (replcp->c_attr.va_nlink == 1) {
			replcp->c_flags |= CN_DESTROY;
		} else {
			replcp->c_flags |= CN_UPDATED;
		}
		replcp->c_attr.va_nlink--;
		CFSOP_MODIFY_COBJECT(fscp, replcp, cr);
		rw_exit(&replcp->c_statelock);
		if (CTOV(replcp)->v_type == VDIR)
			vn_vfsunlock(CTOV(replcp));
		VN_RELE(CTOV(replcp));
		replcp = NULL;
	}

	if (!C_ISFS_STRICT(fscp)) {
		struct fid cookie;
		struct fid *cookiep;
		ino_t fileno = 0;
		int gotdirent;

		/*
		 * if we're not strict consistency we'll modify the
		 * cached directories
		 */
		rw_enter(&odcp->c_statelock, RW_WRITER);
		gotdirent = 0;
		cookiep = NULL;
		if ((odcp->c_flags & CN_NOCACHE) == 0) {
			if (odcp->c_metadata.md_flags & MD_POPULATED) {
				error = cachefs_dirlook(odcp, onm, &cookie,
						NULL, NULL, &fileno, cr);
				if (error == 0 || error == EINVAL) {
					gotdirent = 1;
					if (error == 0)
						cookiep = &cookie;
				} else {
					cachefs_inval_object(odcp, cr);
				}
			}
		}

		error = 0;
		/*
		* Remove the directory entry from the old directory and
		* install it in the new directory.
		*/
		if (gotdirent) {
			error = cachefs_rmdirent(odcp, onm, cr);
			if (error) {
				cachefs_nocache(odcp);
				error = 0;
			}
		}

		CFSOP_MODIFY_COBJECT(fscp, odcp, cr);
		rw_exit(&odcp->c_statelock);

		rw_enter(&ndcp->c_statelock, RW_WRITER);
		if ((ndcp->c_flags & CN_NOCACHE) == 0 &&
			(ndcp->c_metadata.md_flags & MD_POPULATED)) {
			if (dodelete) {
				(void) cachefs_rmdirent(ndcp, nnm, cr);
			}

			error = 1;
			if (gotdirent) {
				ASSERT(fileno != 0);
				error = cachefs_direnter(ndcp, nnm, cookiep,
				    0, fileno, -1, cr, SM_ASYNC);
			}
			if (error) {
				cachefs_nocache(ndcp);
				error = 0;
			}
		}
		if (odcp != ndcp)
			CFSOP_MODIFY_COBJECT(fscp, ndcp, cr);
		rw_exit(&ndcp->c_statelock);
	} else {
		/*
		 * in strict consistency mode, cached directories are
		 * invalidated
		 */
		rw_enter(&odcp->c_statelock, RW_WRITER);
		CFSOP_MODIFY_COBJECT(fscp, odcp, cr);
		rw_exit(&odcp->c_statelock);
		if (odcp != ndcp) {
			rw_enter(&ndcp->c_statelock, RW_WRITER);
			CFSOP_MODIFY_COBJECT(fscp, ndcp, cr);
			rw_exit(&ndcp->c_statelock);
		}
	}

out:
	if (odcp != ndcp)
		rw_exit(&ndcp->c_rwlock);
	rw_exit(&odcp->c_rwlock);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_RENAME)) {
		struct fid gone;

		bzero((caddr_t) &gone, sizeof (gone));
		gone.fid_len = MAXFIDSZ;
		if (replvp != NULL)
			(void) VOP_FID(replvp, &gone);

		cachefs_log_rename(cachep, error, fscp->fs_cfsvfsp,
		    &gone, 0,
		    dodelete && (replvp != NULL), cr->cr_uid);
	}
	if (replvp != NULL)
		VN_RELE(replvp);
	return (error);
}

static int
cachefs_mkdir(struct vnode *dvp, char *nm, register struct vattr *va,
	struct vnode **vpp, cred_t *cr)
{
	cnode_t *cp = NULL, *dcp = VTOC(dvp);
	struct vnode *vp = NULL;
	int error = 0;
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;
	struct fid cookie;
	struct vattr attr;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_mkdir: ENTER vp %x\n", (int) dvp);
#endif

	if (fscp->fs_cache->c_flags & (CACHE_NOFILL | CACHE_NOCACHE))
		ASSERT(dcp->c_flags & CN_NOCACHE);

	rw_enter(&dcp->c_rwlock, RW_WRITER);

	rw_enter(&dcp->c_statelock, RW_WRITER);
	error = cachefs_initstate(dcp, RW_WRITER, 1, cr);
	rw_exit(&dcp->c_statelock);
	if (error)
		goto out;

	error = VOP_MKDIR(dcp->c_backvp, nm, va, &vp, cr);
	if (error) {
		goto out;
	}
	/*
	 * Next we need to get the cookie, so we can do a makecachefsnode
	 */
	attr.va_mask = AT_ALL;
	error = cachefs_getcookie(vp, &cookie, &attr, cr);
	if (error) {
		goto out;
	}

	error = makecachefsnode(attr.va_nodeid, fscp, &cookie, vp, cr, 0, &cp);
	if (error) {
		goto out;
	}
	ASSERT(CTOV(cp)->v_type == VDIR);
	*vpp = CTOV(cp);
	/*
	 * If the mount is a strict consistency mount, there's no need
	 * to do a cachefs_dirent(), since the CFSOP_MODIFY op will inval
	 * the cache anyway.
	 */
	rw_enter(&dcp->c_statelock, RW_WRITER);
	if (!C_ISFS_STRICT(fscp) && ((dcp->c_flags & CN_NOCACHE) == 0) &&
		(dcp->c_metadata.md_flags & MD_POPULATED)) {
		error = cachefs_direnter(dcp, nm, &cookie, 0, attr.va_nodeid,
					-1, cr, SM_ASYNC);
		if (error) {
			cachefs_nocache(dcp);
			error = 0;
		}
		dnlc_enter(dvp, nm, *vpp, cr);
	} else {
		dnlc_purge_vp(dvp);
	}
	dcp->c_attr.va_nlink++;
	dcp->c_flags |= CN_UPDATED;
	CFSOP_MODIFY_COBJECT(fscp, dcp, cr);
	rw_exit(&dcp->c_statelock);

out:

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_mkdir: EXIT error = %d\n", error);
#endif
	if (vp)
		VN_RELE(vp);

	rw_exit(&dcp->c_rwlock);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_MKDIR)) {
		fid_t *fidp = NULL;
		ino_t fileno = 0;

		if (cp != NULL) {
			fidp = &cp->c_metadata.md_cookie;
			fileno = cp->c_fileno;
		}

		cachefs_log_mkdir(cachep, error, fscp->fs_cfsvfsp,
		    fidp, fileno, cr->cr_uid);
	}
	return (error);
}

static int
cachefs_rmdir(struct vnode *dvp, char *nm, struct vnode *cdir, cred_t *cr)
{
	cnode_t *cp = NULL, *dcp = VTOC(dvp);
	vnode_t *vp = NULL;
	int error = 0;
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_rmdir: ENTER vp %x\n", (int) dvp);
#endif

	if (fscp->fs_cache->c_flags & (CACHE_NOFILL | CACHE_NOCACHE))
		ASSERT(dcp->c_flags & CN_NOCACHE);

	error = cachefs_lookup(dvp, nm, &vp, (struct pathname *)NULL, 0,
				(vnode_t *)NULL, cr);
	if (error)
		goto out;
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}
	if (VOP_CMP(vp, cdir)) {
		error = EINVAL;
		goto out;
	}
	cp = VTOC(vp);

	rw_enter(&dcp->c_rwlock, RW_WRITER);
	rw_enter(&dcp->c_statelock, RW_WRITER);
	rw_enter(&cp->c_statelock, RW_WRITER);
	if (dcp->c_backvp == NULL) {
		error = cachefs_getbackvp(fscp, &dcp->c_metadata.md_cookie,
				dcp);
		if (error)
			goto out1;
	}

	error = CFSOP_CHECK_COBJECT(fscp, dcp, 0, RW_WRITER, cr);
	if (error)
		goto out1;

	if ((cp->c_attr.va_nlink != 1) && (cp->c_backvp == NULL)) {
		/*
		* Must get ref to the back vnode so I/O after
		* remove works.
		*/
		error = cachefs_getbackvp(fscp, &cp->c_metadata.md_cookie, cp);
		if (error) {
			goto out1;
		}
	}

	error = VOP_RMDIR(dcp->c_backvp, nm, cdir, cr);
	if (error)
		goto out1;
	/*
	 * If the mount is a strict consistency mount, there's no need
	 * to do a cachefs_rmdirent(), since the CFSOP_MODIFY op will inval
	 * the cache anyway.
	 */
	if (!C_ISFS_STRICT(fscp) &&
	    (dcp->c_flags & CN_NOCACHE) == 0 &&
	    (dcp->c_metadata.md_flags & MD_POPULATED)) {
		error = cachefs_rmdirent(dcp, nm, cr);
		if (error) {
			cachefs_nocache(dcp);
			error = 0;
		}
	}
	/*
	 * *if* the (hard) link count goes to 0, then we set the CDESTROY
	 * flag on the cnode. The cached object will then be destroyed
	 * at inactive time where the chickens come home to roost :-)
	 * The link cnt for directories is bumped down by 2 'cause the "."
	 * entry has to be elided too ! The link cnt for the parent goes down
	 * by 1 (because of "..").
	 */
	cp->c_attr.va_nlink -= 2;
	dcp->c_attr.va_nlink--;
	if (cp->c_attr.va_nlink == 0) {
		cp->c_flags |= CN_DESTROY;
	} else {
		cp->c_flags |= CN_UPDATED;
	}
	dcp->c_flags |= CN_UPDATED;
	/*
	 * for consistency
	 */
	if (C_ISFS_STRICT(fscp))
		dnlc_purge_vp(dvp);
	dnlc_purge_vp(vp);
	CFSOP_MODIFY_COBJECT(fscp, dcp, cr);

out1:
	rw_exit(&cp->c_statelock);
	rw_exit(&dcp->c_statelock);
	rw_exit(&dcp->c_rwlock);
out:
	if (vp)
		VN_RELE(vp);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_RMDIR)) {
		ino_t fileno = 0;
		fid_t *fidp = NULL;

		if (cp != NULL) {
			fidp = &cp->c_metadata.md_cookie;
			fileno = cp->c_fileno;
		}

		cachefs_log_rmdir(cachep, error, fscp->fs_cfsvfsp,
		    fidp, fileno, cr->cr_uid);
	}
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_rmdir: EXIT error = %d\n", error);
#endif

	return (error);
}

static int
cachefs_symlink(struct vnode *dvp, char *lnm, struct vattr *tva,
	char *tnm, cred_t *cr)
{
	cnode_t *dcp = VTOC(dvp);
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_symlink: ENTER dvp %x tnm %s\n",
		    (int) dvp, tnm);
#endif

	if (fscp->fs_cache->c_flags & CACHE_NOCACHE)
		ASSERT(dcp->c_flags & CN_NOCACHE);

	rw_enter(&dcp->c_rwlock, RW_WRITER);
	rw_enter(&dcp->c_statelock, RW_WRITER);
	error = cachefs_initstate(dcp, RW_WRITER, 1, cr);
	if (error)
		goto out;

	error = CFSOP_CHECK_COBJECT(fscp, dcp, 0, RW_WRITER, cr);
	if (error)
		goto out;
	error = VOP_SYMLINK(dcp->c_backvp, lnm, tva, tnm, cr);
	if (error)
		goto out;
	if (C_ISFS_STRICT(fscp))
		dnlc_purge_vp(dvp);
	if (dcp->c_flags & CN_NOCACHE)
		goto out;
	if ((dcp->c_filegrp->fg_flags & CFS_FG_WRITE) == 0) {
		cachefs_nocache(dcp);
		dnlc_purge_vp(dvp);
		goto out;
	}
	CFSOP_MODIFY_COBJECT(fscp, dcp, cr);
	/*
	 * If this is a strict consistency CFS mount, we need to muck
	 * with the cached directories. They'll be invalidated anyway.
	 */
	if (!C_ISFS_STRICT(fscp) &&
	    (dcp->c_flags & CN_NOCACHE) == 0 &&
	    (dcp->c_metadata.md_flags & MD_POPULATED)) {
		vnode_t *vp = NULL;
		struct vattr attr;
		struct fid cookie;

		(void) VOP_LOOKUP(dcp->c_backvp, lnm, &vp,
				(struct pathname *)NULL, 0,
				(vnode_t *)NULL, cr);
		if (vp != NULL) {
			error = cachefs_getcookie(vp, &cookie, &attr, cr);
			VN_RELE(vp);
			if (error)
				goto out;
			error = cachefs_direnter(dcp, lnm, &cookie, 0,
					attr.va_nodeid, -1, cr, SM_ASYNC);
			if (error) {
				cachefs_nocache(dcp);
				error = 0;
			}
		}
	}

out:
	rw_exit(&dcp->c_statelock);
	rw_exit(&dcp->c_rwlock);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_SYMLINK))
		cachefs_log_symlink(cachep, error, fscp->fs_cfsvfsp,
		    &dcp->c_metadata.md_cookie, dcp->c_fileno,
		    cr->cr_uid, strlen(tnm));

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_symlink: EXIT error = %d\n", error);
#endif
	return (error);
}

static int
cachefs_readdir(struct vnode *vp, register struct uio *uiop, cred_t *cr,
	int *eofp)
{
	cnode_t *dcp = VTOC(vp);
	int error;
	fscache_t *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;
	int type;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_readdir: ENTER vp %x\n", (int) vp);
#endif

	error = 0;
	rw_enter(&dcp->c_rwlock, RW_READER);
	type = RW_READER;
	rw_enter(&dcp->c_statelock, type);
	for (;;) {
		error = CFSOP_CHECK_COBJECT(fscp, dcp, 0, type, cr);
		if (error != EAGAIN)
			break;
		rw_exit(&dcp->c_statelock);
		type = RW_WRITER;
		rw_enter(&dcp->c_statelock, type);
	}
	if (error) {
		rw_exit(&dcp->c_statelock);
		goto out;
	}
	dcp->c_usage++;
again:
	error = 0;
	if (fscp->fs_cache->c_flags & CACHE_NOCACHE)
		ASSERT(dcp->c_flags & CN_NOCACHE);
	if ((dcp->c_flags & CN_NOCACHE) ||
	    (((dcp->c_metadata.md_flags & MD_POPULATED) == 0) &&
	    ((dcp->c_filegrp->fg_flags & CFS_FG_WRITE) == 0))) {
		if (dcp->c_backvp == NULL) {
			if (type != RW_WRITER) {
				if (rw_tryupgrade(&dcp->c_statelock) == 0) {
					rw_exit(&dcp->c_statelock);
					rw_enter(&dcp->c_statelock, RW_WRITER);
				}
				type = RW_WRITER;
			}
			error = cachefs_getbackvp(fscp,
			    &dcp->c_metadata.md_cookie, dcp);
		}
		rw_exit(&dcp->c_statelock);
		if (error == 0)
			error = cachefs_readdir_back(dcp, uiop, cr, eofp);
		if (error == 0)
			fscp->fs_stats.st_misses++;
	} else {
		if (type != RW_WRITER) {
			if (rw_tryupgrade(&dcp->c_statelock) == 0) {
				rw_exit(&dcp->c_statelock);
				rw_enter(&dcp->c_statelock, RW_WRITER);
			}
			type = RW_WRITER;
		}
		if (dcp->c_metadata.md_flags & MD_INVALREADDIR) {
			cachefs_inval_object(dcp, cr);
			dcp->c_flags &= ~CN_NOCACHE;
		}
		if ((dcp->c_metadata.md_flags & MD_POPULATED) == 0) {
			if ((error =
			    cachefs_initstate(dcp, type, 1, cr)) == 0) {
				if ((dcp->c_metadata.md_flags & MD_FILE) &&
				    ((dcp->c_flags & CN_NOCACHE) == 0)) {
					error = cachefs_filldir(dcp, type, cr);
				} else {
					/*
					 * couldn't create frontfile.  Try
					 * the readdir again from the backfs.
					 */
					ASSERT(dcp->c_flags & CN_NOCACHE);
					error = EAGAIN;
				}
			}
		}
		if (error) {
			cachefs_nocache(dcp);
			rw_downgrade(&dcp->c_statelock);
			type = RW_READER;
			goto again;
		}
		ASSERT(dcp->c_metadata.md_flags & MD_POPULATED);
		if (dcp->c_frontvp == NULL) {
			error = cachefs_getfrontfile(dcp,
			    (struct vattr *)NULL, cr);
			if (error) {
				cachefs_nocache(dcp);
				rw_downgrade(&dcp->c_statelock);
				type = RW_READER;
				goto again;
			}
		}
		rw_exit(&dcp->c_statelock);
		error = cachefs_read_dir(dcp, uiop, eofp, cr);
		if (error == 0)
			fscp->fs_stats.st_hits++;
	}
out:
	rw_exit(&dcp->c_rwlock);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_READDIR))
		cachefs_log_readdir(cachep, error, fscp->fs_cfsvfsp,
		&dcp->c_metadata.md_cookie, dcp->c_fileno,
		cr->cr_uid, uiop->uio_loffset, *eofp);
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_readdir: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * do a readdir from the back filesystem
 */
static int
cachefs_readdir_back(struct cnode *dcp, struct uio *uiop, cred_t *cr,
	int *eofp)
{
	int error;

	ASSERT(RW_READ_HELD(&dcp->c_rwlock));
	VOP_RWLOCK(dcp->c_backvp, 0);
	error = VOP_READDIR(dcp->c_backvp, uiop, cr, eofp);
	VOP_RWUNLOCK(dcp->c_backvp, 0);
	return (error);
}

/* ARGSUSED */
static int
cachefs_fid(struct vnode *vp, struct fid *fidp)
{
	int error = 0;
	struct cnode *cp = VTOC(vp);

	rw_enter(&cp->c_statelock, RW_READER);
	if (fidp->fid_len < cp->c_metadata.md_cookie.fid_len) {
		fidp->fid_len = cp->c_metadata.md_cookie.fid_len;
		error = ENOSPC;
	} else {
		bcopy((caddr_t)cp->c_metadata.md_cookie.fid_data,
			(caddr_t)fidp->fid_data,
			cp->c_metadata.md_cookie.fid_len);
		fidp->fid_len = cp->c_metadata.md_cookie.fid_len;
	}
	rw_exit(&cp->c_statelock);
	return (error);
}

static void
cachefs_rwlock(struct vnode *vp, int write_lock)
{
	cnode_t *cp = VTOC(vp);

	/*
	 * XXX - This is ifdef'ed out for now. The problem -
	 * getdents() acquires the read version of rwlock, then we come
	 * into cachefs_readdir() and that wants to acquire the write version
	 * of this lock (if its going to populate the directory). This is
	 * a problem, this can be solved by introducing another lock in the
	 * cnode.
	 */
/* XXX */
	if (vp->v_type != VREG)
		return;
	if (write_lock)
		rw_enter(&cp->c_rwlock, RW_WRITER);
	else
		rw_enter(&cp->c_rwlock, RW_READER);
}

/* ARGSUSED */
static void
cachefs_rwunlock(struct vnode *vp, int write_lock)
{
	cnode_t *cp = VTOC(vp);
	if (vp->v_type != VREG)
		return;
	rw_exit(&cp->c_rwlock);
}

/* ARGSUSED */
static int
cachefs_seek(struct vnode *vp, offset_t ooff, offset_t *noffp)
{
	return (0);
}

int cachefs_lostpage = 0;
/*
 * Return all the pages from [off..off+len) in file
 */
cachefs_getpage(struct vnode *vp, offset_t off, u_int len,
	u_int *protp, struct page *pl[], u_int plsz, struct seg *seg,
	caddr_t addr, enum seg_rw rw, cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	cachefscache_t *cachep = cp->c_filegrp->fg_fscp->fs_cache;
	int error;

#ifdef CFSDEBUG
	u_int offx = (u_int)off;

	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_getpage: ENTER vp %x off %d len %d rw %d\n",
		    (int) vp, offx, len, rw);
#endif
	if (pl == NULL) {
		error = 0;
		goto out;
	}
	if (vp->v_flag & VNOMAP) {
		error = ENOSYS;
		goto out;
	}
	if (protp != NULL)
		*protp = PROT_ALL;

	if (cp->c_cred == NULL) {
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (cp->c_cred == NULL) {
			cp->c_cred = cr;
			crhold(cr);
		}
		rw_exit(&cp->c_statelock);
	}
again:
	/*
	 * If we are getting called as a side effect of an cachefs_write()
	 * operation the local file size might not be extended yet.
	 * In this case we want to be able to return pages of zeroes.
	 */
	if ((u_int)off + len > ((cp->c_size + PAGEOFFSET) & PAGEMASK)) {
		if (seg != segkmap) {
			error = EFAULT;
			goto out;
		}
	}
	if (len <= PAGESIZE)
		error = cachefs_getapage(vp, (u_int)off, len, protp, pl, plsz,
					seg, addr, rw, cr);
	else
		error = pvn_getpages(cachefs_getapage, vp, (u_int)off, len,
				protp, pl, plsz, seg, addr, rw, cr);
	if (((cp->c_flags & CN_NOCACHE) && (error == ENOSPC)) ||
		error == EAGAIN) {
		goto again;
	}
out:
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_GETPAGE))
		cachefs_log_getpage(cachep, error, vp->v_vfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno,
		    cr->cr_uid, off, len);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_getpage: EXIT vp %x error %d\n",
		    (int) vp, error);
#endif
	return (error);
}


int getpage_debug = 0;

/*
 * Called from pvn_getpages or cachefs_getpage to get a particular page.
 */
/*ARGSUSED*/
static int
cachefs_getapage(struct vnode *vp, u_int off, u_int len, u_int *protp,
	struct page *pl[], u_int plsz, struct seg *seg, caddr_t addr,
	enum seg_rw rw, cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	page_t **ppp, *pp = NULL;
	fscache_t *fscp = C_TO_FSCACHE(cp);
	cachefscache_t *cachep = fscp->fs_cache;
	int error = 0;
	struct page **ourpl;
	struct page *ourstackpl[17]; /* see ASSERT() below for 17 */
	int index = 0;
	int downgrade;
	int type;
	int popoff, popsize;

	ASSERT(((DEF_POP_SIZE / PAGESIZE) + 1) <= 17);

	if (fscp->fs_options.opt_popsize > DEF_POP_SIZE)
		ourpl = (struct page **)
			cachefs_kmem_alloc(sizeof (struct page *) *
			    ((fscp->fs_options.opt_popsize / PAGESIZE) + 1),
			    KM_SLEEP);
	else
		ourpl = ourstackpl;

	/*
	 * Grab the readers lock so the cnode state won't change
	 * while we're in here.
	 */
	ourpl[0] = NULL;
	type = RW_READER;
	rw_enter(&cp->c_statelock, RW_READER);
	off = off & PAGEMASK;
again:
	/*
	 * Look for the page
	 */
	if (page_exists(vp, off) == 0) {
		/*
		* Need to do work to get the page.
		* Upgrade our lock because we are going to
		* modify the state of the cnode.
		*/
		if (type == RW_READER) {
			if (rw_tryupgrade(&cp->c_statelock)) {
				type = RW_WRITER;
			} else {
				/*
				* Couldn't upgrade the lock so we'll
				* release our read lock, wait for the
				* writer's lock and try to find the page again.
				*/
				rw_exit(&cp->c_statelock);
				rw_enter(&cp->c_statelock, RW_WRITER);
				type = RW_WRITER;
				goto again;
			}
		}
		/*
		* If we're in NOCACHE mode, we will need a backvp
		*/
		if (cp->c_flags & CN_NOCACHE) {
			if (cp->c_backvp == NULL) {
				error = cachefs_getbackvp(fscp,
					&cp->c_metadata.md_cookie, cp);
				if (error)
					goto out;
			}
			error = VOP_GETPAGE(cp->c_backvp, (offset_t)off,
					PAGESIZE, protp, ourpl, PAGESIZE, seg,
					addr, S_READ, cr);
			if (error)
				goto out;
			goto getpages;
		}
		/*
		* We need a front file. If we can't get it,
		* put the cnode in NOCACHE mode and try again.
		*/
		if (cp->c_frontvp == NULL) {
			error = cachefs_getfrontfile(cp, (struct vattr *)NULL,
					cr);
			if (error) {
				cachefs_nocache(cp);
				error = EAGAIN;
				goto out;
			}
		}
		/*
		* Check if the front file needs population.
		* If population is necessary, make sure we have a
		* backvp as well. We will get the page from the backvp.
		*/
		if (cachefs_check_allocmap(cp, off) == 0) {
			if (cp->c_backvp == NULL) {
				error = cachefs_getbackvp(fscp,
					&cp->c_metadata.md_cookie, cp);
				if (error)
					goto out;
			}
			if (cp->c_filegrp->fg_flags & CFS_FG_WRITE) {
				cachefs_cluster_allocmap(cp, off, &popoff,
				    &popsize,
				    fscp->fs_options.opt_popsize);
				if (popsize != 0) {
					error = cachefs_populate(cp, popoff,
							popsize, cr);
					if (error) {
						cachefs_nocache(cp);
						error = EAGAIN;
						goto out;
					}
					popsize = popsize - (off - popoff);
				} else {
					popsize = PAGESIZE;
				}
			}
			error = VOP_GETPAGE(cp->c_backvp, (offset_t)off,
					PAGESIZE, protp, ourpl, popsize,
					seg, addr, S_READ, cr);
			if (error)
				goto out;
			fscp->fs_stats.st_misses++;
		} else {
			if (cp->c_flags & CN_POPULATION_PENDING) {
				error = VOP_FSYNC(cp->c_frontvp, FSYNC, cr);
				cp->c_flags &= ~CN_POPULATION_PENDING;
				if (error) {
					cachefs_nocache(cp);
					error = EAGAIN;
					goto out;
				}
			}
			/*
			* File was populated so we get the page from the
			* frontvp
			*/
			error = VOP_GETPAGE(cp->c_frontvp, (offset_t)off,
				PAGESIZE, protp, ourpl, PAGESIZE, seg, addr,
				rw, cr);
			if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_GPFRONT))
				cachefs_log_gpfront(cachep, error,
				    fscp->fs_cfsvfsp,
				    &cp->c_metadata.md_cookie, cp->c_fileno,
				    cr->cr_uid, off, PAGESIZE);
			if (error) {
				cachefs_nocache(cp);
				error = EAGAIN;
				goto out;
			}
			fscp->fs_stats.st_hits++;
		}
getpages:
		downgrade = 0;
		for (ppp = ourpl; *ppp; ppp++) {
			if ((*ppp)->p_offset < off) {
				index++;
				page_unlock(*ppp);
				continue;
			}
			if (se_shared_lock(&((*ppp)->p_selock))) {
				if (page_tryupgrade(*ppp) == 0) {
					for (ppp = &ourpl[index]; *ppp; ppp++)
						page_unlock(*ppp);
					error = EAGAIN;
					goto out;
				}
				downgrade = 1;
			}
			ASSERT(se_excl_assert((&(*ppp)->p_selock)));
			if ((*ppp)->p_mapping)
				hat_pageunload((*ppp));
			page_rename(*ppp, vp, (*ppp)->p_offset);
		}
		pl[0] = ourpl[index];
		pl[1] = NULL;
		if (downgrade) {
			page_downgrade(ourpl[index]);
		}
		/* Unlock the rest of the pages from the cluster */
		for (ppp = &ourpl[index+1]; *ppp; ppp++)
			page_unlock(*ppp);
	} else {
		if ((pp = page_lookup(vp, off, SE_SHARED)) == NULL) {
			cachefs_lostpage++;
			goto again;
		}
		pl[0] = pp;
		pl[1] = NULL;
		/* XXX increment st_hits?  i don't think so, but... */
	}

out:
	rw_exit(&cp->c_statelock);
	if (fscp->fs_options.opt_popsize > DEF_POP_SIZE)
		cachefs_kmem_free((caddr_t) ourpl, sizeof (struct page *) *
		    ((fscp->fs_options.opt_popsize / PAGESIZE) + 1));
	return (error);
}


/*
 * Flags are composed of {B_INVAL, B_FREE, B_DONTNEED, B_FORCE}
 * If len == 0, do from off to EOF.
 *
 * The normal cases should be len == 0 & off == 0 (entire vp list),
 * len == MAXBSIZE (from segmap_release actions), and len == PAGESIZE
 * (from pageout).
 */

/*ARGSUSED*/
static int
cachefs_putpage(struct vnode *vp, offset_t off, u_int len,
    int flags, cred_t *cr)
{
	struct cnode *cp  = VTOC(vp);
	struct page *pp;
	u_int io_off, io_len, eoff;
	int error = 0;
	struct fscache *fscp = C_TO_FSCACHE(cp);
	struct cachefscache *cachep = fscp->fs_cache;

	if (len == 0 && (flags & B_INVAL) == 0 &&
			(vp->v_vfsp->vfs_flag & VFS_RDONLY)) {
		return (0); /* XXX goto out, for logging? */
	}
	if (vp->v_pages == NULL || (off >= cp->c_size &&
	    (flags & B_INVAL) == 0))
		return (0); /* XXX goto out, for logging? */

	/*
	 * If this is an async putpage let a thread handle it.
	 */
	if (flags & B_ASYNC) {
		struct cachefs_req *rp;
		int tflags = (flags & ~(B_ASYNC|B_DONTNEED));

		if (ttoproc(curthread) == proc_pageout) {
			/*
			 * If this is the page daemon we
			 * do the push synchronously (Dangerous!) and hope
			 * we can free enough to keep running...
			 */
			flags &= ~B_ASYNC;
			goto again;
		}

		/*
		 * if no flags other than B_ASYNC were set,
		 * we coalesce putpage requests into a single one for the
		 * whole file (len = off = 0).  If such a request is
		 * already queued, we're done.
		 *
		 * If there are other flags set (e.g., B_INVAL), we don't
		 * attempt to coalesce and we use the specified length and
		 * offset.
		 */
		mutex_enter(&cp->c_iomutex);
		if ((cp->c_ioflags & CIO_PUTPAGES) == 0 || tflags != 0) {
			rp = (struct cachefs_req *)
			    cachefs_kmem_zalloc(sizeof (struct cachefs_req),
			    /*LINTED alignment okay*/
			    KM_SLEEP);
			mutex_init(&rp->cfs_req_lock, "CFS Request Mutex",
			    MUTEX_DEFAULT, NULL);
			rp->cfs_cmd = CFS_PUTPAGE;
			rp->cfs_req_u.cu_putpage.cp_vp = vp;
			if (tflags == 0) {
				off = len = 0;
				cp->c_ioflags |= CIO_PUTPAGES;
			}
			rp->cfs_req_u.cu_putpage.cp_off = off;
			rp->cfs_req_u.cu_putpage.cp_len = len;
			rp->cfs_req_u.cu_putpage.cp_flags = flags & ~B_ASYNC;
			rp->cfs_cr = cr;
			crhold(rp->cfs_cr);
			VN_HOLD(vp);
			cp->c_nio++;
			error = cachefs_addqueue(rp,
				&(C_TO_FSCACHE(cp)->fs_workq));
			if (error) {
				error = cachefs_addqueue(rp, &cachep->c_workq);
				ASSERT(error == 0);
			}
		}
		mutex_exit(&cp->c_iomutex);
		return (0);
	}


again:
	if (len == 0) {
		/*
		* Search the entire vp list for pages >= off
		*/
		error = pvn_vplist_dirty(vp, off, cachefspush, flags, cr);
	} else {
		/*
		* Do a range from [off...off + len) looking for pages
		* to deal with.
		*/
		eoff = (u_int)off + len;
		for (io_off = (u_int)off; io_off < eoff && io_off < cp->c_size;
			/*LINTED io_len is set before it's used */
			io_off += io_len) {
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
				error = cachefspush(vp, pp, &io_off, &io_len,
					flags, cr);
				if (error != 0)
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

	if (error == 0 && off == 0 && (len == 0 || len >= cp->c_size)) {
		cp->c_flags &= ~CDIRTY;
	}
	if ((flags & (B_INVAL | B_TRUNC)) == (B_INVAL | B_TRUNC) &&
		len == 0 && off == 0 && vp->v_pages) {
			panic("cachefs_putpage:"
			    " Can't throw away pages vp %x\n",
			    (int) vp);
	}

out:
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_PUTPAGE))
		cachefs_log_putpage(cachep, error, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno,
		    cr->cr_uid, off, len);
	return (error);

}

/*ARGSUSED*/
static int
cachefs_map(struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t *addrp,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	fscache_t *fscp = C_TO_FSCACHE(cp);
	struct segvn_crargs vn_a;
	int error;

#ifdef CFSDEBUG
	u_int offx = (u_int)off;

	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_map: ENTER vp %x off %d len %d flags %d \n",
			(int) vp, offx, len, flags);
#endif
	if (vp->v_flag & VNOMAP) {
		error = ENOSYS;
		goto out;
	}
	if ((int)off < 0 || (int)(off + len) < 0) {
		error = EINVAL;
		goto out;
	}
	if (vp->v_type != VREG) {
		error = ENODEV;
		goto out;
	}

	/*
	 * If file is being locked, disallow mapping.
	 */
	if (vp->v_filocks != NULL) {
		error = EAGAIN;
		goto out;
	}
	if (prot & PROT_WRITE && (flags & MAP_PRIVATE) == 0) {
		if (cp->c_backvp == NULL) {
			rw_enter(&cp->c_statelock, RW_WRITER);
			error = cachefs_getbackvp(fscp,
				&cp->c_metadata.md_cookie, cp);
			rw_exit(&cp->c_statelock);
			if (error) {
				goto out;
			}
		}
		if (C_ISFS_WRITE_AROUND(fscp)) {
			rw_enter(&cp->c_statelock, RW_WRITER);
			cachefs_nocache(cp);
			rw_exit(&cp->c_statelock);
		}
	}
	as_rangelock(as);
	if ((flags & MAP_FIXED) == 0) {
		map_addr(addrp, len, (off_t)off, 1);
		if (*addrp == NULL) {
			as_rangeunlock(as);
			error = ENOMEM;
			goto out;
		}
	} else {
		/*
		* User specified address - blow away any previous mappings
		*/
		(void) as_unmap(as, *addrp, len);
	}


	/*
	 * package up all the data passed in into a segvn_args struct and
	 * call as_map with segvn_create function to create a new segment
	 * in the address space.
	 */
	vn_a.vp = vp;
	vn_a.offset = (u_int)off;
	vn_a.type = flags & MAP_TYPE;
	vn_a.prot = (u_char)prot;
	vn_a.maxprot = (u_char)maxprot;
	vn_a.cred = cr;
	vn_a.amp = NULL;
	vn_a.flags = flags & ~MAP_TYPE;
	error = as_map(as, *addrp, len, segvn_create, (caddr_t)&vn_a);
	as_rangeunlock(as);
out:

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_map: EXIT vp %x error %d\n", (int) vp, error);
#endif
	return (error);
}

/*ARGSUSED*/
static int
cachefs_addmap(struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	cred_t *cr)
{
	return (0);
}

static int
cachefs_cmp(vp1, vp2)
	struct vnode *vp1, *vp2;
{
	return (vp1 == vp2);
}

/* ARGSUSED */
static int
cachefs_frlock(struct vnode *vp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr)
{
	struct cnode *cp = VTOC(vp);
	int error;
	struct fscache *fscp = C_TO_FSCACHE(cp);

	if ((cmd != F_GETLK) && (cmd != F_SETLK) && (cmd != F_SETLKW))
		return (EINVAL);
	if (MANDLOCK(vp, cp->c_attr.va_mode)) {
		return (fs_frlock(vp, cmd, bfp, flag, offset, cr));
	} else {
		if ((cp->c_flags & CN_NOCACHE) == 0) {
			rw_enter(&cp->c_statelock, RW_WRITER);
			cachefs_nocache(cp);
			rw_exit(&cp->c_statelock);
		}
		if (bfp->l_whence == 2) {
			bfp->l_start += cp->c_size;
			bfp->l_whence = 0;
		}
		error = 0;
		rw_enter(&cp->c_statelock, RW_WRITER);
		if (cp->c_backvp == NULL) {
			error = cachefs_getbackvp(fscp,
			    &cp->c_metadata.md_cookie, cp);
		}
		rw_exit(&cp->c_statelock);
		if (error)
			return (error);
		error = VOP_FRLOCK(cp->c_backvp, cmd, bfp, flag, offset, cr);
	}

	/*
	 * If we are setting a lock mark the vnode VNOCACHE so the page
	 * cache does not give inconsistent results on locked files shared
	 * between clients.  The VNOCACHE flag is never turned off as long
	 * as the vnode is active because it is hard to figure out when the
	 * last lock is gone.
	 * XXX - what if some already has the vnode mapped in?
	 */
	if ((error == 0) && (bfp->l_type != F_UNLCK) && (cmd != F_GETLK))
		vp->v_flag |= VNOCACHE;

	return (error);
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
cachefs_space(struct vnode *vp, int cmd, struct flock *bfp, int flag,
	offset_t offset, cred_t *cr)
{
	int error;

	ASSERT(vp->v_type == VREG);
	if (cmd != F_FREESP)
		return (EINVAL);

	if ((error = convoff(vp, bfp, 0, (off_t)offset)) == 0) {
		ASSERT(bfp->l_start >= 0);
		if (bfp->l_len == 0) {
			struct vattr va;

			va.va_size = bfp->l_start;
			va.va_mask = AT_SIZE;
			error = cachefs_setattr(vp, &va, 0, cr);
		} else
			error = EINVAL;
	}

	return (error);
}

/*ARGSUSED*/
static int
cachefs_realvp(struct vnode *vp, struct vnode **vpp)
{
	return (EINVAL);
}

/*ARGSUSED*/
static int
cachefs_delmap(struct vnode *vp, offset_t off, struct as *as,
	caddr_t addr, u_int len, u_int prot, u_int maxprot, u_int flags,
	cred_t *cr)
{
	return (0);
}

/*ARGSUSED*/
static int
cachefs_pageio(struct vnode *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr)
{
	return (ENOSYS);
}

/* pin a file in the cache */
static int
cachefs_pin(struct vnode *vp, cred_t *cr)
{
	struct cnode *cp;
	int error;

	cp = VTOC(vp);

	rw_enter(&cp->c_rwlock, RW_WRITER);
	rw_enter(&cp->c_statelock, RW_WRITER);

	error = cachefs_pin_locked(vp, cr);

	rw_exit(&cp->c_statelock);
	rw_exit(&cp->c_rwlock);

	return (error);
}

/* pin a file in the cache, rwlock and statlock must be locked */
static int
cachefs_pin_locked(struct vnode *vp, cred_t *cr)
{
	struct cnode *cp;
	int error;
	fscache_t *fscp;
	off_t off;

	cp = VTOC(vp);
	fscp = C_TO_FSCACHE(cp);

	if ((cp->c_filegrp->fg_flags & CFS_FG_WRITE) == 0)
		return (EROFS);

	if (cp->c_metadata.md_flags & MD_PINNED)
		return (0);

	if (cp->c_flags & CN_STALE)
		return (0);

	if (cp->c_backvp == NULL) {
		error = cachefs_getbackvp(fscp, &cp->c_metadata.md_cookie, cp);
		if (error)
			return (error);
	}

	if (cp->c_frontvp == NULL) {
		error = cachefs_getfrontfile(cp, (struct vattr *)NULL, cr);
		if (error)
			return (error);
	}

	if (vp->v_type == VDIR) {
		if ((cp->c_metadata.md_flags & MD_POPULATED) == 0) {
			if (error = cachefs_filldir(cp, RW_WRITER, cr))
				return (error);
		}
	} else if (vp->v_type == VREG) {
		/*
		* for a regular file, we just populate it and
		* set the MD_PINNED bit in the metadata.
		*/
		for (off = 0; off < cp->c_attr.va_size; off += MAXBSIZE) {
			if (!cachefs_check_allocmap(cp, off)) {
				int popoff, popsize;

				cachefs_cluster_allocmap(cp, off, &popoff,
				    &popsize, fscp->fs_options.opt_popsize);
				if (popsize != 0) {
					error = cachefs_populate(cp, popoff,
							popsize, cr);
					if (error)
						return (error);
					popsize = popsize - (off - popoff);
				}
			}
		}
	} else {
		return (ENOENT);
	}

	/*
	 * active cnodes are not on the lru list (they will
	 * be put back on at inactive time), so we don't need
	 * to worry about removing it here.
	 */

	/* mark the cnode as pinned */
	cp->c_metadata.md_flags |= MD_PINNED;
	cp->c_flags |= CN_UPDATED;
	(void) cachefs_lru_local(fscp->fs_cache, cp->c_metadata.md_lruno, 1);

	return (0);
}

/* unpin a file in the cache */
/*ARGSUSED*/
static int
cachefs_unpin(struct vnode *vp, cred_t *cr)
{
	struct cnode *cp;
	fscache_t *fscp;

	cp = VTOC(vp);
	fscp = C_TO_FSCACHE(cp);

	if ((cp->c_filegrp->fg_flags & CFS_FG_WRITE) == 0)
		return (EROFS);

	rw_enter(&cp->c_statelock, RW_WRITER);

	if (cp->c_metadata.md_flags & MD_PINNED) {
		cp->c_metadata.md_flags &= ~MD_PINNED;
		cp->c_flags |= CN_UPDATED;

		(void) cachefs_lru_local(fscp->fs_cache,
			cp->c_metadata.md_lruno, 0);
	}

	rw_exit(&cp->c_statelock);

	return (0);
}

static int
cachefs_convert_mount(struct fscache *fscp, struct cachefs_cnvt_mnt *ccmp)
{
	int error;
	struct vnode *vp;
	struct vfs *vfsp;
	int mflag = 0;
	extern struct vfs *cachefs_frontrootvfsp;

	/* First, check that the specified file system is not mounted */
	if (ccmp->cm_op == CFS_CM_FRONT) {
		/*
		 * XXX: c_dirvp could be NULL if we booted off the
		 * disk that did not have a cache!
		 */
		ASSERT(cachefs_frontrootvfsp != NULL);
		vfsp = cachefs_frontrootvfsp;
		if (fscp->fs_cache->c_dirvp)
			ASSERT(cachefs_frontrootvfsp ==
			    fscp->fs_cache->c_dirvp->v_vfsp);
	} else {
		vfsp = fscp->fs_backvfsp;
	}

	if (vfsp->vfs_vnodecovered != NULL)
		return (EINVAL);

	/* First, attempt to lookup the name */
	error = lookupname(ccmp->cm_name, UIO_SYSSPACE, FOLLOW, NULLVPP, &vp);
	if (error)
		return (error);

	/*
	 * So, we now have the correct vfsp as well as the vnode that
	 * we are going to cover.
	 * We fake the mount by performing a subset of the
	 * operations that a normal mount would perform.
	 */

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
	RLOCK_VFSSW();
	if (error = vfs_lock(vfsp)) {
		vn_vfsunlock(vp);
		VN_RELE(vp);
		RUNLOCK_VFSSW();
		return (error);
	}
	dnlc_purge_vp(vp);
	vfs_add(vp, vfsp, mflag);
	vp->v_vfsp->vfs_nsubmounts++;
	vfs_unlock(vfsp);
	vn_vfsunlock(vp);
	RUNLOCK_VFSSW();
	return (0);
}

static int
cachefs_setsecattr(vnode_t *vp, vsecattr_t *vsec, int flag, cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_setsecattr: ENTER vp %x\n", (int) vp);
#endif

	rw_enter(&cp->c_statelock, RW_WRITER);
	if (cp->c_backvp == NULL)
		error = cachefs_getbackvp(C_TO_FSCACHE(cp),
		    &cp->c_metadata.md_cookie, cp);
	rw_exit(&cp->c_statelock);
	ASSERT((error != 0) || (cp->c_backvp != NULL));

	if (error == 0)
		error = VOP_SETSECATTR(cp->c_backvp, vsec, flag, cr);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_setsecattr: EXIT error = %d\n", error);
#endif
	return (error);
}

static int
cachefs_getsecattr(vnode_t *vp, vsecattr_t *vsec, int flag, cred_t *cr)
{
	cnode_t *cp = VTOC(vp);
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_getsecattr: ENTER vp %x\n", (int) vp);
#endif

	rw_enter(&cp->c_statelock, RW_WRITER);
	if (cp->c_backvp == NULL)
		error = cachefs_getbackvp(C_TO_FSCACHE(cp),
		    &cp->c_metadata.md_cookie, cp);
	rw_exit(&cp->c_statelock);
	ASSERT((error != 0) || (cp->c_backvp != NULL));

	if (error == 0)
		error = VOP_GETSECATTR(cp->c_backvp, vsec, flag, cr);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_getsecattr: EXIT error = %d\n", error);
#endif
	return (error);
}
