/*
 * Vnode operations for High Sierra filesystem
 *
 * Copyright (c) 1990 by Sun Microsystem, Inc.
 */

#pragma	ident	"@(#)hsfs_vnops.c	1.46	94/05/18	SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mode.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/pathname.h>
#include <sys/debug.h>
#include <sys/vmsystm.h>
#include <sys/cmn_err.h>

#include <sys/dirent.h>
#include <sys/errno.h>

#include <vm/hat.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_kmem.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <vm/page.h>
#include <sys/swap.h>

#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/hsfs_spec.h>
#include <sys/fs/hsfs_node.h>
#include <sys/fs/hsfs_impl.h>
#include <sys/fs/hsfs_susp.h>
#include <sys/fs/hsfs_rrip.h>

#include <fs/fs_subr.h>

#define	ISVDEV(t) ((t) == VCHR || (t) == VBLK || (t) == VFIFO)

#ifdef __STDC__
static struct buf *hsfs_startio(struct vnode *devvp, daddr_t bn, page_t *pp,
	u_int len, u_int pgoff, int flags, int io);
#else
static struct buf *hsfs_startio();
#endif

/* ARGSUSED */
static int
hsfs_fsync(
	vnode_t *cp,
	int syncflag,
	cred_t *cred
)
{
	return (0);
}


/*ARGSUSED*/
static int
hsfs_read(vp, uiop, ioflag, cred)
	struct vnode *vp;
	struct uio *uiop;
	int ioflag;
	struct cred *cred;
{
	struct hsnode *hp;
	register u_int off;
	register int mapon, on;
	register caddr_t base;
	u_int	filesize;
	int nbytes, n;
	u_int flags;
	int error;

	hp = VTOH(vp);
	/*
	 * if vp is of type VDIR, make sure dirent
	 * is filled up with all info (because of ptbl)
	 */
	if (vp->v_type == VDIR) {
		if (hp->hs_dirent.ext_size == 0)
			hs_filldirent(vp, &hp->hs_dirent);
	}
	filesize = hp->hs_dirent.ext_size;

	if (uiop->uio_offset >= filesize) {
		error = 0;
		goto out;
	}

	do {
		/* map file to correct page boundary */
		off = uiop->uio_offset & MAXBMASK;
		mapon = uiop->uio_offset & MAXBOFFSET;

		/* set read in data size */
		on = (uiop->uio_offset) & PAGEOFFSET;
		nbytes = MIN(PAGESIZE - on, uiop->uio_resid);
		/* adjust down if > EOF */
		n = MIN((filesize - uiop->uio_offset), nbytes);
		if (n == 0) {
			error = 0;
			goto out;
		}

		/* map the file into memory */
		base = segmap_getmap(segkmap, vp, off);
		error = uiomove(base+mapon, (long)n, UIO_READ, uiop);
		if (error == 0) {
		/*
		 * if read a whole block, or read to eof,
		 *  won't need this buffer again soon.
		 */
			if (n + on == PAGESIZE ||
			    uiop->uio_offset == filesize)
				flags = SM_DONTNEED;
			else
				flags = 0;
			error = segmap_release(segkmap, base, flags);
		} else
			(void) segmap_release(segkmap, base, 0);

	} while (error == 0 && uiop->uio_resid > 0);

out:
	return (error);
}

/*ARGSUSED*/
static int
hsfs_getattr(vp, vap, flags, cred)
	struct vnode *vp;
	struct vattr *vap;
	int flags;
	struct cred *cred;
{
	register struct hsnode *hp;
	register struct vfs *vfsp;
	struct hsfs *fsp;

	hp = VTOH(vp);
	fsp = VFS_TO_HSFS(vp->v_vfsp);
	vfsp = vp->v_vfsp;

	if ((hp->hs_dirent.ext_size == 0) && (vp->v_type == VDIR)) {
		hs_filldirent(vp, &hp->hs_dirent);
	}
	vap->va_type = IFTOVT(hp->hs_dirent.mode);
	vap->va_mode = hp->hs_dirent.mode;
	vap->va_uid = hp->hs_dirent.uid;
	vap->va_gid = hp->hs_dirent.gid;

	vap->va_fsid = vfsp->vfs_fsid.val[0];
	vap->va_nodeid = hp->hs_nodeid;
	vap->va_nlink = hp->hs_dirent.nlink;
	vap->va_size0 =	 0;
	vap->va_size =	hp->hs_dirent.ext_size;

