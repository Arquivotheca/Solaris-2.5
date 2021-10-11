/*
 * Copyright (c) 1989, 1992 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)pc_node.c 1.20     95/07/23 SMI"

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/cred.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <vm/pvn.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include <sys/dirent.h>
#include <sys/fdio.h>

struct pchead pcfhead[NPCHASH];
struct pchead pcdhead[NPCHASH];

extern krwlock_t pcnodes_lock;

void		pc_init(void);
struct pcnode	*pc_getnode(struct pcfs *, daddr_t, int, struct pcdir *);
void		pc_rele(struct pcnode *);
void		pc_mark(struct pcnode *);
int		pc_truncate(struct pcnode *, long);
int		pc_nodesync(struct pcnode *);
int		pc_nodeupdate(struct pcnode *);
int		pc_verify(struct pcfs *);
void		pc_gldiskchanged(struct pcfs *);
void		pc_diskchanged(struct pcfs *);

static int	pc_getentryblock(struct pcnode *, struct buf **);
static int	syncpcp(struct pcnode *, int);


/*
 * fake entry for root directory, since this does not have a parent
 * pointing to it.
 */
static struct pcdir rootentry = {
	"",
	"",
	PCA_DIR,
	0,
	"",
	{0, 0},
	0,
	0
};

void
pc_init(void)
{
	register struct pchead *hdp, *hfp;
	register int i;
	for (i = 0; i < NPCHASH; i++) {
		hdp = &pcdhead[i];
		hfp = &pcfhead[i];
		hdp->pch_forw =  (struct pcnode *)hdp;
		hdp->pch_back =  (struct pcnode *)hdp;
		hfp->pch_forw =  (struct pcnode *)hfp;
		hfp->pch_back =  (struct pcnode *)hfp;
	}
}

struct pcnode *
pc_getnode(
	register struct pcfs *fsp,	/* filsystem for node */
	register daddr_t blkno,		/* phys block no of dir entry */
	register int offset,		/* offset of dir entry in block */
	register struct pcdir *ep)	/* node dir entry */
{
	register struct pcnode *pcp;
	register struct pchead *hp;
	register struct vnode *vp;
	register pc_cluster_t scluster;

	if (!(fsp->pcfs_flags & PCFS_LOCKED))
		panic("pc_getnode");
	if (ep == (struct pcdir *)0) {
		ep = &rootentry;
		scluster = 0;
	} else {
		scluster = ltohs(ep->pcd_scluster);
	}
	/*
	 * First look for active nodes.
	 * File nodes are identified by the location (blkno, offset) of
	 * its directory entry.
	 * Directory nodes are identified by the starting cluster number
	 * for the entries.
	 */
	if (ep->pcd_attr & PCA_DIR) {
		hp = &pcdhead[PCDHASH(fsp, scluster)];
		rw_enter(&pcnodes_lock, RW_READER);
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = pcp->pc_forw) {
			if ((fsp == VFSTOPCFS(PCTOV(pcp)->v_vfsp)) &&
			    (scluster == pcp->pc_scluster)) {
				VN_HOLD(PCTOV(pcp));
				rw_exit(&pcnodes_lock);
				return (pcp);
			}
		}
		rw_exit(&pcnodes_lock);
	} else {
		hp = &pcfhead[PCFHASH(fsp, blkno, offset)];
		rw_enter(&pcnodes_lock, RW_READER);
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = pcp->pc_forw) {
			if ((fsp == VFSTOPCFS(PCTOV(pcp)->v_vfsp)) &&
			    ((pcp->pc_flags & PC_INVAL) == 0) &&
			    (blkno == pcp->pc_eblkno) &&
			    (offset == pcp->pc_eoffset)) {
				VN_HOLD(PCTOV(pcp));
				rw_exit(&pcnodes_lock);
				return (pcp);
			}
		}
		rw_exit(&pcnodes_lock);
	}
	/*
	 * Cannot find node in active list. Allocate memory for a new node
	 * initialize it, and put it on the active list.
	 */
	pcp = (struct pcnode *)
		kmem_alloc((u_int)sizeof (struct pcnode), KM_SLEEP);
	bzero((caddr_t)pcp, sizeof (struct pcnode));
	vp = (struct vnode *) kmem_alloc(sizeof (struct vnode), KM_SLEEP);
	bzero((caddr_t) vp, sizeof (struct vnode));
	mutex_init(&vp->v_lock, "pcfs v_lock", MUTEX_DEFAULT, DEFAULT_WT);
	pcp->pc_vn = vp;
	pcp->pc_entry = *ep;
	pcp->pc_eblkno = blkno;
	pcp->pc_eoffset = offset;
	pcp->pc_scluster = scluster;
	pcp->pc_flags = 0;
	vp->v_count = 1;
	if (ep->pcd_attr & PCA_DIR) {
		vp->v_op = &pcfs_dvnodeops;
		vp->v_type = VDIR;
		vp->v_flag = VNOMAP; /* directory io go through buffer cache */
		if (scluster == 0) {
			vp->v_flag = VROOT;
			blkno = offset = 0;
			pcp->pc_size = fsp->pcfs_rdirsec * fsp->pcfs_secsize;
		} else
			pcp->pc_size = pc_fileclsize(fsp, scluster) *
			    fsp->pcfs_clsize;
	} else {
		vp->v_op = &pcfs_fvnodeops;
		vp->v_type = VREG;
		vp->v_flag = VNOSWAP;
		fsp->pcfs_frefs++;
		pcp->pc_size = ltohl(ep->pcd_size);
	}
	fsp->pcfs_nrefs++;
	vp->v_data = (caddr_t)pcp;
	vp->v_vfsp = PCFSTOVFS(fsp);
	rw_enter(&pcnodes_lock, RW_WRITER);
	insque(pcp, hp);
	rw_exit(&pcnodes_lock);
	return (pcp);
}

