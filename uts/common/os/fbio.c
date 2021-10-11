/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fbio.c	1.16	94/12/03 SMI"	/* SVr4.0 1.10	*/

#include "sys/types.h"
#include "sys/buf.h"
#include "sys/cmn_err.h"
#include "sys/conf.h"
#include "sys/debug.h"
#include "sys/errno.h"
#include "sys/fbuf.h"
#include "sys/kmem.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/vfs.h"
#include "sys/cred.h"
#include "sys/vnode.h"


#include "vm/hat.h"
#include "vm/as.h"
#include "vm/seg.h"
#include "vm/seg_kmem.h"
#include "vm/seg_map.h"

/*
 * Pseudo-bio routines which use a segmap mapping to address file data.
 */

/*
 * Variables for maintaining the free list of fbuf structures.
 */
static struct fbuf *fb_free;
extern kmutex_t	fb_kmemlock;

/*
 * Return a pointer to locked kernel virtual address for
 * the given <vp, off> for len bytes.  It is not allowed to
 * have the offset cross a MAXBSIZE boundary over len bytes.
 */
int
fbread(vp, off, len, rw, fbpp)
	struct vnode *vp;
	register off_t off;
	uint len;
	enum seg_rw rw;
	struct fbuf **fbpp;
{
	register caddr_t addr;
	register u_int o;
	register struct fbuf *fbp;
	faultcode_t err;
	caddr_t	raddr;
	u_int	rsize;

	o = off & MAXBOFFSET;
	if (o + len > MAXBSIZE)
		cmn_err(CE_PANIC, "fbread");
	addr = segmap_getmapflt(segkmap, vp, off & MAXBMASK, MAXBSIZE, 0, rw);

	raddr = (caddr_t)((u_int)(addr + o) & PAGEMASK);
	rsize = (((u_int)(addr + o + len) + PAGEOFFSET) & PAGEMASK) -
		(u_int)raddr;

	err = segmap_fault(kas.a_hat, segkmap, raddr, rsize, F_SOFTLOCK, rw);
	if (err) {
		(void) segmap_release(segkmap, addr, 0);
		if (FC_CODE(err) == FC_OBJERR)
			return (FC_ERRNO(err));
		else
			return (EIO);
	}

	mutex_enter(&fb_kmemlock);
	if ((fbp = fb_free) == NULL) {
		mutex_exit(&fb_kmemlock);
		fbp = kmem_perm_alloc(sizeof (struct fbuf), 0, KM_SLEEP);
	} else {
		fb_free = (struct fbuf *)(fbp->fb_addr);
		mutex_exit(&fb_kmemlock);
	}

	fbp->fb_addr = addr + o;
	fbp->fb_count = len;
	*fbpp = fbp;
	return (0);
}

/*
 * Similar to fbread() but we call segmap_pagecreate instead of using
 * segmap_fault for SOFTLOCK to create the pages without using VOP_GETPAGE
 * and then we zero up to the length rounded to a page boundary.
 * XXX - this won't work right when bsize < PAGESIZE!!!
 */
void
fbzero(vp, off, len, fbpp)
	struct vnode *vp;
	off_t off;
	uint len;
	struct fbuf **fbpp;
{
	caddr_t addr;
	register uint o, zlen;
	struct fbuf *fbp;

	o = off & MAXBOFFSET;
	if (o + len > MAXBSIZE)
		cmn_err(CE_PANIC, "fbzero: Bad offset/length");
	addr = segmap_getmap(segkmap, vp, off & MAXBMASK) + o;

	mutex_enter(&fb_kmemlock);
	if ((fbp = fb_free) == NULL) {
		mutex_exit(&fb_kmemlock);
		fbp = kmem_perm_alloc(sizeof (struct fbuf), 0, KM_SLEEP);
	} else {
		fb_free = (struct fbuf *)(fbp->fb_addr);
		mutex_exit(&fb_kmemlock);
	}

	fbp->fb_addr = addr;
	fbp->fb_count = len;
	*fbpp = fbp;

	segmap_pagecreate(segkmap, addr, len, 1);

	/*
	 * Now we zero all the memory in the mapping we are interested in.
	 */
	zlen = (caddr_t)ptob(btopr(len + addr)) - addr;
	if (zlen < len || (o + zlen > MAXBSIZE))
		cmn_err(CE_PANIC, "fbzero: Bad zlen");
	bzero(addr, zlen);
}

/*
 * Release the fbp using the rw mode specified.
 */
void
fbrelse(fbp, rw)
	register struct fbuf *fbp;
	enum seg_rw rw;
{
	caddr_t addr;
	u_int	size;

	addr = (caddr_t)((u_int)fbp->fb_addr & PAGEMASK);
	size =
	    (((u_int)(fbp->fb_addr + fbp->fb_count) + PAGEOFFSET) & PAGEMASK) -
	    (u_int)addr;
	(void) segmap_fault(kas.a_hat, segkmap, addr, size, F_SOFTUNLOCK, rw);

	addr = (caddr_t)((uint)fbp->fb_addr & MAXBMASK);
	(void) segmap_release(segkmap, addr, 0);
	mutex_enter(&fb_kmemlock);
	fbp->fb_addr = (caddr_t)fb_free;
	fb_free = (struct fbuf *)fbp;
	mutex_exit(&fb_kmemlock);
}

/*
 * KLUDGE - variant of fbrelse() that invalidates the pages upon releaseing.
 */
void
fbrelsei(fbp, rw)
	register struct fbuf *fbp;
	enum seg_rw rw;
{
	caddr_t addr;
	u_int	size;

	addr = (caddr_t)((u_int)fbp->fb_addr & PAGEMASK);
	size =
	    (((u_int)(fbp->fb_addr + fbp->fb_count) + PAGEOFFSET) & PAGEMASK) -
	    (u_int)addr;
	(void) segmap_fault(kas.a_hat, segkmap, addr, size, F_SOFTUNLOCK, rw);