	vap->va_atime.tv_sec = hp->hs_dirent.adate.tv_sec;
	vap->va_atime.tv_nsec = hp->hs_dirent.adate.tv_usec*1000;
	vap->va_mtime.tv_sec = hp->hs_dirent.mdate.tv_sec;
	vap->va_mtime.tv_nsec = hp->hs_dirent.mdate.tv_usec*1000;
	vap->va_ctime.tv_sec = hp->hs_dirent.cdate.tv_sec;
	vap->va_ctime.tv_nsec = hp->hs_dirent.cdate.tv_usec*1000;
	if (vp->v_type == VCHR || vp->v_type == VBLK)
		vap->va_rdev = hp->hs_dirent.r_dev;
	else
		vap->va_rdev = 0;
	vap->va_blksize = vfsp->vfs_bsize;
	/* no. of blocks = no. of data blocks + no. of xar blocks */
	vap->va_nblocks = howmany(vap->va_size +
		(hp->hs_dirent.xar_len << fsp->hsfs_vol.lbn_shift), DEV_BSIZE);
	vap->va_vcode = hp->hs_vcode;
	return (0);
}

/*ARGSUSED*/
static int
hsfs_readlink(vp, uiop, cred)
	struct vnode *vp;
	struct uio *uiop;
	struct cred *cred;
{
	register int error;
	struct hsnode *hp;

	if (vp->v_type != VLNK)
		return (EINVAL);

	hp = VTOH(vp);

	if (hp->hs_dirent.sym_link == (char *)NULL) {
		mutex_exit(&hp->hs_contents_lock);
		return (ENOENT);
	}

	error = uiomove((caddr_t)hp->hs_dirent.sym_link,
			(int)MIN(hp->hs_dirent.ext_size,
			uiop->uio_resid), UIO_READ, uiop);

	return (error);
}

/*ARGSUSED*/
static void
hsfs_inactive(vp, cred)
	struct vnode *vp;
	struct cred *cred;
{
	register struct hsnode *hp;
	register struct hstable *htp;

	int nopage;

	hp = VTOH(vp);
	htp = ((struct hsfs *)VFS_TO_HSFS(vp->v_vfsp))->hsfs_hstbl;
	/*
	 * Note: acquiring and holding v_lock for quite a while
	 * here serializes on the vnode; this is unfortunate, but
	 * likely not to overly impact performance, as the underlying
	 * device (CDROM drive) is quite slow.
	 */
	rw_enter(&htp->hshash_lock, RW_WRITER);
	mutex_enter(&hp->hs_contents_lock);
	mutex_enter(&vp->v_lock);

	if (vp->v_count < 1)
		cmn_err(CE_PANIC, "hsfs_inactive: v_count <1\n");

	if (vp->v_count > 1 || (hp->hs_flags & HREF) == 0) {
		vp->v_count--;	/* release hold from vn_rele */
		mutex_exit(&vp->v_lock);
		mutex_exit(&hp->hs_contents_lock);
		rw_exit(&htp->hshash_lock);
		return;
	}
	vp->v_count--;	/* release hold from vn_rele */
	if (vp->v_count == 0) {
		/* proceed to free the hsnode */

		/*
		 * if there is no pages associated with the
		 * hsnode, put at front of the free queue,
		 * else put at the end
		 */
		nopage = vp->v_pages == NULL? 1 : 0;
		hs_freenode(VTOH(vp), vp->v_vfsp, nopage);
		hp->hs_flags = 0;
	}
	mutex_exit(&vp->v_lock);
	mutex_exit(&hp->hs_contents_lock);
	rw_exit(&htp->hshash_lock);
}


/*ARGSUSED*/
static int
hsfs_lookup(dvp, nm, vpp, pnp, flags, rdir, cred)
	struct vnode *dvp;
	char *nm;
	struct vnode **vpp;
	struct pathname *pnp;
	int flags;
	struct vnode *rdir;
	struct cred *cred;
{
	int namelen;

	namelen = strlen(nm);

	/*
	 * If we're looking for ourself, life is simple.
	 */
	if ((namelen == 1) && (*nm == '.')) {
		VN_HOLD(dvp);
		*vpp = dvp;
		return (0);
	}

	return (hs_dirlook(dvp, nm, namelen, vpp, cred));
}


