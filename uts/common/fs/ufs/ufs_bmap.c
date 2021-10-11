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
 *	Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */

#ident	"@(#)ufs_bmap.c	2.73	95/10/05 SMI"
/* from "ufs_bmap.c 2.39     90/01/09 SMI"    		*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/disp.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_quota.h>
#include <sys/fs/ufs_trans.h>
#include <vm/seg.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>


static int findextent();

/*
 * Find the extent and the matching block number.
 *
 * bsize > PAGESIZE
 *	boff indicates that we want a page in the middle
 *	min expression is supposed to make sure no extra page[s] after EOF
 * PAGESIZE >= bsize
 *	we assume that a page is a multiple of bsize, i.e.,
 *	boff always == 0
 *
 * We always return a length that is suitable for a disk transfer.
 */
#define	DOEXTENT(fs, lbn, boff, bnp, lenp, size, tblp, n, chkfrag) {	\
	register daddr_t *dp = (tblp);					\
	register int _chkfrag = chkfrag; /* for lint. sigh */		\
									\
	if (*dp == 0) {							\
		*(bnp) = UFS_HOLE;					\
	} else {							\
		register int len;					\
									\
		len = findextent(fs, dp, (int)(n)) << (fs)->fs_bshift;	\
		if (_chkfrag) {						\
			register unsigned int tmp;			\
									\
			tmp = fragroundup((fs), size) -			\
			    ((lbn) << fs->fs_bshift);			\
			len = MIN(tmp, len);				\
		}							\
		len -= (boff);						\
		if (len <= 0) {						\
			*(bnp) = UFS_HOLE;				\
		} else {						\
			*(bnp) = fsbtodb(fs, *dp) + btodb(boff);	\
			*(lenp) = len;					\
		}							\
	}								\
}

/*
 * bmap{rd,wr} define the structure of file system storage by mapping
 * a logical offset in a file to a physical block number on the device.
 * It should be called with a locked inode when allocation is to be
 * done (bmapwr).  Note this strangeness: bmapwr is always called from
 * getpage(), not putpage(), since getpage() is where all the allocation
 * is done.
 *
 * S_READ, S_OTHER -> bmaprd; S_WRITE -> bmapwr.
 *
 * NOTICE: the block number returned is the disk block number, not the
 * file system block number.  All the worries about block offsets and
 * page/block sizes are hidden inside of bmap.  Well, not quite,
 * unfortunately.  It's impossible to find one place to hide all this
 * mess.  There are 3 cases:
 *
 * PAGESIZE < bsize
 *	In this case, the {get,put}page routines will attempt to align to
 *	a file system block boundry (XXX - maybe this is a mistake?).  Since
 *	the kluster routines may be out of memory, we don't always get all
 *	the pages we wanted.  If we called bmap first, to find out how much
 *	to kluster, we handed in the block aligned offset.  If we didn't get
 *	all the pages, we have to chop off the amount we didn't get from the
 *	amount handed back by bmap.
 *
 * PAGESIZE == bsize
 *	Life is quite pleasant here, no extra work needed, mainly because we
 *	(probably?) won't kluster backwards, just forwards.
 *
 * PAGESIZE > bsize
 *	This one has a different set of problems, specifically, we may have to
 *	do N reads to fill one page.  Let us hope that Sun will stay with small
 *	pages.
 *
 * Returns 0 on success, or a non-zero errno if an error occurs.
 *
 * TODO
 *	LMXXX - add a bmap cache.  This could be a couple of extents in the
 *	inode.  Two is nice for PAGESIZE > bsize.
 */
