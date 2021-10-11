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

#ident	"@(#)lseek.c	1.3	94/09/28 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/debug.h>

/*
 * Seek on file.
 */

off_t
lseek(int fdes, off_t offset, int sbase)
{
	file_t *fp;
	register vnode_t *vp;
	struct vattr vattr;
	register int error;
	offset_t new_off, old_off;

	if ((fp = GETF(fdes)) == NULL)
		return ((off_t)set_errno(EBADF));
	vp = fp->f_vnode;

	new_off = (offset_t)offset;
	if (sbase == 0)
		new_off &= (u_int)(-1);
	if (sbase == 1) {
		new_off += fp->f_offset;
	} else if (sbase == 2) {
		vattr.va_mask = AT_SIZE;
		if (error = VOP_GETATTR(vp, &vattr, 0, fp->f_cred)) {
			goto lseekerror;
		}
		new_off += vattr.va_size;
	} else if (sbase != 0) {
		error = EINVAL;
		goto lseekerror;
	}

	old_off = fp->f_offset;
	if ((error = VOP_SEEK(vp, old_off, &new_off)) == 0) {
		fp->f_offset = new_off;
		RELEASEF(fdes);
		return ((off_t)new_off);
	}
lseekerror:
	RELEASEF(fdes);
	return ((off_t)set_errno(error));
}

#ifndef _NO_LONGLONG

offset_t
llseek(int fdes, offset_t off, int sbase)
{
	file_t *fp;
	register vnode_t *vp;
	struct vattr vattr;
	register int error;
	offset_t new_off, old_off;

	if ((fp = GETF(fdes)) == NULL)
		return ((offset_t)set_errno(EBADF));
	vp = fp->f_vnode;
	new_off = off;
	if (sbase == 1) {
		new_off += fp->f_offset;
	} else if (sbase == 2) {
		vattr.va_mask = AT_SIZE;
		if (error = VOP_GETATTR(vp, &vattr, 0, fp->f_cred)) {
			goto llseekerror;
		}
		new_off += vattr.va_size;
	} else if (sbase != 0) {
		error = EINVAL;
		goto llseekerror;
	}

	old_off = fp->f_offset;
	if ((error = VOP_SEEK(vp, old_off, &new_off)) == 0) {
		fp->f_offset = new_off;
		RELEASEF(fdes);
		return (new_off);
	}
llseekerror:
	RELEASEF(fdes);
	return ((offset_t)set_errno(error));
}

#endif /* !_NO_LONGLONG */
