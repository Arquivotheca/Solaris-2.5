/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_cnode.c 1.78     94/09/13 SMI"


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
#include <sys/buf.h>
#include <netinet/in.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
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

extern struct cnode *cachefs_freeback;
extern struct cnode *cachefs_freefront;
kmutex_t cachefs_cnode_freelist_lock;
extern int maxcnodes;
extern struct vnodeops cachefs_vnodeops;
extern int nfsfstyp;

/*
 * Functions for cnode management.
 */

/*
 * Add a cnode to the hash queues.
 */
void
cachefs_addhash(struct cnode *cp)
{
	struct fscache *fscp = C_TO_FSCACHE(cp);
	int hash = CHASH(cp->c_fileno);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_addhash: ENTER cp %x\n", (int) cp);
#endif
	ASSERT(MUTEX_HELD(&fscp->fs_cnodelock));
	ASSERT(cp->c_hash == NULL);
	cp->c_hash = fscp->fs_cnode[hash];
	fscp->fs_cnode[hash] = cp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_addhash: EXIT\n");
#endif
}

/*
 * Remove a cnode from the hash queues
 */
void
cachefs_remhash(struct cnode *cp)
{
	struct cnode **headpp;
	struct fscache *fscp = C_TO_FSCACHE(cp);
	int found = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_remhash: ENTER cp %x\n", (int) cp);
#endif
	ASSERT(MUTEX_HELD(&fscp->fs_cnodelock));

	for (headpp = &fscp->fs_cnode[CHASH(cp->c_fileno)];
		*headpp != NULL; headpp = &(*headpp)->c_hash) {
		if (*headpp == cp) {
			*headpp = cp->c_hash;
			cp->c_hash = NULL;
			found++;
			break;
		}
	}
	ASSERT(found);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_remhash: EXIT\n");
#endif
}

/*
 * Add a cnode to the cnode free list.
 */
void
cachefs_addfree(struct cnode *cp)
{
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_addfree: ENTER cp %x\n", (int) cp);
#endif
	ASSERT(cp->c_backvp == NULL);
	ASSERT(MUTEX_HELD(&cachefs_cnode_freelist_lock));
	cp->c_freefront = cachefs_freeback;
	cp->c_freeback =  NULL;
	cachefs_freeback = cp;
	if (cachefs_freefront == NULL)
		cachefs_freefront = cp;
	if (cp->c_freefront != NULL)
		cp->c_freefront->c_freeback = cp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_addfree: EXIT\n");
#endif
}

/*
 * Remove a cnode from the cnode free list.
 */
void
cachefs_remfree(struct cnode *cp)
{
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_remfree: ENTER cp %x\n", (int) cp);
#endif
	ASSERT(cp->c_backvp == NULL);
	ASSERT(MUTEX_HELD(&cachefs_cnode_freelist_lock));
	if (cp->c_freeback == NULL) {
		ASSERT(cachefs_freeback == cp);
		cachefs_freeback = cp->c_freefront;
		if (cp->c_freefront != NULL)
			cp->c_freefront->c_freeback = NULL;
	} else {
		cp->c_freeback->c_freefront = cp->c_freefront;
		if (cp->c_freefront != NULL)
			cp->c_freefront->c_freeback = cp->c_freeback;
	}
	if (cp->c_freefront == NULL) {
		ASSERT(cachefs_freefront == cp);
		cachefs_freefront = cp->c_freeback;
	}
	cp->c_freefront = cp->c_freeback = NULL;
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_remfree: EXIT\n");
#endif
}

/*
 * Search for the cnode on the hash queues hanging off the fscp struct.
 * On success, returns 0 and *cpp is set to the cnode.
 * If a cnode matches fileno but the cookie does not match then
 *	returns 1 and *cpp is set to the cnode.
 * Returns 0 and *cpp is set to NULL on a clean miss.
 */
int
cfind(ino_t fileno, struct fid *cookiep, struct fscache *fscp,
    struct cnode **cpp)
{
	struct cnode *head;
	int found = 0;
	int badcookie = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cfind: ENTER fileno %lu fscp %x\n", fileno,
		    (int) fscp);