/*ARGSUSED*/
static int
hsfs_readdir(vp, uiop, cred, eofp)
	struct vnode	*vp;
	struct uio	*uiop;
	struct cred	*cred;
	int		*eofp;
{
	struct hsnode	*dhp;
	struct hsfs	*fsp;
	struct hs_direntry hd;
	register struct dirent *nd;
	int		error;
	register u_int	offset;		/* real offset in directory */
	u_int		dirsiz;		/* real size of directory */
	u_char		*blkp;
	register int	hdlen;		/* length of hs directory entry */
	register int	ndlen;		/* length of dirent entry */
	int		bytes_wanted;
	int		bufsize;	/* size of dirent buffer */
	register char	*outbuf;	/* ptr to dirent buffer */
	char		*dname;
	int		dnamelen;
	size_t		dname_size;
	int		direntsz;
	struct fbuf	*fbp;
	u_int		last_offset;	/* last index into current dir block */
	u_long		dir_lbn;	/* lbn of directory */

	dhp = VTOH(vp);
	fsp = VFS_TO_HSFS(vp->v_vfsp);
	if (dhp->hs_dirent.ext_size == 0)
		hs_filldirent(vp, &dhp->hs_dirent);
	dirsiz = dhp->hs_dirent.ext_size;
	dir_lbn = dhp->hs_dirent.ext_lbn;
	offset = uiop->uio_offset;
	if (offset >= dirsiz) {			/* at or beyond EOF */
		if (eofp)
			*eofp = 1;
		return (0);
	}

	dname_size = fsp->hsfs_namemax + 1;	/* 1 for the ending NUL */
	dname = (char *)kmem_alloc(dname_size, KM_SLEEP);
	bufsize = uiop->uio_resid + sizeof (struct dirent);
	outbuf = (char *)kmem_alloc((u_int) bufsize, KM_SLEEP);
	nd = (struct dirent *)outbuf;
	direntsz = (char *)nd->d_name - (char *)nd;

	while (offset < dirsiz) {
		if ((offset & MAXBMASK) + MAXBSIZE > dirsiz)
			bytes_wanted = dirsiz - (offset & MAXBMASK);
		else
			bytes_wanted = MAXBSIZE;

		error = fbread(vp, (off_t)offset & MAXBMASK,
			(unsigned int)bytes_wanted, S_READ, &fbp);
		if (error)
			goto done;

		blkp = (u_char *) fbp->fb_addr;
		last_offset = (offset & MAXBMASK) + fbp->fb_count - 1;

#define	rel_offset(offset) ((offset) & MAXBOFFSET)	/* index into blkp */

		while (offset < last_offset) {
			/*
			 * Directory Entries cannot span sectors.
			 * Unused bytes at the end of each sector are zeroed.
			 * Therefore, detect this condition when the size
			 * field of the directory entry is zero.
			 */
			hdlen = (int)((u_char)
				HDE_DIR_LEN(&blkp[rel_offset(offset)]));
			if (hdlen == 0) {
				/* advance to next sector boundary */
				offset = (offset & MAXHSMASK) + HS_SECTOR_SIZE;

				/*
				 * Have we reached the end of current block?
				 */
				if (offset > last_offset)
					break;
				else
					continue;
			}

			/* make sure this is nullified before  reading it */
			bzero((caddr_t)&hd, sizeof (hd));

			/*
			 * Just ignore invalid directory entries.
			 * XXX - maybe hs_parsedir() will detect EXISTENCE bit
			 */
			if (!hs_parsedir(fsp, &blkp[rel_offset(offset)],
				&hd, dname, &dnamelen)) {
				/*
				 * Determine if there is enough room
				 */
				ndlen = (direntsz + dnamelen + 1 + (NBPW-1))
					& ~(NBPW-1);
				if ((ndlen + (char *)nd -
					outbuf) > uiop->uio_resid) {
					fbrelse(fbp, S_READ);
					goto done; /* output buffer full */
				}
				nd->d_off = offset + hdlen;
				/*
				 * Generate nodeid.
				 * If a directory, nodeid points to the
				 * canonical dirent describing the directory:
				 * the dirent of the "." entry for the
				 * directory, which is pointed to by all
				 * dirents for that directory.
				 * Otherwise, nodeid points to dirent of file.
				 */
				if (hd.type == VDIR) {
					nd->d_ino = MAKE_NODEID(hd.ext_lbn, 0,
					    vp->v_vfsp);
				} else {
					struct hs_volume *hvp;
					u_long lbn, off;

					/*
					 * Normalize lbn and off
					 */
					hvp = &fsp->hsfs_vol;
					lbn = dir_lbn +
					    (offset >> hvp->lbn_shift);
					off = offset & hvp->lbn_maxoffset;
					nd->d_ino = MAKE_NODEID(lbn,
					    off, vp->v_vfsp);
				}
				strcpy(nd->d_name, dname);
				nd->d_reclen = (u_short)ndlen;
				nd = (struct dirent *)((char *)nd + ndlen);

				/*
				 * free up space allocated for symlink
				 */
				if (hd.sym_link != (char *)NULL) {
					kmem_free(hd.sym_link, hd.ext_size+1);
					hd.sym_link = (char *)NULL;
				}
			}

			offset += hdlen;
		}
		fbrelse(fbp, S_READ);
	}

	/*
	 * Got here for one of the following reasons:
	 *	1) outbuf is full (error == 0)
	 *	2) end of directory reached (error == 0)
	 *	3) error reading directory sector (error != 0)
	 *	4) directory entry crosses sector boundary (error == 0)
	 *
	 * If any directory entries have been copied, don't report
	 * case 4.  Instead, return the valid directory entries.
	 *
	 * If no entries have been copied, report the error.
	 * If case 4, this will be indistiguishable from EOF.
	 */
done:
	ndlen = ((char *)nd - outbuf);
	if (ndlen != 0) {
		error = uiomove(outbuf, (long)ndlen, UIO_READ, uiop);
		uiop->uio_offset = offset;
	}
	kmem_free(dname, dname_size);
	kmem_free(outbuf, (u_int) bufsize);
	if (eofp && error == 0)
		*eofp = (uiop->uio_offset >= dirsiz);
	return (error);
}

