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

#pragma ident	"@(#)statfs.c	1.2	94/10/13 SMI"	/* SVr4 1.42	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/fstyp.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/pathname.h>

#include <vm/page.h>

/*
 * statfs(2) and fstatfs(2) have been replaced by statvfs(2) and
 * fstatvfs(2) and will be removed from the system in a near-future
 * release.
 */

static int	cstatfs(struct vfs *, struct statfs *, int);

int
statfs(char *fname, struct statfs *sbp, int len, int fstyp)
{
	vnode_t *vp;
	register int error;

	if (error = lookupname(fname, UIO_USERSPACE, FOLLOW, NULLVPP, &vp))
		return (set_errno(error));
	if (fstyp != 0)
		error = set_errno(EINVAL);
	else
		error = cstatfs(vp->v_vfsp, sbp, len);
	VN_RELE(vp);
	return (error);
}

int
fstatfs(int fdes, struct statfs *sbp, int len, int fstyp)
{
	struct file *fp;
	register int error;

	if (fstyp != 0)
		return (set_errno(EINVAL));
	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	error =  cstatfs(fp->f_vnode->v_vfsp, sbp, len);
	RELEASEF(fdes);
	return (error);
}

/*
 * Common routine for fstatfs and statfs.
 */
static int
cstatfs(register struct vfs *vfsp, struct statfs *sbp, int len)
{
	struct statfs sfs;
	struct statvfs svfs;
	register int error, i;
	char *cp, *cp2;
	register struct vfssw *vswp;

	if (len < 0 || len > sizeof (struct statfs))
		return (set_errno(EINVAL));
	if (error = VFS_STATVFS(vfsp, &svfs))
		return (set_errno(error));

	/*
	 * Map statvfs fields into the old statfs structure.
	 */
	struct_zero((caddr_t)&sfs, sizeof (sfs));
	sfs.f_bsize = svfs.f_bsize;
	sfs.f_frsize = (svfs.f_frsize == svfs.f_bsize) ? 0 : svfs.f_frsize;
	sfs.f_blocks = svfs.f_blocks * (svfs.f_frsize/512);
	sfs.f_bfree = svfs.f_bfree * (svfs.f_frsize/512);
	sfs.f_files = svfs.f_files;
	sfs.f_ffree = svfs.f_ffree;

	cp = svfs.f_fstr;
	cp2 = sfs.f_fname;
	i = 0;
	while (i++ < sizeof (sfs.f_fname))
		if (*cp != '\0')
			*cp2++ = *cp++;
		else
			*cp2++ = '\0';
	while (*cp != '\0' &&
	    i++ < (sizeof (svfs.f_fstr) - sizeof (sfs.f_fpack)))
		cp++;
	cp++;
	cp2 = sfs.f_fpack;
	i = 0;
	while (i++ < sizeof (sfs.f_fpack))
		if (*cp != '\0')
			*cp2++ = *cp++;
		else
			*cp2++ = '\0';
	if ((vswp = vfs_getvfssw(svfs.f_basetype)) == NULL) {
		sfs.f_fstyp = 0;
	} else {
		sfs.f_fstyp = vswp - vfssw;
		RUNLOCK_VFSSW();
	}

	if (copyout((caddr_t)&sfs, (caddr_t)sbp, len))
		return (set_errno(EFAULT));

	return (0);
}
