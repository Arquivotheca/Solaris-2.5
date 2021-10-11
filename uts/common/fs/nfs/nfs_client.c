/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	Copyright (c) 1986-1991,1994,1995 by Sun Microsystems, Inc.
 *  	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */

#pragma ident	"@(#)nfs_client.c	1.63	95/09/13 SMI" /* SVr4.0 1.1 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/stat.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/dnlc.h>
#include <sys/vmsystm.h>
#include <sys/flock.h>
#include <sys/cmn_err.h>
#include <sys/tiuser.h>
#include <sys/sysmacros.h>
#include <sys/callb.h>
#include <sys/acl.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/nfs_acl.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>

static int	nfs_getattr_cache(vnode_t *, struct vattr *);
static int	nfs_remove_locking_pid(vnode_t *, pid_t);

/*
 * Attributes caching:
 *
 * Attributes are cached in the rnode in struct vattr form.
 * There is a time associated with the cached attributes (r_attrtime)
 * which tells whether the attributes are valid. The time is initialized
 * to the difference between current time and the modify time of the vnode
 * when new attributes are cached. This allows the attributes for
 * files that have changed recently to be timed out sooner than for files
 * that have not changed for a long time. There are minimum and maximum
 * timeout values that can be set per mount point.
 */

/*
 * Validate caches by checking cached attributes. If they have timed out,
 * then get new attributes from the server.  As a side effect, cache
 * invalidation is done if the attributes have changed.
 */
int
nfs_validate_caches(vnode_t *vp, cred_t *cr)
{
	int error;
	rnode_t *rp;
	struct vattr va;

	if (ATTRCACHE_VALID(vp))
		return (0);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	while (rp->r_flags & RSERIALIZE) {
		if (!cv_wait_sig(&rp->r_cv, &rp->r_statelock)) {
			mutex_exit(&rp->r_statelock);
			return (EINTR);
		}
	}
	if (ATTRCACHE_VALID(vp)) {
		mutex_exit(&rp->r_statelock);
		return (0);
	}
	rp->r_flags |= RSERIALIZE;
	mutex_exit(&rp->r_statelock);

	va.va_mask = AT_ALL;
	error = nfs_getattr_otw(vp, &va, cr);
	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RSERIALIZE;
	cv_broadcast(&rp->r_cv);
	mutex_exit(&rp->r_statelock);
	return (error);
}

/*
 * Validate caches by checking cached attributes. If they have timed out,
 * then get new attributes from the server.  As a side effect, cache
 * invalidation is done if the attributes have changed.
 */
int
nfs3_validate_caches(vnode_t *vp, cred_t *cr)
{
	int error;
	rnode_t *rp;
	struct vattr va;

	if (ATTRCACHE_VALID(vp))
		return (0);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	while (rp->r_flags & RSERIALIZE) {
		if (!cv_wait_sig(&rp->r_cv, &rp->r_statelock)) {
			mutex_exit(&rp->r_statelock);
			return (EINTR);
		}
	}
	if (ATTRCACHE_VALID(vp)) {
		mutex_exit(&rp->r_statelock);
		return (0);
	}
	rp->r_flags |= RSERIALIZE;
	mutex_exit(&rp->r_statelock);

	va.va_mask = AT_ALL;
	error = nfs3_getattr_otw(vp, &va, cr);
	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RSERIALIZE;
	cv_broadcast(&rp->r_cv);
	mutex_exit(&rp->r_statelock);
	return (error);
}

/*
 * Purge all of the various NFS `data' caches.
 */
void
nfs_purge_caches(vnode_t *vp, cred_t *cr)
{
	rnode_t *rp;
	char *contents;
	int size;
	int error;

	/*
	 * Purge the DNLC for any entries which refer to this file.
	 */
	if (vp->v_count > 1)
		dnlc_purge_vp(vp);

	/*
	 * Clear any readdir state bits and purge the readlink response cache.
	 */
	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~REOF;
	contents = rp->r_symlink.contents;
	size = rp->r_symlink.size;
	rp->r_symlink.contents = NULL;
	mutex_exit(&rp->r_statelock);

	if (contents != NULL) {
#ifdef DEBUG
		symlink_cache_free((void *)contents, size);
#else
		kmem_free(contents, size);
#endif
	}

	/*
	 * Flush the page cache.
	 */
	if (vp->v_pages != NULL) {
		error = VOP_PUTPAGE(vp, (offset_t)0, 0, B_INVAL, cr);
		if (error && (error == ENOSPC || error == EDQUOT)) {
			mutex_enter(&rp->r_statelock);
			if (!rp->r_error)
				rp->r_error = error;
			mutex_exit(&rp->r_statelock);
		}
	}

	/*
	 * Flush the readdir response cache.
	 */
	if (rp->r_dir != NULL)
		nfs_purge_rddir_cache(vp);
}

/*
 * Purge the NFS access cache.  This is primarily used by the
 * NFS Version 3 client.  Control to this cache is based on
 * the ctime (change time) in the attributes.
 */
void
nfs_purge_access_cache(vnode_t *vp)
{
	rnode_t *rp;
	access_cache *acp;
	access_cache *nacp;

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	acp = rp->r_acc;
	rp->r_acc = NULL;
	mutex_exit(&rp->r_statelock);
	while (acp != NULL) {
		crfree(acp->cred);
		nacp = acp->next;
#ifdef DEBUG
		access_cache_free((void *)acp, sizeof (*acp));
#else
		kmem_free((caddr_t)acp, sizeof (*acp));
#endif
		acp = nacp;
	}
}

/*
 * Purge the readdir cache of all entries which are not currently
 * being filled.
 */
void
nfs_purge_rddir_cache(vnode_t *vp)
{
	rnode_t *rp;
	rddir_cache *rdc;
	rddir_cache *prdc;

	rp = VTOR(vp);
top:
	mutex_enter(&rp->r_statelock);
	rdc = rp->r_dir;
	prdc = NULL;
	while (rdc != NULL) {
		if (rdc->flags & RDDIR) {
			prdc = rdc;
			rdc = rdc->next;
			continue;
		}
		if (prdc == NULL)
			rp->r_dir = rdc->next;
		else
			prdc->next = rdc->next;
		if (rp->r_direof == rdc)
			rp->r_direof = NULL;
		mutex_exit(&rp->r_statelock);
		if (rdc->entries != NULL)
			kmem_free(rdc->entries, rdc->buflen);
		cv_destroy(&rdc->cv);
#ifdef DEBUG
		rddir_cache_free((void *)rdc, sizeof (*rdc));
#else
		kmem_free((caddr_t)rdc, sizeof (*rdc));
#endif
		goto top;
	}
	mutex_exit(&rp->r_statelock);
}

/*
 * Check the attribute cache to see if the new attributes match
 * those cached.  If they do, the various `data' caches are
 * considered to be good.  Otherwise, the `data' caches are
 * purged.  If the ctime in the attributes has changed, then
 * purge the access cache.
 */
void
nfs_cache_check(vnode_t *vp, timestruc_t ctime, timestruc_t mtime,
	u_long fsize, long *seqp, cred_t *cr)
{
	rnode_t *rp;
	vsecattr_t *vsp;
	int mtime_changed;
	int ctime_changed;

	ASSERT(seqp != NULL);

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	if (!CACHE_VALID(rp, mtime, fsize))
		mtime_changed = 1;
	else
		mtime_changed = 0;
	if (rp->r_attr.va_ctime.tv_sec != ctime.tv_sec ||
	    rp->r_attr.va_ctime.tv_nsec != ctime.tv_nsec)
		ctime_changed = 1;
	else
		ctime_changed = 0;
	*seqp = rp->r_seq;
	mutex_exit(&rp->r_statelock);

	if (mtime_changed)
		nfs_purge_caches(vp, cr);

	if (ctime_changed) {
		if (rp->r_acc != NULL)
			nfs_purge_access_cache(vp);
		if (rp->r_secattr != NULL) {
			mutex_enter(&rp->r_statelock);
			vsp = rp->r_secattr;
			rp->r_secattr = NULL;
			mutex_exit(&rp->r_statelock);
			if (vsp != NULL)
				nfs_acl_free(vsp);
		}
	}
}

