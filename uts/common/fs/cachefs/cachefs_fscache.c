/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_fscache.c 1.14     94/10/11 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/file.h>
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
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/fbuf.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <sys/fs/cachefs_fs.h>

/* external references */
extern kmutex_t cachefs_cnode_freelist_lock;
extern struct cachefsops nopcfsops, strictcfsops, singlecfsops;
extern struct cachefsops codcfsops;

/* forward references */
int fscdir_create(cachefscache_t *cachep, char *namep,
	struct cachefsoptions *optp, ino_t *fsidp, vnode_t **dvpp,
	vnode_t **avpp);
int fscdir_find(cachefscache_t *cachep, ino_t fsid, vnode_t **dvpp,
	vnode_t **avpp, struct cachefsoptions *optp);

/*
 * ------------------------------------------------------------------
 *
 *		fscache_create
 *
 * Description:
 *	Creates a fscache object.
 * Arguments:
 *	cachep		cache to create fscache object for
 * Returns:
 *	Returns a fscache object.
 * Preconditions:
 *	precond(cachep)
 */

fscache_t *
fscache_create(cachefscache_t *cachep)
{
	fscache_t *fscp;

	/* create and initialize the fscache object */
	/*LINTED alignment okay*/
	fscp = (fscache_t *)cachefs_kmem_zalloc(sizeof (fscache_t), KM_SLEEP);
	fscp->fs_cache = cachep;
	fscp->fs_options.opt_flags = CFS_DUAL_WRITE;
	fscp->fs_options.opt_popsize = DEF_POP_SIZE;
	fscp->fs_options.opt_fgsize = DEF_FILEGRP_SIZE;
	fscp->fs_cfsops = &codcfsops;
	fscp->fs_acregmin = 30;
	fscp->fs_acregmax = 30;
	fscp->fs_acdirmin = 30;
	fscp->fs_acdirmax = 30;
	cachefs_workq_init(&fscp->fs_workq);
	mutex_init(&fscp->fs_fslock, "fscache contents", MUTEX_DEFAULT, NULL);
	mutex_init(&fscp->fs_cnodelock, "cnodelist lock", MUTEX_DEFAULT, NULL);

	return (fscp);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_destroy
 *
 * Description:
 *	Destroys the fscache object.
 * Arguments:
 *	fscp	the fscache object to destroy
 * Returns:
 * Preconditions:
 *	precond(fscp)
 *	precond(fs_vnoderef == 0)
 */

void
fscache_destroy(fscache_t *fscp)
{
	ASSERT(fscp->fs_vnoderef == 0);

	/* destroy any filegroups left */
	mutex_enter(&fscp->fs_fslock);
	filegrp_list_gc(fscp);
	mutex_exit(&fscp->fs_fslock);

	ASSERT(fscp->fs_filegrp == NULL);

	/* drop references to the fscache directory */
	if (fscp->fs_fscdirvp)
		VN_RELE(fscp->fs_fscdirvp);
	if (fscp->fs_fsattrdir)
		VN_RELE(fscp->fs_fsattrdir);

	cachefs_kmem_free((caddr_t)fscp, sizeof (struct fscache));
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_setup
 *
 * Description:
 *	Activates a fscache by associating the fscache object
 *	with on disk data.
 *	If the fscache directory of the specified fsid exists then
 *	it will be used.
 *	Otherwise a new fscache directory will be created using namep
 *	and optp with fsid being ignored.  However if namep or optp
 *	are not NULL or the cache is in NOFILL then this routine fails.
 * Arguments:
 *	fscp	the fscache object to activate
 *	fsid	unique identifier for the cache
 *	namep	name of the cache
 *	optp	options for the cache
 * Returns:
 *	Returns 0 for success, !0 on failure.
 * Preconditions:
 *	precond(fscp)
 *	precond(the cache must not be in NOCACHE mode)
 *	precond(the cache must not alread by active)
 */

static int
fscache_setup(fscache_t *fscp, ino_t fsid, char *namep,
    struct cachefsoptions *optp, int setflags)
{
	int error;
	struct vnode *dvp;
	struct vnode *avp;
	struct cachefsoptions opt;
	cachefscache_t *cachep = fscp->fs_cache;

	ASSERT((cachep->c_flags & CACHE_NOCACHE) == 0);

	/* see if the fscache directory already exists */
	error =	fscdir_find(cachep, fsid, &dvp, &avp, &opt);
	if (error) {
		/* return error if cannot create the directory */
		if ((namep == NULL) || (optp == NULL) ||
		    (cachep->c_flags & CACHE_NOFILL)) {
			return (error);
		}

		/* create the directory */
		error = fscdir_create(cachep, namep, optp, &fsid, &dvp, &avp);
		if (error) {
			if (error == ENOSPC)
				cmn_err(CE_WARN,
				    "CacheFS: not enough space to create %s",
				    namep);
			else
				cmn_err(CE_WARN,
				    "CacheFS: error %d creating %s\n",
				    error, namep);
			return (error);
		}
	} else if (optp == NULL) {
		optp = &opt;
	} else {
		/* compare the options to make sure they are compatable */
		error = fscache_compare_options(&opt, optp);
		if (error) {
			cmn_err(CE_WARN,
				"CacheFS: mount failed, options do not match.");
			return (error);
		}
		optp = &opt;
	}

	mutex_enter(&fscp->fs_fslock);

	fscp->fs_options = *optp;

	fscp->fs_cfsid = fsid;
	fscp->fs_fscdirvp = dvp;
	fscp->fs_fsattrdir = avp;
	if (optp->opt_flags & CFS_DUAL_WRITE)
		fscp->fs_cfsops = &singlecfsops;
	else if (optp->opt_flags & CFS_WRITE_AROUND)
		fscp->fs_cfsops = &strictcfsops;
	else
		fscp->fs_cfsops = &nopcfsops;
	if (optp->opt_flags & CFS_NOCONST_MODE)
		fscp->fs_cfsops = &nopcfsops;
	if (optp->opt_flags & CFS_CODCONST_MODE)
		fscp->fs_cfsops = &codcfsops;

	if (setflags) {
		fscp->fs_flags |= CFS_FS_READ;
		if ((cachep->c_flags & CACHE_NOFILL) == 0)
			fscp->fs_flags |= CFS_FS_WRITE;
	}

	mutex_exit(&fscp->fs_fslock);

	return (0);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_activate
 *
 * Description:
 *	A wrapper routine for fscache_setup, telling it to setup the
 *	fscache for general use.
 *
 */
int
fscache_activate(fscache_t *fscp, ino_t fsid, char *namep,
    struct cachefsoptions *optp)
{
	return (fscache_setup(fscp, fsid, namep, optp, 1));
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_enable
 *
 * Description:
 *	A wrapper routine for fscache_setup, telling it to create a
 *	fscache that can be used during remount.  In this case the
 *	fscache flags that allow general use are not yet turned on.
 *	A later call to fscache_activate_rw will set the flags.
 *
 */
int
fscache_enable(fscache_t *fscp, ino_t fsid, char *namep,
    struct cachefsoptions *optp)
{
	return (fscache_setup(fscp, fsid, namep, optp, 0));
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_activate_rw
 *
 * Description:
 *	Makes the fscache both readable and writable.
 * Arguments:
 *	fscp		fscache object
 * Returns:
 * Preconditions:
 *	precond(fscp)
 */

void
fscache_activate_rw(fscache_t *fscp)
{
	mutex_enter(&fscp->fs_fslock);
	fscp->fs_flags |= (CFS_FS_WRITE|CFS_FS_READ);
	mutex_exit(&fscp->fs_fslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_hold
 *
 * Description:
 *	Increments the reference count on the fscache object
 * Arguments:
 *	fscp		fscache object to incriment reference count on
 * Returns:
 * Preconditions:
 *	precond(fscp)
 */

void
fscache_hold(fscache_t *fscp)
{
	mutex_enter(&fscp->fs_fslock);
	fscp->fs_vnoderef++;
	ASSERT(fscp->fs_vnoderef > 0);
	mutex_exit(&fscp->fs_fslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_rele
 *
 * Description:
 *	Decriments the reference count on the fscache object
 * Arguments:
 *	fscp		fscache object to decriment reference count on
 * Returns:
 * Preconditions:
 *	precond(fscp)
 */

void
fscache_rele(fscache_t *fscp)
{
	mutex_enter(&fscp->fs_fslock);
	ASSERT(fscp->fs_vnoderef > 0);
	fscp->fs_vnoderef--;
	mutex_exit(&fscp->fs_fslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_mounted
 *
 * Description:
 *	Called to indicate the the fscache is mounted.
 * Arguments:
 *	fscp		fscache object
 *	cfsvfsp		cachefs vfsp
 *	backvfsp	vfsp of back file system
 * Returns:
 *	Returns 0 for success, -1 if the cache is already mounted.
 * Preconditions:
 *	precond(fscp)
 */

int
fscache_mounted(fscache_t *fscp, struct vfs *cfsvfsp, struct vfs *backvfsp)
{
	int error;

	mutex_enter(&fscp->fs_fslock);
	if (fscp->fs_flags & CFS_FS_MOUNTED) {
		error = -1;
	} else {
		error = 0;
		fscp->fs_backvfsp = backvfsp;
		fscp->fs_cfsvfsp = cfsvfsp;
		fscp->fs_cod_time = hrestime;
		fscp->fs_flags |= CFS_FS_MOUNTED;
	}
	mutex_exit(&fscp->fs_fslock);
	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_compare_options
 *
 * Description:
 *	Compares the two sets of cachefs options to see
 *	if it is okay to switch from the first set to the second
 *	set.
 * Arguments:
 *	opoldp		old options
 *	opnewp		new options
 * Returns:
 *	Returns 0 for success, EINVAL if not okay.
 * Preconditions:
 *	precond(opoldp)
 *	precond(opnewp)
 */

int
fscache_compare_options(struct cachefsoptions *opoldp,
    struct cachefsoptions *opnewp)
{
	if ((opoldp->opt_popsize == opnewp->opt_popsize) &&
	    (opoldp->opt_fgsize == opnewp->opt_fgsize) &&
	    ((opoldp->opt_flags & ~CFS_ACCESS_BACKFS) ==
	    (opnewp->opt_flags & ~CFS_ACCESS_BACKFS))) {
		return (0);
	} else {
		return (ESRCH);
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_sync
 *
 * Description:
 *	Syncs any data for this fscache to the front file system.
 * Arguments:
 *	fscp	fscache to sync
 *	unmount	1 means toss any cnodes that are not being used
 * Returns:
 * Preconditions:
 *	precond(fscp)
 */

void
fscache_sync(struct fscache *fscp, int unmount)
{
	struct cnode *cp, *ncp;
	struct filegrp *fgp;
	int i;

	mutex_enter(&fscp->fs_fslock);

	/*
	 * Go through the cnodes and sync the dirty ones. If this is an unmount
	 * Throw away the inactive cnodes too.
	 */
	if (unmount)
		mutex_enter(&cachefs_cnode_freelist_lock);
	mutex_enter(&fscp->fs_cnodelock);
	for (i = 0; i < CNODE_BUCKET_SIZE; i++) {
		/*LINTED ncp is set before used */
		for (cp = fscp->fs_cnode[i]; cp != NULL; cp = ncp) {
			ncp = cp->c_hash;
			if (cp->c_flags & (CN_HASHSKIP | CN_DESTROY))
				continue;

			if (cp->c_flags & CN_UPDATED) {
				ASSERT((cp->c_flags & CN_INACTIVE) == 0);
				VN_HOLD(CTOV(cp));
				if (CTOV(cp)->v_type != VDIR)
					(void) cachefs_fsync(CTOV(cp),
					    FSYNC, kcred);
				(void) cachefs_sync_metadata(cp, kcred);
				VN_RELE(CTOV(cp));
			}
			if ((cp->c_flags & CN_INACTIVE) && unmount) {
				cachefs_remhash(cp);
				cachefs_remfree(cp);
				ASSERT(CTOV(cp)->v_count == 0);
				if (CTOV(cp)->v_pages) {
					VN_HOLD(CTOV(cp));
					(void) VOP_PUTPAGE(CTOV(cp),
					    (offset_t)0, 0, B_TRUNC | B_INVAL,
					    kcred);
				}
				ASSERT(CTOV(cp)->v_pages == NULL);

				cachefs_kmem_free((caddr_t)cp,
					sizeof (struct cnode));
				(void) cachefs_cnode_cnt(-1);
			}
		}
	}
	mutex_exit(&fscp->fs_cnodelock);
	if (unmount)
		mutex_exit(&cachefs_cnode_freelist_lock);

	/* sync the attrcache files */
	for (fgp = fscp->fs_filegrp; fgp != NULL; fgp = fgp->fg_next) {
		(void) filegrp_sync(fgp);
	}

	/* garbage collect any unused file groups */
	filegrp_list_gc(fscp);

	mutex_exit(&fscp->fs_fslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_acset
 *
 * Description:
 *	Sets the ac timeout values for the fscache.
 * Arguments:
 *	fscp	fscache object
 * Returns:
 * Preconditions:
 *	precond(fscp)
 */

void
fscache_acset(fscache_t *fscp,
	u_long acregmin, u_long acregmax, u_long acdirmin, u_long acdirmax)
{
	mutex_enter(&fscp->fs_fslock);
	if (acregmin > acregmax)
		acregmin = acregmax;
	if (acdirmin > acdirmax)
		acdirmin = acdirmax;
	if (acregmin != 0)
		fscp->fs_acregmin = acregmin;
	if (acregmax != 0)
		fscp->fs_acregmax = acregmax;
	if (acdirmin != 0)
		fscp->fs_acdirmin = acdirmin;
	if (acdirmax != 0)
		fscp->fs_acdirmax = acdirmax;
	mutex_exit(&fscp->fs_fslock);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_list_find
 *
 * Description:
 *	Finds the desired fscache structure on a cache's
 *	file system list.
 * Arguments:
 *	cachep	holds the list of fscache objects to search
 *	fsid	the numeric identifier of the fscache
 * Returns:
 *	Returns an fscache object on success or NULL on failure.
 * Preconditions:
 *	precond(cachep)
 *	precond(the fslistlock must be held)
 */

fscache_t *
fscache_list_find(cachefscache_t *cachep, ino_t fsid)
{
	fscache_t *fscp = cachep->c_fslist;

	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	while (fscp != NULL) {
		if (fscp->fs_cfsid == fsid) {
			ASSERT(fscp->fs_cache == cachep);
			break;
		}
		fscp = fscp->fs_next;
	}

	return (fscp);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_list_add
 *
 * Description:
 *	Adds the specified fscache object to the list on
 *	the specified cachep.
 * Arguments:
 *	cachep	holds the list of fscache objects
 *	fscp	fscache object to add to list
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond(fscp)
 *	precond(fscp cannot already be on a list)
 *	precond(the fslistlock must be held)
 */

void
fscache_list_add(cachefscache_t *cachep, fscache_t *fscp)
{
	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	fscp->fs_next = cachep->c_fslist;
	cachep->c_fslist = fscp;
	cachep->c_refcnt++;
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_list_remove
 *
 * Description:
 *	Removes the specified fscache object from the list
 *	on the specified cachep.
 * Arguments:
 *	cachep	holds the list of fscache objects
 *	fscp	fscache object to remove from list
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond(fscp)
 *	precond(the fslistlock must be held)
 */

void
fscache_list_remove(cachefscache_t *cachep, fscache_t *fscp)
{
	struct fscache **pfscp = &cachep->c_fslist;

	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	while (*pfscp != NULL) {
		if (fscp == *pfscp) {
			*pfscp = fscp->fs_next;
			cachep->c_refcnt--;
			break;
		}
		pfscp = &(*pfscp)->fs_next;
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_list_gc
 *
 * Description:
 *	Traverses the list of fscache objects on the cachep
 *	list and destroys any that are not mounted and
 *	that are not referenced.
 * Arguments:
 *	cachep	holds the list of fscache objects
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond(the fslistlock must be held)
 */

void
fscache_list_gc(cachefscache_t *cachep)
{
	struct fscache *next, *fscp;

	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	/*LINTED next is set before used */
	for (fscp = cachep->c_fslist; fscp != NULL; fscp = next) {
		next = fscp->fs_next;
		mutex_enter(&fscp->fs_fslock);
		if (((fscp->fs_flags & CFS_FS_MOUNTED) == 0) &&
		    (fscp->fs_vnoderef == 0)) {
			mutex_exit(&fscp->fs_fslock);
			fscache_list_remove(cachep, fscp);
			fscache_destroy(fscp);
		} else {
			mutex_exit(&fscp->fs_fslock);
		}
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_list_mounted
 *
 * Description:
 *	Returns the number of fscache objects that are mounted.
 * Arguments:
 *	cachep	holds the list of fscache objects
 * Returns:
 * Preconditions:
 *	precond(cachep)
 *	precond(the fslistlock must be held)
 */

int
fscache_list_mounted(cachefscache_t *cachep)
{
	struct fscache *fscp;
	int count;

	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	count = 0;
	for (fscp = cachep->c_fslist; fscp != NULL; fscp = fscp->fs_next) {
		mutex_enter(&fscp->fs_fslock);
		if (fscp->fs_flags & CFS_FS_MOUNTED)
			count++;
		mutex_exit(&fscp->fs_fslock);
	}

	return (count);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscdir_create
 *
 * Description:
 *	Creates the fs cache directory.
 *	The directory name is the ascii version of the fsid.
 *	Also makes a symlink to the directory using the specified name.
 * Arguments:
 *	cachep	cache object to create fs cache directory in
 *	namep	symlink name (cacheid = xxx) to the cache directory
 *	optp	the file system options for this fs cache
 *	fsidp	set by this routine to the fsid of the fs cache
 *	dvpp	set to the vnode of the cache directory
 *	avpp	set to the vnode of the attrcache directory
 * Returns:
 *	0 for success, !0 on error
 * Preconditions:
 *	precond(cachep)
 *	precond(namep)
 *	precond(optp)
 *	precond(fsidp)
 *	precond(dvpp)
 *	precond(avpp)
 *	precond(the fslistlock must be held)
 */

int
fscdir_create(cachefscache_t *cachep,
	char *namep,
	struct cachefsoptions *optp,
	ino_t *fsidp,
	vnode_t **dvpp,
	vnode_t **avpp)
{
	int error;
	vnode_t *fscdirvp = NULL;
	vnode_t *optvp = NULL;
	vnode_t *attrdirvp = NULL;
	struct vattr *attrp = (struct vattr *)NULL;
	char name[CFS_FRONTFILE_NAME_SIZE];
	caddr_t addr;
	int files;
	int blocks = 0;

	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	/* directory, symlink and options file + attrcache dir */
	files = 0;
	while (files < 4) {
		error = cachefs_allocfile(cachep);
		if (error)
			goto out;
		files++;
	}
	error = cachefs_allocblocks(cachep, 4, kcred);
	if (error)
		goto out;
	blocks = 4;

	attrp = (struct vattr *)cachefs_kmem_alloc(sizeof (struct vattr),
		/*LINTED alignment okay*/
		KM_SLEEP);
	attrp->va_mode = S_IFDIR | 0777;
	attrp->va_uid = 0;
	attrp->va_gid = 0;
	attrp->va_type = VDIR;
	attrp->va_mask = AT_TYPE | AT_MODE | AT_UID | AT_GID;
	error = VOP_MKDIR(cachep->c_dirvp, namep, attrp, &fscdirvp, kcred);
	if (error) {
		cmn_err(CE_WARN, "Can't create fs cache directory\n");
		goto out;
	}
	*dvpp = fscdirvp;
	/*
	 * Created the directory. Get the fileno. That'll be the cachefs_fsid.
	 */
	attrp->va_mask = AT_NODEID;
	error = VOP_GETATTR(fscdirvp, attrp, 0, kcred);
	if (error) {
		goto out;
	}
	*fsidp = attrp->va_nodeid;
	attrp->va_mode = S_IFREG | 0666;
	attrp->va_uid = 0;
	attrp->va_gid = 0;
	attrp->va_type = VREG;
	attrp->va_mask = AT_TYPE | AT_MODE | AT_UID | AT_GID;
	error = VOP_CREATE(fscdirvp, OPTION_NAME, attrp, EXCL,
			0600, &optvp, kcred);
	if (error) {
		cmn_err(CE_WARN, "Can't create fs option file\n");
		goto out;
	}
	attrp->va_size = MAXBSIZE;
	attrp->va_mask = AT_SIZE;
	error = VOP_SETATTR(optvp, attrp, 0, kcred);
	if (error) {
		cmn_err(CE_WARN, "Can't set size of options file\n");
		goto out;
	}
	/*
	 * Write out the option file
	 */
	addr = segmap_getmap(segkmap, optvp, 0);
	/*LINTED alignment okay*/
	*(struct cachefsoptions *)addr = *optp;
	error = segmap_release(segkmap, addr, SM_WRITE);

	if (error) {
		cmn_err(CE_WARN, "Can't write to option file\n");
		goto out;
	}

	/*
	 * Install the symlink from cachefs_fsid -> directory.
	 */
	make_ascii_name(*fsidp, name);
	error = VOP_RENAME(cachep->c_dirvp, namep, cachep->c_dirvp,
		name, kcred);
	if (error) {
		cmn_err(CE_WARN, "Can't rename cache directory\n");
		goto out;
	}
	attrp->va_mask = AT_MODE | AT_TYPE;
	attrp->va_mode = 0777;
	attrp->va_type = VLNK;
	error = VOP_SYMLINK(cachep->c_dirvp, namep, attrp, name, kcred);
	if (error) {
		cmn_err(CE_WARN, "Can't create cache directory symlink\n");
		goto out;
	}

	/*
	 * Finally, make the attrcache directory
	 */
	attrp->va_mode = S_IFDIR | 0777;
	attrp->va_uid = 0;
	attrp->va_gid = 0;
	attrp->va_type = VDIR;
	attrp->va_mask = AT_TYPE | AT_MODE | AT_UID | AT_GID;
	error = VOP_MKDIR(fscdirvp, ATTRCACHE_NAME, attrp, &attrdirvp, kcred);
	if (error) {
		cmn_err(CE_WARN, "Can't create attrcache dir for fscache\n");
		goto out;
	}

	*avpp = attrdirvp;

out:

	if (error) {
		while (files-- > 0)
			cachefs_freefile(cachep);
		if (fscdirvp)
			VN_RELE(fscdirvp);
		if (blocks)
			cachefs_freeblocks(cachep, blocks);
		if (attrdirvp)
			VN_RELE(attrdirvp);
	}
	if (optvp)
		VN_RELE(optvp);
	if (attrp)
		cachefs_kmem_free((caddr_t)attrp, sizeof (struct vattr));
	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscdir_find
 *
 * Description:
 *	Tries to find the fscache directory indicated by fsid.
 * Arguments:
 *	cachep	cache object to find fs cache directory in
 *	fsid	inode number of directory
 *	dvpp	returns fs cache directory vnode
 *	avpp	returns attrcache directory vnode
 *	optp	returns contents of the option file
 * Returns:
 *	Returns 0 on success, !0 on error.
 * Preconditions:
 *	precond(cachep)
 *	precond(dvpp)
 *	precond(avpp)
 *	precond(optp)
 *	precond(the fslistlock must be held)
 */

int
fscdir_find(
	cachefscache_t *cachep,
	ino_t fsid,
	vnode_t **dvpp,
	vnode_t **avpp,
	struct cachefsoptions *optp)
{
	int error;
	vnode_t *optvp = NULL;
	vnode_t *fscdirvp = NULL;
	vnode_t *attrvp = NULL;
	char dirname[CFS_FRONTFILE_NAME_SIZE];
	caddr_t addr;

	ASSERT(MUTEX_HELD(&cachep->c_fslistlock));

	/* convert the fsid value to the name of the directory */
	make_ascii_name(fsid, dirname);

	/* try to find the directory */
	error = VOP_LOOKUP(cachep->c_dirvp, dirname, &fscdirvp, NULL,
			0, NULL, kcred);
	if (error)
		goto out;

	/* this better be a directory or we are hosed */
	if (fscdirvp->v_type != VDIR) {
		cmn_err(CE_WARN, "cachefs: fscdir_find_a: cache corruption"
			" run fsck, %s\n", dirname);
		error = ENOTDIR;
		goto out;
	}

	/* try to find the option file */
	error = VOP_LOOKUP(fscdirvp, OPTION_NAME, &optvp, NULL, 0, NULL, kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: fscdir_find_b: cache corruption"
			" run fsck, %s\n", dirname);
		goto out;
	}

	/* read in option struct */
	addr = segmap_getmap(segkmap, optvp, 0);
	/*LINTED alignment okay*/
	*optp = *(struct cachefsoptions *)addr;
	error =  segmap_release(segkmap, addr, 0);
	if (error) {
		cmn_err(CE_WARN, "cachefs: fscdir_find_c: cache corruption"
			" run fsck, %s\n", dirname);
		goto out;
	}

	/* try to find the attrcache directory */
	error = VOP_LOOKUP(fscdirvp, ATTRCACHE_NAME,
	    &attrvp, NULL, 0, NULL, kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: fscdir_find_d: cache corruption"
			" run fsck, %s\n", dirname);
		goto out;
	}

	*dvpp = fscdirvp;
	*avpp = attrvp;

out:
	if (optvp)
		VN_RELE(optvp);
	if (error) {
		if (fscdirvp)
			VN_RELE(fscdirvp);
	}
	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		fscache_name_to_fsid
 *
 * Description:
 *	Takes the name of a cache and determines it corresponding
 *	fsid.
 * Arguments:
 *	cachep	cache object to find name of fs cache in
 *	namep	the name of the fs cache
 *	fsidp	set to the fsid if found
 * Returns:
 *	Returns 0 on success, !0 on error.
 * Preconditions:
 *	precond(cachep)
 *	precond(namep)
 *	precond(fsidp)
 */

int
fscache_name_to_fsid(cachefscache_t *cachep, char *namep, ino_t *fsidp)
{
	int error;
	char dirname[CFS_FRONTFILE_NAME_SIZE];
	vnode_t *linkvp = NULL;
	struct uio uio;
	struct iovec iov;
	ino_t nodeid;
	char *pd;
	int xx;
	int c;

	/* get the vnode of the name */
	error = VOP_LOOKUP(cachep->c_dirvp, namep, &linkvp, NULL, 0, NULL,
		kcred);
	if (error)
		goto out;

	/* the vnode had better be a link */
	if (linkvp->v_type != VLNK) {
		error = EINVAL;
		goto out;
	}

	/* read the contents of the link */
	iov.iov_len = CFS_FRONTFILE_NAME_SIZE;
	iov.iov_base = dirname;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = iov.iov_len;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_loffset = 0;
	uio.uio_fmode = 0;
	error = VOP_READLINK(linkvp, &uio, kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: Can't read filesystem cache link\n");
		goto out;
	}

	/* convert the contents of the link to a ino_t */
	nodeid = 0;
	pd = dirname;
	for (xx = 0; xx < (CFS_FRONTFILE_NAME_SIZE - 1); xx++) {
		nodeid <<= 4;
		c = *pd++;
		if (c <= '9')
			c -= '0';
		else if (c <= 'F')
			c = c - 'A' + 10;
		else
			c = c - 'a' + 10;
		nodeid += c;
	}
	*fsidp = nodeid;
out:
	if (linkvp)
		VN_RELE(linkvp);

	return (error);
}
