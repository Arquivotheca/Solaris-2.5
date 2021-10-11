/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_vfsops.c 1.89     95/01/25 SMI"

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
#include <sys/fs/cachefs_log.h>
#include <sys/mkdev.h>
#include <sys/dnlc.h>

extern int cachefs_module_keepcnt;
extern kmutex_t cachefs_kmem_lock;
extern kmutex_t cachefs_cnode_cnt_lock;
kmutex_t cachefs_kstat_key_lock;

/* forward declarations */
static int cachefs_remount(struct vfs *, struct mounta *);
static void cachefs_delete_cachep(cachefscache_t *);

#define	CFS_MAPSIZE	256

kmutex_t cachefs_cachelock;			/* Cache list mutex */
cachefscache_t *cachefs_cachelist = NULL;		/* Cache struct list */
krwlock_t cachefs_freelistlock;		/* Cnode freelist lock */
cnode_t *cachefs_freeback = NULL;
cnode_t *cachefs_freefront = NULL;
int maxcnodes = 100;
int cachefs_mount_retries = 3;
kmutex_t cachefs_minor_lock;		/* Lock for minor device map */
int cachefs_major = 0;
int cachefs_minor = 0;
cachefs_kstat_key_t *cachefs_kstat_key = NULL;
int cachefs_kstat_key_n = 0;
/*
 * cachefs vfs operations.
 */
static	int cachefs_mount(vfs_t *, vnode_t *, struct mounta *, cred_t *);
static	int cachefs_unmount(vfs_t *, cred_t *);
static	int cachefs_root(vfs_t *, vnode_t **);
static	int cachefs_statvfs(register vfs_t *, struct statvfs *);
static	int cachefs_sync(vfs_t *, short, cred_t *);
static	int cachefs_vget(vfs_t *, vnode_t **, fid_t *);
static	int cachefs_mountroot(vfs_t *, whymountroot_t);
static	int cachefs_swapvp(vfs_t *, vnode_t **, char *);

struct vfsops cachefs_vfsops = {
	cachefs_mount,
	cachefs_unmount,
	cachefs_root,
	cachefs_statvfs,
	cachefs_sync,
	cachefs_vget,
	cachefs_mountroot,
	cachefs_swapvp
};

/*
 * Initialize the vfs structure
 */
int cachefsfstyp;
int cnodesize = 0;

dev_t
cachefs_mkmntdev(void)
{
	dev_t cachefs_dev;

	mutex_enter(&cachefs_minor_lock);
	do {
		cachefs_minor = (cachefs_minor + 1) & MAXMIN;
		cachefs_dev = makedevice(cachefs_major, cachefs_minor);
	} while (vfs_devsearch(cachefs_dev));
	mutex_exit(&cachefs_minor_lock);

	return (cachefs_dev);
}

/*
 * vfs operations
 */
static int
cachefs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	char *data = uap->dataptr;
	struct cachefs_mountargs *map = NULL;
	vnode_t *cachedirvp = NULL;
	vnode_t *backrootvp = NULL;
	cachefscache_t *cachep = NULL;
	fscache_t *fscp = NULL;
	cnode_t *cp;
	struct fid *cookiep = NULL;
	struct vattr *attrp = NULL;
	dev_t cachefs_dev;			/* devid for this mount */
	int error = 0;
	int retries = cachefs_mount_retries;
	ino_t fsid;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VFSOP)
		printf("cachefs_mount: ENTER cachefs_mntargs %x\n",
		    (int) data);