/*
 * Do a cache check and possible purge using the wcc_attr.
 * These are the attributes of the file gotten `before'
 * a modify operation on an object.  They are used to
 * determine whether the object was modified by some
 * client other than this one.  Note that the server is
 * counted as a client in this sense.
 */
void
nfs3_cache_check(vnode_t *vp, wcc_attr *wcap, long *seqp, cred_t *cr)
{
	timestruc_t ctime;
	timestruc_t mtime;
	u_long fsize;

	ctime.tv_sec = wcap->ctime.seconds;
	ctime.tv_nsec = wcap->ctime.nseconds;
	mtime.tv_sec = wcap->mtime.seconds;
	mtime.tv_nsec = wcap->mtime.nseconds;
	fsize = (u_long)wcap->size;

	nfs_cache_check(vp, ctime, mtime, fsize, seqp, cr);
}

/*
 * Check the cache based on the fattr3 attributes.  These are
 * generally the post operation attributes because they are a
 * complete set of the attributes which pass over the network.
 */
void
nfs3_cache_check_fattr3(vnode_t *vp, fattr3 *na, long *seqp, cred_t *cr)
{
	timestruc_t ctime;
	timestruc_t mtime;
	u_long fsize;

	ctime.tv_sec = na->ctime.seconds;
	ctime.tv_nsec = na->ctime.nseconds;
	mtime.tv_sec = na->mtime.seconds;
	mtime.tv_nsec = na->mtime.nseconds;
	fsize = (u_long)na->size;

	nfs_cache_check(vp, ctime, mtime, fsize, seqp, cr);
}

/*
 * Do a cache check based on the post-operation attributes.
 * Then make them the new cached attributes.  If no attributes
 * were returned, then mark the attributes as timed out.
 */
void
nfs3_cache_post_op_attr(vnode_t *vp, post_op_attr *poap, cred_t *cr)
{
	long seq;

	if (poap->attributes) {
		nfs3_cache_check_fattr3(vp, &poap->attr, &seq, cr);
		nfs3_attrcache(vp, &poap->attr, seq);
	} else
		PURGE_ATTRCACHE(vp);
}

/*
 * Do a cache check based on the weak cache consistency attributes.
 * These consist of a small set of pre-operation attributes and the
 * full set of post-operation attributes.
 *
 * If we are given the pre-operation attributes, then use them to
 * check the validity of the various caches.  Then, if we got the
 * post-operation attributes, make them the new cached attributes.
 * If we didn't get the post-operation attributes, then mark the
 * attribute cache as timed out so that the next reference will
 * cause a GETATTR to the server to refresh with the current
 * attributes.
 *
 * Otherwise, if we didn't get the pre-operation attributes, but
 * we did get the post-operation attributes, then use these
 * attributes to check the validity of the various caches.  This
 * will probably cause a flush of the caches because if the
 * operation succeeded, the attributes of the object were changed
 * in some way from the old post-operation attributes.  This
 * should be okay because it is the safe thing to do.  After
 * checking the data caches, then we make these the new cached
 * attributes.
 *
 * Otherwise, we didn't get either the pre- or post-operation
 * attributes.  Simply mark the attribute cache as timed out so
 * the next reference will cause a GETATTR to the server to
 * refresh with the current attributes.
 */
void
nfs3_cache_wcc_data(vnode_t *vp, wcc_data *wccp, cred_t *cr)
{
	long seq;

	if (wccp->before.attributes) {
		nfs3_cache_check(vp, &wccp->before.attr, &seq, cr);
		if (wccp->after.attributes)
			nfs3_attrcache(vp, &wccp->after.attr, seq);
		else
			PURGE_ATTRCACHE(vp);
	} else if (wccp->after.attributes) {
		nfs3_cache_check_fattr3(vp, &wccp->after.attr, &seq, cr);
		nfs3_attrcache(vp, &wccp->after.attr, seq);
	} else
		PURGE_ATTRCACHE(vp);
}

/*
 * Set attributes cache for given vnode using nfsattr.
 */
void
nfs_attrcache(vnode_t *vp, struct nfsfattr *na, long seq)
{
	struct vattr va;

	nattr_to_vattr(vp, na, &va);

	nfs_attrcache_va(vp, &va, seq);
}

/*
 * Set attributes cache for given vnode using fattr3.
 */
void
nfs3_attrcache(vnode_t *vp, fattr3 *na, long seq)
{
	struct vattr va;

	fattr3_to_vattr(vp, na, &va);

	nfs_attrcache_va(vp, &va, seq);
}

/*
 * Set attributes cache for given vnode using virtual attributes.
 *
 * Set the timeout value on the attribute cache and fill it
 * with the passed in attributes.
 */
void
nfs_attrcache_va(vnode_t *vp, struct vattr *va, long seq)
{
	rnode_t *rp;
	mntinfo_t *mi;
	int delta;

	rp = VTOR(vp);

	if (rp->r_seq != seq)
		return;

	mi = VTOMI(vp);
	if ((mi->mi_flags & MI_NOAC) || (vp->v_flag & VNOCACHE)) {
		PURGE_ATTRCACHE(vp);
		mutex_enter(&rp->r_statelock);
	} else {
		mutex_enter(&rp->r_statelock);
		/*
		 * Delta is the number of seconds that we will cache
		 * attributes of the file.  It is based on the number
		 * of seconds since the last time that we detected a
		 * a change (i.e. files that changed recently are
		 * likely to change soon), but there is a minimum and
		 * a maximum for regular files and for directories.
		 *
		 * Using the time since last change was detected
		 * eliminates the direct comparison or calculation
		 * using mixed client and server times.  NFS does
		 * not make any assumptions regarding the client
		 * and server clocks being synchronized.
		 */
		if (va->va_mtime.tv_sec != rp->r_attr.va_mtime.tv_sec) {
			rp->r_mtime = hrestime.tv_sec;
			delta = 0;
		} else
			delta = hrestime.tv_sec - rp->r_mtime;

		if (vp->v_type == VDIR) {
			if (delta < mi->mi_acdirmin)
				delta = mi->mi_acdirmin;
			else if (delta > mi->mi_acdirmax)
				delta = mi->mi_acdirmax;
		} else {
			if (delta < mi->mi_acregmin)
				delta = mi->mi_acregmin;
			else if (delta > mi->mi_acregmax)
				delta = mi->mi_acregmax;
		}
		rp->r_attrtime = hrestime.tv_sec + delta;
	}
	rp->r_attr = *va;
	rp->r_seq++;
	/*
	 * The real criteria for updating r_size should be if the
	 * file has grown on the server or if the client has not
	 * modified the file.
	 */
	if (rp->r_size != va->va_size &&
	    (rp->r_size < va->va_size ||
	    (!(rp->r_flags & RDIRTY) && rp->r_count == 0)))
		rp->r_size = va->va_size;
	mutex_exit(&rp->r_statelock);
}

/*
 * Fill in attribute from the cache.
 * If valid, then return 0 to indicate that no error occurred,
 * otherwise return 1 to indicate that an error occurred.
 */
static int
nfs_getattr_cache(vnode_t *vp, struct vattr *vap)
{
	rnode_t *rp;

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	if (ATTRCACHE_VALID(vp)) {
		/*
		 * Cached attributes are valid
		 */
		*vap = rp->r_attr;
		mutex_exit(&rp->r_statelock);
		return (0);
	}
	mutex_exit(&rp->r_statelock);
	return (1);
}

/*
 * Get attributes over-the-wire and update attributes cache
 * if no error occurred in the over-the-wire operation.
 * Return 0 if successful, otherwise error.
 */
int
nfs_getattr_otw(vnode_t *vp, struct vattr *vap, cred_t *cr)
{
	int error;
	struct nfsattrstat ns;
	int douprintf;
	mntinfo_t *mi;
	long seq;

	mi = VTOMI(vp);

	if (mi->mi_flags & MI_ACL) {
		error = acl_getattr2_otw(vp, vap, cr);
		if (mi->mi_flags & MI_ACL)
			return (error);
	}

	douprintf = 1;
	error = rfs2call(mi, RFS_GETATTR,
			xdr_fhandle, (caddr_t)VTOFH(vp),
			xdr_attrstat, (caddr_t)&ns,
			cr, &douprintf, &ns.ns_status);

	if (!error) {
		error = geterrno(ns.ns_status);
		if (!error) {
			nattr_to_vattr(vp, &ns.ns_attr, vap);
			nfs_cache_check(vp, vap->va_ctime, vap->va_mtime,
					vap->va_size, &seq, cr);
			nfs_attrcache_va(vp, vap, seq);
		} else {
			PURGE_STALE_FH(error, vp, cr);
		}
	}

	return (error);
}

