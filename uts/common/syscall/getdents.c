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

#ident	"@(#)getdents.c	1.2	94/09/13 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/dirent.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/uio.h>
#include <sys/ioreq.h>
#include <sys/filio.h>
#include <sys/debug.h>

/*
 * Get directory entries in a file system-independent format.
 */
int
getdents(int fd, char *buf, int count)
{
	register vnode_t *vp;
	file_t *fp;
	struct uio auio;
	struct iovec aiov;
	register int error;
	int sink;

	if (count < sizeof (struct dirent))
		return (set_errno(EINVAL));

	if ((fp = GETF(fd)) == NULL)
		return (set_errno(EBADF));
	vp = fp->f_vnode;
	if (vp->v_type != VDIR) {
		RELEASEF(fd);
		return (set_errno(ENOTDIR));
	}
	aiov.iov_base = buf;
	aiov.iov_len = count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_loffset = fp->f_offset;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_resid = count;
	auio.uio_fmode = 0;
	VOP_RWLOCK(vp, 0);
	error = VOP_READDIR(vp, &auio, fp->f_cred, &sink);
	VOP_RWUNLOCK(vp, 0);
	if (error) {
		RELEASEF(fd);
		return (set_errno(error));
	}
	count = count - auio.uio_resid;
	fp->f_offset = auio.uio_loffset;
	RELEASEF(fd);
	return (count);
}