#endif

	cachefs_module_keepcnt++;
	/*
	 * Make sure we're root
	 */
	if (!suser(cr)) {
		error = EPERM;
		goto out;
	}

	/*
	 * make sure we're mounting on a directory
	 */
	if (mvp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	if (uap->flags & MS_REMOUNT) {
		error = cachefs_remount(vfsp, uap);
		goto out;
	}

	/*
	 * Assign a unique device id to the mount
	 */
	cachefs_dev = cachefs_mkmntdev();
	/*
	 * Copy in the arguments
	 */
	map = (struct cachefs_mountargs *)
		/*LINTED alignment okay*/
		cachefs_kmem_alloc(sizeof (*map), KM_SLEEP);
	error = copyin((caddr_t)data, (caddr_t)map, sizeof (*map));
	if (error)
		goto out;

	if ((map->cfs_options.opt_flags &
	    (CFS_WRITE_AROUND|CFS_DUAL_WRITE)) == 0) {
		error = EINVAL;
		goto out;
	}
	if ((map->cfs_options.opt_popsize % MAXBSIZE) != 0) {
		error = EINVAL;
		goto out;
	}
	/*
	 * Get the cache directory vp
	 */
	error = lookupname(map->cfs_cachedir, UIO_USERSPACE, FOLLOW,
			NULLVPP, &cachedirvp);
	if (error)
		goto out;

	/*
	 * Make sure the thing we just looked up is a directory
	 */
	if (cachedirvp->v_type != VDIR) {
		cmn_err(CE_WARN, "cachefs_mount: cachedir not a directory\n");
		error = EINVAL;
		goto out;
	}

	/*
	 * Make sure the cache doesn't live in cachefs!
	 */

	if (cachedirvp->v_op == cachefs_getvnodeops()) {
		cmn_err(CE_WARN, "cachefs_mount: cachedir in cachefs!\n");
		error = EINVAL;
		goto out;
	}

	/*
	 * Get the back file system root vp
	 */
	error = lookupname(map->cfs_backfs, UIO_USERSPACE, FOLLOW,
			NULLVPP, &backrootvp);
	if (error)
		goto out;

	/*
	 * Make sure the thing we just looked up is a directory
	 * and a root of a file system
	 */
	if (backrootvp->v_type != VDIR || !(backrootvp->v_flag & VROOT)) {
		cmn_err(CE_WARN, "cachefs_mount: backpath not a directory\n");
		error = EINVAL;
		goto out;
	}

	/*
	 * XXX - Temporary hack here until we get VOP_FID and VGET implemented
	 * everywhere
	 */
	cookiep = (struct fid *)
		/*LINTED alignment okay*/
		cachefs_kmem_alloc(sizeof (struct fid), KM_SLEEP);
	attrp = (struct vattr *)cachefs_kmem_alloc(sizeof (struct vattr),
		/*LINTED alignment okay*/
		KM_SLEEP);
	error = cachefs_getcookie(backrootvp, cookiep, attrp, cr);
	if (error)
		goto out;

again:

	/*
	 * In SVR4 it's not acceptable to stack up mounts
	 * unless MS_OVERLAY specified.
	 */
	mutex_enter(&mvp->v_lock);
	if (((uap->flags & MS_OVERLAY) == 0) &&
	    ((mvp->v_count != 1) || (mvp->v_flag & VROOT))) {
		mutex_exit(&mvp->v_lock);
		error = EBUSY;
		goto out;
	}
	mutex_exit(&mvp->v_lock);

	/*
	 * Lock out other mounts and unmounts until we safely have
	 * a mounted fscache object.
	 */
	mutex_enter(&cachefs_cachelock);

	/*
	 * Find the cache structure
	 */
	for (cachep = cachefs_cachelist; cachep != NULL;
		cachep = cachep->c_next) {
		if (cachep->c_dirvp == cachedirvp)
			break;
	}

	/* if the cache object does not exist, then create it */
	if (cachep == NULL) {
		cachep = cachefs_cache_create();
		if (cachep == NULL) {
			mutex_exit(&cachefs_cachelock);
			error = ENOMEM;
			goto out;
		}
		error = cachefs_cache_activate_ro(cachep, cachedirvp);
		if (error) {
			cachefs_cache_destroy(cachep);
			cachep = NULL;
			mutex_exit(&cachefs_cachelock);
			goto out;
		}
		cachefs_cache_activate_rw(cachep);

		cachep->c_next = cachefs_cachelist;
		cachefs_cachelist = cachep;
	}

	/* get the fscache id for this name */
	error = fscache_name_to_fsid(cachep, map->cfs_cacheid, &fsid);
	if (error) {
		fsid = 0;
	}

	/* find the fscache object for this mount point or create it */
	mutex_enter(&cachep->c_fslistlock);
	fscp = fscache_list_find(cachep, fsid);
	if (fscp == NULL) {
		fscp = fscache_create(cachep);
		error = fscache_activate(fscp, fsid, map->cfs_cacheid,
			&map->cfs_options);
		if (error) {
			fscache_destroy(fscp);
			fscp = NULL;
			mutex_exit(&cachep->c_fslistlock);
			mutex_exit(&cachefs_cachelock);
			if ((error == ENOSPC) && (retries-- > 0)) {
				delay(6 * HZ);
				goto again;
			}
			goto out;
		}
		fscache_list_add(cachep, fscp);
	} else {
		/* compare the options to make sure they are compatable */
		error = fscache_compare_options(&fscp->fs_options,
		    &map->cfs_options);
		if (error) {
			cmn_err(CE_WARN,
				"CacheFS: mount failed, options do not match.");
			fscp = NULL;
			mutex_exit(&cachep->c_fslistlock);
			mutex_exit(&cachefs_cachelock);
			goto out;
		}
		fscp->fs_options = map->cfs_options;
	}
	fscache_hold(fscp);
	mutex_exit(&cachep->c_fslistlock);

	/* see if fscache object is already mounted, it not, make it so */
	error = fscache_mounted(fscp, vfsp, backrootvp->v_vfsp);
	if (error) {
		/* fs cache was already mounted */
		mutex_exit(&cachefs_cachelock);
		error = EBUSY;
		goto out;
	}

	/* allow other mounts and unmounts to proceed */
	mutex_exit(&cachefs_cachelock);

	cachefs_kstat_mount(fscp,
	    uap->dir, map->cfs_backfs, map->cfs_cachedir, map->cfs_cacheid);

	/* set nfs style time out parameters */
	fscache_acset(fscp, map->cfs_acregmin, map->cfs_acregmax,
	    map->cfs_acdirmin, map->cfs_acdirmax);

	vfsp->vfs_dev = cachefs_dev;
	vfsp->vfs_data = (caddr_t)fscp;
	vfsp->vfs_fsid.val[0] = cachefs_dev;
	vfsp->vfs_fsid.val[1] = cachefsfstyp;
	vfsp->vfs_fstype = cachefsfstyp;
	vfsp->vfs_bsize = MAXBSIZE;	/* XXX */

	/* make a cnode for the root of the file system */
	error = makecachefsnode(attrp->va_nodeid, fscp, cookiep, backrootvp, cr,
			    CN_ROOT, &cp);
	if (error) {
		cmn_err(CE_WARN, "cachefs_mount: can't create root cnode\n");
		goto out;
	}

	/* stick the root cnode in the fscache object */
	mutex_enter(&fscp->fs_fslock);
	fscp->fs_rootvp = CTOV(cp);
	fscp->fs_rootvp->v_flag |= VROOT;
	fscp->fs_rootvp->v_type |= cp->c_attr.va_type;
	ASSERT(fscp->fs_rootvp->v_type == VDIR);

	mutex_exit(&fscp->fs_fslock);

out:
	/*
	 * make a log entry, if appropriate
	 */

	if ((cachep != NULL) &&
	    CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_MOUNT))
		cachefs_log_mount(cachep, error, vfsp,
		    (fscp != NULL) ? &fscp->fs_options : NULL,
		    uap->dir, UIO_USERSPACE,
		    (map != NULL) ? map->cfs_cacheid : NULL);

	/*
	 * Cleanup our mess
	 */
	if (cookiep != NULL)
		cachefs_kmem_free((caddr_t)cookiep, sizeof (struct fid));
	if (cachedirvp != NULL)
		VN_RELE(cachedirvp);
	if (backrootvp != NULL)
		VN_RELE(backrootvp);
	if (map != NULL)
		cachefs_kmem_free((caddr_t)map, sizeof (*map));
	if (fscp)
		fscache_rele(fscp);
	if (attrp)
		cachefs_kmem_free((caddr_t)attrp, sizeof (struct vattr));

	if (error) {
		cachefs_module_keepcnt--;
		if (cachep) {
			int xx;

			/* lock out mounts and umounts */
			mutex_enter(&cachefs_cachelock);

			/* lock the cachep's fslist */
			mutex_enter(&cachep->c_fslistlock);

			/*
			 * gc isn't necessary for list_mounted(), but
			 * we want to do it anyway.
			 */

			fscache_list_gc(cachep);
			xx = fscache_list_mounted(cachep);

			mutex_exit(&cachep->c_fslistlock);

			/* if no more references to this cachep, punt it. */
			if (xx == 0)
				cachefs_delete_cachep(cachep);
			mutex_exit(&cachefs_cachelock);
		}
	}

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VFSOP)
		printf("cachefs_mount: EXIT\n");