int
bmap_read(ip, off, bnp, lenp)
	struct	inode *ip;
	u_int	off;
	daddr_t *bnp;
	int	*lenp;
{
	daddr_t lbn;
	struct	fs *fs = ip->i_fs;
	struct	buf *bp;
	int	i, j, boff;
	int	shft;			/* we maintain sh = 1 << shft */
	daddr_t	ob, nb, tbn, *bap;
	int	nindirshift, nindiroffset;

	ASSERT(RW_LOCK_HELD(&ip->i_contents));
	lbn = lblkno(fs, off);
	boff = blkoff(fs, off);
	if (lbn < 0)
		return (EFBIG);

	/*
	 * The first NDADDR blocks are direct blocks.
	 */
	if (lbn < NDADDR) {
		DOEXTENT(fs, lbn, boff, bnp, lenp,
		    ip->i_size, &ip->i_db[lbn], NDADDR - lbn, 1);
		return (0);
	}

	nindirshift = ip->i_ufsvfs->vfs_nindirshift;
	nindiroffset = ip->i_ufsvfs->vfs_nindiroffset;

	/*
	 * Determine how many levels of indirection.
	 */
	shft = 0;				/* sh = 1 */
	tbn = lbn - NDADDR;
	for (j = NIADDR; j > 0; j--) {
		int	sh;

		shft += nindirshift;		/* sh *= nindir */
		sh = 1 << shft;
		if (tbn < sh)
			break;
		tbn -= sh;
	}
	if (j == 0)
		return (EFBIG);

	/*
	 * Fetch the first indirect block.
	 */
	nb = ip->i_ib[NIADDR - j];
	if (nb == 0) {
		*bnp = UFS_HOLE;
		return (0);
	}

	/*
	 * Fetch through the indirect blocks.
	 */
	for (; j <= NIADDR; j++) {
		ob = nb;
		bp = bread(ip->i_dev, fsbtodb(fs, ob), fs->fs_bsize);
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			return (EIO);
		}
		bap = bp->b_un.b_daddr;

		ASSERT(!ufs_indir_badblock(ip, bap));

		shft -= nindirshift;		/* sh / nindir */
		i = (tbn >> shft) & nindiroffset; /* (tbn / sh) % nindir */
		nb = bap[i];
		if (nb == 0) {
			*bnp = UFS_HOLE;
			brelse(bp);
			return (0);
		}
		if (j != NIADDR)
			brelse(bp);
	}
	DOEXTENT(fs, lbn, boff, bnp, lenp, ip->i_size, &bap[i],
	    MIN(NINDIR(fs) - i, lblkno(fs, ip->i_size - 1) - lbn + 1), 0);
	brelse(bp);
	return (0);
}

/*
 * See bmaprd for general notes.
 *
 * The block must be at least size bytes and will be extended or
 * allocated as needed.  If alloc_only is set, bmap will not create
 * any in-core pages that correspond to the new disk allocation.
 * Otherwise, the in-core pages will be created and initialized as
 * needed.
 *
 * Returns 0 on success, or a non-zero errno if an error occurs.
 */
int
bmap_write(ip, off, bnp, lenp, size, alloc_only, cr)
	struct	inode *ip;
	u_int	off;
	daddr_t	*bnp;
	int	*lenp, size;
	int	alloc_only;
	struct	cred *cr;
{
	struct	fs *fs;
	struct	buf *bp;
	int	i;
	struct	buf *nbp;
	int	j;
	int	shft;				/* we maintain sh = 1 << shft */
	daddr_t	ob, nb, pref, lbn, llbn, tbn, *bap;
	struct	vnode *vp = ITOV(ip);
	long	bsize = VBSIZE(vp);
	long	osize, nsize;
	long	old_i_size;
	int	issync, metaflag, isdirquota;
	int	err, write_out_later = 0;
	int	boff;
	dev_t	dev;
	struct	fbuf *fbp;
	int	nindirshift;
	int	nindiroffset;
	struct	ufsvfs	*ufsvfsp;

	ASSERT(RW_WRITE_HELD(&ip->i_contents));
	ASSERT((ip->i_owner == NULL) || (ip->i_owner == curthread));

	ufsvfsp = ip->i_ufsvfs;
	fs = ufsvfsp->vfs_bufp->b_un.b_fs;
	lbn = lblkno(fs, off);
	boff = blkoff(fs, off);
	if (lbn < 0)
		return (EFBIG);
	llbn = lblkno(fs, ip->i_size - 1);
	metaflag = isdirquota = 0;
	if ((ip->i_mode & IFMT) == IFDIR)
		isdirquota = metaflag = I_DIR;
	else if ((ip->i_mode & IFMT) == IFSHAD)
		metaflag = I_SHAD;
	else if (ip->i_ufsvfs->vfs_qinod == ip)
		isdirquota = metaflag = I_QUOTA;

