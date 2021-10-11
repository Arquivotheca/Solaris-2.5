/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_FS_CACHEFS_FSCACHE_H
#define	_SYS_FS_CACHEFS_FSCACHE_H

#pragma ident	"@(#)cachefs_fscache.h	1.10	94/08/25 SMI"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cachefs_stats {
	u_int	st_hits;
	u_int	st_misses;
	u_int	st_passes;
	u_int	st_fails;
	u_int	st_modifies;
	u_int	st_gc_count;
	time_t	st_gc_time;
	time_t	st_gc_before_atime;
	time_t	st_gc_after_atime;
} cachefs_stats_t;

#define	CNODE_BUCKET_SIZE	64

/*
 * fscache structure contains per-filesystem information, both filesystem
 * cache directory information and mount-specific information.
 */
struct fscache {
	ino_t			 fs_cfsid;	/* File system ID */
	int			 fs_flags;
	struct vnode		*fs_fscdirvp;	/* vp to fs cache dir */
	struct vnode		*fs_fsattrdir;	/* vp to attrcache dir */
	struct cachefscache	*fs_cache;	/* back ptr to cache struct */
	struct cachefsoptions	 fs_options;	/* mount options */
	struct filegrp		*fs_filegrp;	/* head of filegrp list */
	struct vfs		*fs_cfsvfsp;	/* cfs vfsp */
	struct vfs		*fs_backvfsp;	/* back file system vfsp */
	struct vnode		*fs_rootvp;	/* root vnode ptr */
	int			 fs_vnoderef;	/* activate vnode ref count */
	struct cachefsops	*fs_cfsops;	/* cfsops vector pointer */
	u_long			 fs_acregmin;	/* same as nfs values */
	u_long			 fs_acregmax;
	u_long			 fs_acdirmin;
	u_long			 fs_acdirmax;
	struct fscache		*fs_next;	/* ptr to next fscache */
	struct cachefs_workq	 fs_workq;	/* async thread work queue */
	struct cnode		*fs_cnode[CNODE_BUCKET_SIZE]; /* hash buckets */
	kmutex_t		 fs_fslock;
	/* protect contents - everyting xcept the cnode hash */
	kmutex_t		 fs_cnodelock;	/* protects the cnode hash */
	timestruc_t		 fs_cod_time;	/* time of CoD event */
	int			 fs_kstat_id;
	cachefs_stats_t		 fs_stats;
};
typedef struct fscache fscache_t;

/* valid fscache flags */
#define	CFS_FS_MOUNTED		1	/* fscache is mounted */
#define	CFS_FS_READ		2	/* fscache can be read */
#define	CFS_FS_WRITE		4	/* fscache can be written */

#define	CFSOP_INIT_COBJECT(FSCP, CP, CR)	\
	(*(FSCP)->fs_cfsops->co_init_cobject)(FSCP, CP, CR)
#define	CFSOP_CHECK_COBJECT(FSCP, CP, WHAT, TYPE, CR)	\
	(*(FSCP)->fs_cfsops->co_check_cobject)(FSCP, CP, WHAT, TYPE, CR)
#define	CFSOP_MODIFY_COBJECT(FSCP, CP, CR)	\
	(*(FSCP)->fs_cfsops->co_modify_cobject)(FSCP, CP, CR)
#define	CFSOP_INVALIDATE_COBJECT(FSCP, CP, CR)	\
	(*(FSCP)->fs_cfsops->co_invalidate_cobject)(FSCP, CP, CR)

#define	C_ISFS_WRITE_AROUND(FSCP) \
	((FSCP)->fs_options.opt_flags & CFS_WRITE_AROUND)
#define	C_ISFS_STRICT(FSCP) \
	(((FSCP)->fs_options.opt_flags & CFS_WRITE_AROUND) && \
	(((FSCP)->fs_options.opt_flags & \
		(CFS_NOCONST_MODE | CFS_CODCONST_MODE)) == 0))
#define	C_ISFS_SINGLE(FSCP) \
	((FSCP)->fs_options.opt_flags & CFS_DUAL_WRITE)
#define	C_ISFS_NOCONST(FSCP) \
	((FSCP)->fs_options.opt_flags & CFS_NOCONST_MODE)
#define	C_ISFS_CODCONST(FSCP) \
	((FSCP)->fs_options.opt_flags & CFS_CODCONST_MODE)

fscache_t *fscache_create(cachefscache_t *cachep);
void fscache_destory(fscache_t *fscp);
int fscache_activate(fscache_t *fscp, ino_t fsid, char *namep,
	struct cachefsoptions *optp);
int fscache_enable(fscache_t *fscp, ino_t fsid, char *namep,
	struct cachefsoptions *optp);
void fscache_activate_rw(fscache_t *fscp);
void fscache_hold(fscache_t *fscp);
void fscache_rele(fscache_t *fscp);
int fscache_mounted(fscache_t *fscp, struct vfs *cfsvfsp, struct vfs *backvfsp);
int fscache_compare_options(struct cachefsoptions *opoldp,
    struct cachefsoptions *opnewp);
void fscache_sync(fscache_t *fscp, int unmount);
void fscache_acset(fscache_t *fscp,
	u_long acregmin, u_long acregmax, u_long acdirmin, u_long acdirmax);

fscache_t *fscache_list_find(cachefscache_t *cachep, ino_t fsid);
void fscache_list_add(cachefscache_t *cachep, fscache_t *fscp);
void fscache_list_remove(cachefscache_t *cachep, fscache_t *fscp);
void fscache_list_gc(cachefscache_t *cachep);
int fscache_list_mounted(cachefscache_t *cachep);

int fscache_name_to_fsid(cachefscache_t *cachep, char *namep, ino_t *fsidp);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FS_CACHEFS_FSCACHE_H */