#endif
	return (error);
}

void
cachefs_kstat_mount(struct fscache *fscp,
    char *umountpoint, char *ubackfs, char *ucachedir, char *cacheid)
{
	cachefscache_t *cachep = fscp->fs_cache;
	cachefs_kstat_key_t *key;
	char *mountpoint = NULL, *backfs = NULL, *cachedir = NULL;
	u_int len;
	kstat_t *ksp;
	int i, rc;

	mountpoint = cachefs_kmem_alloc(MAXPATHLEN, KM_SLEEP);
	backfs = cachefs_kmem_alloc(MAXPATHLEN, KM_SLEEP);
	cachedir = cachefs_kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if ((copyinstr(umountpoint, mountpoint, MAXPATHLEN, &len) != 0) ||
	    (copyinstr(ubackfs, backfs, MAXPATHLEN, &len) != 0) ||
	    (copyinstr(ucachedir, cachedir, MAXPATHLEN, &len) != 0))
		goto out;

	ASSERT(strlen(mountpoint) < MAXPATHLEN);
	ASSERT(strlen(backfs) < MAXPATHLEN);
	ASSERT(strlen(cachedir) < MAXPATHLEN);

	mutex_enter(&cachefs_kstat_key_lock); /* protect cachefs_kstat_key */
	for (i = 0; i < cachefs_kstat_key_n; i++) {
		key = cachefs_kstat_key + i;
		if ((strcmp(key->ks_mountpoint, mountpoint) == 0) &&
		    (strcmp(key->ks_backfs, backfs) == 0) &&
		    (strcmp(key->ks_cachedir, cachedir) == 0) &&
		    (strcmp(key->ks_cacheid, cacheid) == 0))
			break;
	}

	if (i >= cachefs_kstat_key_n) {
		key = (cachefs_kstat_key_t *)
		    cachefs_kmem_zalloc((cachefs_kstat_key_n + 1) *
		    sizeof (cachefs_kstat_key_t), KM_SLEEP);
		if (cachefs_kstat_key != NULL) {
			bcopy((caddr_t) cachefs_kstat_key, (caddr_t) key,
			    cachefs_kstat_key_n * sizeof (*key));
			cachefs_kmem_free((caddr_t) cachefs_kstat_key,
			    cachefs_kstat_key_n * sizeof (*key));
		}
		cachefs_kstat_key = key;
		key = cachefs_kstat_key + cachefs_kstat_key_n;
		++cachefs_kstat_key_n;
		rc = key->ks_id = cachefs_kstat_key_n; /* offset + 1 */

		key->ks_mountpoint = cachefs_strdup(mountpoint);
		key->ks_backfs = cachefs_strdup(backfs);
		key->ks_cachedir = cachefs_strdup(cachedir);
		key->ks_cacheid = cachefs_strdup(cacheid);
	} else
		rc = key->ks_id;

	mutex_enter(&fscp->fs_fslock); /* protect fscp */

	fscp->fs_kstat_id = rc;

	mutex_exit(&fscp->fs_fslock); /* finished with fscp */
	mutex_exit(&cachefs_kstat_key_lock); /* finished cachefs_kstat_key */

	key->ks_vfsp = (caddr_t) fscp->fs_cfsvfsp;
	key->ks_mounted = 1;

	/*
	 * we must not be holding any mutex that is a ks_lock field
	 * for one of the kstats when we invoke kstat_create,
	 * kstat_install, and friends.
	 */
	ASSERT(! MUTEX_HELD(&cachefs_kstat_key_lock));
	/* really should be EVERY cachep's c_log_mutex */
	ASSERT(! MUTEX_HELD(&cachep->c_log_mutex));

	/* cachefs.#.log */
	ksp = kstat_create("cachefs", fscp->fs_kstat_id, "log",
	    "misc", KSTAT_TYPE_RAW, 1,
	    KSTAT_FLAG_WRITABLE | KSTAT_FLAG_VIRTUAL);
	if (ksp != NULL) {
		ksp->ks_data = cachep->c_log_ctl;
		ksp->ks_data_size = sizeof (cachefs_log_control_t);
		ksp->ks_lock = &cachep->c_log_mutex;
		ksp->ks_snapshot = cachefs_log_kstat_snapshot;
		kstat_install(ksp);
	}
	/* cachefs.#.stats */
	ksp = kstat_create("cachefs", fscp->fs_kstat_id, "stats",
	    "misc", KSTAT_TYPE_RAW, 1,
	    KSTAT_FLAG_WRITABLE | KSTAT_FLAG_VIRTUAL);
	if (ksp != NULL) {
		ksp->ks_data = fscp;
		ksp->ks_data_size = sizeof (cachefs_stats_t);
		ksp->ks_snapshot = cachefs_stats_kstat_snapshot;
		kstat_install(ksp);
	}

out:
	if (mountpoint != NULL)
		cachefs_kmem_free(mountpoint, MAXPATHLEN);
	if (backfs != NULL)
		cachefs_kmem_free(backfs, MAXPATHLEN);
	if (cachedir != NULL)
		cachefs_kmem_free(cachedir, MAXPATHLEN);
}

