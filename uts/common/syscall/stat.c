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
 * 	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *			All rights reserved.
 *
 */

#ident	"@(#)stat.c	1.4	95/01/23 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/pathname.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/mode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/ioreq.h>
#include <sys/debug.h>

/*
 * Get file attribute information through a file name or a file descriptor.
 */

#ifdef i386
static int	o_cstat(vnode_t *, struct o_stat *, struct cred *);
#endif
static int	cstat(vnode_t *, struct stat *, struct cred *);

int
stat(char *fname, struct stat *sb)
{
	vnode_t *vp;
	register int error;

	if (error = lookupname(fname, UIO_USERSPACE, FOLLOW, NULLVPP, &vp))
		return (set_errno(error));

#ifdef i386
	error = o_cstat(vp, (struct o_stat *)sb, CRED());
#else
	error = cstat(vp, sb, CRED());
#endif
	VN_RELE(vp);
	return (error);
}

int
lstat(char *fname, struct stat *sb)
{
	vnode_t *vp;
	register int error;

	if (error = lookupname(fname, UIO_USERSPACE, NO_FOLLOW, NULLVPP, &vp))
		return (set_errno(error));
#ifdef i386
	error = o_cstat(vp, (struct o_stat *)sb, CRED());
#else
	error = cstat(vp, sb, CRED());
#endif
	VN_RELE(vp);
	return (error);
}

int
fstat(int fdes, struct stat *sb)
{
	file_t *fp;
	register int error;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
#ifdef i386
	error = o_cstat(fp->f_vnode, (struct o_stat *)sb, fp->f_cred);
#else
	error = cstat(fp->f_vnode, sb, fp->f_cred);
#endif

	RELEASEF(fdes);
	return (error);
}

/*
 * Common code for stat(), lstat(), and fstat().
 * For i386, this is as it was in older SPARC versions.  See next line.
 * Was: xcstat, Common code for xstat(), lxstat(), and fxstat().
 */
static int
cstat(register vnode_t *vp, struct stat *ubp, struct cred *cr)
{
	struct stat sb;
	struct vattr vattr;
	register int error;
	register struct vfssw *vswp;

	vattr.va_mask = AT_STAT | AT_NBLOCKS | AT_BLKSIZE | AT_SIZE;
	if (error = VOP_GETATTR(vp, &vattr, 0, cr))
		return (set_errno(error));

	struct_zero((caddr_t)&sb, sizeof (sb));

	sb.st_mode = VTTOIF(vattr.va_type) | vattr.va_mode;
	sb.st_uid = vattr.va_uid;
	sb.st_gid = vattr.va_gid;
	sb.st_dev = vattr.va_fsid;
	sb.st_ino = vattr.va_nodeid;
	sb.st_nlink = vattr.va_nlink;
	sb.st_size = vattr.va_size;
	sb.st_atime = vattr.va_atime;
	sb.st_mtime = vattr.va_mtime;
	sb.st_ctime = vattr.va_ctime;
	sb.st_rdev = vattr.va_rdev;
	sb.st_blksize = vattr.va_blksize;
	sb.st_blocks = vattr.va_nblocks;
	if (vp->v_vfsp) {
		vswp = &vfssw[vp->v_vfsp->vfs_fstype];
		if (vswp->vsw_name && *vswp->vsw_name)
			strcpy(sb.st_fstype, vswp->vsw_name);

	}
	if (copyout((caddr_t)&sb, (caddr_t)ubp, sizeof (sb)))
		return (set_errno(EFAULT));
	return (0);
}

#ifdef i386
/*
 * Common code for stat(), lstat(), and fstat().
 */