static int
hsfs_fid(vp, fidp)
	struct vnode *vp;
	struct fid *fidp;
{
	register struct hsnode *hp;
	register struct hsfid *fid;

	if (fidp->fid_len < (sizeof (*fid) - sizeof (fid->hf_len))) {
		fidp->fid_len = sizeof (*fid) - sizeof (fid->hf_len);
		return (ENOSPC);
	}

	fid = (struct hsfid *)fidp;
	fid->hf_len = sizeof (*fid) - sizeof (fid->hf_len);
	hp = VTOH(vp);
	mutex_enter(&hp->hs_contents_lock);
	fid->hf_dir_lbn = hp->hs_dir_lbn;
	fid->hf_dir_off = (u_short) hp->hs_dir_off;
	mutex_exit(&hp->hs_contents_lock);
	return (0);
}

/*ARGSUSED*/
static int
hsfs_open(vpp, flag, cred)
	struct vnode **vpp;
	int flag;
	struct cred *cred;
{

	return (0);
}

/*ARGSUSED*/
static int
hsfs_close(vp, flag, count, offset, cred)
	struct vnode *vp;
	int flag;
	int count;
	offset_t offset;
	struct cred *cred;
{
	cleanlocks(vp, ttoproc(curthread)->p_pid, 0);
	return (0);
}

/*ARGSUSED*/
static int
hsfs_access(vp, mode, flags, cred)
	struct vnode *vp;
	int mode;
	int flags;
	struct cred *cred;
{

	return (hs_access(vp, (mode_t)mode, cred));
}

/*
 * the seek time of a CD-ROM is very slow, and data transfer
 * rate is even worse (max. 150K per sec).  The design
 * decision is to reduce access to cd-rom as much as possible,
 * and to transfer a sizable block (read-ahead) of data at a time.
 * UFS style of read ahead one block at a time is not appropriate,
 * and is not supported
 */

/*
 * KLUTSIZE should be a multiple of PAGESIZE and <= MAXPHYS.
 */
#define	KLUSTSIZE	(56 * 1024)
/* we don't support read ahead */
int hsfs_lostpage;	/* no. of times we lost original page */

/*ARGSUSED*/
hsfs_getapage(vp, off, len, protp, pl, plsz, seg, addr, rw, cred)
	struct vnode *vp;
	register u_int off;
	u_int len;
	u_int *protp;
	struct page *pl[];
	u_int plsz;
	struct seg *seg;
	caddr_t addr;
	enum seg_rw rw;
	struct cred *cred;
{
	struct hsnode *hp;
	struct hsfs *fsp;
	int err;
	struct buf *bp, *bp2, *bp3, *bp4;
	struct page *pp;
	int	pagefound;
	u_int	bof;
	struct vnode *devvp;
	u_int	blkoff;
	int	blksz;
	u_int	io_off, io_len, io_len1;
	u_int	xlen;
	int	filsiz;
	int	klustsz;
	int	intlf;
	int	intlfsz = 0;
	int	nio;
	int	xarsiz;
	int	leftover;
	daddr_t blkno;

	/*
	 * XXX since we read in 7 pages at a time, should first check
	 * if the page is already in.  Will do in next time
	 */
	hp = VTOH(vp);
	fsp = VFS_TO_HSFS(vp->v_vfsp);
	devvp = fsp->hsfs_devvp;