int
syncpcp(register struct pcnode *pcp, int flags)
{
	int err;
	if (PCTOV(pcp)->v_pages == NULL)
		err = 0;
	else
		err = VOP_PUTPAGE(PCTOV(pcp), (offset_t)0, (u_int)0,
		    flags, (struct cred *)0);

	return (err);
}

void
pc_rele(register struct pcnode *pcp)
{
	register struct pcfs *fsp;
	struct vnode * vp;
	int err;
	vp = PCTOV(pcp);
PCFSDEBUG(8)
prom_printf("pc_rele vp=0x%x\n", vp);

	fsp = VFSTOPCFS(vp->v_vfsp);
	if ((fsp->pcfs_flags & PCFS_LOCKED) == 0) {
		panic("pc_rele");
	}
	pcp->pc_flags |= PC_RELEHOLD;

	if (vp->v_type != VDIR && (pcp->pc_flags & PC_INVAL) == 0) {
		/*
		 * If the file was removed while active it may be safely
		 * truncated now.
		 */

		if (pcp->pc_entry.pcd_filename[0] == PCD_ERASED) {
			(void) pc_truncate(pcp, 0L);
		} else if (pcp->pc_flags & PC_CHG) {
			(void) pc_nodeupdate(pcp);
		}
		err = syncpcp(pcp, B_INVAL);
		if (err) {
			(void) syncpcp(pcp, B_INVAL|B_FORCE);
		}
	}
	(void) pc_syncfat(fsp);
	ASSERT(vp->v_pages == 0);

	mutex_enter(&vp->v_lock);
	vp->v_count--;  /* release our hold from vn_rele */
	if (vp->v_count > 0) { /* Is this check still needed? */
PCFSDEBUG(3)
prom_printf("pc_rele: pcp=0x%x HELD AGAIN!\n", pcp);
		mutex_exit(&vp->v_lock);
		return;
	}
	mutex_exit(&vp->v_lock);

	/* The pcnode can't be pcfs_lookup here because we lock the pcfs. */
	rw_enter(&pcnodes_lock, RW_WRITER);
	remque(pcp);
	rw_exit(&pcnodes_lock);
	if ((vp->v_type == VREG) && ! (pcp->pc_flags & PC_INVAL)) {
		fsp->pcfs_frefs--;
	}
	fsp->pcfs_nrefs--;

	if (fsp->pcfs_nrefs < 0) {
		panic("pc_rele: nrefs count");
	}
	if (fsp->pcfs_frefs < 0) {
		panic("pc_rele: frefs count");
	}

	mutex_destroy(&vp->v_lock);
	kmem_free((caddr_t) vp, sizeof (struct vnode));
	kmem_free((caddr_t)pcp, (u_int)sizeof (struct pcnode));
}

