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
 * 	(c) 1986,1987,1988,1989,1993,1994  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident	"@(#)statvfs.c	1.2	94/10/13 SMI"	/* SVr4 1.42	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/fstyp.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/pathname.h>

#include <vm/page.h>

/*
 * Get file system statistics (statvfs and fstatvfs).
 */

static int	cstatvfs(struct vfs *, struct statvfs *);

int
statvfs(char *fname, struct statvfs *sbp)
{
	vnode_t *vp;
	register int error;

	if (error = lookupname(fname, UIO_USERSPACE, FOLLOW, NULLVPP, &vp))
		return (set_errno(error));
	error = cstatvfs(vp->v_vfsp, sbp);
	VN_RELE(vp);
	return (error);
}

int
fstatvfs(int fdes, struct statvfs *sbp)
{
	struct file *fp;
	register int error;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	error  = cstatvfs(fp->f_vnode->v_vfsp, sbp);
	RELEASEF(fdes);
	return (error);
}

/*
 * Common routine for statvfs and fstatvfs.
 */
static int
cstatvfs(register struct vfs *vfsp, struct statvfs *ubp)
{
	struct statvfs ds;
	register int error;

	struct_zero((caddr_t)&ds, sizeof (ds));
	if (error = VFS_STATVFS(vfsp, &ds))
		return (set_errno(error));
	if (copyout((caddr_t)&ds, (caddr_t)ubp, sizeof (ds)))
		return (set_errno(EFAULT));
	return (0);
}