void
cachefs_kstat_umount(int ksid)
{
	cachefs_kstat_key_t *k = cachefs_kstat_key + (ksid - 1);
	kstat_t *stats, *log;

	ASSERT(k->ks_id == ksid);

	k->ks_mounted = 0;

	mutex_enter(&kstat_chain_lock);
	stats = kstat_lookup_byname("cachefs", ksid, "stats");
	log = kstat_lookup_byname("cachefs", ksid, "log");
	mutex_exit(&kstat_chain_lock);

	if (stats != NULL)
		kstat_delete(stats);
	if (log != NULL)
		kstat_delete(log);
}

int
cachefs_kstat_key_update(kstat_t *ksp, int rw)
{
	cachefs_kstat_key_t *key = *((cachefs_kstat_key_t **) ksp->ks_data);
	cachefs_kstat_key_t *k;
	int i;

	if (rw == KSTAT_WRITE)
		return (EIO);
	if (key == NULL)
		return (EIO);

	ksp->ks_data_size = cachefs_kstat_key_n * sizeof (*key);
	for (i = 0; i < cachefs_kstat_key_n; i++) {
		k = key + i;

		ksp->ks_data_size += strlen(k->ks_mountpoint) + 1;
		ksp->ks_data_size += strlen(k->ks_backfs) + 1;
		ksp->ks_data_size += strlen(k->ks_cachedir) + 1;
		ksp->ks_data_size += strlen(k->ks_cacheid) + 1;
	}

	ksp->ks_ndata = cachefs_kstat_key_n;

	return (0);
}

int
cachefs_kstat_key_snapshot(kstat_t *ksp, void *buf, int rw)
{
	cachefs_kstat_key_t *key = *((cachefs_kstat_key_t **) ksp->ks_data);
	cachefs_kstat_key_t *k;
	caddr_t s;
	int i;

	if (rw == KSTAT_WRITE)
		return (EIO);

	if (key == NULL)
		return (0); /* paranoid */

	bcopy((caddr_t) key, (caddr_t) buf,
	    cachefs_kstat_key_n * sizeof (*key));

	key = (cachefs_kstat_key_t *) buf;
	s = (caddr_t) buf + cachefs_kstat_key_n * sizeof (*key);

	for (i = 0; i < cachefs_kstat_key_n; i++) {
		k = key + i;

		(void) strcpy(s, k->ks_mountpoint);
		k->ks_mountpoint = (char *)(s - (int) buf);
		s += strlen(s) + 1;
		(void) strcpy(s, k->ks_backfs);
		k->ks_backfs = (char *)(s - (int) buf);
		s += strlen(s) + 1;
		(void) strcpy(s, k->ks_cachedir);
		k->ks_cachedir = (char *)(s - (int) buf);
		s += strlen(s) + 1;
		(void) strcpy(s, k->ks_cacheid);
		k->ks_cacheid = (char *)(s - (int) buf);
		s += strlen(s) + 1;
	}

	return (0);
}

extern void  cachefs_inactivate();