/*
 * Mark a pcnode as modified with the current time.
 */
void
pc_mark(register struct pcnode *pcp)
{
	if (PCTOV(pcp)->v_type == VREG) {
		pc_tvtopct(&hrestime, &pcp->pc_entry.pcd_mtime);
		pcp->pc_flags |= PC_CHG;
	}
}

/*
 * Truncate a file to a length.
 * Node must be locked.
 */
int
pc_truncate(register struct pcnode *pcp, long length)
{
	register struct pcfs *fsp;
	struct vnode * vp;
	u_int off;
	int error = 0;

PCFSDEBUG(4)
prom_printf("pc_truncate pcp=0x%x, len=%d, size=%d\n",
pcp, length, pcp->pc_size);
	vp = PCTOV(pcp);
	if (pcp->pc_flags & PC_INVAL)
		return (EIO);
	fsp = VFSTOPCFS(vp->v_vfsp);
	/*
	 * directories are always truncated to zero and are not marked
	 */
	if (vp->v_type == VDIR) {
		error = pc_bfree(pcp, (pc_cluster_t)0);
		return (error);
	}
	/*
	 * If length is the same as the current size
	 * just mark the pcnode and return.
	 */
	if (length > pcp->pc_size) {
		daddr_t bno;
		u_int llcn;

		/*
		 * We are extending a file.
		 * Extend it with _one_ call to pc_balloc (no holes)
		 * since we don't need the use the block number(s).
		 */
		if (howmany(pcp->pc_size, fsp->pcfs_clsize) <
		    (llcn = howmany(length, fsp->pcfs_clsize))) {
			error = pc_balloc(pcp, (daddr_t) (llcn - 1), 1, &bno);
		}
		if (error) {
PCFSDEBUG(2)
prom_printf("pc_truncate: error=%d\n", error);
			/*
			 * probably ran out disk space;
			 * determine current file size
			 */
			pcp->pc_size = fsp->pcfs_clsize *
			    pc_fileclsize(fsp, pcp->pc_scluster);
		} else
			pcp->pc_size = length;

	} else if (length < pcp->pc_size) {
		/*
		 * We are shrinking a file.
		 * Free blocks after the block that length points to.
		 */
		off = pc_blkoff(fsp, length);
		if (off == 0) {
			pvn_vplist_dirty(PCTOV(pcp), (u_int)length,
				pcfs_putapage, B_INVAL | B_TRUNC, CRED());
		} else {
			pvn_vpzero(PCTOV(pcp), (u_int)length,
			    (u_int)(fsp->pcfs_clsize - off));
			pvn_vplist_dirty(PCTOV(pcp), (u_int)length,
				pcfs_putapage, B_INVAL | B_TRUNC, CRED());
		}
		error = pc_bfree(pcp,
		    (pc_cluster_t)howmany(length, fsp->pcfs_clsize));
		pcp->pc_size = length;
	}
	pc_mark(pcp);
	return (error);
}

/*
 * Get block for entry.
 */