	issync = ((ip->i_flag & ISYNC) != 0);

	if (isdirquota || issync)
		alloc_only = 0;		/* make sure */

	/*
	 * If the next write will extend the file into a new block,
	 * and the file is currently composed of a fragment
	 * this fragment has to be extended to be a full block.
	 */
	if (llbn < NDADDR && llbn < lbn && (ob = ip->i_db[llbn]) != 0) {
		osize = blksize(fs, ip, llbn);
		if (osize < bsize && osize > 0) {
			/*
			 * Make sure we have all needed pages setup correctly.
			 *
			 * i_owner prevents recursive locks in ufs_get/putpage
			 *
			 * We pass S_OTHER to fbread here because we want
			 * an exclusive lock on the page in question
			 * (see ufs_getpage). I/O to the old block location
			 * may still be in progress and we are about to free
			 * the old block. We don't want anyone else to get
			 * a hold of the old block once we free it until
			 * the I/O is complete.
			 */
			ASSERT(ip->i_owner == NULL);
			ip->i_owner = curthread;
			err = fbread(ITOV(ip), (u_int)(llbn << fs->fs_bshift),
			    bsize, S_OTHER, &fbp);
			ip->i_owner = NULL;
			if (err)
				return (err);

			pref = blkpref(ip, llbn, (int)llbn, &ip->i_db[0]);
			err = realloccg(ip, ob, pref, osize, bsize, &nb, cr);
			if (err) {
				if (fbp)
					fbrelse(fbp, S_OTHER);
				return (err);
			}
			ASSERT(!ufs_badblock(ip, nb));

			/*
			 * Update the inode before releasing the
			 * lock on the page. If we released the page
			 * lock first, the data could be written to it's
			 * old address and then destroyed.
			 */
			TRANS_MATA_ALLOC(ufsvfsp, ip, nb, bsize, 0);
			ip->i_db[llbn] = nb;
			ip->i_size = (llbn + 1) << fs->fs_bshift;
			ip->i_blocks += btodb(bsize - osize);
			TRANS_INODE(ufsvfsp, ip);
			ip->i_flag |= IUPD | ICHG | IATTCHG;
			/*
			 * Don't check metaflag here, directories won't do this
			 *
			 * i_owner prevents recursive locks in ufs_get/putpage
			 */
			ASSERT(ip->i_owner == NULL);
			ip->i_owner = curthread;
			if (issync) {
				(void) ufs_fbiwrite(fbp, ip, nb, fs->fs_fsize);
			} else {
				fbrelse(fbp, S_WRITE);
			}
			ip->i_owner = NULL;

			if (nb != ob) {
				(void) free(ip, ob, (off_t)osize, metaflag);
			}
		}
	}

