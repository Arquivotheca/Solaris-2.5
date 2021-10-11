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
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

/* #ident  "@(#)ufs_alloc.c 2.26     90/01/08 SMI"	*/
#ident	"@(#)ufs_alloc.c	2.68	95/09/07 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/debug.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/acl.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_acl.h>
#ifdef QUOTA
#include <sys/fs/ufs_quota.h>
#endif
#include <sys/fs/ufs_trans.h>
#include <sys/fs/ufs_panic.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <fs/fs_subr.h>
#include <sys/cmn_err.h>

static ino_t	hashalloc();
static daddr_t	fragextend();
static daddr_t	alloccg();
static daddr_t	alloccgblk();
static ino_t	ialloccg();
static daddr_t	mapsearch();

extern int	inside[], around[];
extern u_char	*fragtbl[];
static clock_t fswhinetime = 0;
void delay();

/*
 * Allocate a block in the file system.
 *
 * The size of the requested block is given, which must be some
 * multiple of fs_fsize and <= fs_bsize.
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadradically rehash into other cylinder groups, until an
 *	available block is located.
 * If no block preference is given the following heirarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *	inode for the file.
 *   2) quadradically rehash into other cylinder groups, until an
 *	available block is located.
 */
alloc(ip, bpref, size, bnp, cr)
	register struct inode *ip;
	daddr_t bpref;
	int size;
	daddr_t *bnp;
	struct cred *cr;
{
	register struct fs *fs;
	register struct ufsvfs *ufsvfsp;
	daddr_t bno;
	int cg;
	int err;

	ufsvfsp = ip->i_ufsvfs;
	fs = ufsvfsp->vfs_fs;
	if ((unsigned)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		err = ufs_fault(ITOV(ip),
	    "alloc: bad size, dev = 0x%x, bsize = %d, size = %d, fs = %s\n",
	    (int)ip->i_dev, (int)fs->fs_bsize, size, fs->fs_fsmnt);
		return (err);
	}
	mutex_enter(&ufsvfsp->vfs_lock);
	if (size == fs->fs_bsize && fs->fs_cstotal.cs_nbfree == 0) {
		mutex_exit(&ufsvfsp->vfs_lock);
		goto nospace;
	}
	if (cr->cr_uid != 0 && freespace(fs, fs->fs_minfree) <= 0) {
		mutex_exit(&ufsvfsp->vfs_lock);
		goto nospace;
	}
	mutex_exit(&ufsvfsp->vfs_lock);
#ifdef QUOTA
	err = chkdq(ip, (long)btodb(size), 0, cr);
	if (err)
		return (err);
#endif QUOTA
	if (bpref >= fs->fs_size)
		bpref = 0;
	if (bpref == 0)
		cg = (int)itog(fs, ip->i_number);
	else
		cg = dtog(fs, bpref);

