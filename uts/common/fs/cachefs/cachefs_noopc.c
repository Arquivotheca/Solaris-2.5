/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_noopc.c 1.12     94/07/07 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/uio.h>
#include <sys/tiuser.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>

#include <sys/fs/cachefs_fs.h>

/*ARGSUSED*/
static int
c_nop_init_cached_object(struct fscache *fscp, struct cnode *cp, cred_t *cr)
{
	int error;

	ASSERT(cr != NULL);
	ASSERT(cp->c_backvp != NULL);
	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	cp->c_attr.va_mask = AT_ALL;
	error = VOP_GETATTR(cp->c_backvp, &cp->c_attr, 0, cr);
	cp->c_size = cp->c_attr.va_size;
	cp->c_flags |= CN_UPDATED;
	return (error);
}

/*ARGSUSED*/
static int
c_nop_check_cached_object(struct fscache *fscp, struct cnode *cp,
	int verify_what, cred_t *cr)
{
	return (0);
}

/*ARGSUSED*/
static void
c_nop_modify_cached_object(struct fscache *fscp, struct cnode *cp, cred_t *cr)
{
	struct vattr attrs;
	int error = 0;
	nlink_t nlink;

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));

	fscp->fs_stats.st_modifies++;

	if (cp->c_backvp) {
		attrs.va_mask = AT_ALL;
		error = VOP_GETATTR(cp->c_backvp, &attrs, 0, cr);
		if (error == 0) {
			nlink = cp->c_attr.va_nlink;
			cp->c_attr = attrs;
			cp->c_attr.va_nlink = nlink;
			cp->c_flags |= CN_UPDATED;
		}
	}
}

/*ARGSUSED*/
static void
c_nop_invalidate_cached_object(struct fscache *fscp, struct cnode *cp,
	cred_t *cr)
{
}

struct cachefsops nopcfsops = {
	c_nop_init_cached_object,
	c_nop_check_cached_object,
	c_nop_modify_cached_object,
	c_nop_invalidate_cached_object
};