#endif
	ASSERT(MUTEX_HELD(&fscp->fs_cnodelock));

	*cpp = NULL;
	if (cookiep == NULL) {
		goto out;
	}

	for (head = fscp->fs_cnode[CHASH(fileno)];
		head != NULL; head = head->c_hash) {
		if (fileno != head->c_fileno)
			continue;
		if (head->c_flags & CN_STALE)
			continue;

		if ((cookiep->fid_len == head->c_cookie.fid_len) &&
			(bcmp((caddr_t)cookiep->fid_data,
			(caddr_t)&head->c_cookie.fid_data,
			cookiep->fid_len)) == 0) {
			ASSERT(found == 0);
			found++;
			*cpp = head;
		} else {
#ifdef CFSDEBUG
			CFS_DEBUG(CFSDEBUG_GENERAL) {
				printf("cfind: dup fileno %lu, cp %x\n",
				    fileno, (int) head);
			}
#endif
			badcookie = 1;
			*cpp = head;
		}
	}
out:

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cfind: EXIT cp %x\n", (int) *cpp);
#endif
	return (badcookie);
}

/*
 * Inactivating the cnode. Make sure that the frontvp and backvp are synced out
 * before reclaiming the cnode.
 * This is only called from unmount.
 */
void
cachefs_inactivate(struct cnode *cp, cred_t *cr)
{
	struct fscache *fscp = C_TO_FSCACHE(cp);
	struct filegrp *fgp = cp->c_filegrp;
	cachefscache_t *cachep = fscp->fs_cache;
	struct cachefs_metadata *mdp = &cp->c_metadata;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_inactivate: ENTER cp %x\n", (int) cp);
#endif
	ASSERT((cp->c_flags & CN_INACTIVE) == 0);

	mutex_enter(&fgp->fg_gc_mutex);

	if (cp->c_flags & CN_UPDATED) {
		(void) cachefs_sync_metadata(cp, cr);
	}
	if ((cp->c_flags & CN_NOCACHE) && (cp->c_metadata.md_flags & MD_FILE)) {
		rw_enter(&cp->c_statelock, RW_WRITER);
		cachefs_inval_object(cp, cr);
		rw_exit(&cp->c_statelock);
	}
	if (cp->c_cred != NULL) {
		crfree(cp->c_cred);
		cp->c_cred = NULL;
	}
	if (mdp->md_lruno &&
	    ((mdp->md_flags & MD_PINNED) == 0) &&
	    ((cp->c_flags & CN_LRU) == 0)) {
		ASSERT((cachep->c_flags & CACHE_NOFILL) == 0);
		cachefs_active_remove(cachep, mdp->md_lruno);
		cachefs_lru_add(cachep, mdp->md_lruno);
	}
	if (cp->c_frontvp) {
		VN_RELE(cp->c_frontvp);
		cp->c_frontvp = NULL;
	}
	if (cp->c_backvp) {
		VN_RELE(cp->c_backvp);
		cp->c_backvp = NULL;
	}

	mutex_enter(&fscp->fs_cnodelock);
	cachefs_remhash(cp);
	mutex_exit(&fscp->fs_cnodelock);
	mutex_exit(&fgp->fg_gc_mutex);
	filegrp_rele(cp->c_filegrp);

	ASSERT(CTOV(cp)->v_pages == NULL);

	bzero((caddr_t)cp, sizeof (cnode_t));
	cachefs_kmem_free((caddr_t)cp, sizeof (cnode_t));
	(void) cachefs_cnode_cnt(-1);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_inactivate: EXIT\n");
#endif

}

int where = 0;
struct cachefs_metadata *metapoint = NULL;

/*
 * We have to initialize the cnode contents. Fill in the contents from the
 * cache (attrcache file), from the info passed in, whatever it takes.
 */