	bno = (daddr_t)hashalloc(ip, cg, (long)bpref, size,
	    (u_long (*)())alloccg);
	if (bno > 0) {
		*bnp = bno;
		return (0);
	}
nospace:
	if ((lbolt - fswhinetime) > HZ*5) {
		fswhinetime = lbolt;
		cmn_err(CE_NOTE, "alloc: %s: file system full\n", fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Reallocate a fragment to a bigger size
 *
 * The number and size of the old block is given, and a preference
 * and new size is also specified.  The allocator attempts to extend
 * the original block.  Failing that, the regular block allocator is
 * invoked to get an appropriate block.
 */
realloccg(ip, bprev, bpref, osize, nsize, bnp, cr)
	register struct inode *ip;
	daddr_t bprev, bpref;
	int osize, nsize;
	daddr_t *bnp;
	struct cred *cr;
{
	daddr_t bno;
	register struct fs *fs;
	register struct ufsvfs *ufsvfsp;
	int cg, request;
	int	fspace;
	int err;

	ufsvfsp = ip->i_ufsvfs;
	fs = ufsvfsp->vfs_fs;
	if ((unsigned)osize > fs->fs_bsize || fragoff(fs, osize) != 0 ||
	    (unsigned)nsize > fs->fs_bsize || fragoff(fs, nsize) != 0) {
		err = ufs_fault(ITOV(ip),
	"realloccg: bad size, dev=0x%x, bsize=%d, osize=%d, nsize=%d, fs=%s\n",
		    (int)ip->i_dev, (int)fs->fs_bsize, osize, nsize,
		    fs->fs_fsmnt);
		return (err);
	}
	mutex_enter(&ufsvfsp->vfs_lock);
	fspace = freespace(fs, fs->fs_minfree);
	mutex_exit(&ufsvfsp->vfs_lock);

	if (cr->cr_uid != 0 && fspace <= 0)
		goto nospace;
	if (bprev == 0) {
		err = ufs_fault(ITOV(ip),
	"realloccg: bad bprev, dev = 0x%x, bsize = %d, bprev = %d, fs = %s\n",
		    (int)ip->i_dev, (int)fs->fs_bsize, (int)bprev,
		    fs->fs_fsmnt);
		return (err);
	}
#ifdef QUOTA
	err = chkdq(ip, (long)btodb(nsize - osize), 0, cr);
	if (err)
		return (err);
#endif QUOTA
	cg = dtog(fs, bprev);
	bno = fragextend(ip, cg, (long)bprev, osize, nsize);
	if (bno != 0) {
		*bnp = bno;
		return (0);
	}
	if (bpref >= fs->fs_size)
		bpref = 0;

	mutex_enter(&ufsvfsp->vfs_lock);
	switch ((int)fs->fs_optim) {
	case FS_OPTSPACE:
		/*
		 * Allocate an exact sized fragment. Although this makes
		 * best use of space, we will waste time relocating it if
		 * the file continues to grow. If the fragmentation is
		 * less than half of the minimum free reserve, we choose
		 * to begin optimizing for time.
		 */
		request = nsize;
		if (fs->fs_minfree < 5 || fs->fs_cstotal.cs_nffree >
		    fs->fs_dsize / (2 * 100) * fs->fs_minfree)
			break;
		cmn_err(CE_NOTE,
		    "%s: optimization changed from SPACE to TIME\n",
		    fs->fs_fsmnt);
		fs->fs_optim = FS_OPTTIME;
		break;
	default:
		/*
		 * Old file systems.
		 */
		cmn_err(CE_NOTE, "%s: bad optimization, defaulting to TIME\n",
			fs->fs_fsmnt);
		fs->fs_optim = FS_OPTTIME;
		/* FALLTHROUGH */
	case FS_OPTTIME:
		/*
		 * At this point we have discovered a file that is trying
		 * to grow a small fragment to a larger fragment. To save
		 * time, we allocate a full sized block, then free the
		 * unused portion. If the file continues to grow, the
		 * `fragextend' call above will be able to grow it in place
		 * without further copying. If aberrant programs cause
		 * disk fragmentation to grow within 2% of the free reserve,
		 * we choose to begin optimizing for space.
		 */
		request = fs->fs_bsize;
		if (fs->fs_cstotal.cs_nffree < fs->fs_dsize /
		    100 * (fs->fs_minfree + 2))
			break;
		cmn_err(CE_NOTE,
		    "%s: optimization changed from TIME to SPACE\n",
		    fs->fs_fsmnt);
		fs->fs_optim = FS_OPTSPACE;
		break;
	}
	mutex_exit(&ufsvfsp->vfs_lock);
	bno = (daddr_t)hashalloc(ip, cg, (long)bpref, request,
		(u_long (*)())alloccg);
	if (bno > 0) {
		*bnp = bno;
		if (nsize < request)
			(void) free(ip, bno + numfrags(fs, nsize),
			    (off_t)(request - nsize), 0);
		return (0);
	}
nospace:
	if ((lbolt - fswhinetime) > HZ*5) {
		fswhinetime = lbolt;
		cmn_err(CE_NOTE,
			"realloccg %s: file system full\n", fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Allocate an inode in the file system.
 *
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate an inode:
 *   1) allocate the requested inode.
 *   2) allocate an inode in the same cylinder group.
 *   3) quadradically rehash into other cylinder groups, until an
 *	available inode is located.
 * If no inode preference is given the following heirarchy is used
 * to allocate an inode:
 *   1) allocate an inode in cylinder group 0.
 *   2) quadradically rehash into other cylinder groups, until an
 *	available inode is located.
 */
ufs_ialloc(pip, ipref, mode, ipp, cr)
	register struct inode *pip;
	ino_t ipref;
	mode_t mode;
	struct inode **ipp;
	struct cred *cr;
{
	register struct inode *ip;
	register struct fs *fs;
	int cg;
	ino_t ino;
	int err;
	int nifree;

	ASSERT(RW_WRITE_HELD(&pip->i_rwlock));
	fs = pip->i_fs;
loop:
	nifree = fs->fs_cstotal.cs_nifree;

	if (nifree == 0)
		goto noinodes;
#ifdef QUOTA
	err = chkiq((struct ufsvfs *)pip->i_vnode.v_vfsp->vfs_data,
	    (struct inode *)NULL, cr->cr_uid, 0, cr);
	if (err)
		return (err);
#endif
	if (ipref >= (u_long)(fs->fs_ncg * fs->fs_ipg))
		ipref = 0;
	cg = itog(fs, ipref);
	ino = (ino_t)hashalloc(pip, cg, (long)ipref, (int) mode,
	    (u_long (*)())ialloccg);
	if (ino == 0)
		goto noinodes;
	err = ufs_iget(pip->i_vfs, ino, ipp, cr);
	if (err) {
		ufs_ifree(pip, ino, 0);
		return (err);
	}
	ip = *ipp;
	ASSERT(!ip->i_ufs_acl);
	rw_enter(&ip->i_contents, RW_WRITER);
	if (ip->i_mode) {
		cmn_err(CE_NOTE, "mode = 0%o, inum = %d, fs = %s\n",
		    ip->i_mode, (int)ip->i_number, fs->fs_fsmnt);
		ufs_iupdat(ip, 1);
		rw_exit(&ip->i_contents);
		VN_RELE(ITOV(ip));
		goto loop;
	}
	if (ip->i_blocks)
		ip->i_blocks = 0;
	if (ip->i_size) {
		cmn_err(CE_NOTE, "free inode %s/%d had size 0x%x\n",
			fs->fs_fsmnt, (int)ino, (int)ip->i_size);
		ip->i_size = 0;
	}
	rw_exit(&ip->i_contents);
	return (0);
noinodes:
	cmn_err(CE_NOTE, "%s: out of inodes\n", fs->fs_fsmnt);
	return (ENOSPC);
}

/*
 * Find a cylinder to place a directory.
 *
 * The policy implemented by this algorithm is to select from
 * among those cylinder groups with above the average number of
 * free inodes, the one with the smallest number of directories.
 */
ino_t
dirpref(ufsvfsp)
	register struct ufsvfs *ufsvfsp;
{
	int cg, minndir, mincg, avgifree;
	register struct fs *fs;

	fs = ufsvfsp->vfs_fs;
	mutex_enter(&ufsvfsp->vfs_lock);
	avgifree = fs->fs_cstotal.cs_nifree / fs->fs_ncg;
	minndir = fs->fs_ipg;
	mincg = 0;
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= avgifree) {
			mincg = cg;
			minndir = fs->fs_cs(fs, cg).cs_ndir;
		}
	}
	mutex_exit(&ufsvfsp->vfs_lock);
	return ((ino_t)(fs->fs_ipg * mincg));
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks. Each additional section contains fs_maxbpg blocks.
 *
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. If no blocks have been allocated in any other section, the
 * policy is to place the section in a cylinder group with a greater than
 * average number of free blocks.  An appropriate cylinder group is found
 * by using a rotor that sweeps the cylinder groups. When a new group of
 * blocks is needed, the sweep begins in the cylinder group following the
 * cylinder group from which the previous allocation was made. The sweep
 * continues until a cylinder group with greater than the average number
 * of free blocks is found. If the allocation is for the first block in an
 * indirect block, the information on the previous allocation is unavailable;
 * here a best guess is made based upon the logical block number being
 * allocated.
 *
 * If a section is already partially allocated, the policy is to
 * contiguously allocate fs_maxcontig blocks.  The end of one of these
 * contiguous blocks and the beginning of the next is physically separated
 * so that the disk head will be in transit between them for at least
 * fs_rotdelay milliseconds.  This is to allow time for the processor to
 * schedule another I/O transfer.
 */
