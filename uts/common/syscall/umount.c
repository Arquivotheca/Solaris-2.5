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
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989,1993  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident	"@(#)umount.c	1.1	94/10/12 SMI"	/* SVr4 1.42	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/fstyp.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/pathname.h>


int
umount(char *pathp)
{
	vnode_t *fsrootvp;
	register struct vfs *vfsp;
	register int error;

	/*
	 * Lookup user-supplied name.
	 */
	if (error = lookupname(pathp, UIO_USERSPACE, FOLLOW,
	    NULLVPP, &fsrootvp))
		return (set_errno(error));
	/*
	 * Find the vfs to be unmounted.  The caller may have specified
	 * either the directory mount point (preferred) or else (for a
	 * disk-based file system) the block device which was mounted.
	 * Check to see which it is; if it's the device, search the VFS
	 * list to find the associated vfs entry.
	 */
	if (fsrootvp->v_flag & VROOT)
		vfsp = fsrootvp->v_vfsp;
	else if (fsrootvp->v_type == VBLK) {
		vfsp = vfs_devsearch(fsrootvp->v_rdev);
	} else
		vfsp = NULL;

	if (vfsp == NULL) {
		VN_RELE(fsrootvp);
		return (set_errno(EINVAL));
	}
	/*
	 * The vn_vfsunlock will be done in dounmount.  Also note that I have
	 * to do this lock before I call VN_RELE.  If I waited until after
	 * the VN_RELE another unmount could have happened and vfsp would
	 * be pointing at garbage.
	 */
	if (vn_vfslock(vfsp->vfs_vnodecovered)) {
		VN_RELE(fsrootvp);
		return (set_errno(EBUSY));
	}
	VN_RELE(fsrootvp);
	/*
	 * Perform the unmount.
	 */
	if (error = dounmount(vfsp, CRED()))
		return (set_errno(error));
	return (0);
}