static int
cachefs_initcnode(ino_t fileno, struct cnode *cp, struct fscache *fscp,
	struct filegrp *fgp, struct fid *cookiep, vnode_t *backvp,
	int flag, cred_t *cr)
{
	int error = 0;
	vnode_t *vp = CTOV(cp);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("cachefs_initcnode:ENTER fileno %lu cp %x\n", fileno,
		    (int) cp);
#endif

	mutex_init(&CTOV(cp)->v_lock, "vnode state lock", MUTEX_DEFAULT, NULL);
	vp->v_op = &cachefs_vnodeops;
	vp->v_data = (caddr_t)cp;
	vp->v_vfsp = (struct vfs *)fscp->fs_cfsvfsp;
	cp->c_cred = NULL;  /* XXX: just get rid of cred stuff entirely! */
	cp->c_fileno = cp->c_attr.va_nodeid = fileno;
	if (backvp != NULL) {
		cp->c_attr.va_mask = AT_ALL;
		error = VOP_GETATTR(backvp, &cp->c_attr, 0, cr);
		if (error)
			goto out1;
		ASSERT(cp->c_fileno == cp->c_attr.va_nodeid);
		cp->c_backvp = backvp;
		VN_HOLD(backvp);
		cp->c_flags |= CN_UPDATED;
	}
	rw_init(&cp->c_rwlock, "cnode serialize lock", RW_DEFAULT, NULL);
	rw_init(&cp->c_statelock, "cnode state lock", RW_DEFAULT, NULL);
	rw_enter(&cp->c_statelock, RW_WRITER);
	mutex_init(&cp->c_iomutex, "IO Request Mutex", MUTEX_DEFAULT, NULL);
	cv_init(&cp->c_iocv, "Async IO CV", CV_DEFAULT, NULL);
	cp->c_flags |= flag;
	ASSERT(cp->c_freefront == NULL && cp->c_freeback == NULL);
	ASSERT(cachefs_freeback != cp && cachefs_freefront != cp);
	ASSERT(cp->c_hash == NULL);
	filegrp_hold(fgp);
	cp->c_filegrp = fgp;
	/*
	 * Finally, if NOCACHE is off, we get the metadata from the front file
	 * system and call the consistency routine to check consistency.
	 * If it is on, we call the consistency routine to initialize the
	 * token (and get attrs from the back FS).
	 */
	if ((cp->c_flags & CN_NOCACHE) == 0) {
		error = filegrp_read_metadata(cp->c_filegrp, fileno,
		    &cp->c_metadata);
		if (error == ENOENT) {
			if (cookiep == NULL) {
				/*
				 * if we're trying to create a local file,
				 * we don't know its cookie yet...
				 */
				error = 0;
				cp->c_flags |= CN_ALLOC_PENDING;
			} else {
				cp->c_cookie = *cookiep;
				cp->c_flags |= CN_UPDATED | CN_ALLOC_PENDING;
				if (cp->c_backvp == NULL) {
					error = cachefs_getbackvp(
					    fscp, cookiep, cp);
					if (error)
						goto out;
				}
				error = CFSOP_INIT_COBJECT(fscp, cp, cr);
				if (error)
					goto out;
			}
		} else if (error == 0) {
			cp->c_size = cp->c_attr.va_size;
			if (cookiep == NULL) {
				cookiep = &cp->c_metadata.md_cookie;
			} else if (bcmp((caddr_t)&cookiep->fid_data,
				(caddr_t)&cp->c_cookie.fid_data,
				cookiep->fid_len) != 0) {
				error = ESTALE;
				goto out;
			}
			error = CFSOP_CHECK_COBJECT(fscp, cp,
			    C_VERIFY_ATTRS, RW_WRITER, cr);
			if (error) {
				goto out;
			}
			ASSERT(cp->c_attr.va_type != 0);
		} else {
			goto out;
		}
	} else if (cookiep != NULL) {
		if (cp->c_backvp == NULL) {
			error = cachefs_getbackvp(fscp, cookiep, cp);
		}
		if (error == 0) {
			error = CFSOP_INIT_COBJECT(fscp, cp, cr);
			if (error)
				goto out;
			cp->c_cookie = *cookiep;
			cp->c_flags |= CN_UPDATED | CN_ALLOC_PENDING;
		}
		ASSERT(cp->c_attr.va_type != 0);
	}

	if (cookiep != NULL) {
		ASSERT(cp->c_attr.va_type != 0);
		vp->v_type = cp->c_attr.va_type;
		vp->v_rdev = cp->c_attr.va_rdev;
	}
	vp->v_count = 1;

	/*
	 * Check for a mismatch between what we think the fileno should
	 * be and what it is.
	 */
	if (cp->c_fileno != cp->c_attr.va_nodeid) {
#ifdef CFSDEBUG
		printf("cachefs_initcnode: fileno does not match %x %x\n",
			(int) cp->c_fileno, (int) cp->c_attr.va_nodeid);
#endif
		error = ESTALE;
	}

out:
	if (error) {
		if (cp->c_frontvp)
			VN_RELE(cp->c_frontvp);
		if (cp->c_backvp)
			VN_RELE(cp->c_backvp);
		rw_exit(&cp->c_statelock);
		filegrp_rele(fgp);
	} else {
		rw_exit(&cp->c_statelock);
	}
out1:

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("c_initcnode: EXIT error %d\n", error);
#endif
	return (error);
}