static int
o_cstat(register vnode_t *vp, struct o_stat *ubp, struct cred *cr)
{
	struct o_stat sb;
	struct vattr vattr;
	register int error;

	vattr.va_mask = AT_STAT;
	if (error = VOP_GETATTR(vp, &vattr, 0, cr))
		return (set_errno(error));
	sb.st_mode = (o_mode_t)(VTTOIF(vattr.va_type) | vattr.va_mode);
	/*
	 * Check for large values.
	 */
	if ((u_long)vattr.va_uid > (u_long)USHRT_MAX ||
	    (u_long)vattr.va_gid > (u_long)USHRT_MAX ||
	    vattr.va_nodeid > USHRT_MAX || vattr.va_nlink > SHRT_MAX)
		return (set_errno(EOVERFLOW));
	sb.st_uid = (o_uid_t)vattr.va_uid;
	sb.st_gid = (o_gid_t)vattr.va_gid;
	/*
	 * Need to convert expanded dev to old dev format.
	 */
	if (vattr.va_fsid & 0x8000)
		sb.st_dev = (o_dev_t)vattr.va_fsid;
	else
		sb.st_dev = (o_dev_t)cmpdev(vattr.va_fsid);
	sb.st_ino = (o_ino_t)vattr.va_nodeid;
	sb.st_nlink = (o_nlink_t)vattr.va_nlink;
	sb.st_size = vattr.va_size;
	sb.st_atime = vattr.va_atime.tv_sec;
	sb.st_mtime = vattr.va_mtime.tv_sec;
	sb.st_ctime = vattr.va_ctime.tv_sec;
	sb.st_rdev = (o_dev_t)cmpdev(vattr.va_rdev);

	if (copyout((caddr_t)&sb, (caddr_t)ubp, sizeof (sb)))
		return (set_errno(EFAULT));
	return (0);
}
#endif

/*
 * XXX - Transitional cruft: remove at end of idr-3.0
 */

#ifdef i386
/*
 *	For i386, USL SVR4 Intel compatibility is needed.  xstat() is
 *	used for distinguishing old and new uses of stat.
 */
int
xstat(int version, char *fname, struct stat *sb)
{
	vnode_t *vp;
	register int error;

	if (error = lookupname(fname, UIO_USERSPACE, FOLLOW, NULLVPP, &vp))
		return (set_errno(error));

	/*
	 * Check version.
	 */
	switch (version) {

	case _STAT_VER:
		/* SVR4 stat */
		error = cstat(vp, sb, CRED());
		break;

	case _R3_STAT_VER:
		/* SVR3 stat */
		error = o_cstat(vp, (struct o_stat *)sb, CRED());
		break;

	default:
		error = set_errno(EINVAL);
	}

	VN_RELE(vp);
	return (error);
}
#else

/* ARGSUSED */
int
xstat(int version, char *fname, struct stat *sb)
{
	return (stat(fname, sb));
}
#endif

#ifdef i386
/* ARGSUSED */
int
lxstat(int version, char *fname, struct stat *sb)
{
	vnode_t *vp;
	register int error;

	if (error = lookupname(fname, UIO_USERSPACE, NO_FOLLOW, NULLVPP, &vp))
		return (set_errno(error));

	/*
	 * Check version.
	 */
	switch (version) {

	case _STAT_VER:
		/* SVR4 stat */
		error = cstat(vp, sb, CRED());
		break;

	case _R3_STAT_VER:
		/* SVR3 stat */
		error = o_cstat(vp, (struct o_stat *)sb, CRED());
		break;

	default:
		error = set_errno(EINVAL);
	}

	VN_RELE(vp);
	return (error);
}
#else

/* ARGSUSED */
int
lxstat(int version, char *fname, struct stat *sb)
{
	return (lstat(fname, sb));
}
#endif

struct xfstatarg {
	int	version;
	int	fdes;
	struct stat *sb;
};

#ifdef i386
int
fxstat(int version, int fdes, struct stat *sb)
{
	file_t *fp;
	register int error;

	/*
	 * Check version number.
	 */
	switch (version) {
	case _STAT_VER:
		break;
	default:
		return (set_errno(EINVAL));
	}

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));

	switch (version) {
	case _STAT_VER:
		/* SVR4 stat */
		error = cstat(fp->f_vnode, sb, fp->f_cred);
		break;

	case _R3_STAT_VER:
		/* SVR3 stat */
		error = o_cstat(fp->f_vnode, (struct o_stat *)sb, fp->f_cred);
		break;

	default:
		error = set_errno(EINVAL);
	}

	RELEASEF(fdes);
	return (error);
}
#else

/* ARGSUSED */
int
fxstat(int version, int fdes, struct stat *sb)
{
	return (fstat(fdes, sb));
}
#endif