#ifdef CFSDEBUG
static void
printbusy(struct fscache *fscp)
{
	register int i;
	register struct cnode *cp;

	printf("busy cnodes in fscache %8x\n", (int) fscp->fs_cfsid);
	for (i = 0; i < CNODE_BUCKET_SIZE; i++) {
		for (cp = fscp->fs_cnode[i]; cp != NULL; cp = cp->c_hash) {
			if ((cp->c_flags &
			    (CN_ROOT|CN_HASHSKIP|
			    CN_DESTROY|CN_INACTIVE)) == 0) {
				printf("cnode %x: count: %lu, fileno: %lu\n",
				    (int) cp, CTOV(cp)->v_count, cp->c_fileno);
			}
		}
	}
}
#endif /* CFSDEBUG */

static int
cachefs_unmount(vfs_t *vfsp, cred_t *cr)
{
	fscache_t *fscp = VFS_TO_FSCACHE(vfsp);
	struct cachefscache *cachep = fscp->fs_cache;
	int error;
	int xx;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VFSOP)
		printf("cachefs_unmount: ENTER fscp %x\n", (int) fscp);
#endif

	if (!suser(cr)) {
		if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_UMOUNT))
			cachefs_log_umount(cachep, EPERM, vfsp);
		return (EPERM);
	}

	/*
	 * wait for the cache-wide async queue to drain.  Someone
	 * here may be trying to sync our fscache...
	 */
	while (cachefs_async_halt(&fscp->fs_cache->c_workq, 0) == EBUSY) {
#ifdef CFSDEBUG
		printf("unmount: waiting for cache async queue...\n");
#endif
	}

	error = cachefs_async_halt(&fscp->fs_workq, 1);
	if (error) {
#ifdef CFSDEBUG
		printf("cachefs_unmount: cachefs_async_halt error %d\n", error);
#endif
		if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_UMOUNT))
			cachefs_log_umount(cachep, error, vfsp);

		return (error);
	}

	/*
	 * No active cnodes on this cache && rootvp refcnt == 1
	 */
	mutex_enter(&fscp->fs_fslock);
	if (fscp->fs_vnoderef > 1 || fscp->fs_rootvp->v_count != 1) {

#ifdef CFSDEBUG
		if (fscp->fs_vnoderef > 1 && cachefsdebug)
			printbusy(fscp);
#endif	/* CFSDEBUG */

		mutex_exit(&fscp->fs_fslock);

		/*
		 * make a try at writing the clean flag
		 * in case we're here in a shutdown/reboot case
		 */
		cachefs_cache_sync(fscp->fs_cache);
		if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_UMOUNT))
			cachefs_log_umount(cachep, EBUSY, vfsp);

		return (EBUSY);
	}
	mutex_exit(&fscp->fs_fslock);

	/* get rid of the root cnode */
	cachefs_inactivate(VTOC(fscp->fs_rootvp), cr);

	/* get rid of any inactive cnodes on the fscp hash list */
	fscache_sync(fscp, 1);

	/* lock out other unmounts and mount */
	mutex_enter(&cachefs_cachelock);

	/* get rid of the last reference to the file system */
	fscache_rele(fscp);

	/* mark the file system as not mounted */
	mutex_enter(&fscp->fs_fslock);
	ASSERT(fscp->fs_vnoderef == 0);
	fscp->fs_flags &= ~CFS_FS_MOUNTED;
	fscp->fs_rootvp = NULL;
	cachefs_kstat_umount(fscp->fs_kstat_id);
	mutex_exit(&fscp->fs_fslock);

	/* get the number of mounts on this cache */
	mutex_enter(&cachep->c_fslistlock);
	xx = fscache_list_mounted(cachep);
	mutex_exit(&cachep->c_fslistlock);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_UMOUNT))
		cachefs_log_umount(cachep, 0, vfsp);

	/* if no mounts left, deactivate the cache */
	if (xx == 0)
		cachefs_delete_cachep(cachep);

	cachefs_module_keepcnt--;
	mutex_exit(&cachefs_cachelock);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_VFSOP)
		printf("cachefs_unmount: EXIT\n");
#endif
	return (0);
}

/*
 * remove the cache from the list of caches
 */

static void
cachefs_delete_cachep(cachefscache_t *cachep)
{
	struct cachefscache **cachepp;
	int found = 0;

	ASSERT(MUTEX_HELD(&cachefs_cachelock));

	for (cachepp = &cachefs_cachelist;
	    *cachepp != NULL;
	    cachepp = &(*cachepp)->c_next) {
		if (*cachepp == cachep) {
			*cachepp = cachep->c_next;
			found++;
			break;
		}
	}
	ASSERT(found);

	/* shut down the cache */
	cachefs_cache_destroy(cachep);
}

static int
cachefs_root(vfs_t *vfsp, vnode_t **vpp)
{
	/*LINTED alignment okay*/
	struct fscache *fscp = (struct fscache *)vfsp->vfs_data;

	ASSERT(fscp != NULL);
	ASSERT(fscp->fs_rootvp != NULL);
	*vpp = fscp->fs_rootvp;
	VN_HOLD(*vpp);
	return (0);
}

/*
 * Get file system statistics.
 */
static int
cachefs_statvfs(register vfs_t *vfsp, struct statvfs *sbp)
{
	struct statvfs sback;
	struct fscache *fscp = VFS_TO_FSCACHE(vfsp);
	int error;

	error = VFS_STATVFS(fscp->fs_backvfsp, &sback);
	if (error)
		return (error);

	sbp->f_fsid = sback.f_fsid;  /* XXX: ??? */
	bcopy((caddr_t)&sback, (caddr_t)sbp, sizeof (struct statvfs));

	/*
	 * Make sure fstype is CFS.
	 */
	strcpy(sbp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name);
	bzero(sbp->f_fstr, sizeof (sbp->f_fstr));

	return (0);
}