/*
 * makecachefsnode() called from lookup (and create). It finds a cnode if one
 * exists. If one doesn't exist on the hash queues, then it allocates one and
 * fills it in calling c_initcnode().
 */
int
makecachefsnode(ino_t fileno, struct fscache *fscp, struct fid *cookiep,
    vnode_t *backvp, cred_t *cr, int flag, struct cnode **cpp)
{
	struct cnode *cp;
	int error;
	struct fscache *freefscp;
	struct filegrp *fgp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("makecachefsnode: ENTER fileno %lu fscp %x\n",
			fileno, (int) fscp);
#endif

	/* get the file group that owns this fileno */
	mutex_enter(&fscp->fs_fslock);
	fgp = filegrp_list_find(fscp, fileno);
	if (fgp == NULL) {
		fgp = filegrp_create(fscp, fileno);
		filegrp_list_add(fscp, fgp);
	}
	filegrp_hold(fgp);
	mutex_exit(&fscp->fs_fslock);

	/* lock the file group against action by inactive or gc */
	mutex_enter(&fgp->fg_gc_mutex);

	if ((fgp->fg_flags & CFS_FG_READ) == 0)
		flag |= CN_NOCACHE;

again:
	error = 0;
	cp = NULL;

	mutex_enter(&cachefs_cnode_freelist_lock);
	mutex_enter(&fscp->fs_cnodelock);

	/* look for the cnode on the hash list */
	error = cfind(fileno, cookiep, fscp, &cp);

	/*
	 * If there already is a cnode with this fileno
	 * but a different cookie.
	 */
	if (error) {
		ASSERT(cp);

		/* if the filegrp cannot be modified then do nothing */
		if ((fgp->fg_flags & CFS_FG_WRITE) == 0) {
			mutex_exit(&fscp->fs_cnodelock);
			mutex_exit(&cachefs_cnode_freelist_lock);
			error = ESTALE;
			goto out;
		}

		/*
		 * If backvp is NULL then someone tried to use
		 * a stale cookie.
		 */
		if (backvp == NULL) {
			mutex_exit(&fscp->fs_cnodelock);
			mutex_exit(&cachefs_cnode_freelist_lock);
			error = ESTALE;
			goto out;
		}

		/*
		 * If here we have no choice but to assume that
		 * the found cnode is the stale one.  If we are
		 * wrong, it is still okay because if the cnode
		 * is active we nocache it.
		 */

		/* if the stale cnode is inactive */
		if (cp->c_flags & CN_INACTIVE) {
			cachefs_removefrontfile(&cp->c_metadata,
				cp->c_fileno, fgp);
			(void) filegrp_destroy_metadata(fgp, cp->c_fileno);
			if (cp->c_metadata.md_lruno &&
			    (cp->c_metadata.md_flags & MD_PINNED) == 0) {
				ASSERT(cp->c_flags & CN_LRU);
				cachefs_lru_remove(fscp->fs_cache,
				    cp->c_metadata.md_lruno);
				cachefs_lru_free(fscp->fs_cache,
				    cp->c_metadata.md_lruno);
			}

			/* nuke the cnode */
			cachefs_remfree(cp);
			cachefs_remhash(cp);
			VN_HOLD(CTOV(cp));
			(void) VOP_PUTPAGE(CTOV(cp), (offset_t)0, 0,
				B_INVAL, cr);
			if (CTOV(cp)->v_pages) {
			cmn_err(CE_WARN,
				"makecacehfsnode: can't toss pages vp %x",
				(int) CTOV(cp));
				(void) VOP_PUTPAGE(CTOV(cp), (offset_t)0, 0,
					B_INVAL | B_TRUNC, cr);
			}
			ASSERT(CTOV(cp)->v_pages == NULL);
			bzero((caddr_t)cp, sizeof (struct cnode));
			cachefs_kmem_free((caddr_t)cp, sizeof (struct cnode));
			(void) cachefs_cnode_cnt(-1);
		}

		/* else if it is active, mark it as stale */
		else {
			VN_HOLD(CTOV(cp));
			rw_enter(&cp->c_statelock, RW_WRITER);
			cp->c_flags |= CN_STALE;
			cachefs_removefrontfile(&cp->c_metadata,
				cp->c_fileno, fgp);
			(void) filegrp_destroy_metadata(fgp, cp->c_fileno);
			cachefs_nocache(cp);
			if (cp->c_frontvp) {
				VN_RELE(cp->c_frontvp);
				cp->c_frontvp = NULL;
			}
			if (cp->c_metadata.md_lruno) {
				ASSERT((cp->c_flags & CN_LRU) == 0);
				cachefs_active_remove(fscp->fs_cache,
				    cp->c_metadata.md_lruno);
				cachefs_lru_free(fscp->fs_cache,
				    cp->c_metadata.md_lruno);
				cp->c_metadata.md_lruno = 0;
			}
			rw_exit(&cp->c_statelock);
			VN_RELE(CTOV(cp));
		}

		cp = NULL;
		error = 0;
	}


	/* if the cnode does not exist */
	if (cp == NULL) {
		mutex_exit(&fscp->fs_cnodelock);
		/*
		 * Cnode not found on the hash queues and we have already
		 * allocated our allotment of cnodes so we re-use one from the
		 * free list(if there are any).
		 */
		if (cachefs_cnode_cnt(0) >= maxcnodes &&
			cachefs_freefront != NULL) {
			/*
			 * Grab the first free cnode, take it off of the cnode
			 * free and hash lists and get rid of its identity.
			 */
			cp = cachefs_freefront;
			freefscp = C_TO_FSCACHE(cp);
			mutex_enter(&freefscp->fs_cnodelock);
			cachefs_remfree(cp);
			cachefs_remhash(cp);
			/* XXX */ VN_HOLD(CTOV(cp));
			error = VOP_PUTPAGE(CTOV(cp), (offset_t)0, 0,
				B_INVAL | B_TRUNC, cr);
			ASSERT(CTOV(cp)->v_pages == NULL);
			if (cp->c_frontvp)
				VN_RELE(cp->c_frontvp);
			if (cp->c_backvp)
				VN_RELE(cp->c_backvp);
			bzero((caddr_t)cp, sizeof (struct cnode));
			mutex_exit(&freefscp->fs_cnodelock);
		}
		mutex_exit(&cachefs_cnode_freelist_lock);

		/* did not get one from the free list, create a new one */
		if (cp == NULL) {
			cp = (struct cnode *)cachefs_kmem_zalloc(
			    /*LINTED alignment okay*/
			    sizeof (struct cnode), KM_SLEEP);
			(void) cachefs_cnode_cnt(1);
		}
		error = cachefs_initcnode(fileno, cp, fscp, fgp,
				cookiep, backvp, flag, cr);
		if (error) {
			struct cachefs_metadata *mdp;

			cachefs_kmem_free((caddr_t)cp, sizeof (struct cnode));
			(void) cachefs_cnode_cnt(-1);
			cp = NULL;
			if (error != ESTALE)
				goto out;
			if ((fgp->fg_flags & CFS_FG_WRITE) == 0) {
				error = ESTALE;
				goto out;
			}
			mdp = (struct cachefs_metadata *)cachefs_kmem_alloc(
			    /*LINTED alignment okay*/
			    sizeof (struct cachefs_metadata), KM_SLEEP);
			error = filegrp_read_metadata(fgp, fileno, mdp);
			if (error) {
				cachefs_kmem_free((caddr_t)mdp,
				    sizeof (struct cachefs_metadata));
				error = ESTALE;
				goto out;
			}
			if (mdp->md_flags & MD_PINNED) {
				cachefs_kmem_free((caddr_t)mdp,
				    sizeof (struct cachefs_metadata));
				error = ESTALE;
				goto out;
			}
			cachefs_removefrontfile(mdp, fileno, fgp);
			(void) filegrp_destroy_metadata(fgp, fileno);
			if (mdp->md_lruno) {
				cachefs_lru_remove(fscp->fs_cache,
				    mdp->md_lruno);
				cachefs_lru_free(fscp->fs_cache, mdp->md_lruno);
			}
			cachefs_kmem_free((caddr_t)mdp,
			    sizeof (struct cachefs_metadata));
			goto again;
		}
		cp->c_size = cp->c_attr.va_size;

		fscache_hold(fscp);

		if (cp->c_metadata.md_lruno &&
		    (cp->c_metadata.md_flags & MD_PINNED) == 0) {
			if (fscp->fs_cache->c_flags & CACHE_NOFILL)
				cp->c_flags |= CN_LRU;
			else {
				cachefs_lru_remove(fscp->fs_cache,
				    cp->c_metadata.md_lruno);
				cachefs_active_add(fscp->fs_cache,
				    cp->c_metadata.md_lruno);
			}
		}

		mutex_enter(&fscp->fs_cnodelock);
		cachefs_addhash(cp);
		mutex_exit(&fscp->fs_cnodelock);
	}

	/* else if the cnode exists */
	else {
		VN_HOLD(CTOV(cp));
		mutex_exit(&fscp->fs_cnodelock);
		if (cp->c_flags & CN_INACTIVE) {
			cachefs_remfree(cp);
			cp->c_flags &= ~CN_INACTIVE;
			ASSERT((cp->c_flags & CN_UPDATED) == 0);
			mutex_exit(&cachefs_cnode_freelist_lock);

			/*
			 * if ALLOC_PENDING is set when it comes off
			 * the freelist, we assume that we were unable
			 * to update it when it went inactive so we
			 * will try again.  (This could routinely happen
			 * during periods when the cache is in
			 * NOCACHE|NOFILL modes -- as during COC boot)
			 */
			if (cp->c_flags & CN_ALLOC_PENDING) {
				cp->c_flags |= CN_UPDATED;
			} else if ((fgp->fg_flags & CFS_FG_ALLOC_ATTR) ||
			    (filegrp_fileno_to_slot(fgp, fileno) == 0)) {
				cp->c_flags |= CN_UPDATED | CN_ALLOC_PENDING;
				ASSERT(cp->c_metadata.md_lruno == 0);
			}

			if (cp->c_metadata.md_lruno &&
			    (fscp->fs_cache->c_flags & CACHE_NOFILL) == 0 &&
			    (cp->c_metadata.md_flags & MD_PINNED) == 0) {
				cachefs_lru_remove(fscp->fs_cache,
					cp->c_metadata.md_lruno);
				cachefs_active_add(fscp->fs_cache,
					cp->c_metadata.md_lruno);
				cp->c_flags &= ~CN_LRU;
			}
			filegrp_hold(fgp);
			cp->c_filegrp = fgp;

			fscache_hold(fscp);

		} else {
			mutex_exit(&cachefs_cnode_freelist_lock);
		}
	}
out:
	*cpp = ((error == 0) ? cp : NULL);
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CNODE)
		printf("makecachefsnode: EXIT cp %x\n", (int) *cpp);
