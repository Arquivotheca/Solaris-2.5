/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_singlewrc.c 1.23     94/07/07 SMI"

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
#include <netinet/in.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <sys/fs/cachefs_fs.h>

#define	C_SINGLE_WRITER	0x1
#define	C_CACHE_VALID(TOKEN_MTIME, NEW_MTIME)	\
	((TOKEN_MTIME.tv_sec == NEW_MTIME.tv_sec) && \
		(TOKEN_MTIME.tv_nsec == NEW_MTIME.tv_nsec))

struct c_single_wr_token {
	timestruc_t		swr_expire_time;
	timestruc_t		swr_mod_time;
	u_short			swr_flags;
};

extern timestruc_t hrestime;
u_long cachefs_gettime_cached_object(struct fscache *fscp, struct cnode *cp,
    u_long mtime);

static int
c_single_init_cached_object(struct fscache *fscp, struct cnode *cp, cred_t *cr)
{
	int error;

	ASSERT(cr != NULL);
	ASSERT(cp->c_backvp != NULL);
	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	cp->c_attr.va_mask = AT_ALL;
	error = VOP_GETATTR(cp->c_backvp, &cp->c_attr, 0, cr);
	if (error)
		return (error);
	cp->c_size = cp->c_attr.va_size;
	/*LINTED alignment okay*/
	((struct c_single_wr_token *)&cp->c_token)->swr_mod_time =
		cp->c_attr.va_mtime;
	/*LINTED alignment okay*/
	((struct c_single_wr_token *)&cp->c_token)->swr_flags = 0;
	/*LINTED alignment okay*/
	((struct c_single_wr_token *)&cp->c_token)->swr_expire_time.tv_nsec = 0;
	/*LINTED alignment okay*/
	((struct c_single_wr_token *)&cp->c_token)->swr_expire_time.tv_sec =
	    cachefs_gettime_cached_object(fscp, cp, cp->c_attr.va_mtime.tv_sec);
	cp->c_flags |= CN_UPDATED;
	return (0);
}

static int
c_single_check_cached_object(struct fscache *fscp, struct cnode *cp,
	int verify_what, int type, cred_t *cr)
{
	struct c_single_wr_token *tokenp =
		/*LINTED alignment okay*/
		(struct c_single_wr_token *)&cp->c_token;
	struct vattr attrs;
	int error = 0;
	int fail = 0, backhit = 0;

	ASSERT(cr != NULL);
	if ((hrestime.tv_sec >= tokenp->swr_expire_time.tv_sec) ||
	    (tokenp->swr_flags & C_SINGLE_WRITER &&
		verify_what == C_VERIFY_ATTRS)) {
		if (type == RW_READER) {
			if (rw_tryupgrade(&cp->c_statelock) == 0) {
				return (EAGAIN);
			}
		}
		/*
		 * Token expired or client wrote to file and needs to verify
		 * attrs.
		 */
		if (cp->c_backvp == NULL)
			error = cachefs_getbackvp(fscp,
					&cp->c_metadata.md_cookie, cp);
		if (error)
			goto out;
		attrs.va_mask = AT_ALL;
		error = VOP_GETATTR(cp->c_backvp, &attrs, 0, cr);
		backhit = 1;
		if (error)
			goto out;
		if (tokenp->swr_flags & C_SINGLE_WRITER) {
			tokenp->swr_flags &= ~C_SINGLE_WRITER;
			tokenp->swr_mod_time = attrs.va_mtime;
		} else if (!C_CACHE_VALID(tokenp->swr_mod_time,
				attrs.va_mtime)) {
			fail = 1;
			cachefs_inval_object(cp, cr);
			if (CTOV(cp)->v_pages) {
				rw_exit(&cp->c_statelock);
				(void) VOP_PUTPAGE(CTOV(cp), (offset_t) 0, 0,
				    B_INVAL, cr);
				rw_enter(&cp->c_statelock, RW_WRITER);
			}
			if ((CTOV(cp))->v_type == VREG) {
				attrs.va_mask = AT_ALL;
				error = VOP_GETATTR(cp->c_backvp,
				    &attrs, 0, cr);
				if (error)
					goto out;
			}
			if (CTOV(cp)->v_pages == NULL)
				cp->c_size = attrs.va_size;
			tokenp->swr_mod_time = attrs.va_mtime;
		}
		cp->c_attr = attrs;
		tokenp->swr_expire_time.tv_sec =
		    cachefs_gettime_cached_object(fscp, cp,
				attrs.va_mtime.tv_sec);
		cp->c_flags |= CN_UPDATED;
out:
		if (type == RW_READER)
			rw_downgrade(&cp->c_statelock);
	}

	if (backhit != 0) {
		if (fail != 0)
			fscp->fs_stats.st_fails++;
		else
			fscp->fs_stats.st_passes++;
	}

	return (error);
}

/*ARGSUSED*/
static void
c_single_modify_cached_object(struct fscache *fscp, struct cnode *cp,
	cred_t *cr)
{
	struct vattr attrs;
	int error = 0;
	nlink_t nlink;
	struct c_single_wr_token *tokenp =
		/*LINTED alignment okay*/
		(struct c_single_wr_token *)&cp->c_token;

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));

	fscp->fs_stats.st_modifies++;

	tokenp->swr_flags |= C_SINGLE_WRITER;
	if (cp->c_backvp) {
		attrs.va_mask = AT_ALL;
		error = VOP_GETATTR(cp->c_backvp, &attrs, 0, cr);
		if (error) {
			tokenp->swr_flags &= ~C_SINGLE_WRITER;
			tokenp->swr_expire_time.tv_sec = 0;
			tokenp->swr_mod_time.tv_sec = 0;
		} else {
			nlink = cp->c_attr.va_nlink;
			cp->c_attr = attrs;
			cp->c_attr.va_nlink = nlink;
		}
	}
	cp->c_flags |= CN_UPDATED;
}

/*ARGSUSED*/
static void
c_single_invalidate_cached_object(struct fscache *fscp, struct cnode *cp,
	cred_t *cr)
{
	struct c_single_wr_token *tokenp =
		/*LINTED alignment okay*/
		(struct c_single_wr_token *)&cp->c_token;

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	tokenp->swr_flags &= ~C_SINGLE_WRITER;
	tokenp->swr_mod_time.tv_sec = 0;
	tokenp->swr_expire_time.tv_sec = 0;
	cp->c_flags |= CN_UPDATED;
}

/*
 * Returns the tod in secs when the consistency of the object should
 * be checked.
 * This function is used by both the single writer and strict consistency
 * modes.
 */
u_long
cachefs_gettime_cached_object(struct fscache *fscp, struct cnode *cp,
	u_long mtime)
{
	u_long xsec;
	u_long acmin, acmax;

	/*
	 * Expire time is based on the number of seconds since the last change
	 * (i.e. files that changed recently are likely to change soon),
	 */
	if ((CTOV(cp))->v_type == VDIR) {
		acmin = fscp->fs_acdirmin;
		acmax = fscp->fs_acdirmax;
	} else {
		acmin = fscp->fs_acregmin;
		acmax = fscp->fs_acregmax;
	}

	xsec = hrestime.tv_sec - mtime;
	xsec = MAX(xsec, acmin);
	xsec = MIN(xsec, acmax);
	xsec += hrestime.tv_sec;
	return (xsec);
}

struct cachefsops singlecfsops = {
	c_single_init_cached_object,
	c_single_check_cached_object,
	c_single_modify_cached_object,
	c_single_invalidate_cached_object
};