/*
 * Return either cached ot remote attributes. If get remote attr
 * use them to check and invalidate caches, then cache the new attributes.
 */
int
nfsgetattr(vnode_t *vp, struct vattr *vap, cred_t *cr)
{
	int error;
	rnode_t *rp;

	/*
	 * If we've got cached attributes, we're done, otherwise go
	 * to the server to get attributes, which will update the cache
	 * in the process.
	 */
	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	while (rp->r_flags & RSERIALIZE) {
		if (!cv_wait_sig(&rp->r_cv, &rp->r_statelock)) {
			mutex_exit(&rp->r_statelock);
			return (EINTR);
		}
	}
	rp->r_flags |= RSERIALIZE;
	mutex_exit(&rp->r_statelock);

	error = nfs_getattr_cache(vp, vap);
	if (error)
		error = nfs_getattr_otw(vp, vap, cr);

	/* Return the client's view of file size */
	vap->va_size = rp->r_size;

	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RSERIALIZE;
	cv_broadcast(&rp->r_cv);
	mutex_exit(&rp->r_statelock);

	return (error);
}

/*
 * Get attributes over-the-wire and update attributes cache
 * if no error occurred in the over-the-wire operation.
 * Return 0 if successful, otherwise error.
 */
int
nfs3_getattr_otw(vnode_t *vp, struct vattr *vap, cred_t *cr)
{
	int error;
	GETATTR3args args;
	GETATTR3res res;
	int douprintf;
	long seq;

	args.object = *VTOFH3(vp);

	douprintf = 1;
	error = rfs3call(VTOMI(vp), NFSPROC3_GETATTR,
			xdr_GETATTR3args, (caddr_t)&args,
			xdr_GETATTR3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (!error) {
		error = geterrno3(res.status);
		if (!error) {
			fattr3_to_vattr(vp, &res.resok.obj_attributes,
					vap);
			nfs_cache_check(vp, vap->va_ctime, vap->va_mtime,
					vap->va_size, &seq, cr);
			nfs_attrcache_va(vp, vap, seq);
		} else {
			PURGE_STALE_FH(error, vp, cr);
		}
	}

	return (error);
}

/*
 * Return either cached or remote attributes. If get remote attr
 * use them to check and invalidate caches, then cache the new attributes.
 */
int
nfs3getattr(vnode_t *vp, struct vattr *vap, cred_t *cr)
{
	int error;
	rnode_t *rp;

	/*
	 * If we've got cached attributes, we're done, otherwise go
	 * to the server to get attributes, which will update the cache
	 * in the process.
	 */
	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	while (rp->r_flags & RSERIALIZE) {
		if (!cv_wait_sig(&rp->r_cv, &rp->r_statelock)) {
			mutex_exit(&rp->r_statelock);
			return (EINTR);
		}
	}
	rp->r_flags |= RSERIALIZE;
	mutex_exit(&rp->r_statelock);

	error = nfs_getattr_cache(vp, vap);
	if (error)
		error = nfs3_getattr_otw(vp, vap, cr);

	/* Return the client's view of file size */
	vap->va_size = rp->r_size;

	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RSERIALIZE;
	cv_broadcast(&rp->r_cv);
	mutex_exit(&rp->r_statelock);

	return (error);
}

/*
 * Convert NFS Version 2 over the network attributes to the local
 * virtual attributes.  The mapping between the UID_NOBODY/GID_NOBODY
 * network representation and the local representation is done here.
 */
void
nattr_to_vattr(vnode_t *vp, struct nfsfattr *na, struct vattr *vap)
{

	vap->va_type = (enum vtype)na->na_type;
	vap->va_mode = na->na_mode;
	vap->va_uid = (na->na_uid == NFS_UID_NOBODY) ? UID_NOBODY : na->na_uid;
	vap->va_gid = (na->na_gid == NFS_GID_NOBODY) ? GID_NOBODY : na->na_gid;
	vap->va_fsid = vp->v_vfsp->vfs_dev;
	vap->va_nodeid = na->na_nodeid;
	vap->va_nlink = na->na_nlink;
	vap->va_size = na->na_size;	/* keep for cache validation */
	vap->va_size0 = 0;
	vap->va_atime.tv_sec  = na->na_atime.tv_sec;
	vap->va_atime.tv_nsec = na->na_atime.tv_usec*1000;
	vap->va_mtime.tv_sec  = na->na_mtime.tv_sec;
	vap->va_mtime.tv_nsec = na->na_mtime.tv_usec*1000;
	vap->va_ctime.tv_sec  = na->na_ctime.tv_sec;
	vap->va_ctime.tv_nsec = na->na_ctime.tv_usec*1000;
	/*
	 * Shannon's law - uncompress the received dev_t
	 * if the top half of is zero indicating a response
	 * from an `older style' OS. Except for when it is a
	 * `new style' OS sending the maj device of zero,
	 * in which case the algorithm still works because the
	 * fact that it is a new style server
	 * is hidden by the minor device not being greater
	 * than 255 (a requirement in this case).
	 */
	if ((na->na_rdev & 0xffff0000) == 0)
		vap->va_rdev = nfsv2_expdev(na->na_rdev);
	else
		vap->va_rdev = na->na_rdev;

	vap->va_nblocks = na->na_blocks;
	switch (na->na_type) {

	case NFBLK:
		vap->va_blksize = DEV_BSIZE;
		break;

	case NFCHR:
		vap->va_blksize = MAXBSIZE;
		break;

	default:
		vap->va_blksize = na->na_blocksize;
		break;
	}
	/*
	 * This bit of ugliness is a hack to preserve the
	 * over-the-wire protocols for named-pipe vnodes.
	 * It remaps the special over-the-wire type to the
	 * VFIFO type. (see note in nfs.h)
	 *
	 * The check here was just for NA_ISFIFO(na). But
	 * (when SUID is set) the 2.x (v2) server code doesn't
	 * pass back na_type (== IFCHR) and na_rdev (== -1).
	 * Instead the map_type() function sets na_type == NFSOC and
	 * sets S_IFSOCK in na_mode. This needs to be checked
	 * for here as well. We need both checks.
	 */
	if (NA_ISFIFO(na) || na->na_type == NFSOC) {
		vap->va_type = VFIFO;
		vap->va_mode = (vap->va_mode & ~S_IFMT) | S_IFIFO;
		vap->va_rdev = 0;
		vap->va_blksize = na->na_blocksize;
	}
	vap->va_vcode = 0;
}

/*
 * Convert NFS Version 3 over the network attributes to the local
 * virtual attributes.  The mapping between the UID_NOBODY/GID_NOBODY
 * network representation and the local representation is done here.
 */
vtype_t nf3_to_vt[] = {
	VBAD, VREG, VDIR, VBLK, VCHR, VLNK, VFIFO, VFIFO
};

void
fattr3_to_vattr(vnode_t *vp, fattr3 *na, struct vattr *vap)
{

	if (na->type < NF3REG || na->type > NF3FIFO)
		vap->va_type = VBAD;
	else
		vap->va_type = nf3_to_vt[na->type];
	vap->va_mode = na->mode;
	vap->va_uid = (na->uid == NFS_UID_NOBODY) ? UID_NOBODY : (uid_t)na->uid;
	vap->va_gid = (na->gid == NFS_GID_NOBODY) ? GID_NOBODY : (gid_t)na->gid;
	vap->va_fsid = vp->v_vfsp->vfs_dev;
	vap->va_nodeid = na->fileid;
	vap->va_nlink = na->nlink;
	vap->va_size = (u_long)na->size;
	vap->va_size0 = 0;
	vap->va_atime.tv_sec  = na->atime.seconds;
	vap->va_atime.tv_nsec = na->atime.nseconds;
	vap->va_mtime.tv_sec  = na->mtime.seconds;
	vap->va_mtime.tv_nsec = na->mtime.nseconds;
	vap->va_ctime.tv_sec  = na->ctime.seconds;
	vap->va_ctime.tv_nsec = na->ctime.nseconds;

	vap->va_rdev = makedevice(na->rdev.specdata1, na->rdev.specdata2);

	switch (na->type) {
	case NF3BLK:
		vap->va_blksize = DEV_BSIZE;
		vap->va_nblocks = 0;
		break;
	case NF3CHR:
		vap->va_blksize = MAXBSIZE;
		vap->va_nblocks = 0;
		break;
	case NF3REG:
	case NF3DIR:
		vap->va_blksize = MAXBSIZE;
		vap->va_nblocks = (u_long)
			((na->used + (size3)MAXBSIZE - (size3)1) /
			(size3)MAXBSIZE);
		break;
	default:
		vap->va_blksize = MAXBSIZE;
		vap->va_nblocks = 0;
		break;
	}
	vap->va_vcode = 0;
}

/*
 * Asynchronous I/O parameters.  nfs_async_threads is the high-water mark
 * for the demand-based allocation of async threads per-mount.  The
 * nfs_async_timeout is the amount of time a thread will live after it
 * becomes idle, unless new I/O requests are received before the thread
 * dies.  See nfs_async_putpage and nfs_async_start.
 */

#define	NFS_ASYNC_TIMEOUT	(60 * 1 * HZ)	/* 1 minute */

int nfs_async_timeout = -1;	/* uninitialized */

int nfs_maxasynccount = 100;	/* max number of async reqs */

static void	nfs_async_start(struct vfs *);

void
nfs_async_readahead(vnode_t *vp, u_int blkoff, caddr_t addr,
	struct seg *seg, cred_t *cr, void (*readahead)(vnode_t *,
	u_int, caddr_t, struct seg *, cred_t *))
{
	rnode_t *rp;
	mntinfo_t *mi;
	struct nfs_async_reqs *args;
	extern int pages_before_pager;	/* XXX */

	rp = VTOR(vp);
	ASSERT(rp->r_freef == NULL);

	mi = VTOMI(vp);

	/*
	 * If memory on the system is starting to get tight, don't
	 * do readahead because this will just use memory that we
	 * don't want to tie up.  In any case, queue to at least
	 * nfs_maxasynccount requests.
	 */
	if (mi->mi_async_count > nfs_maxasynccount &&
	    freemem < lotsfree + pages_before_pager)
		return;

	/*
	 * If addr falls in a different segment, don't bother doing readahead.
	 */
	if (addr >= seg->s_base + seg->s_size)
		return;

	/*
	 * If we can't allocate a request structure, punt on the readahead.
	 */
	if ((args = (struct nfs_async_reqs *)
	    kmem_alloc(sizeof (*args), KM_NOSLEEP)) == NULL)
		return;

	args->a_next = NULL;
#ifdef DEBUG
	args->a_queuer = curthread;
#endif
	mutex_enter(&rp->r_statelock);
	rp->r_count++;
	mutex_exit(&rp->r_statelock);
	args->a_vp = vp;
	ASSERT(cr != NULL);
	crhold(cr);
	args->a_cred = cr;
	args->a_io = NFS_READ_AHEAD;
	args->a_nfs_readahead = readahead;
	args->a_nfs_blkoff = blkoff;
	args->a_nfs_seg = seg;
	args->a_nfs_addr = addr;

	mutex_enter(&mi->mi_async_lock);

	/*
	 * If asyncio has been disabled, don't bother readahead.
	 */
	if (mi->mi_max_threads == 0) {
		mutex_exit(&mi->mi_async_lock);
		goto noasync;
	}

	/*
	 * Check if we should create an async thread.  If we can't and
	 * there aren't any threads already running, punt on the readahead.
	 */
	if (mi->mi_threads < mi->mi_max_threads) {
		if (thread_create(NULL, NULL, nfs_async_start,
		    (caddr_t)vp->v_vfsp, 0, &p0, TS_RUN, 60) == NULL) {
			if (mi->mi_threads == 0) {
				mutex_exit(&mi->mi_async_lock);
				goto noasync;
			}
		} else
			mi->mi_threads++;
	}

	/*
	 * Link request structure into the async list and
	 * wakeup async thread to do the i/o.
	 */
	if (mi->mi_async_reqs == NULL) {
		mi->mi_async_reqs = args;
		mi->mi_async_tail = args;
	} else {
		mi->mi_async_tail->a_next = args;
		mi->mi_async_tail = args;
	}
	mi->mi_async_count++;
	cv_signal(&mi->mi_async_reqs_cv);
	mutex_exit(&mi->mi_async_lock);
	return;

noasync:
	mutex_enter(&rp->r_statelock);
	rp->r_count--;
	cv_signal(&rp->r_cv);
	mutex_exit(&rp->r_statelock);
	crfree(cr);
	kmem_free((caddr_t)args, sizeof (*args));
}

int
nfs_async_putapage(vnode_t *vp, page_t *pp, u_int off, u_int len,
	int flags, cred_t *cr, int (*putapage)(vnode_t *, page_t *,
	u_int, u_int, int, cred_t *))
{
	rnode_t *rp;
	mntinfo_t *mi;
	struct nfs_async_reqs *args;
	extern int pages_before_pager;	/* XXX */

	ASSERT(flags & B_ASYNC);
	ASSERT(vp->v_vfsp != NULL);

	rp = VTOR(vp);
	ASSERT(rp->r_count > 0);

	mi = VTOMI(vp);

	/*
	 * If memory on the system is starting to get tight, do
	 * the request synchronously to reduce the requirements
	 * to process the request and to get the request done as
	 * quickly as possible.  In any case, queue to at least
	 * nfs_maxasynccount requests.
	 */
	if (mi->mi_async_count > nfs_maxasynccount &&
	    freemem < lotsfree + pages_before_pager) {
		args = NULL;
		goto noasync;
	}

	/*
	 * If we can't allocate a request structure, do the putpage
	 * operation synchronously in this thread's context.
	 */
	if ((args = (struct nfs_async_reqs *)
	    kmem_alloc(sizeof (*args), KM_NOSLEEP)) == NULL)
		goto noasync;

	args->a_next = NULL;
#ifdef DEBUG
	args->a_queuer = curthread;
#endif
	mutex_enter(&rp->r_statelock);
	rp->r_count++;
	mutex_exit(&rp->r_statelock);
	args->a_vp = vp;
	ASSERT(cr != NULL);
	crhold(cr);
	args->a_cred = cr;
	args->a_io = NFS_PUTAPAGE;
	args->a_nfs_putapage = putapage;
	args->a_nfs_pp = pp;
	args->a_nfs_off = off;
	args->a_nfs_len = len;
	args->a_nfs_flags = flags;

	mutex_enter(&mi->mi_async_lock);

	/*
	 * If asyncio has been disabled, then make a synchronous request.
	 */
	if (mi->mi_max_threads == 0) {
		mutex_exit(&mi->mi_async_lock);
		goto noasync;
	}

	/*
	 * Check if we should create an async thread.  If we can't
	 * and there aren't any threads already running, do the i/o
	 * synchronously.
	 */
	if (mi->mi_threads < mi->mi_max_threads) {
		if (thread_create(NULL, NULL, nfs_async_start,
		    (caddr_t) vp->v_vfsp, 0, &p0, TS_RUN, 60) == NULL) {
			if (mi->mi_threads == 0) {
				mutex_exit(&mi->mi_async_lock);
				goto noasync;
			}
		} else
			mi->mi_threads++;
	}

	/*
	 * Link request structure into the async list and
	 * wakeup async thread to do the i/o.
	 */
	if (mi->mi_async_reqs == NULL) {
		mi->mi_async_reqs = args;
		mi->mi_async_tail = args;
	} else {
		mi->mi_async_tail->a_next = args;
		mi->mi_async_tail = args;
	}
	mi->mi_async_count++;
	cv_signal(&mi->mi_async_reqs_cv);
	mutex_exit(&mi->mi_async_lock);
	return (0);

noasync:
	if (args != NULL) {
		mutex_enter(&rp->r_statelock);
		rp->r_count--;
		cv_signal(&rp->r_cv);
		mutex_exit(&rp->r_statelock);
		crfree(cr);
		kmem_free((caddr_t)args, sizeof (*args));
	}

	return ((*putapage)(vp, pp, off, len, flags, cr));
}

int
nfs_async_pageio(vnode_t *vp, page_t *pp, u_int io_off, u_int io_len,
	int flags, cred_t *cr, int (*pageio)(vnode_t *, page_t *, u_int,
	u_int, int, cred_t *))
{
	rnode_t *rp;
	mntinfo_t *mi;
	struct nfs_async_reqs *args;
	extern int pages_before_pager;	/* XXX */

	ASSERT(flags & B_ASYNC);
	ASSERT(vp->v_vfsp != NULL);

	rp = VTOR(vp);
	ASSERT(rp->r_count > 0);

	mi = VTOMI(vp);

	/*
	 * If memory on the system is starting to get tight, do
	 * the request synchronously to reduce the requirements
	 * to process the request and to get the request done as
	 * quickly as possible.  In any case, queue to at least
	 * nfs_maxasynccount requests.
	 */
	if (mi->mi_async_count > nfs_maxasynccount &&
	    freemem < lotsfree + pages_before_pager) {
		args = NULL;
		goto noasync;
	}

	/* . . . or if we can't allocate a request structure . . . */
	if ((args = (struct nfs_async_reqs *)
	    kmem_alloc(sizeof (*args), KM_NOSLEEP)) == NULL)
		goto noasync;

	args->a_next = NULL;
#ifdef DEBUG
	args->a_queuer = curthread;
#endif
	mutex_enter(&rp->r_statelock);
	rp->r_count++;
	mutex_exit(&rp->r_statelock);
	args->a_vp = vp;
	ASSERT(cr != NULL);
	crhold(cr);
	args->a_cred = cr;
	args->a_io = NFS_PAGEIO;
	args->a_nfs_pageio = pageio;
	args->a_nfs_pp = pp;
	args->a_nfs_off = io_off;
	args->a_nfs_len = io_len;
	args->a_nfs_flags = flags;

	mutex_enter(&mi->mi_async_lock);

	/* if asyncio has been disabled . . . */
	if (mi->mi_max_threads == 0) {
		mutex_exit(&mi->mi_async_lock);
		goto noasync;
	}

	/* . . . or if we should but can't create a thread. */
	if (mi->mi_threads < mi->mi_max_threads) {
		if (thread_create(NULL, NULL, nfs_async_start,
		    (caddr_t) vp->v_vfsp, 0, &p0, TS_RUN, 60) == NULL) {
			if (mi->mi_threads == 0) {
				mutex_exit(&mi->mi_async_lock);
				goto noasync;
			}
		} else
			mi->mi_threads++;
	}

	/*
	 * Link request structure into the async list and
	 * wakeup async thread to do the i/o.
	 */
	if (mi->mi_async_reqs == NULL) {
		mi->mi_async_reqs = args;
		mi->mi_async_tail = args;
	} else {
		mi->mi_async_tail->a_next = args;
		mi->mi_async_tail = args;
	}
	mi->mi_async_count++;
	cv_signal(&mi->mi_async_reqs_cv);
	mutex_exit(&mi->mi_async_lock);
	return (0);

noasync:
	if (args != NULL) {
		mutex_enter(&rp->r_statelock);
		rp->r_count--;
		cv_signal(&rp->r_cv);
		mutex_exit(&rp->r_statelock);
		crfree(cr);
		kmem_free((caddr_t)args, sizeof (*args));
	}

	/*
	 * If we can't do it ASYNC, for reads we do nothing (but cleanup
	 * the page list), for writes we do it synchronously.
	 */
	if (flags & B_READ) {
		pvn_read_done(pp, B_ERROR | flags);
		return (0);
	}
	return ((*pageio)(vp, pp, io_off, io_len, flags, cr));
}

void
nfs_async_readdir(vnode_t *vp, rddir_cache *rdc, cred_t *cr,
	int (*readdir)(vnode_t *, rddir_cache *, cred_t *))
{
	rnode_t *rp;
	mntinfo_t *mi;
	struct nfs_async_reqs *args;
	extern int pages_before_pager;	/* XXX */

	rp = VTOR(vp);
	ASSERT(rp->r_freef == NULL);

	mi = VTOMI(vp);

	/*
	 * If memory on the system is starting to get tight, do
	 * the request synchronously to reduce the requirements
	 * to process the request and to get the request done as
	 * quickly as possible.  In any case, queue to at least
	 * nfs_maxasynccount requests.
	 */
	if (mi->mi_async_count > nfs_maxasynccount &&
	    freemem < lotsfree + pages_before_pager) {
		args = NULL;
		goto noasync;
	}

	/*
	 * If we can't allocate a request structure, do the readdir
	 * operation synchronously in this thread's context.
	 */
	if ((args = (struct nfs_async_reqs *)
	    kmem_alloc(sizeof (*args), KM_NOSLEEP)) == NULL)
		goto noasync;

	args->a_next = NULL;
#ifdef DEBUG
	args->a_queuer = curthread;
#endif
	mutex_enter(&rp->r_statelock);
	rp->r_count++;
	mutex_exit(&rp->r_statelock);
	args->a_vp = vp;
	ASSERT(cr != NULL);
	crhold(cr);
	args->a_cred = cr;
	args->a_io = NFS_READDIR;
	args->a_nfs_readdir = readdir;
	args->a_nfs_rdc = rdc;

	mutex_enter(&mi->mi_async_lock);

	/*
	 * If asyncio has been disabled, then make a synchronous request.
	 */
	if (mi->mi_max_threads == 0) {
		mutex_exit(&mi->mi_async_lock);
		goto noasync;
	}

	/*
	 * Check if we should create an async thread.  If we can't
	 * and there aren't any threads already running, do the
	 * readdir synchronously.
	 */
	if (mi->mi_threads < mi->mi_max_threads) {
		if (thread_create(NULL, NULL, nfs_async_start,
		    (caddr_t) vp->v_vfsp, 0, &p0, TS_RUN, 60) == NULL) {
			if (mi->mi_threads == 0) {
				mutex_exit(&mi->mi_async_lock);
				goto noasync;
			}
		} else
			mi->mi_threads++;
	}

	/*
	 * Link request structure into the async list and
	 * wakeup async thread to do the i/o.
	 */
	if (mi->mi_async_reqs == NULL) {
		mi->mi_async_reqs = args;
		mi->mi_async_tail = args;
	} else {
		mi->mi_async_tail->a_next = args;
		mi->mi_async_tail = args;
	}
	mi->mi_async_count++;
	cv_signal(&mi->mi_async_reqs_cv);
	mutex_exit(&mi->mi_async_lock);
	return;

noasync:
	if (args != NULL) {
		mutex_enter(&rp->r_statelock);
		rp->r_count--;
		cv_signal(&rp->r_cv);
		mutex_exit(&rp->r_statelock);
		crfree(cr);
		kmem_free((caddr_t)args, sizeof (*args));
	}

	rdc->entries = NULL;
	mutex_enter(&rp->r_statelock);
	ASSERT(rdc->flags & RDDIR);
	rdc->flags &= ~RDDIR;
	rdc->flags |= RDDIRREQ;
	mutex_exit(&rp->r_statelock);
}

static void
nfs_async_start(struct vfs *vfsp)
{
	struct nfs_async_reqs *args;
	mntinfo_t *mi = VFTOMI(vfsp);
	int time_left = 1;
	callb_cpr_t cprinfo;
	rnode_t *rp;
#define	BUGID_1128628
#ifdef	BUGID_1128628
	unsigned int tmp1;
#endif	BUGID_1128628

	/*
	 * Dynamic initialization of nfs_async_timeout to allow nfs to be
	 * built in an implementation independent manner.
	 */
	if (nfs_async_timeout == -1)
		nfs_async_timeout = NFS_ASYNC_TIMEOUT;

	CALLB_CPR_INIT(&cprinfo, &mi->mi_async_lock, callb_generic_cpr, "nas");

	for (;;) {
		mutex_enter(&mi->mi_async_lock);
		while ((args = mi->mi_async_reqs) == NULL) {
			/*
			 * Exiting is considered to be safe for CPR as well
			 */
			CALLB_CPR_SAFE_BEGIN(&cprinfo);

			/*
			 * Wakeup thread waiting to unmount the file
			 * system only if all async threads are inactive.
			 *
			 * If we've timed-out and there's nothing to do,
			 * then get rid of this thread.
			 */
			if (mi->mi_max_threads == 0 || time_left <= 0) {
				if (--mi->mi_threads == 0)
					cv_signal(&mi->mi_async_cv);
				CALLB_CPR_EXIT(&cprinfo);
				mutex_exit(&mi->mi_async_lock);
				thread_exit();
				/* NOTREACHED */
			}
			time_left = cv_timedwait(&mi->mi_async_reqs_cv,
			    &mi->mi_async_lock, nfs_async_timeout + lbolt);

			CALLB_CPR_SAFE_END(&cprinfo, &mi->mi_async_lock);
		}
		mi->mi_async_reqs = args->a_next;
		mi->mi_async_count--;
		mutex_exit(&mi->mi_async_lock);

		/*
		 * Obtain arguments from the async request structure.
		 */
		if (args->a_io == NFS_READ_AHEAD && mi->mi_max_threads > 0) {
			(*args->a_nfs_readahead)(args->a_vp, args->a_nfs_blkoff,
					args->a_nfs_addr, args->a_nfs_seg,
					args->a_cred);
		} else if (args->a_io == NFS_PUTAPAGE) {
#ifdef BUGID_1128628
			/*
			 * this is a workaround due to a register allocator
			 * problem with SC2.0.1 code generator.
			 * already fixed in SC3.0
			 */
			tmp1 = (u_int)args->a_nfs_off;
			(void) (*args->a_nfs_putapage)(args->a_vp,
					args->a_nfs_pp, tmp1,
					args->a_nfs_len, args->a_nfs_flags,
					args->a_cred);
#else
			(void) (*args->a_nfs_putapage)(args->a_vp,
					args->a_nfs_pp, args->a_nfs_off,
					args->a_nfs_len, args->a_nfs_flags,
					args->a_cred);
#endif BUGID_1128628
		} else if (args->a_io == NFS_PAGEIO) {
			(void) (*args->a_nfs_pageio)(args->a_vp,
					args->a_nfs_pp, args->a_nfs_off,
					args->a_nfs_len, args->a_nfs_flags,
					args->a_cred);
		} else if (args->a_io == NFS_READDIR) {
			(void) ((*args->a_nfs_readdir)(args->a_vp,
					args->a_nfs_rdc, args->a_cred));
		}

		/*
		 * Now, release the vnode and free the credentials
		 * structure.
		 */
		rp = VTOR(args->a_vp);
		mutex_enter(&rp->r_statelock);
		rp->r_count--;
		cv_signal(&rp->r_cv);
		mutex_exit(&rp->r_statelock);
		crfree(args->a_cred);
		kmem_free((caddr_t)args, sizeof (*args));
	}
}

void
nfs_async_stop(struct vfs *vfsp)
{
	mntinfo_t *mi = VFTOMI(vfsp);

	/*
	 * Wait for all outstanding putpage operations to complete.
	 */
	mutex_enter(&mi->mi_async_lock);
	mi->mi_max_threads = 0;
	cv_broadcast(&mi->mi_async_reqs_cv);
	while (mi->mi_threads != 0)
		cv_wait(&mi->mi_async_cv, &mi->mi_async_lock);
	mutex_exit(&mi->mi_async_lock);
}

int
writerp(rnode_t *rp, caddr_t base, int tcount, struct uio *uio)
{
	int pagecreate;
	register int n;
	register int saved_n;
	register caddr_t saved_base;
	register int offset;
	int error;
	int sm_error;

	ASSERT(tcount <= MAXBSIZE && tcount <= uio->uio_resid);
	ASSERT(((u_int)base & MAXBOFFSET) + tcount <= MAXBSIZE);
	ASSERT(RW_WRITE_HELD(&rp->r_rwlock));

	/*
	 * Move bytes in at most PAGESIZE chunks. We must avoid
	 * spanning pages in uiomove() because page faults may cause
	 * the cache to be invalidated out from under us. The r_size is not
	 * updated until after the uiomove. If we push the last page of a
	 * file before r_size is correct, we will lose the data written past
	 * the current (and invalid) r_size.
	 */
	do {
		offset = uio->uio_offset;
		pagecreate = 0;

		/*
		 * n is the number of bytes required to satisfy the request
		 *   or the number of bytes to fill out the page.
		 */
		n = MIN((PAGESIZE - ((u_int)base & PAGEOFFSET)), tcount);

		/*
		 * Check to see if we can skip reading in the page
		 * and just allocate the memory.  We can do this
		 * if we are going to rewrite the entire mapping
		 * or if we are going to write to or beyond the current
		 * end of file from the beginning of the mapping.
		 *
		 * The read of r_size is atomic (no lock needed).
		 */
		if (((u_int)base & PAGEOFFSET) == 0 && (n == PAGESIZE ||
		    ((offset + n) >= rp->r_size))) {

			/*
			 * The last argument tells segmap_pagecreate() to
			 * always lock the page, as opposed to sometimes
			 * returning with the page locked. This way we avoid a
			 * fault on the ensuing uiomove(), but also
			 * more importantly (to fix bug 1094402) we can
			 * call segmap_fault() to unlock the page in all
			 * cases. An alternative would be to modify
			 * segmap_pagecreate() to tell us when it is
			 * locking a page, but that's a fairly major
			 * interface change.
			 */
			segmap_pagecreate(segkmap, base, (u_int)n, 1);
			pagecreate = 1;
			saved_base = base;
			saved_n = n;
		}

		/*
		 * The number of bytes of data in the last page can not
		 * be accurately be determined while page is being
		 * uiomove'd to and the size of the file being updated.
		 * Thus, inform threads which need to know accurately
		 * how much data is in the last page of the file.  They
		 * will not do the i/o immediately, but will arrange for
		 * the i/o to happen later when this modify operation
		 * will have finished.
		 */
		ASSERT(!(rp->r_flags & RMODINPROGRESS));
		mutex_enter(&rp->r_statelock);
		rp->r_flags |= RMODINPROGRESS;
		rp->r_modaddr = (offset & MAXBMASK);
		mutex_exit(&rp->r_statelock);

		error = uiomove(base, n, UIO_WRITE, uio);

		/*
		 * r_size is the maximum number of
		 * bytes known to be in the file.
		 * Make sure it is at least as high as the
		 * last byte we just wrote into the buffer.
		 */
		mutex_enter(&rp->r_statelock);
		if (rp->r_size < uio->uio_offset)
			rp->r_size = uio->uio_offset;
		rp->r_flags &= ~RMODINPROGRESS;
		rp->r_flags |= RDIRTY;
		cv_signal(&rp->r_cv);
		mutex_exit(&rp->r_statelock);

		n = uio->uio_offset - offset; /* n = # of bytes written */
		base += n;
		tcount -= n;
		/*
		 * If we created pages w/o initializing them completely,
		 * we need to zero the part that wasn't set up.
		 * This happens on a most EOF write cases and if
		 * we had some sort of error during the uiomove.
		 */
		if (pagecreate) {
			if ((uio->uio_offset & PAGEOFFSET) || n == 0) {
				(void) kzero(base, (u_int)(PAGESIZE - n));
			}

			/*
			 * For bug 1094402: segmap_pagecreate locks page.
			 * Unlock it.
			 */
			sm_error = segmap_fault(kas.a_hat, segkmap,
				saved_base, saved_n, F_SOFTUNLOCK, S_WRITE);

			if (error == 0)
				error = sm_error;
		}
	} while (tcount > 0 && error == 0);

	return (error);
}

int
nfs_putpages(vnode_t *vp, offset_t off, u_int len, int flags, cred_t *cr)
{
	register rnode_t *rp;
	register page_t *pp;
	register u_int eoff;
	u_int io_off;
	u_int io_len;
	int error;
	int rdirty;
	int err;

	rp = VTOR(vp);
	ASSERT(rp->r_count > 0);

	if (vp->v_pages == NULL)
		return (0);

	ASSERT(vp->v_type != VCHR);

	/*
	 * If ROUTOFSPACE is set, then all writes turn into B_INVAL
	 * writes.  B_FORCE is set to force the VM system to actually
	 * invalidate the pages, even if the i/o failed.  The pages
	 * need to get invalidated because they can't be written out
	 * because there isn't any space left on either the server's
	 * file system or in the user's disk quota.
	 */
	if (rp->r_flags & ROUTOFSPACE)
		flags |= (B_INVAL | B_FORCE);

	if (len == 0) {
		/*
		 * If doing a full file operation, then clear the
		 * RDIRTY bit.  If a page gets dirtied while the flush
		 * is happening, then RDIRTY will get set again.  The
		 * RDIRTY bit must get cleared before the flush so that
		 * we don't lose this information.
		 */
		if (off == 0 && (rp->r_flags & RDIRTY)) {
			mutex_enter(&rp->r_statelock);
			rdirty = (rp->r_flags & RDIRTY);
			rp->r_flags &= ~RDIRTY;
			mutex_exit(&rp->r_statelock);
		} else
			rdirty = 0;

		/*
		 * Search the entire vp list for pages >= off, and flush
		 * the dirty pages.
		 */
		error = pvn_vplist_dirty(vp, (u_int)off, rp->r_putapage,
					flags, cr);

		/*
		 * If an error occured and the file was marked as dirty
		 * before and we aren't forcibly invalidating pages, then
		 * reset the RDIRTY flag.
		 */
		if (error && rdirty &&
		    (flags & (B_INVAL | B_FORCE)) != (B_INVAL | B_FORCE)) {
			mutex_enter(&rp->r_statelock);
			rp->r_flags |= RDIRTY;
			mutex_exit(&rp->r_statelock);
		}
	} else {
		/*
		 * Do a range from [off...off + len) looking for pages
		 * to deal with.
		 */
		error = 0;
#ifdef lint
		io_len = 0;
#endif
		eoff = (u_int)off + len;
		for (io_off = (u_int)off; io_off < eoff && io_off < rp->r_size;
		    io_off += io_len) {
			/*
			 * If we are not invalidating, synchronously
			 * freeing or writing pages use the routine
			 * page_lookup_nowait() to prevent reclaiming
			 * them from the free list.
			 */
			if ((flags & B_INVAL) || !(flags & B_ASYNC)) {
				pp = page_lookup(vp, io_off,
					(flags & (B_INVAL | B_FREE)) ?
					    SE_EXCL : SE_SHARED);
			} else {
				pp = page_lookup_nowait(vp, io_off,
					(flags & B_FREE) ? SE_EXCL : SE_SHARED);
			}

			if (pp == NULL || !pvn_getdirty(pp, flags))
				io_len = PAGESIZE;
			else {
				err = (*rp->r_putapage)(vp, pp, &io_off,
							&io_len, flags, cr);
				if (!error)
					error = err;
				/*
				 * "io_off" and "io_len" are returned as
				 * the range of pages we actually wrote.
				 * This allows us to skip ahead more quickly
				 * since several pages may've been dealt
				 * with by this iteration of the loop.
				 */
			}
		}
	}

	return (error);
}

void
nfs_invalidate_pages(vnode_t *vp, u_int off, cred_t *cr)
{
	rnode_t *rp;

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	while (rp->r_flags & RTRUNCATE)
		cv_wait(&rp->r_cv, &rp->r_statelock);
	rp->r_flags |= RTRUNCATE;
	if (off == 0) {
		rp->r_flags &= ~RDIRTY;
		rp->r_error = 0;
	}
	mutex_exit(&rp->r_statelock);
	rp->r_truncaddr = off;
	(void) pvn_vplist_dirty(vp, off, rp->r_putapage, B_INVAL | B_TRUNC, cr);
	mutex_enter(&rp->r_statelock);
	rp->r_flags &= ~RTRUNCATE;
	cv_signal(&rp->r_cv);
	mutex_exit(&rp->r_statelock);
}

static int nfs_write_error_to_cons_only = 0;
#define	MSG(x)	(nfs_write_error_to_cons_only ? (x) : (x) + 1)

/*
 * Print a file handle
 */
void
nfs_printfhandle(nfs_fhandle *fhp)
{
	int *ip;
	char *buf;
	size_t bufsize;
	char *cp;

	/*
	 * 13 == "(file handle:"
	 * maximum of NFS_FHANDLE / sizeof (*ip) elements in fh_buf times
	 *	1 == ' '
	 *	8 == maximum strlen of "%x"
	 * 3 == ")\n\0"
	 */
	bufsize = 13 + ((NFS_FHANDLE_LEN / sizeof (*ip)) * (1 + 8)) + 3;
	buf = kmem_alloc(bufsize, KM_NOSLEEP);
	if (buf == NULL)
		return;

	cp = buf;
	strcpy(cp, "(file handle:");
	while (*cp != '\0')
		cp++;
	for (ip = (int *)fhp->fh_buf;
	    ip < (int *)&fhp->fh_buf[fhp->fh_len];
	    ip++) {
		sprintf(cp, " %x", *ip);
		while (*cp != '\0')
			cp++;
	}
	strcpy(cp, ")\n");

	cmn_err(CE_CONT, MSG("^%s"), buf);

	kmem_free(buf, bufsize);
}

/*
 * Notify the system administrator that an NFS write error has
 * occurred.
 */

/* seconds between ENOSPC/EDQUOT messages */
static clock_t nfs_write_error_interval = 5;

void
nfs_write_error(vnode_t *vp, int error, cred_t *cr)
{
	mntinfo_t *mi;

	/*
	 * No use in flooding the console with ENOSPC
	 * messages from the same file system.
	 */
	mi = VTOMI(vp);
	if ((error != ENOSPC && error != EDQUOT) || lbolt > mi->mi_printftime) {
#ifdef DEBUG
		nfs_perror(error, "NFS%ld write error on host %s: %m.\n",
			mi->mi_vers, mi->mi_hostname);
#else
		nfs_perror(error, "NFS write error on host %s: %m.\n",
			mi->mi_hostname);
#endif
		if (error == ENOSPC || error == EDQUOT) {
			cmn_err(CE_CONT,
				MSG("^File: userid=%ld, groupid=%ld\n"),
				cr->cr_uid, cr->cr_gid);
			if (curthread->t_cred->cr_uid != cr->cr_uid ||
			    curthread->t_cred->cr_gid != cr->cr_gid) {
				cmn_err(CE_CONT,
					MSG("^User: userid=%ld, groupid=%ld\n"),
					curthread->t_cred->cr_uid,
					curthread->t_cred->cr_gid);
			}
			mi->mi_printftime = lbolt +
					nfs_write_error_interval * HZ;
		}
		nfs_printfhandle(&VTOR(vp)->r_fh);
#ifdef DEBUG
		if (error == EACCES) {
			cmn_err(CE_CONT, MSG("^nfs_bio: cred is%s kcred\n"),
				cr == kcred ? "" : " not");
		}
#endif
	}
}

/*
 * NFS Client initialization routine.  This routine should only be called
 * once.  It performs the following tasks:
 *	- Initalize all global locks
 * 	- Call sub-initialization routines (localize access to variables)
 */
int
nfs_clntinit(void)
{
#ifdef DEBUG
	static boolean_t nfs_clntup = B_FALSE;
#endif
	int error;

#ifdef DEBUG
	ASSERT(nfs_clntup == B_FALSE);
#endif

	error = nfs_subrinit();
	if (error)
		return (error);

	error = nfs_vfsinit();
	if (error)
		return (error);

#ifdef DEBUG
	nfs_clntup = B_TRUE;
#endif
	return (0);
}

/*
 * nfs_lockrelease:
 *
 * Release any locks on the given vnode that are held by the current
 * process.
 *
 * Returns:
 * 0		no error.
 * else		error returned when trying to release lock(s).
 */
int
nfs_lockrelease(vnode_t *vp, int flag, offset_t offset, cred_t *cr)
{
	flock_t ld;
	int remote_lock_possible;
	int rv;

	/*
	 * Generate an explicit unlock operation for the entire file.  As a
	 * partial optimization, only generate the unlock if there is a
	 * lock registered for the file.  We could check whether this
	 * particular process has any locks on the file, but that would
	 * require the local locking code to provide yet another query
	 * routine.  Note that no explicit synchronization is needed here.
	 * At worst, flk_has_remote_locks() will return a false positive,
	 * in which case the unlock call wastes time but doesn't harm
	 * correctness.
	 *
	 * In addition, an unlock request is generated if the process
	 * is listed as possibly having a lock on the file because the
	 * server and client lock managers may have gotten out of sync.
	 * N.B. It is important to make sure nfs_remove_locking_pid() is
	 * called here even if flk_has_remote_locks(vp) reports true.
	 * If it is not called and there is an entry on the process id
	 * list, that entry will never get removed.
	 */
	remote_lock_possible =
		nfs_remove_locking_pid(vp, ttoproc(curthread)->p_pid);
	if (flk_has_remote_locks(vp) || remote_lock_possible) {
		ld.l_type = F_UNLCK;	/* set to unlock entire file */
		ld.l_whence = 0;	/* unlock from start of file */
		ld.l_start = 0;
		ld.l_len = 0;		/* do entire file */
		rv = VOP_FRLOCK(vp, F_SETLK, &ld, flag, offset, cr);
		/*
		 * the call to VOP_FRLOCK may put the pid back on the
		 * list.  We need to remove it.
		 */
		(void) nfs_remove_locking_pid(vp, ttoproc(curthread)->p_pid);
		return (rv);
	}
	return (0);
}

/*
 * nfs_lockcompletion:
 *
 * If the vnode has a lock, mark it as non cachable (set VNOCACHE bit).
 *
 * The client side of the lock manager code uses the local locking
 * code to keep track of locks held remotely.  This allows us to look
 * down into the local locking code to see if anything exists for a vnode
 */
void
nfs_lockcompletion(vnode_t *vp, int cmd, struct flock *bfp)
{

	if (cmd == F_SETLK || cmd == F_SETLKW) {
		/*
		 * The v_lock mutex is held here so that v_flag can be set
		 * atomically with determining if the vnode has locks.  It
		 * must be held before the call to flk_has_remote_locks()
		 * to keep another thread out of this section until v_flag
		 * is set.
		 */
		mutex_enter(&vp->v_lock);
		if (flk_has_remote_locks(vp)) {
			if (bfp->l_type != F_UNLCK) {
				/*
				 * we have just acquired a lock
				 * make sure the VNOCACHE bit is set
				 */
				vp->v_flag |= VNOCACHE;
			}
		} else {
			if (bfp->l_type == F_UNLCK) {
				/*
				 * we have just released the last lock
				 * clear the VNOCACHE bit
				 */
				vp->v_flag &= ~VNOCACHE;
			}
		}
		mutex_exit(&vp->v_lock);
	}
}

/*
 * The lock manager holds state making it possible for the client
 * and server to be out of sync.  For example, if the response from
 * the server granting a lock request is lost, the server will think
 * the lock is granted and the client will think the lock is lost.
 * The client can tell when it is not positive if it is in sync with
 * the server.
 *
 * To deal with this, a list of processes for which the client is
 * not sure if the server holds a lock is attached to the rnode.
 * When such a process closes the rnode, an unlock request is sent
 * to the server to unlock the entire file.
 *
 * The list is kept as a singularly linked NULL terminated list.
 * Because it is only added to under extreme error conditions, the
 * list shouldn't get very big.  DEBUG kernels print a message if
 * the list gets bigger than nfs_lmpl_high_water.  This is arbitrarily
 * choosen to be 8, but can be tuned at runtime.
 */
#ifdef DEBUG
int nfs_lmpl_high_water = 8;
int nfs_cnt_add_locking_pid = 0;
int nfs_len_add_locking_pid = 0;
#endif DEBUG

/*
 * Record that the nfs lock manager server may be holding a lock on
 * a vnode for a process.
 *
 * Because the nfs lock manager server holds state, it is possible
 * for the server to get out of sync with the client.  This routine is called
 * from the client when it is no longer sure if the server is in sync
 * with the client.  nfs_lockrelease() will then notice this and send
 * an unlock request when the file is closed
 */
void
nfs_add_locking_pid(vnode_t *vp, pid_t pid)
{
	rnode_t *rp;
	lmpl_t *new;
	lmpl_t *cur;
	lmpl_t **lmplp;
#ifdef DEBUG
	int list_len = 1;
#endif DEBUG

#ifdef DEBUG
	++nfs_cnt_add_locking_pid;
#endif DEBUG
	/*
	 * allocate new lmpl_t now so we don't sleep
	 * later after grabbing mutexes
	 */
	new = (lmpl_t *) kmem_alloc(sizeof (lmpl_t), KM_SLEEP);
	new->lmpl_pid = pid;
	new->lmpl_next = (lmpl_t *) NULL;
	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);

	/*
	 * Add this pid to the list for this rnode only if the
	 * rnode is active and the pid is not already there.
	 */
	ASSERT(rp->r_flags & RHASHED);
	lmplp = &(rp->r_lmpl);
	for (cur = rp->r_lmpl; cur != (lmpl_t *) NULL; cur = cur->lmpl_next) {
		if (cur->lmpl_pid == pid) {
			kmem_free((void *) new, sizeof (lmpl_t));
			break;
		}
		lmplp = &cur->lmpl_next;
#ifdef DEBUG
		++list_len;
#endif DEBUG
	}
	if (cur == (lmpl_t *) NULL) {
		*lmplp = new;
#ifdef DEBUG
		if (list_len > nfs_len_add_locking_pid) {
			nfs_len_add_locking_pid = list_len;
		}
		if (list_len > nfs_lmpl_high_water) {
			cmn_err(CE_WARN,
				"nfs_add_locking_pid: long list vp=0x%x is %d",
				(int) vp, list_len);
		}
#endif DEBUG
	}
	mutex_exit(&rp->r_statelock);
}

