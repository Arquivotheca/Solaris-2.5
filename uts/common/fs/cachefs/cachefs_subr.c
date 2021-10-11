/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_subr.c 1.118     95/01/10 SMI"

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
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/fbuf.h>
#include <sys/dnlc.h>
#include <sys/callb.h>
#include <sys/kobj.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <sys/fs/cachefs_fs.h>
#include <sys/fs/cachefs_log.h>


extern struct seg *segkmap;
caddr_t segmap_getmap();
int segmap_release();

extern int maxcnodes;
extern struct cnode *cachefs_freeback;
extern struct cnode *cachefs_freefront;
extern kmutex_t cachefs_cnode_freelist_lock;
extern cachefscache_t *cachefs_cachelist;

#ifdef CFSDEBUG
int cachefsdebug = 0;
#endif

int cachefs_max_threads = CFS_MAX_THREADS;
ino_t cachefs_check_fileno = 0;


/*
 * Cache routines
 */

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_create
 *
 * Description:
 *	Creates a cachefscache_t object and initializes it to
 *	be NOCACHE and NOFILL mode.
 * Arguments:
 * Returns:
 *	Returns a pointer to the created object or NULL if
 *	threads could not be created.
 * Preconditions:
 */

cachefscache_t *
cachefs_cache_create(void)
{
	cachefscache_t *cachep;
	int error;
	struct cachefs_req *rp;

	/* allocate zeroed memory for the object */
	cachep = (cachefscache_t *)cachefs_kmem_zalloc(sizeof (cachefscache_t),
		/*LINTED alignment okay*/
		KM_SLEEP);

	cv_init(&cachep->c_gccv, "gc thread cv", CV_DEFAULT, NULL);
	cv_init(&cachep->c_gchaltcv, "gc thread halt cv", CV_DEFAULT, NULL);
	mutex_init(&cachep->c_contentslock, "cache contents", MUTEX_DEFAULT,
		NULL);
	mutex_init(&cachep->c_fslistlock, "cache fslist", MUTEX_DEFAULT, NULL);
	mutex_init(&cachep->c_log_mutex, "cachefs logging mutex",
	    MUTEX_DEFAULT, NULL);

	/* set up the work queue and get the sync thread created */
	cachefs_workq_init(&cachep->c_workq);
	cachep->c_workq.wq_keepone = 1;
	cachep->c_workq.wq_cachep = cachep;
	rp = (struct cachefs_req *)cachefs_kmem_zalloc(
		    /*LINTED alignment okay*/
		    sizeof (struct cachefs_req), KM_SLEEP);
	mutex_init(&rp->cfs_req_lock, "CFS Request Mutex", MUTEX_DEFAULT, NULL);
	rp->cfs_cmd = CFS_NOOP;
	rp->cfs_cr = kcred;
	rp->cfs_req_u.cu_fs_sync.cf_cachep = cachep;
	crhold(rp->cfs_cr);
	error = cachefs_addqueue(rp, &cachep->c_workq);
	if (error) {
		goto out;
	}

	cachep->c_flags |= CACHE_NOCACHE | CACHE_NOFILL;
out:
	if (error) {
		cachefs_kmem_free((caddr_t)cachep, sizeof (cachefscache_t));
		cachep = NULL;
	}
	return (cachep);
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_destroy
 *
 * Description:
 *	Destroys the cachefscache_t object.
 * Arguments:
 *	cachep	the cachefscache_t object to destroy
 * Returns:
 * Preconditions:
 *	precond(cachep)
 */

void
cachefs_cache_destroy(cachefscache_t *cachep)
{
	int tend;

	/* stop async threads */
	while (cachep->c_workq.wq_thread_count > 0)
		(void) cachefs_async_halt(&cachep->c_workq, 1);

	/* kill off the garbage collection thread */
	mutex_enter(&cachep->c_contentslock);
	while (cachep->c_flags & CACHE_GARBAGE_THREADRUN) {
		cachep->c_flags |= CACHE_GARBAGE_THREADEXIT;
		cv_signal(&cachep->c_gccv);
		tend = lbolt + (60 * HZ);
		(void) cv_timedwait(&cachep->c_gchaltcv,
			&cachep->c_contentslock, tend);
	}
	mutex_exit(&cachep->c_contentslock);

	/* if there is a cache */
	if ((cachep->c_flags & CACHE_NOCACHE) == 0) {
		if ((cachep->c_flags & CACHE_NOFILL) == 0) {
			/* sync the cache */
			cachefs_cache_sync(cachep);
		} else {
			/* get rid of any unused fscache objects */
			mutex_enter(&cachep->c_fslistlock);
			fscache_list_gc(cachep);
			mutex_exit(&cachep->c_fslistlock);
		}
		ASSERT(cachep->c_fslist == NULL);

		VN_RELE(cachep->c_resfilevp);
		VN_RELE(cachep->c_dirvp);
		VN_RELE(cachep->c_lockvp);
	}

	if (cachep->c_log_ctl != NULL)
		cachefs_kmem_free((caddr_t) cachep->c_log_ctl,
		    sizeof (cachefs_log_control_t));
	if (cachep->c_log != NULL)
		cachefs_log_destroy_cookie(cachep->c_log);

	bzero((caddr_t)cachep, sizeof (cachefscache_t));
	cachefs_kmem_free((caddr_t)cachep, sizeof (cachefscache_t));
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_active_ro
 *
 * Description:
 *	Activates the cachefscache_t object for a read-only file system.
 * Arguments:
 *	cachep	the cachefscache_t object to activate
 *	cdvp	the vnode of the cache directory
 * Returns:
 *	Returns 0 for success, !0 if there is a problem with the cache.
 * Preconditions:
 *	precond(cachep)
 *	precond(cdvp)
 *	precond(cachep->c_flags & CACHE_NOCACHE)
 */

int
cachefs_cache_activate_ro(cachefscache_t *cachep, vnode_t *cdvp)
{
	cachefs_log_control_t *lc;
	vnode_t *labelvp = NULL;
	vnode_t *rifvp = NULL;
	vnode_t *lockvp = NULL;
	vnode_t *statevp = NULL;
	struct vattr *attrp = NULL;
	int lru_link_size;
	int error;

	ASSERT(cachep->c_flags & CACHE_NOCACHE);
	mutex_enter(&cachep->c_contentslock);

	attrp = (struct vattr *)cachefs_kmem_alloc(sizeof (struct vattr),
			/*LINTED alignment okay*/
			KM_SLEEP);

	/* get the mode bits of the cache directory */
	attrp->va_mask = AT_ALL;
	error = VOP_GETATTR(cdvp, attrp, 0, kcred);
	if (error)
		goto out;

	/* ensure the mode bits are 000 to keep out casual users */
	if (attrp->va_mode & S_IAMB) {
		cmn_err(CE_WARN, "cachefs: Cache Directory Mode must be 000\n");
		error = EPERM;
		goto out;
	}

	/* Get the lock file */
	error = VOP_LOOKUP(cdvp, CACHEFS_LOCK_FILE, &lockvp, NULL, 0, NULL,
		kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: activate_a: cache corruption"
			" run fsck.\n");
		goto out;
	}

	/* Get the label file */
	error = VOP_LOOKUP(cdvp, CACHELABEL_NAME, &labelvp, NULL, 0, NULL,
		kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: activate_b: cache corruption"
			" run fsck.\n");
		goto out;
	}

	/* read in the label */
	error = vn_rdwr(UIO_READ, labelvp, (caddr_t)&cachep->c_label,
			sizeof (struct cache_label), 0, UIO_SYSSPACE,
			0, 0, kcred, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: activate_c: cache corruption"
			" run fsck.\n");
		goto out;
	}

	/* Verify that we can handle the version this cache was created under */
	if (cachep->c_label.cl_cfsversion != CFSVERSION) {
		cmn_err(CE_WARN, "cachefs: Invalid Cache Version, run fsck\n");
		error = EINVAL;
		goto out;
	}

	/* Open the resource file */
	error = VOP_LOOKUP(cdvp, RESOURCE_NAME, &rifvp, NULL, 0, NULL, kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: activate_d: cache corruption"
			" run fsck.\n");
		goto out;
	}

	/*  Read the usage struct for this cache */
	error = vn_rdwr(UIO_READ, rifvp, (caddr_t)&cachep->c_usage,
			sizeof (struct cache_usage), 0, UIO_SYSSPACE, 0, 0,
			kcred, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: activate_e: cache corruption"
			" run fsck.\n");
		goto out;
	}

	if (cachep->c_usage.cu_flags & CUSAGE_ACTIVE) {
		cmn_err(CE_WARN, "cachefs: cache not clean.  Run fsck\n");
		/* ENOSPC is what UFS uses for clean flag check */
		error = ENOSPC;
		goto out;
	}

	/*  Read the lruinfo for this cache */
	error = vn_rdwr(UIO_READ, rifvp, (caddr_t)&cachep->c_lruinfo,
			sizeof (struct lru_info), sizeof (struct cache_usage),
			UIO_SYSSPACE, 0, 0, kcred, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: activate_f: cache corruption"
			" run fsck.\n");
		goto out;
	}

	/*
	 * resource file layout:
	 *
	 * 	Offset		Item
	 *	0		Cache Usage Structure
	 *			LRU Info Structure
	 *	MAXBSIZE	LRU Links
	 *	Next MAXBSIZE	LRU Idents
	 */
	lru_link_size =
		((cachep->c_label.cl_maxinodes * sizeof (struct lru_pointers) +
		(MAXBSIZE - 1)) & ~(MAXBSIZE - 1));
	cachep->c_lru_link_off = MAXBSIZE;
	cachep->c_lru_idents_off = cachep->c_lru_link_off + lru_link_size;

	VN_HOLD(rifvp);
	VN_HOLD(cdvp);
	VN_HOLD(lockvp);
	cachep->c_resfilevp = rifvp;
	cachep->c_dirvp = cdvp;
	cachep->c_lockvp = lockvp;

	/* allocate the `logging control' field */
	mutex_enter(&cachep->c_log_mutex);
	cachep->c_log_ctl = (struct cachefs_log_control *)
	    cachefs_kmem_zalloc(sizeof (cachefs_log_control_t), KM_SLEEP);
	lc = (cachefs_log_control_t *) cachep->c_log_ctl;

	/* if the LOG_STATUS_NAME file exists, read it in and set up logging */
	(void) VOP_LOOKUP(cachep->c_dirvp, LOG_STATUS_NAME, &statevp,
	    NULL, 0, NULL, kcred);
	if (statevp != NULL) {
		int vnrw_error;

		vnrw_error = vn_rdwr(UIO_READ, statevp, (caddr_t) lc,
		    sizeof (*lc), 0, UIO_SYSSPACE, 0, RLIM_INFINITY,
		    kcred, NULL);
		VN_RELE(statevp);

		if (vnrw_error == 0) {
			if ((cachep->c_log = cachefs_log_create_cookie(lc))
			    == NULL)
				cachefs_log_error(cachep, ENOMEM, 0);
			else if ((lc->lc_magic != CACHEFS_LOG_MAGIC) ||
			    (lc->lc_path[0] != '/') ||
			    (cachefs_log_logfile_open(cachep,
			    lc->lc_path) != 0))
				cachefs_log_error(cachep, EINVAL, 0);
		}
	}
	lc->lc_magic = CACHEFS_LOG_MAGIC;
	lc->lc_cachep = cachep;
	mutex_exit(&cachep->c_log_mutex);