/*
 * queue a request to sync the given fscache
 */
static void
queue_sync(struct cachefscache *cachep, cred_t *cr)
{
	struct cachefs_req *rp;
	int error;

	rp = (struct cachefs_req *)cachefs_kmem_zalloc(
		    /*LINTED alignment okay*/
		    sizeof (struct cachefs_req), KM_SLEEP);
	mutex_init(&rp->cfs_req_lock, "CFS Request Mutex",
			    MUTEX_DEFAULT, NULL);
	rp->cfs_cmd = CFS_CACHE_SYNC;
	rp->cfs_cr = cr;
	rp->cfs_req_u.cu_fs_sync.cf_cachep = cachep;
	crhold(rp->cfs_cr);
	error = cachefs_addqueue(rp, &cachep->c_workq);
	ASSERT(error == 0);
}

/*ARGSUSED*/
static int
cachefs_sync(vfs_t *vfsp, short flag, cred_t *cr)
{
	struct fscache *fscp;
	struct cachefscache *cachep;

	if (!(flag & SYNC_ATTR)) {
		/*
		 * queue an async request to do the sync.
		 * We always sync an entire cache (as opposed to an
		 * individual fscache) so that we have an opportunity
		 * to set the clean flag.
		 */
		if (vfsp) {
			/*LINTED alignment okay*/
			fscp = (struct fscache *)vfsp->vfs_data;
			queue_sync(fscp->fs_cache, cr);
		} else {
			mutex_enter(&cachefs_cachelock);
			for (cachep = cachefs_cachelist; cachep != NULL;
			    cachep = cachep->c_next) {
				queue_sync(cachep, cr);
			}
			mutex_exit(&cachefs_cachelock);
		}
	}
	return (0);
}

/*ARGSUSED*/
static int
cachefs_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp)
{
	int error;
	/*LINTED alignment okay*/
	struct fscache *fscp = (struct fscache *)vfsp->vfs_data;
	/*LINTED alignment okay*/
	struct cachefs_fid *cfsfidp = (struct cachefs_fid *)fidp;

	struct vnode *backvp;
	struct cnode *cp;
	struct vattr va;

	*vpp = NULL;
	error = VFS_VGET(fscp->fs_backvfsp, &backvp, fidp);
	if (error)
		return (error);
	if (backvp == NULL)
		return (0);
	va.va_mask = AT_ALL;
	error = VOP_GETATTR(backvp, &va, 0, kcred);
	if (error) {
		VN_RELE(backvp);
		return (error);
	}
	error = makecachefsnode(va.va_nodeid, fscp, fidp, backvp,
		kcred, 0, &cp);
	if (error) {
		VN_RELE(backvp);
		return (error);
	}
	*vpp = CTOV(cp);
	return (error);
}

struct vfs *cachefs_frontrootvfsp;	/* XXX: kluge for convert_mount */