	/*
	 * The first NDADDR blocks are direct blocks.
	 */
	if (lbn < NDADDR) {
		nb = ip->i_db[lbn];
		if (nb == 0 || ip->i_size < (lbn + 1) << fs->fs_bshift) {
			if (nb != 0) {
				/* consider need to reallocate a frag */
				osize = fragroundup(fs, blkoff(fs, ip->i_size));
				nsize = fragroundup(fs, size);
				if (nsize <= osize)
					goto gotit;
				/*
				 * need to allocate a block or frag
				 */
				ob = nb;
				pref = blkpref(ip, lbn, (int)lbn,
								&ip->i_db[0]);
				err = realloccg(ip, ob, pref, osize, nsize,
								&nb, cr);
				if (err)
					return (err);
				ASSERT(!ufs_badblock(ip, nb));

			} else {
				/*
				 * need to allocate a block or frag
				 */
				osize = 0;
				if (ip->i_size < (lbn + 1) << fs->fs_bshift)
					nsize = fragroundup(fs, size);
				else
					nsize = bsize;
				pref = blkpref(ip, lbn, (int)lbn, &ip->i_db[0]);
				err = alloc(ip, pref, nsize, &nb, cr);
				if (err)
					return (err);
				ASSERT(!ufs_badblock(ip, nb));
				ob = nb;
			}

			/*
			 * Read old/create new zero pages
			 */
			fbp = NULL;
			if (osize == 0) {
				/*
				 * mmap S_WRITE faults always enter here
				 */
				if (!alloc_only ||
					roundup(size, PAGESIZE) < nsize) {
					/* fbzero doesn't cause a pagefault */
					fbzero(ITOV(ip),
					    (u_int)(lbn << fs->fs_bshift),
					    (u_int)nsize, &fbp);
				}
			} else {
				/*
				 * i_owner prevents recursive locks in
				 * ufs_get/putpage.
				 */
				ASSERT(ip->i_owner == NULL);
				ip->i_owner = curthread;
				err = fbread(vp, (long)(lbn << fs->fs_bshift),
				    nsize, S_OTHER, &fbp);
				ip->i_owner = NULL;
				if (err) {
					if (nb != ob) {
						(void) free(ip, nb,
						    (off_t)nsize, metaflag);
					} else {
						(void) free(ip,
						    ob + numfrags(fs, osize),
						    (off_t)(nsize - osize),
						    metaflag);
					}
#ifdef QUOTA
					(void) chkdq(ip,
					    -(long)btodb(nsize - osize), 0, cr);
#endif QUOTA
					return (err);
				}
			}
			TRANS_MATA_ALLOC(ufsvfsp, ip, nb, nsize, 0);
			ip->i_db[lbn] = nb;
			ip->i_blocks += btodb(nsize - osize);
			TRANS_INODE(ufsvfsp, ip);
			ip->i_flag |= IUPD | ICHG | IATTCHG;

			/*
			 * Write directory and shadow blocks synchronously so
			 * that they never appear with garbage in them on the
			 * disk.
			 *
			 * i_owner prevents recursive locks in ufs_get/putpage
			 */
			ASSERT(ip->i_owner == NULL);
			ip->i_owner = curthread;
			if (isdirquota && (ip->i_size ||
			    TRANS_ISTRANS(ufsvfsp)))
			/*
			 * XXX man not be necessary with harpy trans
			 * bug id 1130055
			 */
				(void) ufs_fbiwrite(fbp, ip, nb, fs->fs_fsize);
			else if (fbp) {
				fbrelse(fbp, S_WRITE);
			}
			ip->i_owner = NULL;

			if (nb != ob)
				(void) free(ip, ob, (off_t)osize, metaflag);
		}
gotit:
		if (bnp != NULL) {
			DOEXTENT(fs, lbn, boff, bnp, lenp,
			    ip->i_size, &ip->i_db[lbn], NDADDR - lbn, 1);
		}
		return (0);
	}

	/*
	 * Determine how many levels of indirection.
	 */
	nindirshift = ip->i_ufsvfs->vfs_nindirshift;
	nindiroffset = ip->i_ufsvfs->vfs_nindiroffset;
	pref = 0;
	shft = 0;				/* sh = 1 */
	tbn = lbn - NDADDR;
	for (j = NIADDR; j > 0; j--) {
		int	sh;

		shft += nindirshift;		/* sh *= nindir */
		sh = 1 << shft;
		if (tbn < sh)
			break;
		tbn -= sh;
	}

	if (j == 0)
		return (EFBIG);

	/*
	 * Fetch the first indirect block.
	 */
	dev = ip->i_dev;
	nb = ip->i_ib[NIADDR - j];
	if (nb == 0) {
		/*
		 * Need to allocate an indirect block.
		 */
		pref = blkpref(ip, lbn, 0, (daddr_t *)0);
		err = alloc(ip, pref, bsize, &nb, cr);
		if (err)
			return (err);
		TRANS_MATA_ALLOC(ufsvfsp, ip, nb, bsize, 1);
		ASSERT(!ufs_badblock(ip, nb));

		/*
		 * Write zero block synchronously so that
		 * indirect blocks never point at garbage.
		 */
		bp = getblk(dev, fsbtodb(fs, nb), bsize);

		clrbuf(bp);
		/* XXX Maybe special-case this? */
		TRANS_BUF(ufsvfsp, 0, bsize, bp, DT_ABZERO);
		bwrite2(bp);
		if (bp->b_flags & B_ERROR) {
			err = geterror(bp);
			brelse(bp);
			return (err);
		}
		brelse(bp);

		ip->i_ib[NIADDR - j] = nb;
		ip->i_blocks += btodb(bsize);
		TRANS_INODE(ufsvfsp, ip);
		ip->i_flag |= IUPD | ICHG | IATTCHG;

		/*
		 * In the ISYNC case, rwip will notice that the block
		 * count on the inode has changed and will be sure to
		 * ufs_iupdat the inode at the end of rwip.
		 */
	}