out:
	if (error == 0) {
		cachep->c_flags &= ~CACHE_NOCACHE;
	}
	if (attrp)
		cachefs_kmem_free((caddr_t)attrp, sizeof (struct vattr));
	if (labelvp != NULL)
		VN_RELE(labelvp);
	if (rifvp != NULL)
		VN_RELE(rifvp);
	if (lockvp)
		VN_RELE(lockvp);

	mutex_exit(&cachep->c_contentslock);
	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_active_rw
 *
 * Description:
 *	Activates the cachefscache_t object for a read-write file system.
 * Arguments:
 *	cachep	the cachefscache_t object to activate
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond((cachep->c_flags & CACHE_NOCACHE) == 0)
 *	precond(cachep->c_flags & CACHE_NOFILL)
 */

void
cachefs_cache_activate_rw(cachefscache_t *cachep)
{
	ASSERT((cachep->c_flags & CACHE_NOCACHE) == 0);
	ASSERT(cachep->c_flags & CACHE_NOFILL);

	/* get the garbage collection thread created */
	cachep->c_flags |= CACHE_GARBAGE_THREADRUN;
	if (thread_create(NULL, NULL, cachefs_garbage_collect_thread,
	    (caddr_t)cachep, 0, &p0, TS_RUN, 60) == NULL) {
		cmn_err(CE_WARN,
			"cachefs: Can't start garbage collection thread.\n");
	}

	/* move the active list to the lru list */
	cachefs_move_active_to_lru(cachep);

	mutex_enter(&cachep->c_contentslock);
	cachep->c_flags &= ~CACHE_NOFILL;
	cachefs_cache_dirty(cachep, 0);
	mutex_exit(&cachep->c_contentslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_dirty
 *
 * Description:
 *	Marks the cache as dirty (active).
 * Arguments:
 *	cachep	the cachefscache_t to mark as dirty
 *	lockit	1 means grab contents lock, 0 means caller grabbed it
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond(cache is in rw mode)
 */

void
cachefs_cache_dirty(struct cachefscache *cachep, int lockit)
{
	int error;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE | CACHE_NOFILL)) == 0);

	if (lockit) {
		mutex_enter(&cachep->c_contentslock);
	} else {
		ASSERT(MUTEX_HELD(&cachep->c_contentslock));
	}
	if (cachep->c_flags & CACHE_DIRTY) {
		ASSERT(cachep->c_usage.cu_flags & CUSAGE_ACTIVE);
	} else {
		/*
		 * turn on the "cache active" (dirty) flag and write it
		 * synchronously to disk
		 */
		cachep->c_flags |= CACHE_DIRTY;
		cachep->c_usage.cu_flags |= CUSAGE_ACTIVE;
		if (error = vn_rdwr(UIO_WRITE, cachep->c_resfilevp,
		    (caddr_t)&cachep->c_usage, sizeof (struct cache_usage),
		    0, UIO_SYSSPACE, FSYNC, RLIM_INFINITY, kcred, NULL)) {
			cmn_err(CE_WARN,
			    "cachefs: clean flag write error: %d\n", error);
		}
	}

	if (lockit)
		mutex_exit(&cachep->c_contentslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_rssync
 *
 * Description:
 *	Syncs out the resource file for the cachefscache_t object.
 * Arguments:
 *	cachep	the cachefscache_t object to operate on
 * Returns:
 *	Returns 0 for success, !0 on an error writing data.
 * Preconditions:
 *	precond(cachep)
 *	precond(cache is in rw mode)
 */

int
cachefs_cache_rssync(struct cachefscache *cachep)
{
	int error;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE | CACHE_NOFILL)) == 0);

	if (cachep->c_lru_idents != NULL) {
		(void) segmap_release(segkmap,
			(caddr_t)cachep->c_lru_idents, 0);
		cachep->c_lru_idents = NULL;
	}
	if (cachep->c_lru_ptrs != NULL) {
		(void) segmap_release(segkmap, (caddr_t)cachep->c_lru_ptrs, 0);
		cachep->c_lru_ptrs = NULL;
	}

	/* write the usage struct for this cache */
	error = vn_rdwr(UIO_WRITE, cachep->c_resfilevp,
		(caddr_t)&cachep->c_usage, sizeof (struct cache_usage),
		0, UIO_SYSSPACE, 0, RLIM_INFINITY, kcred, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: Can't Write Cache Usage Info\n");
	}

	/* Read the lruinfo for this cache */
	error = vn_rdwr(UIO_WRITE, cachep->c_resfilevp,
			(caddr_t)&cachep->c_lruinfo, sizeof (struct lru_info),
			sizeof (struct cache_usage), UIO_SYSSPACE,
			0, RLIM_INFINITY, kcred, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: Can't Write Cache LRU Info\n");
	}
	error = VOP_FSYNC(cachep->c_resfilevp, FSYNC, kcred);
	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_sync
 *
 * Description:
 *	Sync a cache which includes all of its fscaches.
 * Arguments:
 *	cachep	the cachefscache_t object to sync
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond(cache is in rw mode)
 */