static int
pc_getentryblock(register struct pcnode *pcp, struct buf **bpp)
{
	register struct pcfs *fsp;

PCFSDEBUG(7)
prom_printf("pc_getentryblock ");
	fsp = VFSTOPCFS(PCTOV(pcp)->v_vfsp);
	if (pcp->pc_eblkno >= fsp->pcfs_datastart ||
	    (pcp->pc_eblkno - fsp->pcfs_rdirstart) <
	    (fsp->pcfs_rdirsec & ~(fsp->pcfs_spcl - 1))) {
		*bpp = bread(fsp->pcfs_xdev,
		    pc_dbdaddr(fsp, pcp->pc_eblkno), fsp->pcfs_clsize);
	} else {
		*bpp = bread(fsp->pcfs_xdev,
		    pc_dbdaddr(fsp, pcp->pc_eblkno),
		    (int) (fsp->pcfs_datastart-pcp->pc_eblkno) *
		    fsp->pcfs_secsize);
	}
	if ((*bpp)->b_flags & B_ERROR) {
PCFSDEBUG(1)
prom_printf("pc_getentryblock: error ");
		brelse(*bpp);
		pc_gldiskchanged(fsp);
		return (EIO);
	}
	return (0);
}

/*
 * Sync all data associated with a file.
 * Flush all the blocks in the buffer cache out to disk, sync the FAT and
 * update the directory entry.
 */
int
pc_nodesync(register struct pcnode *pcp)
{
	register struct pcfs *fsp;
	int err;
	struct vnode * vp;

PCFSDEBUG(7)
prom_printf("pc_nodesync pcp=0x%x\n", pcp);
	vp = PCTOV(pcp);
	fsp = VFSTOPCFS(vp->v_vfsp);
	if ((pcp->pc_flags & (PC_MOD | PC_CHG)) && (vp->v_type == VDIR)) {
		panic("pc_nodesync");
	}
	err = 0;
	if (pcp->pc_flags & PC_MOD) {
		/*
		 * Flush all data blocks from buffer cache and
		 * update the FAT which points to the data.
		 */
		if (err = syncpcp(pcp, 0)) { /* %% ?? how to handle error? */
			if (err == ENOMEM)
				return (err);
			else {
				pc_diskchanged(fsp);
				return (EIO);
			}
		}
		pcp->pc_flags &= ~PC_MOD;
	}
	/*
	 * update the directory entry
	 */
	if (pcp->pc_flags & PC_CHG)
		(void) pc_nodeupdate(pcp);
	return (err);
}

/*
 * Update the node's directory entry.
 */
int
pc_nodeupdate(register struct pcnode *pcp)
{
	struct buf *bp;
	int error;
	struct vnode * vp;

	vp = PCTOV(pcp);
	if (vp -> v_flag & VROOT) {
		panic("pc_nodeupdate");
	}
	if (pcp->pc_flags & PC_INVAL)
		return (0);
PCFSDEBUG(7)
prom_printf("pc_nodeupdate pcp=0x%x, bn=%d, off=%d\n",
pcp, pcp->pc_eblkno, pcp->pc_eoffset);

	if (error = pc_getentryblock(pcp, &bp)) {
		return (error);
	}
	if (vp->v_type == VREG) {
		if (pcp->pc_flags & PC_CHG)
			pcp->pc_entry.pcd_attr |= PCA_ARCH;
		pcp->pc_entry.pcd_size = htoll(pcp->pc_size);
	}
	pcp->pc_entry.pcd_scluster = htols(pcp->pc_scluster);
	*((struct pcdir *)(bp->b_un.b_addr + pcp->pc_eoffset)) = pcp->pc_entry;
	bwrite2(bp);
	error = geterror(bp);
	if (error)
		error = EIO;
	brelse(bp);
	if (error) {
PCFSDEBUG(1)
prom_printf("pc_nodeupdate ERROR\n");
		pc_gldiskchanged(VFSTOPCFS(vp->v_vfsp));
	}
	pcp->pc_flags &= ~PC_CHG;
	return (error);
}

/*
 * Verify that the disk in the drive is the same one that we
 * got the pcnode from.
 * MUST be called with node unlocked.
 */