daddr_t
blkpref(ip, lbn, indx, bap)
	struct inode *ip;
	daddr_t lbn;
	int indx;
	daddr_t *bap;
{
	register struct fs *fs;
	register struct ufsvfs *ufsvfsp;
	register int cg;
	int avgbfree, startcg;
	daddr_t nextblk;

	ufsvfsp = ip->i_ufsvfs;
	fs = ip->i_fs;
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		if (lbn < NDADDR) {
			cg = itog(fs, ip->i_number);
			return (fs->fs_fpg * cg + fs->fs_frag);
		}
		/*
		 * Find a cylinder with greater than average
		 * number of unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg = itog(fs, ip->i_number) + lbn / fs->fs_maxbpg;
		else
			startcg = dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;

		mutex_enter(&ufsvfsp->vfs_lock);
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				mutex_exit(&ufsvfsp->vfs_lock);
				return (fs->fs_fpg * cg + fs->fs_frag);
			}
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				fs->fs_cgrotor = cg;
				mutex_exit(&ufsvfsp->vfs_lock);
				return (fs->fs_fpg * cg + fs->fs_frag);
			}
		mutex_exit(&ufsvfsp->vfs_lock);
		return (NULL);
	}
	/*
	 * One or more previous blocks have been laid out. If less
	 * than fs_maxcontig previous blocks are contiguous, the
	 * next block is requested contiguously, otherwise it is
	 * requested rotationally delayed by fs_rotdelay milliseconds.
	 */
	nextblk = bap[indx - 1] + fs->fs_frag;
	if (indx > fs->fs_maxcontig &&
	    bap[indx - fs->fs_maxcontig] + blkstofrags(fs, fs->fs_maxcontig)
	    != nextblk)
		return (nextblk);
	if (fs->fs_rotdelay != 0)
		/*
		 * Here we convert ms of delay to frags as:
		 * (frags) = (ms) * (rev/sec) * (sect/rev) /
		 *	((sect/frag) * (ms/sec))
		 * then round up to the next block.
		 */
		nextblk += roundup(fs->fs_rotdelay * fs->fs_rps * fs->fs_nsect /
		    (NSPF(fs) * 1000), fs->fs_frag);
	return (nextblk);
}

/*
 * Free a block or fragment.
 *
 * The specified block or fragment is placed back in the
 * free map. If a fragment is deallocated, a possible
 * block reassembly is checked.
 */
void
free(ip, bno, size, flags)
	register struct inode *ip;
	daddr_t bno;
	off_t size;
	int flags;
{
	register struct fs *fs = ip->i_fs;
	register struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	register struct cg *cgp;
	register struct buf *bp;
	int cg, bmap, bbase;
	register int i;
	u_char *blksfree;
	long *blktot;
	short *blks;
	daddr_t blkno, cylno, rpos;

	if ((unsigned)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		(void) ufs_fault(ITOV(ip),
		"free: bad size, dev = 0x%x, bsize = %d, size = %d, fs = %s\n",
		    (int)ip->i_dev, (int)fs->fs_bsize, (int)size, fs->fs_fsmnt);
		return;
	}
	cg = dtog(fs, bno);
	ASSERT(!ufs_badblock(ip, bno));
	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, cgtod(fs, cg)),
		    (int)fs->fs_cgsize);

	cgp = bp->b_un.b_cg;
	if (bp->b_flags & B_ERROR || !cg_chkmagic(cgp)) {
		brelse(bp);
		return;
	}
#if defined(DEBUG)
	ufs_cgcheck("free begin", ufsvfsp, cgp);