	/*
	 * Fetch through the indirect blocks.
	 */
	for (; j <= NIADDR; j++) {
		ob = nb;
		bp = bread(ip->i_dev, fsbtodb(fs, ob), bsize);

		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			return (EIO);
		}
		bap = bp->b_un.b_daddr;
		shft -= nindirshift;		/* sh /= nindir */
		i = (tbn >> shft) & nindiroffset; /* (tbn / sh) % nindir */
		nb = bap[i];
		if (nb == 0) {
			if (pref == 0) {
				if (j < NIADDR) {
					/* Indirect block */
					pref = blkpref(ip, lbn, 0,
						(daddr_t *)0);
				} else {
					/* Data block */
					pref = blkpref(ip, lbn, i, &bap[0]);
				}
			}

			err = alloc(ip, pref, bsize, &nb, cr);
			if (err) {
				brelse(bp);
				/*
				 * Bug 1178761
				 * Trunc back to old size.
				 */
				old_i_size = ip->i_size;
				ip->i_size = off + size;
				(void) ufs_itrunc(ip, old_i_size, 0, cr);
				return (err);
			}

			ASSERT(!ufs_badblock(ip, nb));

			if (j < NIADDR) {
				TRANS_MATA_ALLOC(ufsvfsp, ip, nb, bsize, 1);
				/*
				 * Write synchronously so indirect
				 * blocks never point at garbage.
				 */
				nbp = getblk(dev, fsbtodb(fs, nb), bsize);

				clrbuf(nbp);
				/* XXX Maybe special-case this? */
				TRANS_BUF(ufsvfsp, 0, bsize, nbp, DT_ABZERO);
				bwrite2(nbp);
				if (nbp->b_flags & B_ERROR) {
					err = geterror(nbp);
					brelse(nbp);
					brelse(bp);
					return (err);
				}
				brelse(nbp);
			} else if (!alloc_only ||
			    roundup(size, PAGESIZE) < bsize) {
				TRANS_MATA_ALLOC(ufsvfsp, ip, nb, bsize, 0);
				/*
				 * To avoid deadlocking if the pageout
				 * daemon decides to push a page for this
				 * inode while we are sleeping holding the
				 * bp but waiting more pages for fbzero,
				 * we give up the bp now.
				 *
				 * XXX - need to avoid having the pageout
				 * daemon get in this situation to begin with!
				 */
				brelse(bp);
				fbzero(ITOV(ip),
				    (off_t)(lbn << fs->fs_bshift),
				    bsize, &fbp);

				/*
				 * Cases which we need to do a synchronous
				 * write of the zeroed data pages:
				 *
				 * 1) If we are writing a directory then we
				 * want to write synchronously so blocks in
				 * directories never contain garbage.
				 *
				 * 2) If we are filling in a hole and the
				 * indirect block is going to be synchronously
				 * written back below we need to make sure
				 * that the zeroes are written here before
				 * the indirect block is updated so that if
				 * we crash before the real data is pushed
				 * we will not end up with random data is
				 * the middle of the file.
				 *
				 * 3) If the size of the request rounded up
				 * to the system page size is smaller than
				 * the file system block size, we want to
				 * write out all the pages now so that
				 * they are not aborted before they actually
				 * make it to ufs_putpage since the length
				 * of the inode will not include the pages.
				 *
				 * i_owner prevents recursive locks in
				 * ufs_get/putpage
				 */
				ASSERT(ip->i_owner == NULL);
				ip->i_owner = curthread;
				if (isdirquota || (issync && lbn < llbn))
					(void) ufs_fbiwrite(fbp, ip, nb,
						fs->fs_fsize);
				else
					fbrelse(fbp, S_WRITE);
				ip->i_owner = NULL;

				/*
				 * Now get the bp back
				 */
				bp = bread(ip->i_dev, fsbtodb(fs, ob),
					bsize);

				err = geterror(bp);
				if (err) {
					(void) free(ip, nb, (off_t)bsize,
					    metaflag);
#ifdef QUOTA
					(void) chkdq(ip, -(long)btodb(bsize),
					    0, cr);
#endif QUOTA
					brelse(bp);
					return (err);
				}
				bap = bp->b_un.b_daddr;
			}

			bap[i] = nb;
			TRANS_BUF_ITEM_128(ufsvfsp, bap[i], bap, bp, DT_AB);
			ip->i_blocks += btodb(bsize);
			TRANS_INODE(ufsvfsp, ip);
			ip->i_flag |= IUPD | ICHG | IATTCHG;

			if (j != NIADDR) {
				if (issync) {
					bwrite2(bp);
					if (bp->b_flags & B_ERROR) {
						err = geterror(bp);
						brelse(bp);
						return (err);
					}
					brelse(bp);
				} else {
					bdwrite(bp);
				}
			} else {
				write_out_later = 1;
			}
		} else {
			if (j != NIADDR)
				brelse(bp);
		}
	}
	if (bnp != NULL) {
		/*
		 * The llbn - lbn + 1 is because llbn is the last logical bn,
		 * not the one after the last logical bn.
		 */
		DOEXTENT(fs, lbn, boff, bnp, lenp,
		    ip->i_size, &bap[i], MIN(NINDIR(fs) - i, llbn - lbn + 1),
		    0);
	}
	if (write_out_later) {
		if (issync) {
			bwrite2(bp);
			if (bp->b_flags & B_ERROR) {
				err = geterror(bp);
				brelse(bp);
				return (err);
			}
			brelse(bp);
		} else {
			bdwrite(bp);
		}
	} else {
		brelse(bp);
	}
	return (0);
}