#endif
	if (error == 0) {
		rw_enter(&cp->c_statelock, RW_WRITER);
		ASSERT(cp->c_filegrp != NULL);
		if ((CTOV(cp)->v_type == VDIR) &&
			(cp->c_flags & CN_NOCACHE) == 0 &&
			(cp->c_flags & CN_NEED_FRONT_SYNC)) {
			ASSERT(cp->c_metadata.md_flags & MD_POPULATED);
		}
		ASSERT((cp->c_flags & CN_INACTIVE) == 0);
		rw_exit(&cp->c_statelock);
	}

	mutex_exit(&fgp->fg_gc_mutex);
	filegrp_rele(fgp);

	return (error);
}

#define	TIMEMATCH(a, b)	((a)->tv_sec == (b)->tv_sec && \
	(a)->tv_nsec == (b)->tv_nsec)

static void
cnode_enable_caching(struct cnode *cp)
{
	struct vnode *iovp;
	struct filegrp *fgp;
	struct cachefs_metadata md;
	int error, cacheable;

	ASSERT(cp->c_frontvp == NULL);  /* XXX: changes with cfsboot? */
	iovp = NULL;
	if ((CTOV(cp)->v_type == VREG) &&
	    (cp->c_flags & CFASTSYMLNK) == 0 &&
	    (cp->c_metadata.md_flags & MD_PINNED) == 0) {
		iovp = cp->c_backvp;
	}
	if (iovp) {
		(void) VOP_PUTPAGE(iovp, (offset_t)0,
		    (u_int)0, B_INVAL, kcred);
	}
	cacheable = 1;
	rw_enter(&cp->c_statelock, RW_WRITER);
	fgp = cp->c_filegrp;
	if (fgp == NULL)
		ASSERT(cp->c_flags & CN_INACTIVE);
	if (cp->c_flags & CN_INACTIVE) {
		/*
		 * We don't try to update cnodes on the freelist.
		 * Just mark them as stale so they can be reused.
		 */
		cp->c_flags |= CN_STALE;
		rw_exit(&cp->c_statelock);
		return;
	}
	ASSERT(cp->c_flags & CN_NOCACHE);  /* XXX: changes with cfsboot? */
	error = filegrp_read_metadata(fgp, cp->c_fileno, &md);
	if (error == 0) {
		/*
		 * A rudimentary consistency check
		 * here.  If the cookie and mtime
		 * from the cnode match those from the
		 * cache metadata, we assume for now that
		 * the cached data is OK.
		 */
		if (bcmp((caddr_t)&md.md_cookie.fid_data,
		    (caddr_t)&cp->c_cookie.fid_data,
		    cp->c_cookie.fid_len) == 0 &&
		    TIMEMATCH(&cp->c_attr.va_mtime, &md.md_vattr.va_mtime)) {
			cp->c_metadata = md;
		} else {
			/*
			 * Here we're skeptical about the validity of
			 * the front file.
			 * We'll keep the attributes already present in
			 * the cnode, and bring along the parts of the
			 * metadata that we need to eventually nuke this
			 * bogus front file -- in inactive or getfrontfile,
			 * whichever comes first...
			 */
			cp->c_metadata.md_flags = md.md_flags;
			cp->c_metadata.md_lruno = md.md_lruno;
			cp->c_metadata.md_fid = md.md_fid;
			cp->c_metadata.md_frontblks = md.md_frontblks;
			cp->c_metadata.md_timestamp.tv_sec = 0;
			cp->c_metadata.md_timestamp.tv_nsec = 0;
			cp->c_metadata.md_allocents = md.md_allocents;
			bcopy((caddr_t)md.md_allocinfo,
			    (caddr_t) cp->c_metadata.md_allocinfo,
			    sizeof (md.md_allocinfo));
			cacheable = 0;
			cp->c_flags |= (CN_NOCACHE|CN_UPDATED);
#ifdef CFSDEBUG
			CFS_DEBUG(CFSDEBUG_GENERAL) {
				printf(
				    "fileno %d stays nocache due "
				    "to cookie and/or mtime mismatch\n",
				    (int) cp->c_fileno);
			}
#endif
		}
		if (cp->c_metadata.md_lruno)
			cp->c_flags |= CN_LRU;
	}
	if (cacheable)
		cp->c_flags &= ~CN_NOCACHE;
	rw_exit(&cp->c_statelock);
}