#endif /* DEBUG */
	if (flags & (I_DIR|I_IBLK|I_SHAD|I_QUOTA)) {
		TRANS_CANCEL(ufsvfsp, ldbtob(fsbtodb(fs, bno)), size);
		TRANS_MATA_FREE(ufsvfsp, ldbtob(fsbtodb(fs, bno)),
		    size);
	}
	blksfree = cg_blksfree(cgp);
	blktot = cg_blktot(cgp);
	mutex_enter(&ufsvfsp->vfs_lock);
	cgp->cg_time = hrestime.tv_sec;
	bno = dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		blkno = fragstoblks(fs, bno);
		cylno = cbtocylno(fs, bno);
		rpos = cbtorpos(ufsvfsp, bno);
		blks = cg_blks(ufsvfsp, cgp, cylno);
		if (!isclrblock(fs, blksfree, blkno)) {
			mutex_exit(&ufsvfsp->vfs_lock);
			(void) ufs_fault(ITOV(ip),
		"free: freeing free block,dev = 0x%x, block = %d, fs = %s\n",
				(int)ip->i_dev, (int)bno, fs->fs_fsmnt);
			return;
		}
		setblock(fs, blksfree, blkno);
		TRANS_BUF_ITEM(ufsvfsp, bitloc(blksfree, bno), cgp, bp, DT_CG);
		blks[rpos]++;
		TRANS_BUF_ITEM(ufsvfsp, blks[rpos], cgp, bp, DT_CG);
		blktot[cylno]++;
		TRANS_BUF_ITEM(ufsvfsp, blktot[cylno], cgp, bp, DT_CG);
		cgp->cg_cs.cs_nbfree++;		/* Log below */
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;
	} else {
		bbase = bno - fragnum(fs, bno);
		/*
		 * Decrement the counts associated with the old frags
		 */
		bmap = blkmap(fs, blksfree, bbase);
		fragacct(fs, bmap, cgp->cg_frsum, -1);
		/*
		 * Deallocate the fragment
		 */
		for (i = 0; i < numfrags(fs, size); i++) {
			if (isset(blksfree, bno + i)) {
				(void) ufs_fault(ITOV(ip),
		"free: freeing free frag, dev = 0x%x, block = %d, cg = %d, "
		"fs = %s\n",
				    (int)ip->i_dev, (int)bno + i,
				    (int)cgp->cg_cgx, fs->fs_fsmnt);
				return;
			}
			setbit(blksfree, bno + i);
			TRANS_BUF_ITEM(ufsvfsp, bitloc(blksfree, bno + i), cgp,
			    bp, DT_CG);
		}
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		/*
		 * Add back in counts associated with the new frags
		 */
		bmap = blkmap(fs, blksfree, bbase);
		fragacct(fs, bmap, cgp->cg_frsum, 1);
		/*
		 * If a complete block has been reassembled, account for it
		 */
		blkno = fragstoblks(fs, bbase);
		if (isblock(fs, blksfree, blkno)) {
			cylno = cbtocylno(fs, bbase);
			rpos = cbtorpos(ufsvfsp, bbase);
			blks = cg_blks(ufsvfsp, cgp, cylno);
			blks[rpos]++;
			TRANS_BUF_ITEM(ufsvfsp, blks[rpos], cgp, bp, DT_CG);
			blktot[cylno]++;
			TRANS_BUF_ITEM(ufsvfsp, blktot[cylno], cgp,
			    bp, DT_CG);
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;
		}
	}
	fs->fs_fmod = 1;
	ufs_notclean(ufsvfsp);
	TRANS_CG(ufsvfsp, bp);
	TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
	ufs_cgcheck("free end", ufsvfsp, cgp);
#endif /* DEBUG */
	bdwrite(bp);
}

/*
 * Free an inode.
 *
 * The specified inode is placed back in the free map.
 */
void
ufs_ifree(ip, ino, mode)
	struct inode *ip;
	ino_t ino;
	mode_t mode;
{
	register struct fs *fs = ip->i_fs;
	register struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	register struct cg *cgp;
	register struct buf *bp;
	ino_t inot;
	int cg;
	char *iused;

	if (ip->i_number == ino && ip->i_mode != 0) {
		(void) ufs_fault(ITOV(ip),
		    "ufs_ifree: illegal mode: %o, ino %d, fs = %s\n",
		    ip->i_mode, (int)ip->i_number, fs->fs_fsmnt);
		return;
	}
	if ((unsigned)ino >= fs->fs_ipg * fs->fs_ncg) {
		(void) ufs_fault(ITOV(ip),
		    "ifree: range, dev = 0x%x, ino = %d, fs = %s\n",
		    (int)ip->i_dev, (int)ino, fs->fs_fsmnt);
		return;
	}
	cg = itog(fs, ino);
	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, cgtod(fs, cg)),
		    (int)fs->fs_cgsize);

	cgp = bp->b_un.b_cg;
	if (bp->b_flags & B_ERROR || !cg_chkmagic(cgp)) {
		brelse(bp);
		return;
	}
#if defined(DEBUG)
	ufs_cgcheck("ufs_ifree begin", ufsvfsp, cgp);
#endif /* DEBUG */
	mutex_enter(&ufsvfsp->vfs_lock);
	cgp->cg_time = hrestime.tv_sec;
	iused = cg_inosused(cgp);
	inot = ino % (u_long)fs->fs_ipg;
	if (isclr(iused, inot)) {
		mutex_exit(&ufsvfsp->vfs_lock);
		(void) ufs_fault(ITOV(ip),
		"ufs_ifree: freeing free inode, mode= %o, ino = %d, fs = %s\n",
		    ip->i_mode, (int)ino, fs->fs_fsmnt);
		return;
	}
	clrbit(iused, inot);
	TRANS_BUF_ITEM(ufsvfsp, bitloc(iused, inot), cgp, bp, DT_CG);

	if (inot < (u_long)cgp->cg_irotor)
		cgp->cg_irotor = inot;
	cgp->cg_cs.cs_nifree++;
	fs->fs_cstotal.cs_nifree++;
	fs->fs_cs(fs, cg).cs_nifree++;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir--;
		fs->fs_cstotal.cs_ndir--;
		fs->fs_cs(fs, cg).cs_ndir--;
	}
	fs->fs_fmod = 1;
	ufs_notclean(ufsvfsp);
	TRANS_CG(ufsvfsp, bp);
	TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
	ufs_cgcheck("ufs_ifree end", ufsvfsp, cgp);
#endif /* DEBUG */
	bdwrite(bp);
}

#ifdef notneeded
/*
 * Fserr prints the name of a file system with an error diagnostic.
 *
 * The form of the error message is:
 *	fs: error message
 */