static int
cachefs_mountroot_init(struct vfs *vfsp)
{
	char *backfsdev, *front_dev, *front_fs;
	struct vfs *frontvfsp, *backvfsp;
	struct vnode *frontrootvp, *backrootvp, *cachedirvp;
	dev_t mydev;
	struct vfssw *fvsw, *bvsw;
	struct vattr va;
	cachefscache_t *cachep;
	fscache_t *fscp;
	cnode_t *rootcp;
	struct fid cookie;
	ino_t fsid;
	int error = 0;
	int cnflag = 0;
	int foundcache = 0;

	backfsdev = backfs.bo_name;
	front_dev = frontfs.bo_name;
	front_fs = frontfs.bo_fstype;

	frontvfsp = NULL;
	backvfsp = NULL;
	fscp = NULL;
	cachep = NULL;

	/*
	 * The rule here is as follows:
	 * If frontfs.bo_name is not null,
	 *	frontfs.bo_name and frontfs.bo_fstype are valid
	 * else
	 *	we don't have a frontfs
	 */
	if (*front_dev == '\0') {
		/*
		 * we don't have a cache.  Fire up CFS
		 * in "all-miss/no-fill" mode for now.
		 * Some time later, bcheckrc will give us
		 * a front filesystem and remount root as
		 * a writeable fs.
		 */
		cnflag = CN_NOCACHE;
	} else {
		/*
		 * booted off the frontfs.  Here we use the
		 * cache in "read-only/no-fill" mode until
		 * root is remounted after cachefs_fsck runs.
		 *
		 * XXX: we assume that frontfs is always UFS for now.
		 */
		ASSERT(front_dev[0] != '\0');
		frontvfsp = (struct vfs *)
		    /*LINTED alignment okay*/
		    cachefs_kmem_zalloc(sizeof (struct vfs), KM_SLEEP);
		RLOCK_VFSSW();
		fvsw = vfs_getvfsswbyname(front_fs);
		if (fvsw) {
			VFS_INIT(frontvfsp, fvsw->vsw_vfsops, NULL);
			error = VFS_MOUNTROOT(frontvfsp, ROOT_FRONTMOUNT);
		} else {
			error = ENOTTY;
		}
		RUNLOCK_VFSSW();
		if (error)
			goto out;
	}

	backvfsp = (struct vfs *) cachefs_kmem_zalloc(
	    /*LINTED alignment okay*/
	    sizeof (struct vfs), KM_SLEEP);
	RLOCK_VFSSW();
	bvsw = vfs_getvfsswbyname(backfs.bo_fstype);
	if (bvsw) {
		strcpy(rootfs.bo_name, backfsdev);
		VFS_INIT(backvfsp, bvsw->vsw_vfsops, NULL);
		error = VFS_MOUNTROOT(backvfsp, ROOT_BACKMOUNT);
	} else {
		error = ENOTTY;
	}
	RUNLOCK_VFSSW();
	if (error)
		goto out;

	error = VFS_ROOT(backvfsp, &backrootvp);
	if (error)
		goto out;

	cachep = NULL;
	if (frontvfsp && (VFS_ROOT(frontvfsp, &frontrootvp) == 0)) {
		/* create a cache object in NOCACHE and NOFILL mode */
		cachep = cachefs_cache_create();
		if (cachep == NULL) {
			error = ENOMEM;
			goto out;
		}

		/*
		 * find cache info from front fs and call
		 * activate_cache.
		 *
		 * For now we insist on the convention of
		 * finding the root cache at "/rootcache"
		 * in the frontfs.
		 */
		error = VOP_LOOKUP(frontrootvp, "rootcache",
		    &cachedirvp, (struct pathname *)NULL, 0,
		    (vnode_t *)NULL, kcred);
		VN_RELE(frontrootvp);
		if (error) {
			printf("can't find '/rootcache' in frontfs!\n");
			goto done;
		} else if (cachedirvp->v_type != VDIR) {
			printf("cachefs_mountroot:/rootcache not a dir!\n");
			goto done;
		}

		/* take the cache out of NOCACHE mode, leave in NOFILL */
		error = cachefs_cache_activate_ro(cachep, cachedirvp);
		if (error)
			goto done;

		/* get the fsid for the fscache */
		error = fscache_name_to_fsid(cachep, "rootcache", &fsid);
		if (error)
			goto done;

		/* create the fscache object */
		fscp = fscache_create(cachep);

		mutex_enter(&cachep->c_fslistlock);
		error = fscache_activate(fscp, fsid, NULL, NULL);
		if (error) {
			mutex_exit(&cachep->c_fslistlock);
			fscache_destroy(fscp);
			fscp = NULL;
			goto done;
		}
		fscache_list_add(cachep, fscp);
		mutex_exit(&cachep->c_fslistlock);
		foundcache = 1;
		printf("cachefs: booting from a clean cache.\n");
	}
done:

	if (!foundcache) {
		/* destroy the old cache object if it exists */
		if (cachep)
			cachefs_cache_destroy(cachep);

		/* create a cache object in NOCACHE and NOFILL mode */
		cachep = cachefs_cache_create();
		if (cachep == NULL) {
			error = ENOMEM;
			goto out;
		}

		/* create the fscache object and put in on the list */
		fscp = fscache_create(cachep);
		mutex_enter(&cachep->c_fslistlock);
		fscache_list_add(cachep, fscp);
		mutex_exit(&cachep->c_fslistlock);
	}

	/* mark file system as mounted */
	error = fscache_mounted(fscp, vfsp, backvfsp);
	ASSERT(error == 0);

	vfsp->vfs_data = (caddr_t)fscp;
	mydev = cachefs_mkmntdev();
	vfsp->vfs_dev = mydev;
	vfsp->vfs_fsid.val[0] = mydev;
	vfsp->vfs_fsid.val[1] = cachefsfstyp;
	vfsp->vfs_fstype = cachefsfstyp;
	vfsp->vfs_bsize = MAXBSIZE;
	error = cachefs_getcookie(backrootvp, &cookie, &va, kcred);
	if (error == 0) {
		error = makecachefsnode(va.va_nodeid, fscp, &cookie,
		    backrootvp, kcred, cnflag, &rootcp);
	}
	if (error)
		goto out;

	mutex_enter(&fscp->fs_fslock);
	fscp->fs_rootvp = CTOV(rootcp);
	fscp->fs_rootvp->v_flag |= VROOT;
	fscp->fs_rootvp->v_type |= rootcp->c_attr.va_type;
	ASSERT(fscp->fs_rootvp->v_type == VDIR);
	mutex_exit(&fscp->fs_fslock);

out:
	if (error) {
		/*
		 * XXX: do we need to unmount and stuff?  We're
		 * just going to reboot anyway aren't we?
		 */
		if (frontvfsp)
			cachefs_kmem_free((caddr_t)frontvfsp,
				sizeof (struct vfs));
		if (backvfsp)
			cachefs_kmem_free((caddr_t)backvfsp,
				sizeof (struct vfs));
		if (fscp) {
			mutex_enter(&cachep->c_fslistlock);
			fscache_list_remove(cachep, fscp);
			mutex_exit(&cachep->c_fslistlock);
			fscache_destroy(fscp);
		}
		if (cachep) {
			cachefs_cache_destroy(cachep);
		}
	} else {
		cachefs_cachelist = cachep;
		cachefs_frontrootvfsp = frontvfsp;
		cachefs_module_keepcnt++;
	}
	return (error);
}