	addr = (caddr_t)((uint)fbp->fb_addr & MAXBMASK);
	(void) segmap_release(segkmap, addr, SM_INVAL);
	mutex_enter(&fb_kmemlock);
	fbp->fb_addr = (caddr_t)fb_free;
	fb_free = (struct fbuf *)fbp;
	mutex_exit(&fb_kmemlock);
}

/*
 * Perform a direct write using segmap_release and the mapping
 * information contained in the inode.  Upon return the fbp is
 * invalid.
 */
int
fbwrite(fbp)
	register struct fbuf *fbp;
{
	int err;
	caddr_t addr;
	u_int	size;

	addr = (caddr_t)((u_int)fbp->fb_addr & PAGEMASK);
	size =
	    (((u_int)(fbp->fb_addr + fbp->fb_count) + PAGEOFFSET) & PAGEMASK) -
	    (u_int)addr;
	(void) segmap_fault(kas.a_hat, segkmap, addr, size, F_SOFTUNLOCK,
		S_WRITE);

	addr = (caddr_t)((uint)fbp->fb_addr & MAXBMASK);
	err = segmap_release(segkmap, addr, SM_WRITE);
	mutex_enter(&fb_kmemlock);
	fbp->fb_addr = (caddr_t)fb_free;
	fb_free = (struct fbuf *)fbp;
	mutex_exit(&fb_kmemlock);
	return (err);
}

/*
 * KLUDGE - variant of fbwrite() that does a delayed write
 */
int
fbdwrite(fbp)
	register struct fbuf *fbp;
{
	int err;
	caddr_t addr;
	u_int	size;

	addr = (caddr_t)((u_int)fbp->fb_addr & PAGEMASK);
	size =
	    (((u_int)(fbp->fb_addr + fbp->fb_count) + PAGEOFFSET) & PAGEMASK) -
	    (u_int)addr;
	(void) segmap_fault(kas.a_hat, segkmap, addr, size, F_SOFTUNLOCK,
		S_WRITE);
	addr = (caddr_t)((uint)fbp->fb_addr & MAXBMASK);
	err = segmap_release(segkmap, addr, 0);
	mutex_enter(&fb_kmemlock);
	fbp->fb_addr = (caddr_t)fb_free;
	fb_free = (struct fbuf *)fbp;
	mutex_exit(&fb_kmemlock);
	return (err);
}

/*
 * KLUDGE - variant of fbwrite() that invalidates the pages upon releasing
 */
int
fbwritei(fbp)
	register struct fbuf *fbp;
{
	int err;
	caddr_t addr;
	u_int	size;

	addr = (caddr_t)((u_int)fbp->fb_addr & PAGEMASK);
	size =
	    (((u_int)(fbp->fb_addr + fbp->fb_count) + PAGEOFFSET) & PAGEMASK) -
	    (u_int)addr;
	(void) segmap_fault(kas.a_hat, segkmap, addr, size, F_SOFTUNLOCK,
		S_WRITE);
	addr = (caddr_t)((uint)fbp->fb_addr & MAXBMASK);
	err = segmap_release(segkmap, addr, SM_WRITE | SM_INVAL);
	mutex_enter(&fb_kmemlock);
	fbp->fb_addr = (caddr_t)fb_free;
	fb_free = (struct fbuf *)fbp;
	mutex_exit(&fb_kmemlock);
	return (err);
}

/*
 * Perform a synchronous indirect write of the given block number
 * on the given device, using the given fbuf.  Upon return the fbp
 * is invalid.
 */
int
fbiwrite(fbp, devvp, bn, bsize)
	register struct fbuf *fbp;
	register struct vnode *devvp;
	daddr_t bn;
	int bsize;
{
	register struct buf *bp;
	int error;
	caddr_t addr;
	u_int	size;

	/*
	 * Allocate a temp bp using pageio_setup, but then use it
	 * for physio to the area mapped by fbuf which is currently
	 * all locked down in place.
	 *
	 * XXX - need to have a generalized bp header facility
	 * which we build up pageio_setup on top of.  Other places
	 * (like here and in device drivers for the raw I/O case)
	 * could then use these new facilities in a more straight
	 * forward fashion instead of playing all these games.
	 */
	bp = pageio_setup((struct page *)NULL, fbp->fb_count, devvp, B_WRITE);
	bp->b_flags &= ~B_PAGEIO;		/* XXX */
	bp->b_un.b_addr = fbp->fb_addr;

	bp->b_blkno = bn * btod(bsize);
	bp->b_dev = cmpdev(devvp->v_rdev);	/* store in old dev format */
	bp->b_edev = devvp->v_rdev;
	bp->b_proc = NULL;			/* i.e. the kernel */

	bdev_strategy(bp);

	error = biowait(bp);
	pageio_done(bp);

	addr = (caddr_t)((u_int)fbp->fb_addr & PAGEMASK);
	size =
	    (((u_int)(fbp->fb_addr + fbp->fb_count) + PAGEOFFSET) & PAGEMASK) -
	    (u_int)addr;
	(void) segmap_fault(kas.a_hat, segkmap, addr, size, F_SOFTUNLOCK,
		S_OTHER);
	addr = (caddr_t)((uint)fbp->fb_addr & MAXBMASK);
	if (error == 0)
		error = segmap_release(segkmap, addr, 0);
	else
		(void) segmap_release(segkmap, addr, 0);
	mutex_enter(&fb_kmemlock);
	fbp->fb_addr = (caddr_t)fb_free;
	fb_free = (struct fbuf *)fbp;
	mutex_exit(&fb_kmemlock);
	return (error);
}