	/* file data size */
	filsiz = hp->hs_dirent.ext_size;
	/* disk addr for start of file */
	bof = LBN_TO_BYTE(hp->hs_dirent.ext_lbn, vp->v_vfsp);
	/* xarsiz byte must be skipped for data */
	xarsiz = hp->hs_dirent.xar_len << fsp->hsfs_vol.lbn_shift;

	/* klustsz is a multiple of 8K */
	/* but in interleaving, klustsz is limited to interleaving size */
	/* to avoid the complexity of releasing unused "skip" data blocks */
	/* This may have serious performance implications */
	/* compute klutze size */
	if ((intlf = hp->hs_dirent.intlf_sz + hp->hs_dirent.intlf_sk) != 0) {
		/* interleaving */
		/* convert total (sz+sk) into bytes */
		intlf = LBN_TO_BYTE(intlf, vp->v_vfsp);
		/* convert interleaving size into bytes */
		intlfsz = LBN_TO_BYTE(hp->hs_dirent.intlf_sz, vp->v_vfsp);
		/* read in only the interleaving size */
		klustsz = roundup(MIN(intlfsz, KLUSTSIZE), PAGESIZE);
	}
	/* no interleaving, set default klutsize */
	else
		klustsz = KLUSTSIZE;

reread:
	err = 0;
	bp = bp2 = bp3 = bp4 = NULL;
	pagefound = 0;
	nio = 0;
	if (pl != NULL)
		pl[0] = NULL;

/* convert file relative offset into absolute byte  offset */
again:
	/* search page in buffer */
	if ((pagefound = page_exists(vp, off)) == 0) {
		/*
		 * Need to really do disk IO to get the page.
		 */
		blkoff = (off / klustsz) * klustsz;

		if (blkoff + klustsz <= filsiz)
			blksz = klustsz;
		else
#define	BUG_1152652	/* real fix may be to the cdrom driver */
#if defined(BUG_1152652) && defined(i386)
			blksz = roundup((filsiz - blkoff), HS_SECTOR_SIZE);
#else
			blksz = roundup((filsiz - blkoff), DEV_BSIZE);
#endif

		/*
		 * try to limit amount of data read in to klutsz
		 * or interleaving size or remaining unread sector
		 * from last read
		 */
		if (intlfsz) blksz = MIN(blksz, (intlfsz-(blkoff%intlfsz)));

		pp = pvn_read_kluster(vp, off, seg, addr, &io_off, &io_len1,
			blkoff, (u_int) blksz, 0);

		if (pp == NULL)
			goto again;

		/* compute disk address for interleving */
		/* diskoff = (off / sz) * (sk + sz) + (off % sz)  */
		blkno = (daddr_t)(intlf ? btodb(bof+	(xarsiz+io_off) /
			intlfsz * intlf + ((xarsiz+io_off) % intlfsz))
				: btodb(bof + xarsiz + io_off));

		/*
		 * Zero part of page which we are not
		 * going to be reading from disk now.
		 */
		xlen = io_len1 & PAGEOFFSET;
		if (xlen != 0) {
			pagezero(pp->p_prev, xlen, PAGESIZE - xlen);
			/*
			 * xlen must be zero for non-interleaving file
			 * except reading the last sector
			 */
			if ((leftover = filsiz-(io_off+io_len1)) > 0) {
				if (intlf) {
					/*
					 * find out how many reads needed to
					 * fill a page, be careful of eof
					 */
					leftover = MIN(PAGESIZE-xlen, leftover);
					nio = howmany(leftover, intlfsz);
					/*
					 * at most three io needed
					 * to fill a 8K page from 2K sector
					 */
					if (nio > 3)
						cmn_err(CE_PANIC, "bad nio\n");
				} else {
					/*
					 * our policy is to allocate
					 * a klutsz of multiple of 8K
					 * except last read
					 */
					cmn_err(CE_PANIC, "bad klustsz\n");
				}
			} else
				nio = 0;
		} else
			nio = 0;

		bp = hsfs_startio(devvp, blkno, pp, io_len1, 0,
		    pl == NULL ? (B_ASYNC | B_READ) : B_READ, (seg == segkmap));

		/* for interleaving only */
		/*
		 * since interleaving size may not be multiple of PAGESIZE,
		 * we have to fill up the remaining page area with data
		 * from another interleaving data area in the CD-ROM
		 */
		if (!nio)
			goto intlf_done;

		/* xlen is the amount of data in a page */
		/* xlen is less than PAGESIZE */
		io_off = io_off + xlen; /* compute new io offset */
		io_len = MIN((PAGESIZE - xlen), intlfsz);
		/* adjust for eof */
		io_len = MIN(io_len, filsiz-io_off);

		blkno = btodb(bof+(xarsiz+io_off) /
			intlfsz * intlf + ((xarsiz+io_off) % intlfsz));

		xlen = (io_off + io_len) & PAGEOFFSET;
		if (xlen != 0) {
			pagezero(pp->p_prev, xlen, PAGESIZE - xlen);
		}

		bp2 = hsfs_startio(devvp, blkno, pp, io_len, 0,
		    pl == NULL ? (B_ASYNC | B_READ) : B_READ, (seg == segkmap));

		if (nio == 1)
			goto intlf_done;
		io_off = io_off + io_len; /* compute new io offset */
		io_len = MIN((PAGESIZE - xlen), intlfsz);
		/* adjust for eof */
		io_len = MIN(io_len, filsiz-io_off);

		blkno = btodb(bof+(xarsiz+io_off) /
			intlfsz * intlf + ((xarsiz+io_off) % intlfsz));

		xlen = (io_off + io_len) & PAGEOFFSET;
		if (xlen != 0) {
			pagezero(pp->p_prev, xlen, PAGESIZE - xlen);
		}
		bp3 = hsfs_startio(devvp, blkno, pp, io_len, 0,
		    pl == NULL ? (B_ASYNC | B_READ) : B_READ, (seg == segkmap));

		if (nio == 2) goto intlf_done;

		io_off = io_off + io_len; /* compute new io offset */
		io_len = MIN((PAGESIZE - xlen), intlfsz);
		/* adjust for eof */
		io_len = MIN(io_len, filsiz-io_off);

		blkno = btodb(bof+(xarsiz+io_off) /
			intlfsz * intlf + ((xarsiz+io_off) % intlfsz));

		xlen = (io_off + io_len) & PAGEOFFSET;
		if (xlen != 0) {
			pagezero(pp->p_prev, xlen, PAGESIZE - xlen);
		}
		bp4 = hsfs_startio(devvp, blkno, pp, io_len, 0,
		    pl == NULL ? (B_ASYNC | B_READ) : B_READ, (seg == segkmap));
	}

intlf_done:

	if (pl == NULL)
		return (err);

	if (bp != NULL) {
		if (err == 0)
			err = biowait(bp);
		else
			(void) biowait(bp);

		(void) pageio_done(bp);

		if (nio == 0) goto intlf_iodone;
		/* second read for page (nio == 1) */
		if (bp2 != NULL) {
			if (err == 0)
				err = biowait(bp2);
			else
				(void) biowait(bp2);
			(void) pageio_done(bp2);
		}
		if (nio == 1) goto intlf_iodone;

		/* third read for page (nio == 2) */
		if (bp3 != NULL) {
			if (err == 0)
				err = biowait(bp3);
			else
				(void) biowait(bp3);
			(void) pageio_done(bp3);
		}
		if (nio == 2) goto intlf_iodone;

		/* fourth read for page (nio == 3) */
		if (bp4 != NULL) {
			if (err == 0)
				err = biowait(bp4);
			else
				(void) biowait(bp4);
			(void) pageio_done(bp4);
		}
	}

intlf_iodone:
	if (err) {
		if (pl != NULL)
			pvn_read_done(pp, B_ERROR);
		return (err);
	}

	if (pagefound) {
		int index;
		u_int soff;

		if ((pp = page_lookup(vp, off, SE_SHARED)) == NULL) {
			hsfs_lostpage++;
			goto reread;
		}
		pl[0] = pp;
		index = 1;

		/*
		 * Try to lock the next page if it exists without
		 * blocking.
		 */
		plsz -= PAGESIZE;
/* LINTED (plsz is unsigned) */
		for (soff = off + PAGESIZE; plsz > 0;
		    soff += PAGESIZE, plsz -= PAGESIZE) {
			pp = page_lookup_nowait(vp, soff, SE_SHARED);
			if (pp == NULL)
				break;
			pl[index++] = pp;
		}
		pl[index] = NULL;
		return (0);
	}

	if (pp != NULL) {
		pvn_plist_init(pp, pl, plsz, off, io_len1, rw);
	}

	return (err);

}

static int
hsfs_getpage(vp, off, len, protp, pl, plsz, seg, addr, rw, cred)
	struct vnode *vp;
	offset_t off;
	register u_int len;
	u_int *protp;
	struct page *pl[];
	u_int plsz;
	struct seg *seg;
	caddr_t addr;
	enum seg_rw rw;
	struct cred *cred;
{
	int err;
	int filsiz;
	struct hsnode *hp = VTOH(vp);