static int
cachefs_mountroot_unmount(vfs_t *vfsp)
{
	fscache_t *fscp = VFS_TO_FSCACHE(vfsp);
	struct cachefscache *cachep = fscp->fs_cache;
	struct vfs *f_vfsp;
	int error = 0;
	int sync_flag = 0;
	int tend;

	/*
	 * Try to write the cache clean flag now in case we do not
	 * get another chance.
	 */
	cachefs_cache_sync(cachep);

	/* wait for async threads to stop */
	while (fscp->fs_workq.wq_thread_count > 0)
		(void) cachefs_async_halt(&fscp->fs_workq, 1);
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

	/* if the cache is still dirty, try to make it clean */
	if (cachep->c_usage.cu_flags & CUSAGE_ACTIVE) {
		cachefs_cache_sync(cachep);
		if (cachep->c_usage.cu_flags & CUSAGE_ACTIVE) {
#ifdef CFSDEBUG
			printf("cachefs root: cache is dirty\n");
#endif
			error = 1;
		/*LINTED printf below...*/
		} else {
#ifdef CFSDEBUG
			printf("cachefs root: cache is clean\n");
#endif
		}
	/*LINTED printf below...*/
	} else {
#ifdef CFSDEBUG
		printf("cachefs root: cache is pristine\n");
#endif
	}

	if (cachep->c_resfilevp) {
		f_vfsp = cachep->c_resfilevp->v_vfsp;
		VFS_SYNC(f_vfsp, sync_flag, kcred);
		VFS_SYNC(f_vfsp, sync_flag, kcred);
		VFS_SYNC(f_vfsp, sync_flag, kcred);
	}
	cachefs_module_keepcnt--;
	return (error);
}

/*ARGSUSED*/
static int
cachefs_mountroot(vfs_t *vfsp, whymountroot_t why)
{
	int error;

	switch (why) {
	case ROOT_INIT:
		error = cachefs_mountroot_init(vfsp);
		break;
	case ROOT_UNMOUNT:
		error = cachefs_mountroot_unmount(vfsp);
		break;
	default:
		error = ENOSYS;
		break;
	}
	return (error);
}

/*ARGSUSED*/
static int
cachefs_swapvp(vfs_t *vfsp, vnode_t **vpp, char *nm)
{
	return (ENOSYS);
}

static int
cachefs_remount(struct vfs *vfsp, struct mounta *uap)
{
	int error = 0;
	struct cachefs_mountargs *map;
	struct vnode *cachedirvp;
	fscache_t *fscp;
	cachefscache_t *cachep;
	ino_t fsid;

	map = (struct cachefs_mountargs *)cachefs_kmem_alloc(
		/*LINTED alignment okay*/
		sizeof (*map), KM_SLEEP);
	error = copyin((caddr_t)uap->dataptr, (caddr_t)map, sizeof (*map));
	if (error)
		goto out;

	/*
	 * get cache directory vp
	 */
	error = lookupname(map->cfs_cachedir, UIO_USERSPACE, FOLLOW,
	    NULLVPP, &cachedirvp);
	if (error)
		goto out;
	if (cachedirvp->v_type != VDIR) {
		error = EINVAL;
		goto out;
	}

	/*
	 * XXX: for now we assume that the only thing we ever remount
	 * is root, so we just take the first (and presumably only)
	 * thing we find on cachefs_cachelist.  It should have a single
	 * fscache associated with it...
	 */
	cachep = cachefs_cachelist;
	if (vfsp != rootvfs ||
	    (cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0) {
		error = EINVAL;
		goto out;
	}
	ASSERT(cachep->c_next == NULL);
	fscp = cachep->c_fslist;
	ASSERT(fscp->fs_next == NULL);

	if (cachep->c_flags & CACHE_NOCACHE) {
		error = cachefs_cache_activate_ro(cachep, cachedirvp);
		if (error)
			goto out;
		cachefs_cache_activate_rw(cachep);

		/* get the fsid for the fscache */
		error = fscache_name_to_fsid(cachep, map->cfs_cacheid, &fsid);
		if (error)
			fsid = 0;

		/* activate the fscache */
		mutex_enter(&cachep->c_fslistlock);
		error = fscache_enable(fscp, fsid, map->cfs_cacheid,
			&map->cfs_options);
		mutex_exit(&cachep->c_fslistlock);
		if (error) {
			/* XXX probably should just panic */
			cmn_err(CE_WARN, "cachefs: cannot remount %s\n",
				map->cfs_cacheid);
			goto out;
		}
	} else {
		ASSERT(cachep->c_flags & CACHE_NOFILL);
		cachefs_cache_activate_rw(cachep);
	}

	fscache_acset(fscp, map->cfs_acregmin, map->cfs_acregmax,
	    map->cfs_acdirmin, map->cfs_acdirmax);

	/* enable the cache */
	cachefs_enable_caching(fscp);
	fscache_activate_rw(fscp);

	cachefs_kstat_mount(fscp,
	    uap->dir, map->cfs_backfs, map->cfs_cachedir, map->cfs_cacheid);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_MOUNT))
		cachefs_log_mount(cachep, error, vfsp,
		    (fscp != NULL) ? &fscp->fs_options : NULL,
		    uap->dir, UIO_USERSPACE,
		    (map != NULL) ? map->cfs_cacheid : NULL);

out:
	return (error);
}