/*
 * Return 1 if inode has unmapped blocks (UFS holes).
 */
int
bmap_has_holes(ip)
	struct inode	*ip;
{
	struct fs *fs = ip->i_fs;
	u_int	dblks; 			/* # of data blocks */
	u_int	mblks;			/* # of data + metadata blocks */
	int	nindirshift;
	int	nindiroffset;
	u_int	cnt;

	int	fsbshift = fs->fs_bshift;
	int	fsboffset = (1 << fsbshift) - 1;

	dblks = (ip->i_size + fsboffset) >> fsbshift;
	mblks = (dbtob(ip->i_blocks) + fsboffset) >> fsbshift;

	/*
	 * File has only direct blocks.
	 */
	if (dblks <= NDADDR)
		return (mblks < dblks);

	nindirshift = ip->i_ufsvfs->vfs_nindirshift;
	nindiroffset = ip->i_ufsvfs->vfs_nindiroffset;

	dblks -= NDADDR;
	cnt = NDADDR + 1;

	do {
		cnt += dblks;
		dblks = (dblks + nindiroffset) >> nindirshift;
	} while (dblks > 1);

	return (mblks < cnt);
}

/*
 * find some contig blocks starting at *sbp and going for min(n, max_contig)
 * return the number of blocks (not frags) found.
 * The array passed in must be at least [0..n-1].
 */
static int
findextent(fs, sbp, n)
	struct fs *fs;
	daddr_t *sbp;
	register int n;
{
	register daddr_t bn, nextbn;
	register daddr_t *bp;
	register int diff;

	if (n <= 0)
		return (0);
	bn = *sbp;
	if (bn == 0)
		return (0);
	diff = fs->fs_frag;
	n = MIN(n, fs->fs_maxcontig);
	bp = sbp;
	while (--n > 0) {
		nextbn = *(bp + 1);
		if (nextbn == 0 || bn + diff != nextbn)
			break;
		bn = nextbn;
		bp++;
	}
	return ((int)(bp - sbp) + 1);
}