void
cachefs_cache_sync(struct cachefscache *cachep)
{
	struct fscache *fscp;
	struct fscache **syncfsc;
	int nfscs, fscidx;
	int try;
	int done;

	if (cachep->c_flags & (CACHE_NOCACHE | CACHE_NOFILL))
		return;

	done = 0;
	for (try = 0; (try < 2) && !done; try++) {

		nfscs = 0;

		/*
		 * here we turn off the cache-wide DIRTY flag.  If it's still
		 * off when the sync completes we can write the clean flag to
		 * disk telling fsck it has no work to do.
		 */
#ifdef CFSCLEANFLAG
		mutex_enter(&cachep->c_contentslock);
		cachep->c_flags &= ~CACHE_DIRTY;
		mutex_exit(&cachep->c_contentslock);
#endif /* CFSCLEANFLAG */

		cachefs_log_process_queue(cachep, 1);

		mutex_enter(&cachep->c_fslistlock);
		syncfsc = (struct fscache **) cachefs_kmem_alloc(
				(cachep->c_refcnt*sizeof (struct fscache *)),
				/*LINTED alignment okay*/
				KM_SLEEP);
		for (fscp = cachep->c_fslist; fscp; fscp = fscp->fs_next) {
			fscache_hold(fscp);
			ASSERT(nfscs < cachep->c_refcnt);
			syncfsc[nfscs++] = fscp;
		}
		ASSERT(nfscs == cachep->c_refcnt);
		mutex_exit(&cachep->c_fslistlock);
		for (fscidx = 0; fscidx < nfscs; fscidx++) {
			fscp = syncfsc[fscidx];
			fscache_sync(fscp, 0);
			fscache_rele(fscp);
		}

		/* get rid of any unused fscache objects */
		mutex_enter(&cachep->c_fslistlock);
		fscache_list_gc(cachep);
		mutex_exit(&cachep->c_fslistlock);

		/*
		 * here we check the cache-wide DIRTY flag.
		 * If it's off,
		 * we can write the clean flag to disk.
		 */
#ifdef CFSCLEANFLAG
		mutex_enter(&cachep->c_contentslock);
		if ((cachep->c_flags & CACHE_DIRTY) == 0) {
			if (cachep->c_usage.cu_flags & CUSAGE_ACTIVE) {
				cachep->c_usage.cu_flags &= ~CUSAGE_ACTIVE;
				if (cachefs_cache_rssync(cachep) == 0) {
					done = 1;
				} else {
					cachep->c_usage.cu_flags |=
						CUSAGE_ACTIVE;
				}
			} else {
				done = 1;
			}
		}
		mutex_exit(&cachep->c_contentslock);
#else /* CFSCLEANFLAG */
		mutex_enter(&cachep->c_contentslock);
		(void) cachefs_cache_rssync(cachep);
		mutex_exit(&cachep->c_contentslock);
		done = 1;
#endif /* CFSCLEANFLAG */
		cachefs_kmem_free((caddr_t)syncfsc,
			(nfscs*sizeof (struct fscache *)));
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		cachefs_cache_unique
 *
 * Description:
 * Arguments:
 * Returns:
 *	Returns a unique number.
 * Preconditions:
 *	precond(cachep)
 */

u_int
cachefs_cache_unique(cachefscache_t *cachep)
{
	u_int unique = 0;
	int error = 0;

	mutex_enter(&cachep->c_contentslock);
	if (cachep->c_usage.cu_flags & CUSAGE_NEED_ADJUST ||
		++(cachep->c_unique) == 0) {
		cachep->c_usage.cu_unique++;

		if (cachep->c_unique == 0)
			cachep->c_unique = 1;
		cachep->c_flags &= ~CUSAGE_NEED_ADJUST;
		(void) cachefs_cache_rssync(cachep);
		/* write the usage struct for this cache */

		/* XXX is this necessary? */
		error = vn_rdwr(UIO_WRITE, cachep->c_resfilevp,
			(caddr_t)&cachep->c_usage, sizeof (struct cache_usage),
			0, UIO_SYSSPACE, 0, RLIM_INFINITY, kcred, NULL);
		if (error) {
			cmn_err(CE_WARN, "Can't Write Cache Usage Info\n");
		}
	}
	if (error == 0)
		unique = (cachep->c_usage.cu_unique << 16) + cachep->c_unique;
	mutex_exit(&cachep->c_contentslock);
	return (unique);
}

/*
 * Called from c_getfrontfile. Shouldn't be called from anywhere else !
 */
static int
cachefs_createfrontfile(cnode_t *cp, struct filegrp *fgp,
	struct vattr *vap, cred_t *cr)
{
	char name[CFS_FRONTFILE_NAME_SIZE];
	struct vattr *attrp = NULL;
	int error = 0;
	int mode;
	int alloc = 0;
	int freefile = 0;
	int ffrele = 0;
	int lrufree = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_createfrontfile: ENTER cp %x fgp %x vap %x\n",
			(int) cp, (int) fgp, (int) vap);
#endif

	ASSERT(cp->c_frontvp == NULL);

	/* quit if we cannot write to the filegrp */
	if ((fgp->fg_flags & CFS_FG_WRITE) == 0) {
		error = ENOENT;
		goto out;
	}

	/* find or create the filegrp attrcache file if necessary */
	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR) {
		error = filegrp_allocattr(fgp);
		if (error)
			goto out;
	}

	make_ascii_name(cp->c_fileno, name);

	/* set up attributes for the front file we want to create */
	if (vap != NULL) {
		attrp = vap;
		if (vap->va_mask & AT_MODE)
			mode = (int)vap->va_mode;
		else
			mode = 0666;
		attrp->va_mask |= AT_MODE;
	} else {
		alloc++;
		attrp = (struct vattr *)cachefs_kmem_zalloc(
			/*LINTED alignment okay*/
			sizeof (struct vattr), KM_SLEEP);
		attrp->va_mode = S_IFREG | 0666;
		mode = 0666;
		attrp->va_uid = 0;
		attrp->va_gid = 0;
		attrp->va_type = VREG;
		attrp->va_size = 0;
		attrp->va_mask = AT_SIZE | AT_TYPE | AT_MODE | AT_UID | AT_GID;
	}

	/* get a file from the resource counts */
	error = cachefs_allocfile(fgp->fg_fscp->fs_cache);
	if (error) {
		error = EINVAL;
		goto out;
	}
	freefile++;

	/* create the metadata slot if necessary */
	if (cp->c_flags & CN_ALLOC_PENDING) {
		error = filegrp_create_metadata(fgp, &cp->c_metadata,
		    cp->c_fileno);
		if (error) {
			error = EINVAL;
			goto out;
		}
		cp->c_flags &= ~CN_ALLOC_PENDING;
		cp->c_flags |= CN_UPDATED;
	}

	/* incriment number of front files */
	error = filegrp_ffhold(fgp);
	if (error) {
		error = EINVAL;
		goto out;
	}
	ffrele++;

	/* create the front file */
	error = VOP_CREATE(fgp->fg_dirvp, name, attrp, EXCL, mode,
		&cp->c_frontvp, cr);
	if (error) {
#ifdef CFSDEBUG
		printf("c_createfrontfile: Can't create cached object"
		    " error %u, fileno %lx\n", error, cp->c_fileno);
#endif
		goto out;
	}

	/* get a copy of the fid of the front file */
	cp->c_metadata.md_fid.fid_len = MAXFIDSZ;
	error = VOP_FID(cp->c_frontvp, &cp->c_metadata.md_fid);
	if (error) {
		/*
		 * If we get back ENOSPC then the fid we passed in was too
		 * small.  For now we don't do anything and map to EINVAL.
		 */
		if (error == ENOSPC) {
			error = EINVAL;
		}
		goto out;
	}

	dnlc_purge_vp(cp->c_frontvp);

	/* get an lru entry */
	error = cachefs_lru_alloc(fgp->fg_fscp->fs_cache,
	    fgp->fg_fscp, &cp->c_metadata.md_lruno,
	    cp->c_attr.va_nodeid);
	if (error)
		goto out;
	cachefs_active_add(fgp->fg_fscp->fs_cache,
		cp->c_metadata.md_lruno);
	lrufree++;

	cp->c_metadata.md_flags |= MD_FILE;
	cp->c_flags |= CN_UPDATED | CN_NEED_FRONT_SYNC;

out:
	if (error) {
		if (cp->c_frontvp) {
			VN_RELE(cp->c_frontvp);
			VOP_REMOVE(fgp->fg_dirvp, name, cr);
			cp->c_frontvp = NULL;
		}
		if (ffrele)
			filegrp_ffrele(fgp);
		if (freefile)
			cachefs_freefile(fgp->fg_fscp->fs_cache);
		if (lrufree) {
			cachefs_active_remove(fgp->fg_fscp->fs_cache,
				cp->c_metadata.md_lruno);
			cachefs_lru_free(fgp->fg_fscp->fs_cache,
				cp->c_metadata.md_lruno);
			cp->c_metadata.md_lruno = 0;
		}
		cachefs_nocache(cp);
	}
	if (alloc)
		cachefs_kmem_free((caddr_t)attrp, sizeof (struct vattr));
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_createfrontfile: EXIT error = %d name %s\n", error,
			name);
