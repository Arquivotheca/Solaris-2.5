/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_strict.c 1.24     94/07/07 SMI"

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

#define	C_CACHE_VALID(TOKEN_MTIME, NEW_MTIME)   \
	((TOKEN_MTIME.tv_sec == NEW_MTIME.tv_sec) && \
		(TOKEN_MTIME.tv_nsec == NEW_MTIME.tv_nsec))

struct c_strict_token {
	timestruc_t		str_expire_time;
	timestruc_t		str_mod_time;
};

extern timestruc_t hrestime;
u_long cachefs_gettime_cached_object(struct fscache *fscp, struct cnode *cp,
    u_long mtime);

static int
c_strict_init_cached_object(struct fscache *fscp, struct cnode *cp, cred_t *cr)
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
	((struct c_strict_token *)&cp->c_token)->str_mod_time =
		cp->c_attr.va_mtime;
	/*
	 * Expire time is based on the number of seconds since the last change
	 * (i.e. files that changed recently are likely to change soon),
	 * XXX needs to be changed ! Not true !!!
	 */
	/*LINTED alignment okay*/
	((struct c_strict_token *)&cp->c_token)->str_expire_time.tv_nsec = 0;
	/*LINTED alignment okay*/
	((struct c_strict_token *)&cp->c_token)->str_expire_time.tv_sec =
	    cachefs_gettime_cached_object(fscp, cp, cp->c_attr.va_mtime.tv_sec);
	cp->c_flags |= CN_UPDATED;
	return (0);
}

static int
c_strict_check_cached_object(struct fscache *fscp, struct cnode *cp,
	int verify_what, int type, cred_t *cr)
{
	/*LINTED alignment okay*/
	struct c_strict_token *tokenp = (struct c_strict_token *)&cp->c_token;
	struct vattr attrs;
	int error = 0;
	int fail = 0, backhit = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("c_strict_check_cached_object: ENTER cp %x\n",
		    (int) cp);
#endif

	ASSERT(cr != NULL);
	if (type == RW_READER) {
		if (rw_tryupgrade(&cp->c_statelock) == 0) {
			error = EAGAIN;
			goto out;
		}
	}
	if ((verify_what & C_BACK_CHECK) == 0 &&
	    hrestime.tv_sec < tokenp->str_expire_time.tv_sec) {
		goto out1;
	}
	error = cachefs_initstate(cp, RW_WRITER, 0, cr);
	if (error) {
		if (type == RW_READER)
			rw_downgrade(&cp->c_statelock);
		goto out;
	}
	attrs.va_mask = AT_ALL;
	error = VOP_GETATTR(cp->c_backvp, &attrs, 0, cr);
	backhit = 1;
	if (error)
		goto out1;
	if (!C_CACHE_VALID(tokenp->str_mod_time, attrs.va_mtime) ||
		(cp->c_size != attrs.va_size)) {
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
			error = VOP_GETATTR(cp->c_backvp, &attrs, 0, cr);
			if (error)
				goto out1;
		}
		if (CTOV(cp)->v_pages == NULL) {
			cp->c_size = attrs.va_size;
		/*LINTED printf below...*/
		} else {
#ifdef CFSDEBUG
			printf("c_strict_check: v_pages not null\n");
#endif
		}
	}
	cp->c_attr = attrs;
	if (attrs.va_size > cp->c_size)
		cp->c_size = attrs.va_size;
	ASSERT(cp->c_fileno == cp->c_attr.va_nodeid);
	tokenp->str_expire_time.tv_sec =
	    cachefs_gettime_cached_object(fscp, cp, attrs.va_mtime.tv_sec);
	tokenp->str_mod_time = attrs.va_mtime;
	cp->c_flags |= CN_UPDATED;

out1:
	if (type == RW_READER)
		rw_downgrade(&cp->c_statelock);
out:

	if (backhit != 0) {
		if (fail != 0)
			fscp->fs_stats.st_fails++;
		else
			fscp->fs_stats.st_passes++;
	}

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("c_strict_check_cached_object: EXIT expires %x\n",
			(int) tokenp->str_expire_time.tv_sec);
#endif

	return (error);
}

static void
c_strict_modify_cached_object(struct fscache *fscp, struct cnode *cp,
	cred_t *cr)
{
	struct vattr attrs;
	/*LINTED alignment okay*/
	struct c_strict_token *tokenp = (struct c_strict_token *)&cp->c_token;
	int error = 0;
	nlink_t nlink;

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));

	fscp->fs_stats.st_modifies++;

	if ((CTOV(cp)->v_type == VDIR) || (cp->c_backvp == NULL)) {
		tokenp->str_expire_time.tv_sec = 0;
		tokenp->str_mod_time.tv_sec = 0;
	} else {
		attrs.va_mask = AT_ALL;
		error = VOP_GETATTR(cp->c_backvp, &attrs, 0, cr);
		if (error) {
			tokenp->str_expire_time.tv_sec = 0;
			tokenp->str_mod_time.tv_sec = 0;
		} else {
			tokenp->str_expire_time.tv_sec =
				cachefs_gettime_cached_object(fscp, cp,
				attrs.va_mtime.tv_sec);
			tokenp->str_mod_time = attrs.va_mtime;
			nlink = cp->c_attr.va_nlink;
			cp->c_attr = attrs;
			cp->c_attr.va_nlink = nlink;
		}
	}
out:
	cp->c_flags |= CN_UPDATED;
}

/*ARGSUSED*/
static void
c_strict_invalidate_cached_object(struct fscache *fscp, struct cnode *cp,
	cred_t *cr)
{
	/*LINTED alignment okay*/
	struct c_strict_token *tokenp = (struct c_strict_token *)&cp->c_token;

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	tokenp->str_expire_time.tv_sec = 0;
	tokenp->str_mod_time.tv_sec = 0;
	cp->c_flags |= CN_UPDATED;
}

struct cachefsops strictcfsops = {
	c_strict_init_cached_object,
	c_strict_check_cached_object,
	c_strict_modify_cached_object,
	c_strict_invalidate_cached_object
};
