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

#ident	"@(#)open.c	1.2	94/09/13 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <c2/audit.h>

/*
 * Common code for open() and creat().  Check permissions, allocate
 * an open file structure, and call the device open routine (if any).
 */
static int
copen(char *fname, int filemode, int createmode)
{
	vnode_t *vp;
	file_t *fp;
	register int error;
	int fd, dupfd;
	enum vtype type;

	if ((filemode & (FREAD|FWRITE)) == 0)
		return (set_errno(EINVAL));

	if ((filemode & (FNONBLOCK|FNDELAY)) == (FNONBLOCK|FNDELAY))
		filemode &= ~FNDELAY;

	if (error = falloc((vnode_t *)NULL, filemode & FMASK, &fp, &fd))
		return (set_errno(error));

	/*
	 * Last arg is a don't-care term if !(filemode & FCREAT).
	 */
	error = vn_open(fname, UIO_USERSPACE, filemode,
	    (int)((createmode & MODEMASK) & ~u.u_cmask), &vp, CRCREAT);
	if (error) {
		setf(fd, NULLFP);
		unfalloc(fp);
		return (set_errno(error));
	}
#ifdef C2_AUDIT
	if (audit_active)
		audit_copen(fd, fp, vp);
#endif /* C2_AUDIT */
	if (vp->v_flag & VDUP) {
		/*
		 * Special handling for /dev/fd.  Give up the file pointer
		 * and dup the indicated file descriptor (in v_rdev).  This
		 * is ugly, but I've seen worse.
		 */
		unfalloc(fp);
		dupfd = getminor(vp->v_rdev);
		type = vp->v_type;
		mutex_enter(&vp->v_lock);
		vp->v_flag &= ~VDUP;
		mutex_exit(&vp->v_lock);
		VN_RELE(vp);
		if (type != VCHR)
			return (set_errno(EINVAL));
		if ((fp = GETF(dupfd)) == NULL) {
			setf(fd, NULLFP);
			return (set_errno(EBADF));
		}
		mutex_enter(&fp->f_tlock);
		fp->f_count++;
		mutex_exit(&fp->f_tlock);
		setf(fd, fp);
		RELEASEF(dupfd);
	} else {
		fp->f_vnode = vp;
		mutex_exit(&fp->f_tlock);

		/*
		 * We must now fill in the slot falloc reserved.
		 */
		setf(fd, fp);
	}

	return (fd);
}

/*
 * Open a file.
 */
int
open(char *fname, int fmode, int cmode)
{
	return (copen(fname, (int)(fmode-FOPEN), cmode));
}

/*
 * Create a file.
 */
int
creat(char *fname, int cmode)
{
	return (copen(fname, FWRITE|FCREAT|FTRUNC, cmode));
}
