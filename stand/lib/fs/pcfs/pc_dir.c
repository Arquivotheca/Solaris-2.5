/*
 * Copyright (c) 1989, 1992-1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)pc_dir.c 1.4     94/08/09 SMI"
/*
 *	mini-MS-DOS filesystem for secondary boot
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include "pcfilep.h"

#define	PCROOT	0x100	/* substitute for VROOT in pc_flags */

extern devid_t *fdevp;

static int pc_findentry();
static int pc_parsename();

/*
 * slot structure is used by the directory search routine to return
 * the results of the search.  If the search is successful sl_blkno and
 * sl_offset reflect the disk address of the entry and sl_ep points to
 * the actual entry data in buffer sl_bp. sl_flags is set to whether the
 * entry is dot or dotdot. If the search is unsuccessful sl_blkno and
 * sl_offset points to an empty directory slot if there are any. Otherwise
 * it is set to -1.
 */
struct slot {
	enum {SL_NONE, SL_FOUND, SL_EXTEND}
		sl_status;	/* slot status */
	daddr_t sl_blkno;	/* disk block number which has entry */
	int sl_offset;		/* offset of entry within block */
	struct buf *sl_bp;	/* buffer containing entry data */
	struct pcdir *sl_ep;	/* pointer to entry data */
	int sl_flags;		/* flags (see below) */
};
#define	SL_DOT		1	/* entry point to self */
#define	SL_DOTDOT	2	/* entry points to parent */

/*
 * Lookup a name in a directory. Return a pointer to the pc_node
 * which represents the entry.
 */
int
pc_dirlook(dp, namep, pcpp)
	register struct pcnode *dp;	/* parent directory */
	char *namep;			/* name to lookup */
	struct pcnode **pcpp;		/* result */
{
	struct slot slot;
	int error;

PCFSDEBUG(4)
printf("pc_dirlook (dp %x name %s)\n", dp, namep);

	if (!(dp->pc_entry.pcd_attr & PCA_DIR)) {
		return (ENOTDIR);
	}

	/*
	 * Null component name is synonym for directory being searched.
	 */
	if (*namep == '\0') {
		*pcpp = dp;
		return (0);
	}
	/*
	 * The root directory does not have "." and ".." entries,
	 * so they are faked here.
	 */
	if (dp->pc_flags & PCROOT) {
		if (bcmp(namep, ".", 2) == 0 || bcmp(namep, "..", 3) == 0) {
			*pcpp = dp;
			return (0);
		}
	}
	error = pc_findentry(dp, namep, &slot);
	if (error == 0) {
		*pcpp = pc_getnode(&fdevp->un_fs.di_pcfs,
		    slot.sl_blkno, slot.sl_offset, slot.sl_ep);
		kmem_free(slot.sl_bp, fdevp->un_fs.di_pcfs.pcfs_clsize);
PCFSDEBUG(4)
printf("pc_dirlook: FOUND pcp=%x\n", *pcpp);
	} else if (error == EINVAL) {
		error = ENOENT;
	}
	return (error);
}

/*
 * Search a directory for an entry.
 * The directory should be locked as this routine
 * will sleep on I/O while searching.
 */
static int
pc_findentry(
	register struct pcnode *dp,	/* parent directory */
	char *namep,			/* name to lookup */
	struct slot *slotp)
{
	register long offset;
	struct pcfs *fsp = &fdevp->un_fs.di_pcfs;
	struct pcdir *ep = 0;
	register int boff;
	daddr_t pblkno;
	int size;
	int error;
	char fname[PCFNAMESIZE];
	char fext[PCFEXTSIZE];

PCFSDEBUG(6)
printf("pc_findentry: looking for %s in dir 0x%x\n", namep, dp);

	slotp->sl_status = SL_NONE;
	if (!(dp->pc_entry.pcd_attr & PCA_DIR)) {
		return (ENOTDIR);
	}

	error = pc_parsename(namep, fname, fext);
	if (error) {
PCFSDEBUG(3)
printf("pc_findentry: pc_parsename error\n");
		return (error);
	}

	slotp->sl_bp = (struct buf *) kmem_alloc(fsp->pcfs_clsize);