fserr(fs, cp)
	struct fs *fs;
	char *cp;
{

	log(LOG_ERR, "%s: %s\n", fs->fs_fsmnt, cp);
}
#endif

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadradically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 */
static ino_t
hashalloc(ip, cg, pref, size, allocator)
	struct inode *ip;
	int cg;
	long pref;
	int size;	/* size for data blocks, mode for inodes */
	u_long (*allocator)();
{
	register struct fs *fs;
	register int i;
	long result;
	int icg = cg;

	fs = ip->i_fs;
	/*
	 * 1: preferred cylinder group
	 */
	result = (*allocator)(ip, cg, pref, size);
	if (result)
		return (result);
	/*
	 * 2: quadratic rehash
	 */
	for (i = 1; i < fs->fs_ncg; i *= 2) {
		cg += i;
		if (cg >= fs->fs_ncg)
			cg -= fs->fs_ncg;
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 */
	cg = (icg + 2) % fs->fs_ncg;
	for (i = 2; i < fs->fs_ncg; i++) {
		result = (*allocator)(ip, cg, 0, size);
		if (result)
			return (result);
		cg++;
		if (cg == fs->fs_ncg)
			cg = 0;
	}
	return (NULL);
}

/*
 * Determine whether a fragment can be extended.
 *
 * Check to see if the necessary fragments are available, and
 * if they are, allocate them.
 */
static daddr_t
fragextend(ip, cg, bprev, osize, nsize)
	struct inode *ip;
	int cg;
	long bprev;
	int osize, nsize;
{
	register struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	register struct fs *fs = ip->i_fs;
	register struct buf *bp;
	register struct cg *cgp;
	u_char *blksfree;
	long bno;
	int frags, bbase;
	int i, j;

	if (fs->fs_cs(fs, cg).cs_nffree < numfrags(fs, nsize - osize))
		return (NULL);
	frags = numfrags(fs, nsize);
	bbase = fragnum(fs, bprev);
	if (bbase > fragnum(fs, (bprev + frags - 1))) {
		/* cannot extend across a block boundary */
		return (NULL);
	}

	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, cgtod(fs, cg)),
		    (int)fs->fs_cgsize);
	cgp = bp->b_un.b_cg;
	if (bp->b_flags & B_ERROR || !cg_chkmagic(cgp)) {
		brelse(bp);
		return (NULL);
	}
#if defined(DEBUG)
	ufs_cgcheck("fragextend begin", ufsvfsp, cgp);
#endif /* DEBUG */
	if (TRANS_ISCANCEL(ufsvfsp, ldbtob(fsbtodb(fs, blknum(fs, bprev))),
			fs->fs_bsize)) {
		brelse(bp);
		return (NULL);
	}
	blksfree = cg_blksfree(cgp);
	mutex_enter(&ufsvfsp->vfs_lock);
	bno = dtogd(fs, bprev);
	for (i = numfrags(fs, osize); i < frags; i++)
		if (isclr(blksfree, bno + i)) {
			mutex_exit(&ufsvfsp->vfs_lock);
			brelse(bp);
			return (NULL);
		}
	cgp->cg_time = hrestime.tv_sec;
	/*
	 * The current fragment can be extended,
	 * deduct the count on fragment being extended into
	 * increase the count on the remaining fragment (if any)
	 * allocate the extended piece.
	 */
	for (i = frags; i < fs->fs_frag - bbase; i++)
		if (isclr(blksfree, bno + i))
			break;
	j = i - numfrags(fs, osize);
	cgp->cg_frsum[j]--;
	ASSERT(cgp->cg_frsum[j] >= 0);
	if (i != frags)
		cgp->cg_frsum[i - frags]++;
	for (i = numfrags(fs, osize); i < frags; i++) {
		clrbit(blksfree, bno + i);
		cgp->cg_cs.cs_nffree--;
		fs->fs_cs(fs, cg).cs_nffree--;
		fs->fs_cstotal.cs_nffree--;
	}
	TRANS_BUF_ITEM(ufsvfsp, bitloc(blksfree, bno), cgp, bp, DT_CG);
	fs->fs_fmod = 1;
	ufs_notclean(ufsvfsp);
	TRANS_CG(ufsvfsp, bp);
	TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
	ufs_cgcheck("fragextend end", ufsvfsp, cgp);
#endif /* DEBUG */
	bdwrite(bp);
	return (bprev);
}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the apprpriate size
 * is available, and if it is, allocate it.
 */
static daddr_t
alloccg(ip, cg, bpref, size)
	struct inode *ip;
	int cg;
	daddr_t bpref;
	int size;
{
	register struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	register struct fs *fs = ip->i_fs;
	register struct buf *bp;
	register struct cg *cgp;
	u_char *blksfree;
	int bno, frags;
	int allocsiz;
	register int i;

	if (fs->fs_cs(fs, cg).cs_nbfree == 0 && size == fs->fs_bsize)
		return (0);
	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, cgtod(fs, cg)),
		    (int)fs->fs_cgsize);

	cgp = bp->b_un.b_cg;
	if (bp->b_flags & B_ERROR || !cg_chkmagic(cgp) ||
	    (cgp->cg_cs.cs_nbfree == 0 && size == fs->fs_bsize)) {
		brelse(bp);
		return (0);
	}
#if defined(DEBUG)
	ufs_cgcheck("alloccg begin", ufsvfsp, cgp);