	/* does not support write */
	if (rw == S_WRITE) {
		cmn_err(CE_PANIC, "write attempt on READ ONLY HSFS");
	}

	if (vp->v_flag & VNOMAP) {
		return (ENOSYS);
	}

	/*
	 * Determine file data size for EOF check.
	 */
	filsiz = hp->hs_dirent.ext_size;
	if ((u_int)off + len > filsiz + PAGEOFFSET && seg != segkmap)
		return (EFAULT);	/* beyond EOF */

	if (protp != NULL)
		*protp = PROT_ALL;

	if (len <= PAGESIZE)
		err = hsfs_getapage(vp, (u_int)off, len, protp, pl, plsz,
		    seg, addr, rw, cred);
	else
		err = pvn_getpages(hsfs_getapage, vp, (u_int)off, len, protp,
		    pl, plsz, seg, addr, rw, cred);

	return (err);
}



/*
 * This function should never be called. We need to have it to pass
 * it as an argument to other functions.
 */
/*ARGSUSED*/
int
hsfs_putapage(vp, pp, offp, lenp, flags, cr)
	struct vnode	*vp;
	page_t		*pp;
	u_int		*offp;
	u_int		*lenp;
	int		flags;
	struct cred	*cr;
{
	cmn_err(CE_NOTE, "hsfs_putapage: dirty HSFS page\n");
	pvn_write_done(pp, B_ERROR | B_WRITE | flags);
	return (0);
}


/*
 * The only flags we support are B_INVAL, B_FREE and B_DONTNEED.
 * B_INVAL is set by:
 *
 *	1) the MC_SYNC command of memcntl(2) to support the MS_INVALIDATE flag.
 *	2) the MC_ADVISE command of memcntl(2) with the MADV_DONTNEED advice
 *	   which translates to an MC_SYNC with the MS_INVALIDATE flag.
 *
 * The B_FREE (as well as the B_DONTNEED) flag is set when the
 * MADV_SEQUENTIAL advice has been used. VOP_PUTPAGE is invoked
 * from SEGVN to release pages behind a pagefault.
 */
/*ARGSUSED*/
static int
hsfs_putpage(vp, off, len, flags, cr)
	struct vnode	*vp;
	offset_t	off;
	u_int		len;
	int		flags;
	struct cred	*cr;
{
	int error = 0;

	if (vp->v_count == 0)
		cmn_err(CE_PANIC, "hsfs_putpage: bad v_count\n");

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	if (vp->v_pages == NULL)	/* no pages mapped */
		return (0);

	if (len == 0)		/* from 'off' to EOF */
		error = pvn_vplist_dirty(vp, (u_int)off,
					hsfs_putapage, flags, cr);
	else {
		offset_t end_off = off + len;
		offset_t file_size = VTOH(vp)->hs_dirent.ext_size;
		offset_t io_off;

		file_size = (file_size + PAGESIZE - 1) & PAGEMASK;
		if (end_off > file_size)
			end_off = file_size;

		for (io_off = off; io_off < end_off; io_off += PAGESIZE) {
			page_t *pp;

			/*
			 * We insist on getting the page only if we are
			 * about to invalidate, free or write it and
			 * the B_ASYNC flag is not set.
			 */
			if ((flags & B_INVAL) || ((flags & B_ASYNC) == 0)) {
				pp = page_lookup(vp, (u_int)io_off,
					(flags & (B_INVAL | B_FREE)) ?
					    SE_EXCL : SE_SHARED);
			} else {
				pp = page_lookup_nowait(vp, (u_int)io_off,
					(flags & B_FREE) ? SE_EXCL : SE_SHARED);
			}

			if (pp == NULL)
				continue;
			/*
			 * Normally pvn_getdirty() should return 0, which
			 * impies that it has done the job for us.
			 * The shouldn't-happen scenario is when it returns 1.
			 * This means that the page has been modified and
			 * needs to be put back.
			 * Since we can't write on a CD, we fake a failed
			 * I/O and let pvn_write_done() destroy the page.
			 */
			if (pvn_getdirty(pp, flags) == 1) {
				cmn_err(CE_NOTE,
					"hsfs_putpage: dirty HSFS page\n");
				pvn_write_done(pp, B_ERROR | B_WRITE | flags);
			}
		}
	}
	return (error);
}


