/*
 * Copyright (c) 1989, 1992-1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)pc_alloc.c 1.4	94/08/09 SMI"
/*
 *	mini-MS-DOS filesystem for secondary boot
 */

/*
 * Routines to allocate and deallocate data blocks on the disk
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include "pcfilep.h"

#define	PCROOT  0x100   /* substitute for VROOT in pc_flags */

extern devid_t *fdevp;

/*
 * internal routines
 */
static pc_cluster_t pc_getcluster	/* get the next cluster number */
	(struct pcfs *, pc_cluster_t);

/*
 * Convert file logical block (cluster) numbers to disk block numbers.
 * Also return number of physically contiguous blocks if asked for.
 * Used for reading only. Use pc_balloc for writing.
 */
int
pc_bmap(pcp, lcn, dbnp, contigbp)
	register struct pcnode *pcp;	/* pcnode for file */
	register daddr_t lcn;		/* logical cluster no */
	daddr_t *dbnp;			/* ptr to phys block no */
	u_int *contigbp;		/* ptr to number of contiguous bytes */
					/* may be zero if not wanted */
{
	register struct pcfs *fsp;	/* pcfs that file is in */
	pc_cluster_t cn, ncn;		/* current, next cluster number */

PCFSDEBUG(6)
printf("pc_bmap: pcp=0x%x, lcn=%d\n", pcp, lcn);

	fsp = &fdevp->un_fs.di_pcfs;
	if (lcn < 0)
		return (ENOENT);

	if (pcp->pc_flags & PCROOT) {
		register daddr_t lbn, bn;	/* logical (disk) block num */

		lbn = pc_cltodb(fsp, lcn);
		if (lbn >= fsp->pcfs_rdirsec) {
PCFSDEBUG(2)
printf("pc_bmap: ENOENT1\n");
			return (ENOENT);
		}
		bn = fsp->pcfs_rdirstart + lbn;
		*dbnp = pc_dbdaddr(fsp, bn);
		if (contigbp) {
			*contigbp = (*contigbp < (u_int)(fsp->pcfs_secsize *
			    (fsp->pcfs_rdirsec - lbn))) ? *contigbp :
			    fsp->pcfs_secsize * (fsp->pcfs_rdirsec - lbn);
		}
	} else {

		if (lcn >= fsp->pcfs_ncluster) {
PCFSDEBUG(2)
printf("pc_bmap: ENOENT2\n");
			return (ENOENT);
		}
		if (!(pcp->pc_entry.pcd_attr & PCA_DIR) &&
		    (pcp->pc_size == 0 ||
		    lcn >= howmany(pcp->pc_size, fsp->pcfs_clsize))) {
PCFSDEBUG(2)
printf("pc_bmap: ENOENT3\n");
			return (ENOENT);
		}
		ncn = pcp->pc_scluster;
		do {
			cn = ncn;
			if (!pc_validcl(fsp, cn)) {
				if (cn >= PCF_LASTCLUSTER &&
				    (pcp->pc_entry.pcd_attr & PCA_DIR)) {
PCFSDEBUG(2)
printf("pc_bmap: ENOENT4\n");
					return (ENOENT);
				} else {
PCFSDEBUG(1) {
printf("pc_bmap: WARNING bad cluster chain cn=%d\n", cn);
goany();
}
					return (EIO);
				}
			}
			ncn = pc_getcluster(fsp, cn);
		} while (lcn--);
		*dbnp = pc_cldaddr(fsp, cn);

		if (contigbp) {
			u_int count = fsp->pcfs_clsize;

			while ((cn + 1) == ncn && count < *contigbp &&
			    pc_validcl(fsp, ncn)) {
				count += fsp->pcfs_clsize;
				cn = ncn;
				ncn = pc_getcluster(fsp, ncn);
			}
			*contigbp = count;
		}
	}
	return (0);
}


/*
 * Cluster manipulation routines.
 * FAT must be resident.
 */

/*
 * Get the next cluster in the file cluster chain.
 *	cn = current cluster number in chain
 */
static pc_cluster_t
pc_getcluster(register struct pcfs *fsp, register pc_cluster_t cn)
{
	register unsigned char *fp;

PCFSDEBUG(7)
printf("pc_getcluster: cn=%x ", cn);
	if (fsp->pcfs_fatp == (u_char *)0 || !pc_validcl(fsp, cn))
		prom_panic("pc_getcluster");

	if (fsp->pcfs_flags & PCFS_FAT16) {	/* 16 bit FAT */
		fp = fsp->pcfs_fatp + (cn << 1);
		cn = *(pc_cluster_t *)fp;
	} else {	/* 12 bit FAT */
		fp = fsp->pcfs_fatp + (cn + (cn >> 1));
		if (cn & 01) {
			cn = (((unsigned int)*fp++ & 0xf0) >> 4);
			cn += (*fp << 4);
		} else {
			cn = *fp++;
			cn += ((*fp & 0x0f) << 8);
		}
		if (cn >= PCF_12BCLUSTER)
			cn |= PCF_RESCLUSTER;
	}
PCFSDEBUG(7)
printf(" %x\n", cn);
	return (cn);
}

/*
 * Get the number of clusters used by a file or subdirectory
 */
int
pc_fileclsize(register struct pcfs *fsp, register pc_cluster_t strtcluster)
{
	int count = 0;

	while (pc_validcl(fsp, strtcluster)) {
		count++;
		strtcluster = pc_getcluster(fsp, strtcluster);
	}
	return (count);
}
