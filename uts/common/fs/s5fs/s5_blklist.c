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


#ident "@(#)s5_blklist.c	1.3	94/01/21 SMI"
#include "sys/types.h"
#include "sys/t_lock.h"
#include "sys/buf.h"
#include "sys/cmn_err.h"
#include "sys/conf.h"
#include "sys/cred.h"
#include "sys/debug.h"
#include "sys/errno.h"
#include "sys/fcntl.h"
#include "sys/file.h"
#include "sys/flock.h"
#include "sys/param.h"
#include "sys/stat.h"
#include "sys/sysmacros.h"
#include "sys/systm.h"
#include "sys/var.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/mode.h"
#include "sys/user.h"
#include "sys/kmem.h"

#include "vm/pvn.h"

#include "sys/fs/s5_fs.h"
#include "sys/fs/s5_inode.h"
#include "fs/fs_subr.h"

int ufs_bldblklst();
#if 0	/* Block address map not used in Solaris 2.0 */


/*
 *  Allocate and build the block address map
 */

s5_allocmap(ip)
register struct inode *ip;
{
	register int	*bnptr;
	register int	bsize;
	register int	nblks;
	register struct vnode *vp;

	vp = ITOV(ip);
	ASSERT(RW_WRITE_HELD(&ip->i_contents));
	if (ip->i_map)
		return (1);

	/*
	 * Get number of blocks to be mapped.
	 */

	ASSERT(ip->i_map == 0);
	bsize = VBSIZE(vp);
	nblks = (ip->i_size + bsize - 1)/bsize;
	bnptr = ip->i_map = kmem_zalloc(sizeof (int) * nblks, KM_SLEEP);

	/*
	 * Build the actual list of block numbers for the file.
	 */

	(void) ufs_bldblklst(bnptr, ip, nblks);

	/*
	 * If the size is not an integral number of
	 * pages long, then the last few block
	 * number up to the next page boundary are
	 * made zero so that no one will try to
	 * read them in.  See code in fault.c/vfault.
	 */

	/*
	 * This is done by using kmemzalloc above
	 * XXX Should this be filled out with zeroes (no allocation) or
	 * allocate on write  (-1)  How to get bnptr updated by ufs_bldblklst??
	 */
#if 0
	while (nblks%blkspp != 0) {
		*bnptr++ = -1;
		nblks++;
	}
#endif
	return (1);
}

/*
 *	Build the list of block numbers for a file.  This is used
 *	for mapped files.
 */

ufs_bldblklst(lp, ip, nblks)
register int		*lp;
register struct inode	*ip;
register int		nblks;
{
	register int	lim;
	register int	*eptr;
	register int	i;
	register struct vnode *vp;
	int		*ufs_bldindr();
	dev_t	 dev;

	/*
	 * Get the block numbers from the direct blocks first.
	 */

	vp = ITOV(ip);
	eptr = &lp[nblks];
	if (nblks < NDADDR)
		lim = nblks;
	else
		lim = NDADDR;

	for (i = 0; i < lim; i++)
		*lp++ = ip->i_db[i];

	if (lp >= eptr)
		return (1);

	dev = vp->v_vfsp->vfs_dev;
	i = 0;
	while (lp < eptr) {
		lp = ufs_bldindr(ip, lp, eptr, dev, (int) ip->i_ib[i], i);
		if (lp == 0)
			return (0);
		i++;
	}
	return (1);
}

int  *
ufs_bldindr(ip, lp, eptr, dev, blknbr, indlvl)
struct inode 		*ip;
register int		*lp;
register int		*eptr;
register dev_t		dev;
int			blknbr;
int			indlvl;
{
	register struct buf *bp;
	register int	*bnptr;
	int		cnt;
	struct buf 	*bread();
	int 		bsize;
	struct vnode	*vp;
	struct fs *fs;
	int sksize;

	vp = ITOV(ip);
	bsize = vp->v_vfsp->vfs_bsize;

	/*
	 * XXX - sksize will be of the wrong size when
	 * set this way...  It is set to the maximum number of blocks
	 * that can be addressed by a given indlvl whereas the array is
	 * only allocated equal to the size of ip->i_isize / VBSIZE(vp)
	 * in ufs_allocmap(ip)
	 */
	if (blknbr == 0) {
		sksize = 1;
		for (cnt = 0; cnt <= indlvl; cnt++)
			sksize *= (bsize/sizeof (int));
		for (cnt = 0; cnt < sksize; cnt++)
			*lp++ = 0;
		return (lp);
	}
	fs = getfs(vp->v_vfsp);
	bp = bread(dev, fsbtodb(fs, blknbr), bsize);
	if (ttolwp(curthread)->lwp_error) {
		brelse(bp);
		return ((int *) 0);
	}
	bnptr = bp->b_un.b_words;
	cnt = NINDIR(ip->i_s5vfs);

	ASSERT(indlvl >= 0);
	while (cnt-- && lp < eptr) {
		if (indlvl == 0) {
			*lp++ = *bnptr++;
		} else {
			lp = ufs_bldindr(ip, lp, eptr, dev, *bnptr++, indlvl-1);
			if (lp == 0) {
				brelse(bp);
				return ((int *) 0);
			}
		}
	}

	brelse(bp);
	return (lp);
}

/*
 *	Free the block list attached to an inode.
 */

void
ufs_freemap(ip)
struct inode	*ip;
{
	register int	nblks;
	register	bsize;
	register struct vnode *vp;
	register int	type;

	vp = ITOV(ip);
	ASSERT(RW_LOCK_HELD(&ip->i_contents));

	type = ip->i_mode & IFMT;
	if (type != IFREG || ip->i_map == NULL)
		return;

	bsize = VBSIZE(vp);
	nblks = (ip->i_size + bsize - 1)/bsize;

	kmem_free(ip->i_map, nblks*sizeof (int));
	ip->i_map = NULL;
}
#endif	/* block map address not used in Solaris 2.0 */