#endif
	return (error);
}

/*
 * Removes the front file for the cnode if it exists.
 */
void
cachefs_removefrontfile(struct cachefs_metadata *mdp, ino_t fileno,
    filegrp_t *fgp)
{
	int error;
	char name[CFS_FRONTFILE_NAME_SIZE];

	if (mdp->md_flags & MD_FILE) {
		if (fgp->fg_dirvp == NULL) {
			cmn_err(CE_WARN, "cachefs: remove error, run fsck\n");
			return;
		}
		make_ascii_name(fileno, name);
		error = VOP_REMOVE(fgp->fg_dirvp, name, kcred);
		if ((error) && (error != ENOENT)) {
			cmn_err(CE_WARN, "UFS remove error %s %d, run fsck\n",
			    name, error);
		}
		mdp->md_flags &= ~MD_FILE;
		mdp->md_flags &= ~MD_POPULATED;
		bzero((caddr_t)&mdp->md_allocinfo, mdp->md_allocents *
			sizeof (struct cachefs_allocmap));
		cachefs_freefile(fgp->fg_fscp->fs_cache);
		filegrp_ffrele(fgp);
	}
	if (mdp->md_frontblks) {
		cachefs_freeblocks(fgp->fg_fscp->fs_cache, mdp->md_frontblks);
		mdp->md_frontblks = 0;
	}
}

/*
 * This is the interface to the rest of CFS. This takes a cnode, and returns
 * the frontvp (stuffs it in the cnode). This creates an attrcache slot and
 * and frontfile if necessary.
 */

int
cachefs_getfrontfile(cnode_t *cp, struct vattr *vap, cred_t *cr)
{
	struct filegrp *fgp = cp->c_filegrp;
	int error;
	struct vattr va;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_getfrontfile: ENTER cp %x vap %x\n",
		    (int) cp, (int) vap);
#endif
	ASSERT(RW_WRITE_HELD(&cp->c_statelock));

	/*
	 * Now we check to see if there is a front file for this entry.
	 * If there is, we get the vnode for it and stick it in the cnode.
	 * Otherwise, we create a front file, get the vnode for it and stick
	 * it in the cnode.
	 */
	if (vap == NULL)
		vap = &va;
	if ((cp->c_metadata.md_flags & MD_FILE) == 0) {
#ifdef CFSDEBUG
		if (cp->c_frontvp != NULL)
			printf(
		"c_getfrontfile: !MD_FILE and frontvp not null cp %x\n",
			    (int) cp);
#endif
		ASSERT((cp->c_flags & CN_STALE) == 0);
		if (CTOV(cp)->v_type == VDIR)
			ASSERT((cp->c_metadata.md_flags & MD_POPULATED) == 0);
		error = cachefs_createfrontfile(cp, fgp,
		    (struct vattr *)NULL, cr);
		if (error)
			goto out;
		if (CTOV(cp)->v_type == VDIR)
			ASSERT((cp->c_metadata.md_flags & MD_POPULATED) == 0);
#ifdef CFSDEBUG
		if (cp->c_frontvp->v_count > 1) {
			printf("c_getfrontfile: front vcount %lu\n",
					cp->c_frontvp->v_count);
		}
#endif
	} else {
		/*
		 * A front file exists, all we need to do is to grab the fid,
		 * do a VFS_VGET() on the fid, stuff the vnode in the cnode,
		 * and return.
		 */
		if (fgp->fg_dirvp == NULL) {
			cmn_err(CE_WARN, "cachefs: gff0: corrupted file system"
				" run fsck\n");
			cp->c_metadata.md_flags &= ~MD_POPULATED;
			cp->c_flags &= ~CN_NEED_FRONT_SYNC;
			cp->c_flags |= CN_UPDATED | CN_NOCACHE;
			error = ESTALE;
			goto out;
		}
		error = VFS_VGET(fgp->fg_dirvp->v_vfsp, &cp->c_frontvp,
				&cp->c_metadata.md_fid);
		if (error || (cp->c_frontvp == NULL)) {
#ifdef CFSDEBUG
			printf("cachefs: gff1: front file system error %d",
			    error);
#endif /* CFSDEBUG */
			cp->c_metadata.md_flags &= ~MD_POPULATED;
			cp->c_flags &= ~CN_NEED_FRONT_SYNC;
			cp->c_flags |= CN_UPDATED | CN_NOCACHE;
			error = ESTALE;
			goto out;
		}

		if ((cp->c_flags & CN_NEED_FRONT_SYNC) == 0) {
			if (CTOV(cp)->v_type == VDIR &&
			    ((cp->c_metadata.md_flags & MD_POPULATED) == 0))
				goto out;
			/* Verify that were are sane */
			vap->va_mask = AT_MTIME;
			error = VOP_GETATTR(cp->c_frontvp, vap, 0, cr);
			if (error) {
				cmn_err(CE_WARN, "cachefs: gff2: front file"
				" system error %d", error);
				cachefs_inval_object(cp, cr);
				error = ESTALE;
				goto out;
			}
			if (bcmp((caddr_t)&vap->va_mtime,
				(caddr_t)(&cp->c_metadata.md_timestamp),
				sizeof (timestruc_t)) != 0) {
#ifdef CFSDEBUG
				CFS_DEBUG(CFSDEBUG_GENERAL) {
					int sec, nsec;

					sec =
					    cp->c_metadata.md_timestamp.tv_sec;
					nsec =
					    cp->c_metadata.md_timestamp.tv_nsec;
					printf(
					    "c_getfrontfile: timestamps don't"
					    " match fileno %d va %lx %x"
					    " meta %x %x\n",
					    (int) cp->c_fileno,
					    vap->va_mtime.tv_sec,
					    (int) vap->va_mtime.tv_nsec,
					    sec, nsec);
				}
#endif
				cachefs_inval_object(cp, cr);
				error = ESTALE;
			}
		}
	}
out:

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_getfrontfile: EXIT error = %d\n", error);
#endif
	return (error);
}