/*
 * Remove a pid from the lock manager pid list.
 *
 * If the pid is not in the list return 0.  If it was found and
 * removed, return 1;
 */
static int
nfs_remove_locking_pid(vnode_t *vp, pid_t pid)
{
	lmpl_t *cur;
	lmpl_t **lmplp;
	rnode_t *rp;
	int rv = 0;

	rp = VTOR(vp);
	mutex_enter(&rp->r_statelock);
	ASSERT(rp->r_flags & RHASHED);
	lmplp = &(rp->r_lmpl);
	/*
	 * search through the list and remove the entry for
	 * this pid if it is there
	 */
	for (cur = rp->r_lmpl; cur != (lmpl_t *) NULL; cur = cur->lmpl_next) {
		if (cur->lmpl_pid == pid) {
			*lmplp = cur->lmpl_next;
			kmem_free((void *) cur, sizeof (lmpl_t));
			rv = 1;
			break;
		}
		lmplp = &cur->lmpl_next;
	}
	mutex_exit(&rp->r_statelock);
	return (rv);
}

void
nfs_free_mi(mntinfo_t *mi)
{

	if (mi->mi_netnamelen > 0)
		kmem_free((caddr_t)mi->mi_netname, (u_int)mi->mi_netnamelen);

	if (mi->mi_addr.len > 0)
		kmem_free((caddr_t)mi->mi_addr.buf, mi->mi_addr.len);

	if (mi->mi_authflavor == AUTH_DES || mi->mi_authflavor == AUTH_KERB)
		kmem_free((caddr_t)mi->mi_syncaddr.buf, mi->mi_syncaddr.len);

	if (mi->mi_knetconfig != NULL) {
		kmem_free((caddr_t)mi->mi_knetconfig->knc_protofmly,
			KNC_STRSIZE);
		kmem_free((caddr_t)mi->mi_knetconfig->knc_proto, KNC_STRSIZE);
		kmem_free((caddr_t)mi->mi_knetconfig,
			sizeof (*mi->mi_knetconfig));
	}
	mutex_destroy(&mi->mi_lock);
	mutex_destroy(&mi->mi_async_lock);
	cv_destroy(&mi->mi_async_reqs_cv);
	cv_destroy(&mi->mi_async_cv);
	kmem_free((caddr_t)mi, sizeof (*mi));
}