#endif /* DEBUG */
	blksfree = cg_blksfree(cgp);
	mutex_enter(&ufsvfsp->vfs_lock);
	cgp->cg_time = hrestime.tv_sec;
	if (size == fs->fs_bsize) {
		if ((bno = alloccgblk(ufsvfsp, cgp, bpref, bp)) == 0)
			goto errout;
		fs->fs_fmod = 1;
		ufs_notclean(ufsvfsp);
		TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
		ufs_cgcheck("alloccg end 1", ufsvfsp, cgp);
#endif /* DEBUG */
		bdwrite(bp);
		return (bno);
	}
	/*
	 * Check to see if any fragments are already available
	 * allocsiz is the size which will be allocated, hacking
	 * it down to a smaller size if necessary.
	 */
	frags = numfrags(fs, size);
	for (allocsiz = frags; allocsiz < fs->fs_frag; allocsiz++)
		if (cgp->cg_frsum[allocsiz] != 0)
			break;

	if (allocsiz != fs->fs_frag)
		bno = mapsearch(ufsvfsp, cgp, bpref, allocsiz);

	if (allocsiz == fs->fs_frag || bno < 0) {
		/*
		 * No fragments were available, so a block
		 * will be allocated and hacked up.
		 */
		if (cgp->cg_cs.cs_nbfree == 0)
			goto errout;
		if ((bno = alloccgblk(ufsvfsp, cgp, bpref, bp)) == 0)
			goto errout;
		bpref = dtogd(fs, bno);
		for (i = frags; i < fs->fs_frag; i++)
			setbit(blksfree, bpref + i);
		i = fs->fs_frag - frags;
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		cgp->cg_frsum[i]++;
		fs->fs_fmod = 1;
		ufs_notclean(ufsvfsp);
		TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
		ufs_cgcheck("alloccg end 2", ufsvfsp, cgp);
#endif /* DEBUG */
		bdwrite(bp);
		return (bno);
	}

	for (i = 0; i < frags; i++)
		clrbit(blksfree, bno + i);
	TRANS_BUF_ITEM(ufsvfsp, bitloc(blksfree, bno), cgp, bp, DT_CG);
	cgp->cg_cs.cs_nffree -= frags;
	fs->fs_cstotal.cs_nffree -= frags;
	fs->fs_cs(fs, cg).cs_nffree -= frags;
	cgp->cg_frsum[allocsiz]--;
	ASSERT(cgp->cg_frsum[allocsiz] >= 0);
	if (frags != allocsiz) {
		cgp->cg_frsum[allocsiz - frags]++;
	}
	fs->fs_fmod = 1;
	ufs_notclean(ufsvfsp);
	TRANS_CG(ufsvfsp, bp);
	TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
	ufs_cgcheck("alloccg end 3", ufsvfsp, cgp);
#endif /* DEBUG */
	bdwrite(bp);
	return (cg * fs->fs_fpg + bno);
errout:
	mutex_exit(&ufsvfsp->vfs_lock);
	brelse(bp);
	return (0);
}

/*
 * Allocate a block in a cylinder group.
 *
 * This algorithm implements the following policy:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate the next available block on the block rotor for the
 *	specified cylinder group.
 * Note that this routine only allocates fs_bsize blocks; these
 * blocks may be fragmented by the routine that allocates them.
 */
static daddr_t
alloccgblk(ufsvfsp, cgp, bpref, bp)
	register struct ufsvfs *ufsvfsp;
	register struct cg *cgp;
	daddr_t bpref;
	struct buf *bp;
{
	daddr_t bno;
	int cylno, pos, delta;
	short *cylbp;
	register int i;
	register struct fs *fs;
	u_char *blksfree;
	daddr_t blkno, rpos, frag;
	short *blks;
	long *blktot;

	ASSERT(MUTEX_HELD(&ufsvfsp->vfs_lock));
	fs = ufsvfsp->vfs_fs;
	blksfree = cg_blksfree(cgp);
	if (bpref == 0) {
		bpref = cgp->cg_rotor;
		goto norot;
	}
	bpref = blknum(fs, bpref);
	bpref = dtogd(fs, bpref);
	/*
	 * If the requested block is available, use it.
	 */
	if (isblock(fs, blksfree, (daddr_t)fragstoblks(fs, bpref))) {
		bno = bpref;
		goto gotit;
	}
	/*
	 * Check for a block available on the same cylinder.
	 */
	cylno = cbtocylno(fs, bpref);
	if (cg_blktot(cgp)[cylno] == 0)
		goto norot;
	if (fs->fs_cpc == 0) {
		/*
		 * Block layout info is not available, so just
		 * have to take any block in this cylinder.
		 */
		bpref = howmany(fs->fs_spc * cylno, NSPF(fs));
		goto norot;
	}
	/*
	 * Check the summary information to see if a block is
	 * available in the requested cylinder starting at the
	 * requested rotational position and proceeding around.
	 */
	cylbp = cg_blks(ufsvfsp, cgp, cylno);
	pos = cbtorpos(ufsvfsp, bpref);
	for (i = pos; i < ufsvfsp->vfs_nrpos; i++)
		if (cylbp[i] > 0)
			break;
	if (i == ufsvfsp->vfs_nrpos)
		for (i = 0; i < pos; i++)
			if (cylbp[i] > 0)
				break;
	if (cylbp[i] > 0) {
		/*
		 * Found a rotational position, now find the actual
		 * block.  A "panic" if none is actually there.
		 */
		pos = cylno % fs->fs_cpc;
		bno = (cylno - pos) * fs->fs_spc / NSPB(fs);
		if (fs_postbl(ufsvfsp, pos)[i] == -1) {
			(void) ufs_fault(ufsvfsp->vfs_root,
	    "alloccgblk: cyl groups corrupted, pos = %d, i = %d, fs = %s\n",
				    pos, i, fs->fs_fsmnt);
			return (0);
		}
		i = fs_postbl(ufsvfsp, pos)[i];
		for (;;) {
			if (isblock(fs, blksfree, (daddr_t)(bno + i))) {
				bno = blkstofrags(fs, (bno + i));
				goto gotit;
			}
			delta = fs_rotbl(fs)[i];
			if (delta <= 0 ||
			    delta + i > fragstoblks(fs, fs->fs_fpg))
				break;
			i += delta;
		}
		(void) ufs_fault(ufsvfsp->vfs_root,
	"alloccgblk: can't find blk in cyl, pos:%d, i:%d, fs:%s bno: %x\n",
		    pos, i, fs->fs_fsmnt, (int) bno);
		return (0);
	}
norot:
	/*
	 * No blocks in the requested cylinder, so take
	 * next available one in this cylinder group.
	 */
	bno = mapsearch(ufsvfsp, cgp, bpref, (int)fs->fs_frag);
	if (bno < 0)
		return (0);
	cgp->cg_rotor = bno;
gotit:
	blkno = fragstoblks(fs, bno);
	frag = (cgp->cg_cgx * fs->fs_fpg) + bno;
	if (TRANS_ISCANCEL(ufsvfsp, ldbtob(fsbtodb(fs, frag)), fs->fs_bsize))
		goto norot;
	clrblock(fs, blksfree, (long)blkno);
	TRANS_BUF_ITEM(ufsvfsp, bitloc(blksfree, bno), cgp, bp, DT_CG);
	/*
	 * the other cg/sb/si fields are TRANS'ed by the caller
	 */
	cgp->cg_cs.cs_nbfree--;
	fs->fs_cstotal.cs_nbfree--;
	fs->fs_cs(fs, cgp->cg_cgx).cs_nbfree--;
	cylno = cbtocylno(fs, bno);
	blks = cg_blks(ufsvfsp, cgp, cylno);
	rpos = cbtorpos(ufsvfsp, bno);
	blktot = cg_blktot(cgp);
	blks[rpos]--;
	blktot[cylno]--;
	TRANS_BUF_ITEM(ufsvfsp, blks[rpos], cgp, bp, DT_CG);
	TRANS_BUF_ITEM(ufsvfsp, blktot[cylno], cgp, bp, DT_CG);
	TRANS_CG(ufsvfsp, bp);
	fs->fs_fmod = 1;
	return (frag);
}