void
cachefs_enable_caching(struct fscache *fscp)
{
	register int i;
	register struct cnode *cp;

	/*
	 * set up file groups so we can read them.  Note that general
	 * users (makecfsnode) will *not* start using them (i.e., all
	 * newly created cnodes will be NOCACHE)
	 * until we "enable_caching_rw" below.
	 */
	mutex_enter(&fscp->fs_fslock);
	filegrp_list_enable_caching_ro(fscp);
	mutex_exit(&fscp->fs_fslock);

	mutex_enter(&cachefs_cnode_freelist_lock);
	mutex_enter(&fscp->fs_cnodelock);

	for (i = 0; i < CNODE_BUCKET_SIZE; i++) {
		for (cp = fscp->fs_cnode[i]; cp != NULL; cp = cp->c_hash) {
			cnode_enable_caching(cp);
		}
	}
	mutex_exit(&fscp->fs_cnodelock);
	mutex_exit(&cachefs_cnode_freelist_lock);

	/* enable general use of the filegrps */
	mutex_enter(&fscp->fs_fslock);
	filegrp_list_enable_caching_rw(fscp);
	mutex_exit(&fscp->fs_fslock);
}

/*
 * Adjusts the number of cnodes by the specified amount and
 * returns the new number of cnodes.
 */
int cachefs_cnode_count = 0;
kmutex_t cachefs_cnode_cnt_lock;

int
cachefs_cnode_cnt(int delta)
{
	/* grab lock */
	mutex_enter(&cachefs_cnode_cnt_lock);

	/* adjust count, check for error */
	cachefs_cnode_count += delta;
	ASSERT(cachefs_cnode_count >= 0);

	/* free lock */
	mutex_exit(&cachefs_cnode_cnt_lock);

	/* return new value of count */
	return (cachefs_cnode_count);
}