/* ARGSUSED */
int
pc_verify(struct pcfs *fsp)
{
	int fdstatus = 0;
	int error = 0;

	if (!(fsp->pcfs_flags & PCFS_NOCHK) && fsp->pcfs_fatp) {
PCFSDEBUG(4)
prom_printf("pc_verify fsp=0x%x\n", fsp);

#ifdef NEXT_VERSION
		/*
		 * FDGETCHANGE needs to be fixed so that FDGC_HISTORY
		 * is correct for each call, not just last floppy
		 * i/o operation
		 */
		error = cdev_ioctl(fsp->pcfs_vfs->vfs_dev,
		    (int) FDGETCHANGE, (int) &fdstatus, FKIOCTL, NULL, NULL);

#endif	/* NEXT_VERSION */
		if (error || (fdstatus & FDGC_HISTORY)) {
PCFSDEBUG(1)
prom_printf("pc_verify: change detected\n");
			pc_gldiskchanged(fsp);
		}
	}
	if (!(error || fsp->pcfs_fatp)) {
		error = pc_getfat(fsp);
	}

	return (error);
}

/*
 * The disk has changed, grab lock and call pc_diskchanged.
 */
void
pc_gldiskchanged(register struct pcfs *fsp)
{
	if (!(fsp->pcfs_flags & PCFS_NOCHK)) {
		rw_enter(&pcnodes_lock, RW_WRITER);
		pc_diskchanged(fsp);
		rw_exit(&pcnodes_lock);
	}
}


/*
 * The disk has been changed!
 */
void
pc_diskchanged(register struct pcfs *fsp)
{
	register struct pcnode *pcp, *npcp = NULL;
	register struct pchead *hp;

	/*
	 * Eliminate all pcnodes (dir & file) associated to this fs.
	 * If the node is internal, ie, no references outside of
	 * pcfs itself, then release the associated vnode structure.
	 * Invalidate the in core FAT.
	 * Invalidate cached data blocks and blocks waiting for I/O.
	 */
PCFSDEBUG(1)
prom_printf("pc_diskchanged fsp=0x%x\n", fsp);

	printf("I/O error or floppy disk change: possible file damage\n");
	for (hp = pcdhead; hp < &pcdhead[NPCHASH]; hp++) {
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = npcp) {
			npcp = pcp -> pc_forw;
			if (VFSTOPCFS(PCTOV(pcp)->v_vfsp) == fsp &&
			    !(pcp->pc_flags & PC_RELEHOLD)) {
				remque(pcp);
				PCTOV(pcp)->v_data = NULL;
				if (! (pcp->pc_flags & PC_EXTERNAL)) {
					kmem_free((caddr_t) PCTOV(pcp),
					    sizeof (struct vnode));
				}
				kmem_free((caddr_t) pcp,
					sizeof (struct pcnode));
				fsp -> pcfs_nrefs --;
			}
		}
	}
	for (hp = pcfhead; fsp->pcfs_frefs && hp < &pcfhead[NPCHASH]; hp++) {
		for (pcp = hp->pch_forw; fsp->pcfs_frefs &&
		    pcp != (struct pcnode *)hp; pcp = npcp) {
			npcp = pcp -> pc_forw;
			if (VFSTOPCFS(PCTOV(pcp)->v_vfsp) == fsp &&
			    !(pcp->pc_flags & PC_RELEHOLD)) {
				remque(pcp);
				PCTOV(pcp)->v_data = NULL;
				if (! (pcp->pc_flags & PC_EXTERNAL)) {
					kmem_free((caddr_t) PCTOV(pcp),
						sizeof (struct vnode));
				}
				kmem_free((caddr_t) pcp,
					sizeof (struct pcnode));
				fsp -> pcfs_frefs --;
				fsp -> pcfs_nrefs --;
			}
		}
	}
#ifdef undef
	if (fsp->pcfs_frefs) {
		rw_exit(&pcnodes_lock);
		panic("pc_diskchanged: frefs");
	}
	if (fsp->pcfs_nrefs) {
		rw_exit(&pcnodes_lock);
		panic("pc_diskchanged: nrefs");
	}
#endif
	if (fsp->pcfs_fatp != (u_char *)0) {
		pc_invalfat(fsp);
	} else {
		binval(fsp->pcfs_xdev);
	}
}