void
cachefs_inval_object(cnode_t *cp, cred_t *cr)
{
	struct vattr va;
	cachefscache_t *cachep = C_TO_FSCACHE(cp)->fs_cache;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_inval_object: ENTER cp %x\n", (int) cp);
#endif
	if (C_TO_FSCACHE(cp)->fs_cache->c_flags &
		(CACHE_NOFILL | CACHE_NOCACHE)) {
		cp->c_flags |= CN_NEED_INVAL;
		goto out;
	}
	if (cp->c_metadata.md_flags & MD_FILE) {
		if ((cp->c_frontvp == NULL) &&
		    (cachefs_getfrontfile(cp, (struct vattr *)NULL, cr) != 0))
			    goto out;

		/*
		 * De-allocate any space used
		 */
		if (cp->c_metadata.md_frontblks) {
			cachefs_freeblocks(cachep, cp->c_metadata.md_frontblks);
			cp->c_metadata.md_frontblks = 0;
			cp->c_flags |= CN_UPDATED;
		}
		/*
		 * Remove the cache front file's contents.
		 */
		va.va_mask = AT_SIZE;
		va.va_size = 0;
		if (VOP_SETATTR(cp->c_frontvp, &va, 0, cr) != 0)
			goto out;
		bzero((caddr_t)&cp->c_metadata.md_allocinfo,
			cp->c_metadata.md_allocents *
			sizeof (struct cachefs_allocmap));
		cp->c_metadata.md_frontblks = 0;
		va.va_mask = AT_MTIME;
		if (VOP_GETATTR(cp->c_frontvp, &va, 0, cr) != 0)
			goto out;
		cp->c_metadata.md_timestamp = va.va_mtime;

		/* if a directory, v_type is zero if called from initcnode */
		if (cp->c_attr.va_type == VDIR) {
			if (cp->c_usage < CFS_DIRCACHE_COST) {
				cp->c_invals++;
				if (cp->c_invals > CFS_DIRCACHE_INVAL) {
					cp->c_invals = 0;
				}
			} else
				cp->c_invals = 0;
			cp->c_usage = 0;
		}
	}
out:
	cp->c_metadata.md_flags &= ~MD_POPULATED;
	cp->c_flags &= ~CN_NEED_FRONT_SYNC;
	cp->c_flags |= CN_UPDATED | CN_NOCACHE;

	/*
	 * If the object invalidated is a directory, the dnlc should be purged
	 * to elide all references to this (directory) vnode.
	 */
	if (CTOV(cp)->v_type == VDIR)
		dnlc_purge_vp(CTOV(cp));

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_inval_object: EXIT\n");
#endif
}


/*
 * Yetanudder good reason why a cache can't be transported between a
 * SPARC and a i486 and used interchangeably :-)
 */
void
make_ascii_name(int name, char *strp)
{
	int i = CFS_FRONTFILE_NAME_SIZE - 1;
	u_int index;

	ASSERT(i == 8);		/* :-) */
	do {
		index = (((u_int)name) & 0xf0000000) >> 28;
		index &= 0xf;
		*strp++ = "0123456789abcdef"[index];
		name <<= 4;
	} while (--i);
	*strp = '\0';
}

void
cachefs_nocache(cnode_t *cp)
{
	fscache_t *fscp = C_TO_FSCACHE(cp);
	cachefscache_t *cachep = fscp->fs_cache;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_nocache: ENTER cp %x\n", (int) cp);
#endif

	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	if (!C_ISFS_WRITE_AROUND(fscp)) {
		cachefs_inval_object(cp, kcred);
	} else {
		/*
		 * Here we are waiting until inactive time to do
		 * the inval_object.  In case we don't get to inactive
		 * (because of a crash, say) we set up a timestamp mismatch
		 * such that getfrontfile will blow the front file away
		 * next time we try to use it.
		 */
		cp->c_metadata.md_timestamp.tv_sec = 0;
		cp->c_metadata.md_timestamp.tv_nsec = 0;
	}
	cp->c_flags |= (CN_NOCACHE | CN_UPDATED);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_NOCACHE))
		cachefs_log_nocache(cachep, 0, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_SUBR)
		printf("c_nocache: EXIT cp %x\n", (int) cp);
#endif
}

/*
 * Checks to see if the page is in the disk cache, by checking the allocmap.
 */
int
cachefs_check_allocmap(cnode_t *cp, u_int off)
{
	int i;
	int size_to_look = MIN(PAGESIZE, cp->c_attr.va_size - off);

	for (i = 0; i < cp->c_metadata.md_allocents; i++) {
		struct cachefs_allocmap *allocp =
				cp->c_metadata.md_allocinfo + i;

		if (off >= allocp->am_start_off) {
			if ((off + size_to_look) <=
			    (allocp->am_start_off + allocp->am_size)) {
				struct fscache *fscp = C_TO_FSCACHE(cp);
				cachefscache_t *cachep = fscp->fs_cache;

				if (CACHEFS_LOG_LOGGING(cachep,
				    CACHEFS_LOG_CALLOC))
					cachefs_log_calloc(cachep, 0,
					    fscp->fs_cfsvfsp,
					    &cp->c_metadata.md_cookie,
					    cp->c_fileno,
					    off, size_to_look);
			/*
			 * Found the page in the CFS disk cache.
			 */
				return (1);
			}
		} else {
			return (0);
		}
	}
	return (0);
}

/*
 * Updates the allocmap to reflect a new chunk of data that has been
 * populated.
 */
void
cachefs_update_allocmap(cnode_t *cp, u_int off, u_int size)
{
	int i;
	struct cachefs_allocmap *allocp;
	struct fscache *fscp =  C_TO_FSCACHE(cp);
	cachefscache_t *cachep = fscp->fs_cache;
	u_int saveoff, savesize;
	u_int logoff = off;
	u_int logsize = size;
	u_int endoff;

	/*
	 * We try to see if we can coalesce the current block into an existing
	 * allocation and mark it as such.
	 * If we can't do that then we make a new entry in the allocmap.
	 * when we run out of allocmaps, put the cnode in NOCACHE mode.
	 */
again:
	allocp = cp->c_metadata.md_allocinfo;
	for (i = 0; i < cp->c_metadata.md_allocents; i++, allocp++) {

		if (off <= (allocp->am_start_off)) {
			if ((off + size) >= allocp->am_start_off) {
				endoff = MAX((allocp->am_start_off +
					allocp->am_size), (off + size));
				allocp->am_size = endoff - off;
				allocp->am_start_off = off;
				if (allocp->am_size >= cp->c_size)
					cp->c_metadata.md_flags |= MD_POPULATED;
				return;
			} else {
				saveoff = off;
				savesize = size;
				off = allocp->am_start_off;
				size = allocp->am_size;
				allocp->am_size = savesize;
				allocp->am_start_off = saveoff;
				goto again;
			}
		} else {
			if (off < (allocp->am_start_off + allocp->am_size)) {
				endoff = MAX((allocp->am_start_off +
					allocp->am_size), (off + size));
				allocp->am_size = endoff - allocp->am_start_off;
				if (allocp->am_size >= cp->c_size)
					cp->c_metadata.md_flags |= MD_POPULATED;
				return;
			}
			if (off == (allocp->am_start_off + allocp->am_size)) {
				allocp->am_size += size;
				if (allocp->am_size >= cp->c_size)
					cp->c_metadata.md_flags |= MD_POPULATED;
				return;
			}
		}
	}
	if (i == C_MAX_ALLOCINFO_SLOTS) {
#ifdef CFSDEBUG
printf("c_update_alloc_map: Too many allinfo entries cp %x fileno %lu %x\n",
	(int) cp, cp->c_fileno, (int) cp->c_metadata.md_allocinfo);
#endif
		cachefs_nocache(cp);
		return;
	}
	allocp->am_start_off = off;
	allocp->am_size = size;
	if (allocp->am_size >= cp->c_size)
		cp->c_metadata.md_flags |= MD_POPULATED;
	cp->c_metadata.md_allocents++;

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_UALLOC))
		cachefs_log_ualloc(cachep, 0, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno, logoff, logsize);
}

/*
 * CFS population function -
 * This function used to be called cachefs_populate, but this name is more apt.
 */