/*
 * Determine whether an inode can be allocated.
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using the following policy:
 *   1) allocate the requested inode.
 *   2) allocate the next available inode after the requested
 *	inode in the specified cylinder group.
 */
static ino_t
ialloccg(ip, cg, ipref, mode)
	struct inode *ip;
	int cg;
	daddr_t ipref;
	int mode;
{
	register struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	register struct fs *fs = ip->i_fs;
	register struct cg *cgp;
	struct buf *bp;
	int start, len, loc, map, i;
	char *iused;

	if (fs->fs_cs(fs, cg).cs_nifree == 0)
		return (0);
	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, cgtod(fs, cg)),
		    (int)fs->fs_cgsize);

	cgp = bp->b_un.b_cg;
	if (bp->b_flags & B_ERROR || !cg_chkmagic(cgp) ||
	    cgp->cg_cs.cs_nifree == 0) {
		brelse(bp);
		return (0);
	}
#if defined(DEBUG)
	ufs_cgcheck("ialloccg begin", ufsvfsp, cgp);
#endif /* DEBUG */
	iused = cg_inosused(cgp);
	mutex_enter(&ufsvfsp->vfs_lock);
	/*
	 * While we are waiting for the mutex, someone may have taken
	 * the last available inode.  Need to recheck.
	 */
	if (cgp->cg_cs.cs_nifree == 0) {
		mutex_exit(&ufsvfsp->vfs_lock);
		brelse(bp);
		return (0);
	}

	cgp->cg_time = hrestime.tv_sec;
	if (ipref) {
		ipref %= fs->fs_ipg;
		if (isclr(iused, ipref))
			goto gotit;
	}
	start = cgp->cg_irotor / NBBY;
	len = howmany(fs->fs_ipg - cgp->cg_irotor, NBBY);
	loc = skpc(0xff, (u_int)len, &iused[start]);
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = skpc(0xff, (u_int)len, &iused[0]);
		if (loc == 0) {
			(void) ufs_fault(ITOV(ip),
		    "ialloccg: map corrupted, cg = %d, irotor = %d, fs = %s\n",
				    cg, (int)cgp->cg_irotor, fs->fs_fsmnt);
			return (0);
		}
	}
	i = start + len - loc;
	map = iused[i];
	ipref = i * NBBY;
	for (i = 1; i < (1 << NBBY); i <<= 1, ipref++) {
		if ((map & i) == 0) {
			cgp->cg_irotor = ipref;
			goto gotit;
		}
	}

	(void) ufs_fault(ITOV(ip), "ialloccg: block not in mapfs = %s\n",
							    fs->fs_fsmnt);
	return (0);
gotit:
	setbit(iused, ipref);
	TRANS_BUF_ITEM(ufsvfsp, bitloc(iused, ipref), cgp, bp, DT_CG);
	cgp->cg_cs.cs_nifree--;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir++;
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++;
	}
	fs->fs_fmod = 1;
	ufs_notclean(ufsvfsp);
	TRANS_CG(ufsvfsp, bp);
	TRANS_SI(ufsvfsp, fs, cg);
#if defined(DEBUG)
	ufs_cgcheck("ialloccg end", ufsvfsp, cgp);
#endif /* DEBUG */
	bdwrite(bp);
	return (cg * fs->fs_ipg + ipref);
}