/*ARGSUSED*/
static int
hsfs_map(
	struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t *addrp,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	struct cred *cred
)
{
	struct segvn_crargs vn_a;
	int error;

	/* VFS_RECORD(vp->v_vfsp, VS_MAP, VS_CALL); */

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	if ((int)off < 0 || (int)(off + len) < 0)
		return (EINVAL);

	if (vp->v_type != VREG) {
		return (ENODEV);
	}

	/*
	 * If file is being locked, disallow mapping.
	 */
	if (vp->v_filocks != NULL && MANDLOCK(vp, VTOI(vp)->i_mode))
		return (EAGAIN);

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
	vn_a.cred = cred;
	vn_a.amp = NULL;

	error = as_map(as, *addrp, len, segvn_create, (caddr_t)&vn_a);
	as_rangeunlock(as);
	return (error);
}

/* ARGSUSED */
static int
hsfs_addmap(
	struct vnode *vp,
	offset_t off,
	struct as *as,
	caddr_t addr,
	u_int len,
	u_char prot,
	u_char maxprot,
	u_int flags,
	struct cred *cr
)
{
	register struct hsnode *hp;

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	hp = VTOH(vp);
	mutex_enter(&hp->hs_contents_lock);
	hp->hs_mapcnt += btopr(len);
	mutex_exit(&hp->hs_contents_lock);
	return (0);
}

/*ARGSUSED*/
static int
hsfs_delmap(vp, off, as, addr, len, prot, maxprot, flags, cr)
	struct vnode *vp;
	offset_t off;
	struct as *as;
	caddr_t addr;
	u_int len;
	u_int prot, maxprot;
	u_int flags;
	struct cred *cr;
{
	register struct hsnode *hp;

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	hp = VTOH(vp);
	mutex_enter(&hp->hs_contents_lock);
	hp->hs_mapcnt -= btopr(len);	/* Count released mappings */
	ASSERT(hp->hs_mapcnt >= 0);
	mutex_exit(&hp->hs_contents_lock);
	return (0);
}

/* ARGSUSED */
static int
hsfs_seek(vp, ooff, noffp)
	struct vnode *vp;
	offset_t ooff;
	offset_t *noffp;
{
	return ((*noffp < 0 || *noffp > MAXOFF_T) ? EINVAL : 0);
}


/*
 * Flags are composed of {B_ASYNC, B_INVAL, B_FREE, B_DONTNEED}
 */
/* ARGSUSED6 */
static struct buf *
hsfs_startio(devvp, bn, pp, len, pgoff, flags, io)
	register struct vnode *devvp;
	daddr_t bn;
	page_t *pp;
	u_int len;
	u_int pgoff;
	int flags;
	int io;
{
	register struct buf *bp;

	bp = pageio_setup(pp, len, devvp, flags);
	ASSERT(bp != NULL);

	bp->b_edev = devvp->v_rdev;
	bp->b_dev = cmpdev(devvp->v_rdev);
	bp->b_blkno = bn;
	bp->b_un.b_addr = (caddr_t)pgoff;

	bdev_strategy(bp);
	return (bp);
}

struct vnodeops hsfs_vnodeops = {
	hsfs_open,
	hsfs_close,
	hsfs_read,
	fs_nosys,	/* write */
	fs_nosys,	/* ioctl */
	fs_setfl,	/* setfl */
	hsfs_getattr,
	fs_nosys,	/* setattr */
	hsfs_access,
	hsfs_lookup,
	fs_nosys,	/* create */
	fs_nosys,	/* remove */
	fs_nosys,	/* link */
	fs_nosys,	/* rename */
	fs_nosys,	/* mkdir */
	fs_nosys,	/* rmdir */
	hsfs_readdir,
	fs_nosys,	/* symlink */
	hsfs_readlink,
	hsfs_fsync,	/* fsync */
	hsfs_inactive,
	hsfs_fid,
	fs_rwlock,	/* rwlock */
	fs_rwunlock,	/* rwunlock */
	hsfs_seek,	/* seek */
	fs_cmp,
	fs_frlock,	/* frlock */
	fs_nosys,	/* space */
	fs_nosys,	/* realvp */
	hsfs_getpage,
	hsfs_putpage,
	hsfs_map,
	hsfs_addmap,	/* addmap */
	hsfs_delmap,	/* delmap */
	fs_poll,	/* poll */
	fs_nosys,	/* dump */
	fs_pathconf,
	fs_nosys,	/* pageio */
	fs_nosys,	/* dumpctl */
	fs_dispose,
	fs_nosys,	/* setsecattr */
	fs_fab_acl	/* getsecattr */
};