int
cachefs_populate(cnode_t *cp, u_int off, int popsize, cred_t *cr)
{
	int error;
	caddr_t addr;
	u_int upto;
	u_int size, from = off;
	cachefscache_t *cachep = C_TO_FSCACHE(cp)->fs_cache;
	int resid;
	struct fbuf *fbp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_populate: ENTER cp %x off %d\n",
		    (int) cp, off);
#endif

	ASSERT(cp->c_backvp != NULL);
	upto = MIN((off + popsize), cp->c_attr.va_size);

	while (from < upto) {
		u_int blkoff = (from & MAXBMASK);
		u_int n = from - blkoff;

		size = upto - from;
		if ((from + size) > (blkoff + MAXBSIZE))
			size = MAXBSIZE - n;
		error = fbread(cp->c_backvp, blkoff, size, S_OTHER, &fbp);
		if (error) {
#ifdef CFSDEBUG
			printf("cachefs_populate: fbread error %d\n", error);
#endif
			cachefs_nocache(cp);
			goto out;
		}
		addr = fbp->fb_addr;
		ASSERT(addr != NULL);
		if (n == 0 || cachefs_check_allocmap(cp, blkoff) == 0) {
			error = cachefs_allocblocks(cachep, 1, cr);
			if (error) {
				fbrelse(fbp, S_OTHER);
				cachefs_nocache(cp);
				goto out;
			}
			cp->c_metadata.md_frontblks++;
		}
		resid = 0;
		error = vn_rdwr(UIO_WRITE, cp->c_frontvp, addr + n, size,
				from, UIO_SYSSPACE, 0,
				RLIM_INFINITY, cr, &resid);
		fbrelse(fbp, S_OTHER);
		if (error) {
			cachefs_nocache(cp);
#ifdef CFSDEBUG
			printf("cachefs_populate:Got error = %d from vn_rdwr\n",
				error);
#endif
			goto out;
		}
#ifdef CFSDEBUG
		if (resid)
			printf("cachefs_populate: non-zero resid %d\n", resid);
#endif
		from += size;
	}
	(void) cachefs_update_allocmap(cp, off, upto - off);
	cp->c_flags |= CN_UPDATED | CN_NEED_FRONT_SYNC | CN_POPULATION_PENDING;
out:
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_POPULATE))
		cachefs_log_populate(cachep, error,
		    C_TO_FSCACHE(cp)->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno, off, popsize);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VOPS)
		printf("cachefs_populate: EXIT cp %x error %d\n",
		    (int) cp, error);
#endif
	return (0);
}

void
cachefs_cluster_allocmap(struct cnode *cp, int off, int *popoffp,
    int *popsizep, int size)
{
	int i;
	int lastoff = 0;
	int forward_diff = 0;
	int backward_diff = 0;

	ASSERT(size <= C_TO_FSCACHE(cp)->fs_options.opt_popsize);

	for (i = 0; i < cp->c_metadata.md_allocents; i++) {
		struct cachefs_allocmap *allocp =
			cp->c_metadata.md_allocinfo + i;

		if (allocp->am_start_off > off) {
			if ((off + size) > allocp->am_start_off) {
				forward_diff = allocp->am_start_off - off;
				backward_diff = size - forward_diff;
				if (lastoff > (off - backward_diff))
					backward_diff = off - lastoff;
			} else {
				forward_diff = size;
			}
			*popoffp = (off - backward_diff) & PAGEMASK;
			*popsizep = ((off + forward_diff) - *popoffp) &
					PAGEMASK;
			return;
		} else {
			lastoff = allocp->am_start_off + allocp->am_size;
		}
	}
	if ((lastoff + size) > off) {
		*popoffp = (lastoff & PAGEMASK);
	} else {
		*popoffp = off & PAGEMASK;
	}
	if ((*popoffp + size) > cp->c_size)
		size = (cp->c_size - *popoffp + PAGEOFFSET) & PAGEMASK;
	*popsizep = size;
}

/*
 * "populate" a symlink in the cache
 */
int
cachefs_cachesymlink(struct cnode *cp, cred_t *cr)
{
	int error;
	struct uio uio;
	struct iovec iov;
	caddr_t buf;
	int buflen;
	struct fscache *fscp = C_TO_FSCACHE(cp);
	cachefscache_t *cachep = fscp->fs_cache;

	ASSERT(cp->c_backvp != NULL);
	ASSERT(RW_WRITE_HELD(&cp->c_rwlock));
	ASSERT(RW_WRITE_HELD(&cp->c_statelock));

	bzero((caddr_t)&uio, sizeof (struct uio));
	bzero((caddr_t)&iov, sizeof (struct iovec));
	buf = (char *)cachefs_kmem_alloc(MAXPATHLEN, KM_SLEEP);
	iov.iov_base = buf;
	iov.iov_len = MAXPATHLEN;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = MAXPATHLEN;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_offset = 0;
	uio.uio_fmode = 0;
	uio.uio_limit = RLIM_INFINITY;
	error = VOP_READLINK(cp->c_backvp, &uio, cr);
	if (error == 0) {
		buflen = MAXPATHLEN - uio.uio_resid;
		cp->c_size = buflen;
		if (buflen <= C_FSL_SIZE) {
			bzero((caddr_t)cp->c_metadata.md_allocinfo, C_FSL_SIZE);
			bcopy(buf, (caddr_t)
			    cp->c_metadata.md_allocinfo, buflen);
			cp->c_metadata.md_flags |= MD_FASTSYMLNK;
			if (cp->c_metadata.md_flags & MD_FILE)
				cachefs_removefrontfile(&cp->c_metadata,
				    cp->c_fileno, cp->c_filegrp);
		} else {
			if ((cp->c_metadata.md_flags & MD_FILE) == 0)
				error = cachefs_getfrontfile(cp,
				    (struct vattr *)NULL, cr);
			if (error == 0) {
				error = vn_rdwr(UIO_WRITE, cp->c_frontvp, buf,
				    buflen, 0, UIO_SYSSPACE, 0,
				    RLIM_INFINITY, cr, NULL);
				if (error == 0) {
					cp->c_metadata.md_flags |= MD_POPULATED;
					cp->c_flags |= CN_NEED_FRONT_SYNC;
				}
			}
		}
	}
	cachefs_kmem_free(buf, MAXPATHLEN);
	if (error == 0)
		cp->c_flags |= CN_UPDATED;

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_CSYMLINK))
		cachefs_log_csymlink(cachep, error, fscp->fs_cfsvfsp,
		    &cp->c_metadata.md_cookie, cp->c_fileno, buflen);

	return (error);
}

int
cachefs_getbackvp(struct fscache *fscp, struct fid *cookiep,
    struct cnode *cp)
{
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CHEAT)
		printf("cachefs_getbackvp: ENTER fscp %x cp %x\n",
		    (int) fscp, (int) cp);
#endif
	ASSERT(cp != NULL);
	ASSERT(RW_WRITE_HELD(&cp->c_statelock));
	error = VFS_VGET(fscp->fs_backvfsp, &cp->c_backvp,
	    (struct fid *)cookiep);
	if (error || cp->c_backvp == NULL) {
#ifdef CFSDEBUG
		CFS_DEBUG(CFSDEBUG_GENERAL) {
			printf("Stale cookie cp %x fileno %lu type %d \n",
			    (int) cp, cp->c_fileno, CTOV(cp)->v_type);
		}
#endif
		cp->c_backvp = NULL;
		error = ESTALE;
	}

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CHEAT)
		printf("cachefs_getbackvp: EXIT error = %d\n", error);
#endif
	return (error);
}

int
cachefs_getcookie(vp, cookiep, attrp, cr)
	vnode_t *vp;
	struct fid *cookiep;
	struct vattr *attrp;
	cred_t *cr;
{
	int error = 0;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CHEAT)
		printf("cachefs_getcookie: ENTER vp %x\n", (int) vp);
