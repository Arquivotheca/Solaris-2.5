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

#ident	"@(#)pathconf.c	1.2	94/09/13 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/debug.h>

/*
 * Common code for pathconf(), fpathconf() system calls
 */
static int
cpathconf(register vnode_t *vp, int cmd, struct cred *cr)
{
	register int error;
	u_long val;

	switch (cmd) {
	case _PC_ASYNC_IO:
	case _PC_PRIO_IO:
		return (-1);

	case _PC_SYNC_IO:
		if (VOP_FSYNC(vp, FSYNC, cr) == 0)
			return (1);
		else
			return (-1);

	default:
		break;
	}

	if (error = VOP_PATHCONF(vp, cmd, &val, cr))
		return (set_errno(error));
	return (val);
}

/* fpathconf/pathconf interfaces */

int
fpathconf(int fdes, int name)
{
	file_t *fp;
	register int retval;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	retval = cpathconf(fp->f_vnode, name, fp->f_cred);
	RELEASEF(fdes);
	return (retval);
}

int
pathconf(char *fname, int name)
{
	vnode_t *vp;
	register int	retval;
	register int	error;

	if (error = lookupname(fname, UIO_USERSPACE, FOLLOW, NULLVPP, &vp))
		return (set_errno(error));
	retval = cpathconf(vp, name, CRED());
	VN_RELE(vp);
	return (retval);
}