/*
 * Find a block of the specified size in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
static daddr_t
mapsearch(ufsvfsp, cgp, bpref, allocsiz)
	struct ufsvfs *ufsvfsp;
	register struct cg *cgp;
	daddr_t bpref;
	int allocsiz;
{
	register struct fs *fs	= ufsvfsp->vfs_fs;
	daddr_t bno, cfrag;
	int start, len, loc, i, last, first, secondtime;
	int blk, field, subfield, pos;
	int gotit;

	/*
	 * ufsvfs->vfs_lock is held when calling this.
	 */
	/*
	 * Find the fragment by searching through the
	 * free block map for an appropriate bit pattern.
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = cgp->cg_frotor / NBBY;
	/*
	 * the following loop performs two scans -- the first scan
	 * searches the bottom half of the array for a match and the
	 * second scan searches the top half of the array.  The loops
	 * have been merged just to make things difficult.
	 */
	first = start;
	last = howmany(fs->fs_fpg, NBBY);
	secondtime = 0;
	cfrag = cgp->cg_cgx * fs->fs_fpg;
	while (first < last) {
		len = last - first;
		/*
		 * search the array for a match
		 */
		loc = scanc((unsigned)len, (u_char *)&cg_blksfree(cgp)[first],
			(u_char *)fragtbl[fs->fs_frag],
			(int)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
		/*
		 * match found
		 */
		if (loc) {
			bno = (last - loc) * NBBY;

			/*
			 * Found the byte in the map, sift
			 * through the bits to find the selected frag
			 */
			cgp->cg_frotor = bno;
			gotit = 0;
			for (i = bno + NBBY; bno < i; bno += fs->fs_frag) {
				blk = blkmap(fs, cg_blksfree(cgp), bno);
				blk <<= 1;
				field = around[allocsiz];
				subfield = inside[allocsiz];
				for (pos = 0;
				    pos <= fs->fs_frag - allocsiz;
				    pos++) {
					if ((blk & field) == subfield) {
						gotit++;
						break;
					}
					field <<= 1;
					subfield <<= 1;
				}
				if (gotit)
					break;
			}
			bno += pos;

			/*
			 * success if block is *not* being converted from
			 * metadata into userdata (harpy).  If so, ignore.
			 */
			if (!TRANS_ISCANCEL(ufsvfsp,
				ldbtob(fsbtodb(fs, (cfrag+bno))), fs->fs_bsize))
				return (bno);
			/*
			 * keep looking -- this block is being converted
			 */
			first = (last - loc) + 1;
			loc = 0;
			if (first < last)
				continue;
		}
		/*
		 * no usable matches in bottom half -- now search the top half
		 */
		if (secondtime)
			/*
			 * no usable matches in top half -- all done
			 */
			break;
		secondtime = 1;
		last = start + 1;
		first = 0;
	}
	/*
	 * no usable matches
	 */
	return ((daddr_t)-1);
}

#define	UFSNADDR (NDADDR + NIADDR)	/* NADDR applies to S5 only */
#define	IB(i)	(NDADDR + (i))	/* index of i'th indirect block ptr */
#define	SINGLE	0		/* single indirect block ptr */
#define	DOUBLE	1		/* double indirect block ptr */
#define	TRIPLE	2		/* triple indirect block ptr */

/*
 * Free storage space associated with the specified inode.  The portion
 * to be freed is specified by lp->l_start and lp->l_len (already
 * normalized to a "whence" of 0).

 * This is an experimental facility whose continued existence is not
 * guaranteed.  Currently, we only support the special case
 * of l_len == 0, meaning free to end of file.
 *
 * Blocks are freed in reverse order.  This FILO algorithm will tend to
 * maintain a contiguous free list much longer than FIFO.
 * See also ufs_itrunc() in ufs_inode.c.
 *
 * Bug: unused bytes in the last retained block are not cleared.
 * This may result in a "hole" in the file that does not read as zeroes.
 */
/* ARGSUSED */
int
ufs_freesp(vp, lp, flag, cr)
	register struct vnode *vp;
	register struct flock *lp;
	int flag;
	struct cred *cr;
{
	register int i;
	register struct inode *ip = VTOI(vp);
	int error;

	ASSERT(vp->v_type == VREG);
	ASSERT(lp->l_start >= 0);	/* checked by convoff */

	if (lp->l_len != 0)
		return (EINVAL);

	rw_enter(&ip->i_contents, RW_READER);
	if (ip->i_size == lp->l_start) {
		rw_exit(&ip->i_contents);
		return (0);
	}

	/*
	 * Check if there is any active mandatory lock on the
	 * range that will be truncated/expanded.
	 */
	if (MANDLOCK(vp, ip->i_mode)) {
		int save_start;

		save_start = lp->l_start;

		if (ip->i_size < lp->l_start) {
			/*
			 * "Truncate up" case: need to make sure there
			 * is no lock beyond current end-of-file. To
			 * do so, we need to set l_start to the size
			 * of the file temporarily.
			 */
			lp->l_start = ip->i_size;
		}
		lp->l_type = F_WRLCK;
		lp->l_sysid = 0;
		lp->l_pid = ttoproc(curthread)->p_pid;
		i = (flag & (FNDELAY|FNONBLOCK)) ? 0 : SLPFLCK;
		rw_exit(&ip->i_contents);
		if ((i = reclock(vp, lp, i, 0, lp->l_start)) != 0 ||
		    lp->l_type != F_UNLCK) {
			return (i ? i : EAGAIN);
		}
		rw_enter(&ip->i_contents, RW_READER);

		lp->l_start = save_start;
	}

	/*
	 * Make sure a write isn't in progress (allocating blocks)
	 * by acquiring i_rwlock (we promised ufs_bmap we wouldn't
	 * truncate while it was allocating blocks).
	 * Grab the locks in the right order.
	 */
	rw_exit(&ip->i_contents);
	rw_enter(&ip->i_rwlock, RW_WRITER);
	error = TRANS_ITRUNC(ip, lp->l_start, 0, cr);
	rw_exit(&ip->i_rwlock);
	return (error);
}