#endif
	/*
	 * This assumes that the cookie is a full size fid, if we go to
	 * variable length fids we will need to change this.
	 */
	cookiep->fid_len = MAXFIDSZ;
	error = VOP_FID(vp, cookiep);
	if (!error) {
		ASSERT(attrp != NULL);
		attrp->va_mask = AT_ALL;
		error = VOP_GETATTR(vp, attrp, 0, cr);
	} else {
		if (error == ENOSPC) {
			/*
			 * This is an indication that the underlying filesystem
			 * needs a bigger fid.  For now just map to EINVAL.
			 */
			error = EINVAL;
		}
	}
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_CHEAT)
		printf("cachefs_getcookie: EXIT error = %d\n", error);
#endif
	return (error);
}

void
cachefs_workq_init(struct cachefs_workq *qp)
{
	qp->wq_head = qp->wq_tail = NULL;
	qp->wq_length =
	    qp->wq_thread_count =
	    qp->wq_max_len =
	    qp->wq_halt_request = 0;
	qp->wq_keepone = 0;
	cv_init(&qp->wq_req_cv, "cachefs async io cv", CV_DEFAULT, NULL);
	cv_init(&qp->wq_halt_cv, "cachefs halt drain cv",
		CV_DEFAULT, NULL);
	mutex_init(&qp->wq_queue_lock, "cachefs work q lock",
			MUTEX_DEFAULT, NULL);
}

void
cachefs_async_start(struct cachefs_workq *qp)
{
	struct cachefs_req *rp;
	int left;
	callb_cpr_t cprinfo;

	CALLB_CPR_INIT(&cprinfo, &qp->wq_queue_lock, callb_generic_cpr, "cas");
	mutex_enter(&qp->wq_queue_lock);
	left = 1;
	for (;;) {
		/* if there are no pending requests */
		if (qp->wq_head == NULL) {
			/* see if thread should exit */
			if (qp->wq_halt_request || (left == -1)) {
				if ((qp->wq_thread_count > 1) ||
				    (qp->wq_keepone == 0))
					break;
			}

			/* wake up thread in async_halt if necessary */
			if (qp->wq_halt_request)
				cv_signal(&qp->wq_halt_cv);

			CALLB_CPR_SAFE_BEGIN(&cprinfo);
			/* sleep until there is something to do */
			left = cv_timedwait(&qp->wq_req_cv, &qp->wq_queue_lock,
				CFS_ASYNC_TIMEOUT + lbolt);
			CALLB_CPR_SAFE_END(&cprinfo, &qp->wq_queue_lock);
			if ((qp->wq_head == NULL) && (qp->wq_logwork == 0))
				continue;
		}
		left = 1;

		if (qp->wq_logwork) {
			qp->wq_logwork = 0;
			mutex_exit(&qp->wq_queue_lock);
			cachefs_log_process_queue(qp->wq_cachep, 1);
			mutex_enter(&qp->wq_queue_lock);
			continue;
		}

		/* remove request from the list */
		rp = qp->wq_head;
		qp->wq_head = rp->cfs_next;
		if (rp->cfs_next == NULL)
			qp->wq_tail = NULL;

		/* do the request */
		mutex_exit(&qp->wq_queue_lock);
		cachefs_do_req(rp);
		mutex_enter(&qp->wq_queue_lock);

		/* decrement count of requests */
		qp->wq_length--;
	}
	ASSERT(qp->wq_head == NULL);
	qp->wq_thread_count--;
	if (qp->wq_halt_request && qp->wq_thread_count == 0)
		cv_signal(&qp->wq_halt_cv);
	CALLB_CPR_SAFE_BEGIN(&cprinfo);
	CALLB_CPR_EXIT(&cprinfo);
	mutex_exit(&qp->wq_queue_lock);
	thread_exit();
	/*NOTREACHED*/
}

/*
 * attempt to halt all the async threads associated with a given workq
 */
int
cachefs_async_halt(struct cachefs_workq *qp, int force)
{
	int error = 0;
	int tend;

	mutex_enter(&qp->wq_queue_lock);
	if (force)
		qp->wq_keepone = 0;

	if (qp->wq_thread_count > 0) {
		qp->wq_halt_request = 1;
		cv_broadcast(&qp->wq_req_cv);
		tend = lbolt + (60 * HZ);
		(void) cv_timedwait(&qp->wq_halt_cv, &qp->wq_queue_lock, tend);
		qp->wq_halt_request = 0;
		if (qp->wq_thread_count > 0) {
			if ((qp->wq_thread_count == 1) &&
			    (qp->wq_length == 0) && qp->wq_keepone)
				error = EAGAIN;
			else
				error = EBUSY;
		} else {
			ASSERT(qp->wq_length == 0 && qp->wq_head == NULL);
		}
	}
	mutex_exit(&qp->wq_queue_lock);
	return (error);
}

int
cachefs_addqueue(struct cachefs_req *rp, struct cachefs_workq *qp)
{
	int error = 0;

	mutex_enter(&qp->wq_queue_lock);
	if (qp->wq_thread_count < cachefs_max_threads) {
		if (qp->wq_thread_count == 0 ||
		    (qp->wq_length >= (qp->wq_thread_count * 2))) {
			if (thread_create(NULL, NULL, cachefs_async_start,
			    (caddr_t)qp, 0, &p0, TS_RUN, 60) != NULL) {
				qp->wq_thread_count++;
			} else {
				if (qp->wq_thread_count == 0) {
					error = EBUSY;
					goto out;
				}
			}
		}
	}
	mutex_enter(&rp->cfs_req_lock);
	if (qp->wq_tail)
		qp->wq_tail->cfs_next = rp;
	else
		qp->wq_head = rp;
	qp->wq_tail = rp;
	rp->cfs_next = NULL;
	qp->wq_length++;
	if (qp->wq_length > qp->wq_max_len)
		qp->wq_max_len = qp->wq_length;

	cv_signal(&qp->wq_req_cv);
	mutex_exit(&rp->cfs_req_lock);
out:
	mutex_exit(&qp->wq_queue_lock);
	return (error);
}

void
cachefs_async_putpage(struct cachefs_putpage_req *prp, cred_t *cr)
{
	struct cnode *cp = VTOC(prp->cp_vp);

	mutex_enter(&cp->c_iomutex);
	(void) VOP_PUTPAGE(prp->cp_vp, prp->cp_off, prp->cp_len,
		prp->cp_flags, cr);
	if (--cp->c_nio == 0)
		cv_broadcast(&cp->c_iocv);
	if (prp->cp_off == 0 && prp->cp_len == 0 &&
	    (cp->c_ioflags & CIO_PUTPAGES)) {
		cp->c_ioflags &= ~CIO_PUTPAGES;
	}
	mutex_exit(&cp->c_iomutex);
}

void
cachefs_do_req(struct cachefs_req *rp)
{
	struct cachefscache *cachep;

	mutex_enter(&rp->cfs_req_lock);
	switch (rp->cfs_cmd) {
	case CFS_CACHE_SYNC:
		cachep = rp->cfs_req_u.cu_fs_sync.cf_cachep;
		cachefs_cache_sync(cachep);
		break;
	case CFS_INACTIVE:
		cachefs_inactive(rp->cfs_req_u.cu_inactive.ci_vp, rp->cfs_cr);
		break;
	case CFS_PUTPAGE:
		cachefs_async_putpage(&rp->cfs_req_u.cu_putpage, rp->cfs_cr);
		VN_RELE(rp->cfs_req_u.cu_putpage.cp_vp);
		break;
	case CFS_NOOP:
		break;
	default:
		panic("c_do_req: Invalid CFS async operation\n");
	}
	crfree(rp->cfs_cr);
	mutex_exit(&rp->cfs_req_lock);
	cachefs_kmem_free((caddr_t)rp, sizeof (struct cachefs_req));
}




int cachefs_mem_usage = 0;

struct km_wrap {
	int kw_size;
	struct km_wrap *kw_other;
};

kmutex_t cachefs_kmem_lock;

