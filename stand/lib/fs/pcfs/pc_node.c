/*
 * Copyright (c) 1989, 1992-1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)pc_node.c 1.6     94/08/09 SMI"
/*
 *	mini-MS-DOS filesystem for secondary boot
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include <sys/dirent.h>
#include "pcfilep.h"

#define	PCROOT	0x100	/* substitute for VROOT in pc_flags */

extern devid_t *fdevp;


struct pchead pcfhead[NPCHASH];
struct pchead pcdhead[NPCHASH];


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

void pc_diskchanged();

void
pc_init()
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
pc_getnode(fsp, blkno, offset, ep)
	struct pcfs *fsp;	/* filsystem for node */
	daddr_t blkno;		/* phys block no of dir entry */
	int offset;		/* offset of dir entry in block */
	struct pcdir *ep;	/* node dir entry */
{
	register struct pcnode *pcp;
	register struct pchead *hp;
	pc_cluster_t scluster;

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
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = pcp->pc_forw) {
			if (scluster == pcp->pc_scluster) {
				return (pcp);
			}
		}
	} else {
		hp = &pcfhead[PCFHASH(fsp, blkno, offset)];
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = pcp->pc_forw) {
			if ((pcp->pc_flags & PC_INVAL) == 0 &&
			    blkno == pcp->pc_eblkno &&
			    offset == pcp->pc_eoffset) {
				return (pcp);
			}
		}
	}
	/*
	 * Cannot find node in active list. Allocate memory for a new node
	 * initialize it, and put it on the active list.
	 */
	pcp = (struct pcnode *) kmem_alloc((u_int) sizeof (struct pcnode));
	bzero((caddr_t) pcp, sizeof (struct pcnode));
	pcp->pc_entry = *ep;
	pcp->pc_eblkno = blkno;
	pcp->pc_eoffset = offset;
	pcp->pc_scluster = scluster;
	pcp->pc_flags = 0;
	if (ep->pcd_attr & PCA_DIR) {
		if (scluster == 0) {
			pcp->pc_flags = PCROOT;
			blkno = offset = 0;
			pcp->pc_size = fsp->pcfs_rdirsec * fsp->pcfs_secsize;
		} else
			pcp->pc_size = pc_fileclsize(fsp, scluster) *
			    fsp->pcfs_clsize;
	} else {
		pcp->pc_size = ltohl(ep->pcd_size);
	}
	pcp->pc_back = (struct pcnode *) hp;
	pcp->pc_forw = hp->pch_forw;
	hp->pch_forw->pc_back = pcp;
	hp->pch_forw = pcp;

	return (pcp);
}


void
pc_rele(pcp)
	register struct pcnode *pcp;
{
	register struct pcfs *fsp;

	fsp = &fdevp->un_fs.di_pcfs;

	pcp->pc_back->pc_forw = pcp->pc_forw;
	pcp->pc_forw->pc_back = pcp->pc_back;

	kmem_free((caddr_t)pcp, (u_int)sizeof (struct pcnode));
}

/*
 * Verify that the disk in the drive is the same one that we
 * got the pcnode from.
 * MUST be called with node unlocked.
 */
int
pc_verify(struct pcfs *fsp)
{
	int fdstatus = 0;
	int error = 0;

	if (!(fsp->pcfs_flags & PCFS_NOCHK) && fsp->pcfs_fatp) {

		if (error = chk_diskette(fsp)) {
PCFSDEBUG(1)
printf("pc_verify: change detected\n");
			pc_diskchanged(fsp);
		}
	}
	if (!(error || fsp->pcfs_fatp)) {
		error = pc_getfat(fsp);
	}
	return (error);
}


/*
 * The disk has been changed!
 */
void
pc_diskchanged(fsp)
	register struct pcfs *fsp;
{
	register struct pcnode *pcp, * npcp;
	register struct pchead *hp;

	/*
	 * Eliminate all pcnodes (dir & file) associated to this fs.
	 * Invalidate the in core FAT.
	 */
PCFSDEBUG(1)
printf("pc_diskchanged fsp=0x%x\n", fsp);

	for (hp = pcdhead; hp < &pcdhead[NPCHASH]; hp++) {
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = npcp) {
			npcp = pcp -> pc_forw;
				pcp->pc_back->pc_forw = pcp->pc_forw;
				pcp->pc_forw->pc_back = pcp->pc_back;
				pcp->pc_vn = NULL;
				kmem_free((caddr_t) pcp,
					sizeof (struct pcnode));
		}
	}
	for (hp = pcfhead; hp < &pcfhead[NPCHASH]; hp++) {
		for (pcp = hp->pch_forw;
		    pcp != (struct pcnode *)hp; pcp = npcp) {
			npcp = pcp -> pc_forw;
				pcp->pc_back->pc_forw = pcp->pc_forw;
				pcp->pc_forw->pc_back = pcp->pc_back;
				pcp->pc_vn = NULL;
				kmem_free((caddr_t) pcp,
					sizeof (struct pcnode));
		}
	}
	if (fsp->pcfs_fatp != (u_char *)0) {
		pc_invalfat(fsp);
	}
}