	for (offset = 0; offset < dp->pc_size;
	    ep++, offset += sizeof (struct pcdir)) {
		/*
		 * If offset is on a block boundary,
		 * read in the next directory block.
		 */
		if (!(boff = pc_blkoff(fsp, offset))) {

			/*  convert offset to block number */
			if (error = pc_bmap(dp, pc_lblkno(fsp, offset),
			    &pblkno, (u_int *) 0)) {
				if (error == ENOENT &&
				    slotp->sl_status == SL_NONE) {
					slotp->sl_status = SL_EXTEND;
					slotp->sl_offset = offset;
				}
				kmem_free((caddr_t)slotp->sl_bp,
				    fsp->pcfs_clsize);
				return (error);
			}
			size = dp->pc_flags & PCROOT ?
			    (offset >=
			    (fsp->pcfs_rdirsec & ~(fsp->pcfs_spcl - 1)) *
			    fsp->pcfs_secsize ?
			    (fsp->pcfs_rdirsec & (fsp->pcfs_spcl - 1)) *
			    fsp->pcfs_secsize :
			    fsp->pcfs_clsize) :
			    fsp->pcfs_clsize;
			error = prom_read_diskette(fdevp->di_dcookie,
			    (caddr_t)slotp->sl_bp, size, pblkno, 0);
			if (error != size) {
				kmem_free((caddr_t)slotp->sl_bp,
				    fsp->pcfs_clsize);
				return (EIO);
			}
			ep = (struct pcdir *)slotp->sl_bp;
		}
		if ((ep->pcd_filename[0] == PCD_UNUSED) ||
		    (ep->pcd_filename[0] == PCD_ERASED)) {
			/*
			 * note empty slots, in case name is not found
			 */
			if (slotp->sl_status == SL_NONE) {
				slotp->sl_status = SL_FOUND;
				slotp->sl_blkno = pc_daddrdb(fsp, pblkno);
				slotp->sl_offset = boff;
			}
			/*
			 * If unused we've hit the end of the directory
			 */
			if (ep->pcd_filename[0] == PCD_UNUSED)
				break;
			else
				continue;
		}
		/*
		 * Hidden files do not participate in the search
		if (ep->pcd_attr & (PCA_HIDDEN | PCA_SYSTEM | PCD_LABEL))
			continue;
		 */
		if (ep->pcd_attr & PCA_LABEL)
			continue;
		if ((bcmp(fname, ep->pcd_filename, PCFNAMESIZE) == 0) &&
		    (bcmp(fext, ep->pcd_ext, PCFEXTSIZE) == 0)) {
			/*
			 * found the file
			 */
			if (fname[0] == '.') {
				if (fname[1] == '.')
					slotp->sl_flags = SL_DOTDOT;
				else
					slotp->sl_flags = SL_DOT;
			} else {
				slotp->sl_flags = 0;
			}
			slotp->sl_blkno = pc_daddrdb(fsp, pblkno);
			slotp->sl_offset = boff;
			slotp->sl_ep = ep;
			return (0);
		}
	}
	kmem_free(slotp->sl_bp, fsp->pcfs_clsize);
	slotp->sl_bp = NULL;

	return (ENOENT);
}


/*
 * Parse user filename into the pc form of "filename.extension".
 * If names are too long for the format they are truncated silently.
 * Tests for characters that are invalid in PCDOS and converts to upper case.
 */
static int
pc_parsename(namep, fnamep, fextp)
	register char *namep;
	register char *fnamep;
	register char *fextp;
{
	register int n;
	register char c;

	n = PCFNAMESIZE;
	c = *namep++;
	if (c == 0)
		return (EINVAL);
	if (c == '.') {
		/*
		 * check for "." and "..".
		 */
		*fnamep++ = c;
		n--;
		if (c = *namep++) {
			if ((c != '.') || (c = *namep)) /* ".x" or "..x" */
				return (EINVAL);
			*fnamep++ = '.';
			n--;
		}
	} else {
		/*
		 * filename up to '.'
		 */
		do {
			if (n-- > 0) {
				c = toupper(c);
				if (!pc_validchar(c))
					return (EINVAL);
				*fnamep++ = c;
			}
		} while ((c = *namep++) && c != '.');
	}
	while (n-- > 0) {		/* fill with blanks */
		*fnamep++ = ' ';
	}
	/*
	 * remainder is extension
	 */
	n = PCFEXTSIZE;
	if (c == '.') {
		while ((c = *namep++) && n--) {
			c = toupper(c);
			if (!pc_validchar(c))
				return (EINVAL);
			*fextp++ = c;
		}
	}
	while (n-- > 0) {		/* fill with blanks */
		*fextp++ = ' ';
	}
	return (0);
}