caddr_t
cachefs_kmem_alloc(int size, int flag)
{
	caddr_t mp = NULL;
	struct km_wrap *kwp;
	int n = (size + (2 * sizeof (struct km_wrap)) + 3) & ~3;

	ASSERT(n >= (size + 8));
	mp = kmem_alloc(n, flag);
	if (mp == NULL) {
		return (NULL);
	}
	/*LINTED alignment okay*/
	kwp = (struct km_wrap *)mp;
	kwp->kw_size = n;
	/*LINTED alignment okay*/
	kwp->kw_other = (struct km_wrap *) (mp + n - sizeof (struct km_wrap));
	kwp = (struct km_wrap *)kwp->kw_other;
	kwp->kw_size = n;
	/*LINTED alignment okay*/
	kwp->kw_other = (struct km_wrap *)mp;

	mutex_enter(&cachefs_kmem_lock);
	ASSERT(cachefs_mem_usage >= 0);
	cachefs_mem_usage += n;
	mutex_exit(&cachefs_kmem_lock);

	return (mp + sizeof (struct km_wrap));
}

caddr_t
cachefs_kmem_zalloc(int size, int flag)
{
	caddr_t mp = NULL;
	struct km_wrap *kwp;
	int n = (size + (2 * sizeof (struct km_wrap)) + 3) & ~3;

	ASSERT(n >= (size + 8));
	mp = kmem_zalloc(n, flag);
	if (mp == NULL) {
		return (NULL);
	}
	/*LINTED alignment okay*/
	kwp = (struct km_wrap *)mp;
	kwp->kw_size = n;
	/*LINTED alignment okay*/
	kwp->kw_other = (struct km_wrap *)(mp + n - sizeof (struct km_wrap));
	kwp = (struct km_wrap *)kwp->kw_other;
	kwp->kw_size = n;
	/*LINTED alignment okay*/
	kwp->kw_other = (struct km_wrap *)mp;

	mutex_enter(&cachefs_kmem_lock);
	ASSERT(cachefs_mem_usage >= 0);
	cachefs_mem_usage += n;
	mutex_exit(&cachefs_kmem_lock);

	return (mp + sizeof (struct km_wrap));
}

void
cachefs_kmem_free(caddr_t mp, int size)
{
	struct km_wrap *front_kwp;
	struct km_wrap *back_kwp;
	int n = (size + (2 * sizeof (struct km_wrap)) + 3) & ~3;
	caddr_t p;

	ASSERT(n >= (size + 8));
	/*LINTED alignment okay*/
	front_kwp = (struct km_wrap *)(mp - sizeof (struct km_wrap));
	back_kwp = (struct km_wrap *)
		/*LINTED alignment okay*/
		((caddr_t)front_kwp + n - sizeof (struct km_wrap));

	ASSERT(front_kwp->kw_other == back_kwp);
	ASSERT(front_kwp->kw_size == n);
	ASSERT(back_kwp->kw_other == front_kwp);
	ASSERT(back_kwp->kw_size == n);

	mutex_enter(&cachefs_kmem_lock);
	cachefs_mem_usage -= n;
	ASSERT(cachefs_mem_usage >= 0);
	mutex_exit(&cachefs_kmem_lock);

	p = (caddr_t)front_kwp;
	front_kwp->kw_size = back_kwp->kw_size = 0;
	front_kwp->kw_other = back_kwp->kw_other = NULL;
	kmem_free(p, n);
}

char *
cachefs_strdup(char *s)
{
	char *rc;

	ASSERT(s != NULL);

	rc = cachefs_kmem_alloc(strlen(s) + 1, KM_SLEEP);
	(void) strcpy(rc, s);

	return (rc);
}

int
cachefs_stats_kstat_snapshot(kstat_t *ksp, void *buf, int rw)
{
	struct fscache *fscp = (struct fscache *) ksp->ks_data;
	cachefscache_t *cachep = fscp->fs_cache;

	if (rw == KSTAT_WRITE) {
		bcopy((caddr_t) buf, (caddr_t) &fscp->fs_stats,
		    sizeof (fscp->fs_stats));
		cachep->c_gc_count = fscp->fs_stats.st_gc_count;
		cachep->c_gc_time = fscp->fs_stats.st_gc_time;
		cachep->c_gc_before = fscp->fs_stats.st_gc_before_atime;
		cachep->c_gc_after = fscp->fs_stats.st_gc_after_atime;
		return (0);
	}

	fscp->fs_stats.st_gc_count = cachep->c_gc_count;
	fscp->fs_stats.st_gc_time = cachep->c_gc_time;
	fscp->fs_stats.st_gc_before_atime = cachep->c_gc_before;
	fscp->fs_stats.st_gc_after_atime = cachep->c_gc_after;
	bcopy((caddr_t) &fscp->fs_stats, (caddr_t) buf,
	    sizeof (fscp->fs_stats));

	return (0);
}

#ifdef DEBUG
cachefs_debug_info_t *
cachefs_debug_save(cachefs_debug_info_t *oldcdb, int chain,
    char *message, u_int flags, int number, void *pointer,
    cachefscache_t *cachep, struct fscache *fscp, struct cnode *cp)
{
	cachefs_debug_info_t *cdb;

	if ((chain) || (oldcdb == NULL))
		cdb = (cachefs_debug_info_t *)
		    cachefs_kmem_zalloc(sizeof (*cdb), KM_SLEEP);
	else
		cdb = oldcdb;
	if (chain)
		cdb->cdb_next = oldcdb;

	if (message != NULL) {
		if (cdb->cdb_message != NULL)
			cachefs_kmem_free(cdb->cdb_message,
			    strlen(cdb->cdb_message) + 1);
		cdb->cdb_message = cachefs_kmem_alloc(strlen(message) + 1,
		    KM_SLEEP);
		(void) strcpy(cdb->cdb_message, message);
	}
	cdb->cdb_flags = flags;
	cdb->cdb_int = number;
	cdb->cdb_pointer = pointer;

	cdb->cdb_count++;

	cdb->cdb_cnode = cp;
	if (cp != NULL) {
		cdb->cdb_frontvp = cp->c_frontvp;
		cdb->cdb_backvp = cp->c_backvp;
	}
	if (fscp != NULL)
		cdb->cdb_fscp = fscp;
	else if (cp != NULL)
		cdb->cdb_fscp = C_TO_FSCACHE(cp);
	if (cachep != NULL)
		cdb->cdb_cachep = cachep;
	else if (cdb->cdb_fscp != NULL)
		cdb->cdb_cachep = cdb->cdb_fscp->fs_cache;

	cdb->cdb_thread = curthread;
	cdb->cdb_timestamp = gethrtime();
	cdb->cdb_depth = getpcstack(cdb->cdb_stack, CACHEFS_DEBUG_DEPTH);

	return (cdb);
}

void
cachefs_debug_show(cachefs_debug_info_t *cdb)
{
	hrtime_t now = gethrtime();
	timestruc_t ts;
	int i;

	while (cdb != NULL) {
		hrt2ts(now - cdb->cdb_timestamp, &ts);
		printf("cdb: %x count: %d timelapse: %ld.%9ld\n",
		    (u_int) cdb, cdb->cdb_count, ts.tv_sec, ts.tv_nsec);
		if (cdb->cdb_message != NULL)
			printf("message: %s", cdb->cdb_message);
		printf("flags: %x int: %d pointer: %x\n",
		    cdb->cdb_flags, cdb->cdb_int, (u_int) cdb->cdb_pointer);

		printf("cnode: %x fscp: %x cachep: %x\n",
		    (u_int) cdb->cdb_cnode,
		    (u_int) cdb->cdb_fscp, (u_int) cdb->cdb_cachep);
		printf("frontvp: %x backvp: %x\n",
		    (u_int) cdb->cdb_frontvp, (u_int) cdb->cdb_backvp);

		printf("thread: %x stack...\n", (u_int) cdb->cdb_thread);
		for (i = 0; i < cdb->cdb_depth; i++) {
			u_int off;
			char *sym;

			sym = kobj_getsymname(cdb->cdb_stack[i], &off);
			printf("%s+%x\n", sym ? sym : "?", off);
		}
		delay(2*HZ);
		cdb = cdb->cdb_next;
	}
	debug_enter(NULL);
}
#endif /* DEBUG */
