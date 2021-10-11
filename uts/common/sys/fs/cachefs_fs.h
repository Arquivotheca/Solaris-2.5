/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_FS_CACHEFS_FS_H
#define	_SYS_FS_CACHEFS_FS_H

#pragma ident	"@(#)cachefs_fs.h	1.69	95/06/20 SMI"

#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/thread.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Some default values */
#define	DEF_FILEGRP_SIZE	256
#define	DEF_POP_SIZE		0x10000		/* 64K */
#define	CFSVERSION		3
#define	CACHELABEL_NAME		".cfs_label"
#define	RESOURCE_NAME		".cfs_resource"
#define	OPTION_NAME		".cfs_option"
#define	ATTRCACHE_NAME		".cfs_attrcache"
#define	BACKMNT_NAME		".cfs_mnt_points"
#define	CACHEFS_LOCK_FILE	".cfs_lock"
#define	LOG_STATUS_NAME		".cfs_logging"
#define	NOBACKUP_NAME		".nsr"
#define	ROOTLINK_NAME		"root"
#define	CFS_FRONTFILE_NAME_SIZE	9
#define	CACHEFS_BASETYPE	"cachefs" /* used in statvfs() */

/*
 * The options structure is passed in as part of the mount arguments.
 * It is stored in the .options file and kept track of in the fscache
 * structure.
 */
struct cachefsoptions {
	u_int		opt_flags;		/* mount flags */
	int		opt_popsize;		/* cache population size */
	int		opt_fgsize;		/* filegrp size, default 256 */
};

typedef struct cachefscache cachefscache_t;

/*
 * all the stuff needed to manage a queue of requests to be processed
 * by async threads.
 */
struct cachefs_workq {
	struct cachefs_req	*wq_head;		/* head of work q */
	struct cachefs_req	*wq_tail;		/* tail of work q */
	int			wq_length;		/* # of requests on q */
	int			wq_thread_count;	/* # of threads */
	int			wq_max_len;		/* longest queue */
	unsigned int		wq_halt_request:1;	/* halt requested */
	unsigned int		wq_keepone:1;		/* keep one thread */
	unsigned int		wq_logwork:1;		/* write logfile */
	kcondvar_t		wq_req_cv;		/* wait on work to do */
	kcondvar_t		wq_halt_cv;		/* wait/signal halt */
	kmutex_t		wq_queue_lock;		/* protect queue */
	cachefscache_t		*wq_cachep;		/* sometimes NULL */
};

#include <sys/fs/cachefs_fscache.h>
#include <sys/fs/cachefs_filegrp.h>

/*
 * One cache_label structure per cache. Contains mainly user defined or
 * default values for cache resource management. Contents is static.
 */
struct cache_label {
	int	cl_cfsversion;	/* cfs version number */
	int	cl_maxblks;	/* max blocks to be used by cache */
	int	cl_blkhiwat;	/* high water-mark for block usage */
	int	cl_blklowat;	/* low water-mark for block usage */
	int	cl_maxinodes;	/* max inodes to be used by cache */
	int	cl_filehiwat;	/* high water-mark for inode usage */
	int	cl_filelowat;	/* low water-mark for indoe usage */
	int	cl_blocktresh;	/* block max usage treshold */
	int	cl_blockmin;	/* block min usage treshold */
	int	cl_filetresh;	/* inode max usage treshold */
	int	cl_filemin;	/* inode min usage treshold */
	int	cl_maxfiles;	/* max cache file size */
};

/*
 * One cache_usage structure per cache. Keeps track of cache usage figures.
 * Contents gets updated frequently.
 */
struct cache_usage {
	int	cu_blksused;	/* actual number of blocks used */
	int	cu_filesused;	/* actual number of files used */
	u_int	cu_flags;	/* Cache state flags */
	u_short cu_unique;	/* Fid persistent uniquifier */
};

#define	CUSAGE_ACTIVE	1	/* Cache is active */
#define	CUSAGE_NEED_ADJUST 2	/* Adjust uniquifier before assigning new fid */

/*
 * Global LRU pointers. One per cache.
 */
struct lru_info {
	int	lru_front;	/* global pointer to front of LRU */
	int	lru_back;	/* global pointer to back of LRU */
	int	lru_free;	/* beginning of free LRU entries */
	int	lru_entries;	/* number of entries allocated in LRU */
	int	active_front;	/* global pointer to front of active list */
	int	active_back;	/* global pointer to back of active list */
};

/*
 * The actual LRU list consists of two arrays: one of lru_idents and one
 * of lru_pointers. This has been done for efficiency. lru_idents does
 * not get updated as often as lru_pointers array. These had better remain
 * a power of two and the same size !!!
 */
struct lru_idents {
	u_int	lru_attrc: 1;	/* 1 if file is an attrcache file */
	u_int	lru_fsid: 31;	/* used to locate fscache */
	u_int	lru_local: 1;	/* Local, pinned, etc. */
	u_int	lru_fileno: 31;	/* fileno of file */
};

struct lru_pointers {
	off_t	lru_fwd_idx;	/* forward pointer */
	off_t	lru_bkwd_idx;	/* backward pointer */
};

/*
 * struct cache contains cache-wide information, and provides access
 * to lower level info. There is one cache structure per cache.
 */
struct cachefscache {
	struct cachefscache	*c_next;	/* list of caches */
	u_int			c_flags;	/* misc flags */
	struct cache_label	c_label;	/* cache resource info */
	struct cache_usage	c_usage;	/* cache usage info */
	struct lru_info		c_lruinfo;	/* lru global pointers */
	struct vnode		*c_resfilevp;	/* resource file vp */
	u_int			c_lru_link_off; /* offset of LRU links */
	u_int			c_lru_idents_off;
	u_int			c_li_window;	/* Entry # of idents window */
	u_int			c_lp_window;	/* Entry # of pointer window */
	struct lru_idents	*c_lru_idents;	/* lru identifiers */
	struct lru_pointers	*c_lru_ptrs;	/* lru pointers */
	struct vnode		*c_dirvp;	/* cache directory vp */
	struct vnode		*c_lockvp;	/* lock file vp */
	int			c_refcnt;	/* active fs ref count */
	struct fscache		*c_fslist;	/* fscache list head */
	struct cachefs_workq	c_workq;	/* async work */
	kmutex_t		c_contentslock; /* protect cache struct */
	kmutex_t		c_fslistlock;	/* protect fscache list */
	u_short			c_unique;	/* In core fid uniquifier */
	kcondvar_t		c_gccv;		/* gc wait on work to do */
	kcondvar_t		c_gchaltcv;	/* wait on gc thread exit */
	u_int			c_gc_count;	/* garbage collection count */
	time_t			c_gc_time;	/* last garbage collection */
	time_t			c_gc_before;	/* atime of front before gc */
	time_t			c_gc_after;	/* atime of front after gc */
	struct cachefs_log_cookie
				*c_log;		/* in-core logging stuff */
	struct cachefs_log_control
				*c_log_ctl;	/* on-disk logging stuff */
	kmutex_t		c_log_mutex;	/* protects c_log* */
};

/*
 * Various cache structure flags.
 */
#define	CACHE_NOCACHE		0x1	/* all cache refs go to back fs */
#define	CACHE_ALLOC_PENDING	0x4	/* Allocation pending */
#define	CACHE_NOFILL		0x8	/* No fill mode */
#define	CACHE_GARBAGE_COLLECT	0x10	/* Garbage collect in progress */
#define	CACHE_GARBAGE_THREADRUN	0x20	/* Garbage collect thread is alive */
#define	CACHE_GARBAGE_THREADEXIT 0x40	/* gc thread should exit */
#define	CACHE_DIRTY		0x80

/*
 * Values for the cachefs options flag.
 */
/*
 * Mount options
 */
#define	CFS_WRITE_AROUND	0x01	/* write-around */
#define	CFS_DUAL_WRITE		0x02	/* write to cache and back file */
#define	CFS_NOCONST_MODE	0x08	/* no-op consistency mode */
#define	CFS_ACCESS_BACKFS	0x10	/* pass VOP_ACCESS to backfs */
#define	CFS_PURGE		0x20	/* force purge of cache */
#define	CFS_AUTOPURGE		0x40	/* purge cache if mismatch */
#define	CFS_CODCONST_MODE	0x80	/* cod consistency mode */

#define	MAXTOKEN_SIZE	32
#define	MAXCOOKIE_SIZE	36

#define	C_VERIFY_ATTRS	0x1
#define	C_BACK_CHECK	0x2

/*
 * The following token is opaque to everything except to the the
 * CFS consistency mechanism.
 */
struct cachefstoken {
	char	ct_data[MAXTOKEN_SIZE];
};

struct cachefs_allocmap {
	int			am_start_off;	/* Start offset of this chunk */
	int			am_size;	/* size of this chunk */
};

#define	C_MAX_ALLOCINFO_SLOTS	32

/*
 * CFS fastsymlinks. For symlink of size < C_FSL_SIZE, the symlink
 * is stored in the cnode allocmap array.
 */
#define	C_FSL_SIZE	(sizeof (struct cachefs_allocmap) * \
			C_MAX_ALLOCINFO_SLOTS)

/*
 * Structure representing a cached object.
 */
struct cachefs_metadata {
	struct vattr		md_vattr;	/* attributes */
	struct fid		md_cookie;	/* back fid */
	int			md_flags;	/* various flags */
	u_int			md_lruno;	/* lru entry for this file */
	struct cachefstoken		md_token;	/* token */
	fid_t			md_fid;		/* Fid of front file */
	u_long			md_frontblks;	/* # blks used in frontfs */
	timestruc_t		md_timestamp;	/* Cache timestamp */
	u_long			md_gen;	/* fid uniquifier */
	int			md_allocents;	/* nbr of entries in allocmap */
	struct cachefs_allocmap	md_allocinfo[C_MAX_ALLOCINFO_SLOTS];
};

/*
 * Various flags to be stored in md_flags field of the metadata.
 */
#define	MD_POPULATED	0x2		/* front file or dir is populated */
#define	MD_FILE		0x4		/* front file or dir exists */
#define	MD_FASTSYMLNK	0x8		/* fast symbolic link */
#define	MD_PINNED	0x10		/* file is pinned */
#define	MD_INVALREADDIR	0x40		/* repopulate on readdir */


#define	C_MAX_MOUNT_FSCDIRNAME		128
/*
 * cachefs mount structure and related data
 */
struct cachefs_mountargs {
	struct cachefsoptions	cfs_options;	/* consistency modes, etc. */
	char			*cfs_fsid;	/* CFS ID fpr file system */
	char			cfs_cacheid[C_MAX_MOUNT_FSCDIRNAME];
	/* CFS fscdir name */
	char			*cfs_cachedir;	/* path for this cache dir */
	char			*cfs_backfs;	/* back filesystem dir */
	u_long			cfs_acregmin;	/* same as nfs values */
	u_long			cfs_acregmax;
	u_long			cfs_acdirmin;
	u_long			cfs_acdirmax;
};


/*
 * struct cachefsops - consistency modules.
 */
struct cachefsops {
	int	(* co_init_cobject)();
	int	(* co_check_cobject)();
	void	(* co_modify_cobject)();
	void	(* co_invalidate_cobject)();
};



/*
 * The attrcache file consists of a attrcache_header structure and an
 * array of attrcache_slot structures (one per front file).
 */

/*
 * Attrcache file format
 *
 *	Header
 *	Offset array (# of entries = file group size)
 *	alloc list	(1 bit per entry, 0 = free) Note that the
 *			file will be extended as needed
 *	attrcache entries
 *
 */
struct attrcache_header {
	u_int		ach_count;		/* number of entries */
	int		ach_nffs;		/* number of front files */
	int		ach_nblks;		/* number of allocated blocks */
	u_int		ach_lruno;		/* lru entry for this file */
};

struct attrcache_index {
	u_int	ach_written:1;		/* 1 if metadata written */
	u_int	ach_offset:31;		/* seek offset to metadata */
};

#define	MAXCNODES		100
#define	C_FRONT			0x1	/* Put cnode at front of free list */
#define	C_BACK			0x2	/* Put cnode at back of free list */
/*
 * cnode structure, one per file. Cnode hash bucket kept in fscache
 * structure below.
 */
#define	c_attr			c_metadata.md_vattr
#define	c_token			c_metadata.md_token
#define	c_cookie		c_metadata.md_cookie

/*
 * LOCKS:	rwlock		Read / Write serialization
 *		statelock	Protects other fields
 */
struct cnode {
	int			c_flags;
	struct cnode		*c_hash;	/* hash list pointer */
	struct cnode		*c_freeback;	/* free list back pointer */
	struct cnode		*c_freefront;	/* free list front pointer */
	struct vnode		*c_frontvp;	/* front vnode pointer */
	struct vnode		*c_backvp;	/* back vnode pointer */
	u_int			c_size;		/* client view of the size */
	struct filegrp		*c_filegrp;	/* back pointer to filegrp */
	u_long			c_fileno;	/* unique file number */
	int			c_invals;	/* # of recent dir invals */
	int			c_usage;	/* Usefulness of cache */
	struct vnode		c_vnode;	/* vnode itself */
	struct cachefs_metadata	c_metadata;	/* cookie, frontvp,... */
	int			c_error;
	int			c_nio;		/* Number of io's pending */
	u_int			c_ioflags;
	kcondvar_t		c_iocv;		/* IO cond var. */
	krwlock_t		c_rwlock;	/* serialize lock */
	krwlock_t		c_statelock;	/* statelock */
	kmutex_t		c_iomutex;
	cred_t			*c_cred;
	int			c_ipending;	/* 1 if inactive is pending */
};

typedef struct cnode cnode_t;

/*
 * Directory caching parameters - First cut...
 */
#define	CFS_DIRCACHE_COST	3
#define	CFS_DIRCACHE_INVAL	3
#define	CFS_DIRCACHE_ENABLE	(CFS_DIRCACHE_INVAL * CFS_DIRCACHE_COST)

/*
 * Conversion macros
 */
#define	VTOC(VP)		((struct cnode *)((void *)((VP)->v_data)))
#define	CTOV(CP)		((vnode_t *)((void *)(&((CP)->c_vnode))))
#define	VFS_TO_FSCACHE(VFSP)	((struct fscache *)((void *)((VFSP)->vfs_data)))
#define	C_TO_FSCACHE(CP)	(VFS_TO_FSCACHE(CTOV(CP)->v_vfsp))

/*
 * Various flags stored in the flags field of the cnode structure.
 */
#define	CN_NOCACHE	0x1		/* no-cache mode */
#define	CN_DESTROY	0x2		/* destroy when inactive */
#define	CN_ROOT		0x4		/* root of the file system */
#define	CN_INACTIVE	0x8		/* file is inactive */
#define	CN_UPDATED	0x40		/* Metadata was updated - needs sync */
#define	CDIRTY		0x80
#define	CN_NEED_FRONT_SYNC	0x100	/* front file needs to be sync'd */
#define	CN_ALLOC_PENDING	0x200	/* Need to alloc attr cache entry */
#define	CN_STALE	0x400		/* back fid is stale */
#define	CFASTSYMLNK	0x800		/* Fast CFS Symlink */
#define	CN_MODIFIED	0x1000		/* Object has been written to */
#define	CN_NEED_INVAL	0x2000		/* Object needs invalidation */
#define	CN_HASHSKIP	0x4000		/* Pretend not on the hash list */
#define	CN_POPULATION_PENDING	0x8000	/* Population data needs to be sync'd */
#define	CN_LRU		0x10000		/* lru entry is on lru list */

/*
 * io flags (in c_ioflag)
 */
#define	CIO_PUTPAGES	0x1		/* putpage pending: off==0, len==0 */

#define	CHASH(FILENO)		((int)(FILENO & (CNODE_BUCKET_SIZE - 1)))

#define	CFS_MAX_THREADS		5
#define	CFS_ASYNC_TIMEOUT	(60 * HZ)

enum cachefs_cmd {
	CFS_CACHE_SYNC,
	CFS_PUTPAGE,
	CFS_INACTIVE,
	CFS_NOOP
};

struct cachefs_fs_sync_req {
	struct cachefscache *cf_cachep;
};

struct cachefs_inactive_req {
	vnode_t *ci_vp;
};

struct cachefs_reep_req {
	cachefscache_t *cr_cachep;
};

struct cachefs_putpage_req {
	vnode_t *cp_vp;
	offset_t cp_off;
	int cp_len;
	int cp_flags;
};

struct cachefs_req {
	struct cachefs_req	*cfs_next;
	enum cachefs_cmd	cfs_cmd;	/* Command to execute */
	cred_t *cfs_cr;
	union {
		struct cachefs_fs_sync_req cu_fs_sync;
		struct cachefs_inactive_req cu_inactive;
		struct cachefs_reep_req cu_reep;
		struct cachefs_putpage_req cu_putpage;
	} cfs_req_u;
	kmutex_t cfs_req_lock;	/* Protects contents */
};

#define	CFS_MAX_EXPORT_SIZE	10
#define	CFS_FID_LOCAL		1
#define	CFS_FID_BACK		2
#define	CFS_FID_SIZE 		(sizeof (struct cachefs_fid) - sizeof (u_short))

struct cachefs_fid {
	u_short cf_len;
	u_short	cf_flag;
	ino_t cf_fileno;
	u_int cf_gen;
};

struct cachefs_cnvt_mnt {
	int	cm_op;
	int	cm_namelen;
	char	*cm_name;
};
#define	CFS_CM_FRONT	1
#define	CFS_CM_BACK	2

struct cachefs_boinfo {
	int	boi_which;	/* one of CFS_BOI_* below */
	int	boi_device;	/* return bo_name, not bo_fstype */
	int	boi_len;	/* size of boi_value */
	char	*boi_value;	/* return value stored here */
};
#define	CFS_BOI_ROOTFS	1
#define	CFS_BOI_FRONTFS	2
#define	CFS_BOI_BACKFS	3

/*
 *
 * cachefs kstat stuff.  each time you mount a cachefs filesystem, it
 * gets a unique number.  it'll get that number again if you remount
 * the same thing.  the number is unique until reboot, but it doesn't
 * survive reboots.
 *
 * each cachefs kstat uses this per-filesystem identifier.  to get the
 * valid identifiers, the `cachefs.0.key' kstat has a mapping of all
 * the available filesystems.  its structure, cachefs_kstat_key, is
 * below.
 *
 */

typedef struct cachefs_kstat_key {
	int ks_id;
	int ks_mounted;
	caddr_t ks_vfsp;
	char *ks_mountpoint;
	char *ks_backfs;
	char *ks_cachedir;
	char *ks_cacheid;
} cachefs_kstat_key_t;
extern cachefs_kstat_key_t *cachefs_kstat_key;
extern int cachefs_kstat_key_n;

/*
 * cachefs debugging aid.  cachefs_debug_info_t is a cookie that we
 * can keep around to see what was happening at a certain time.
 *
 * for example, if we have a deadlock on the cnode's statelock
 * (i.e. someone is not letting go of it), we can add a
 * cachefs_debug_info_t * to the cnode structure, and call
 * cachefs_debug_save() whenever we grab the lock.  then, when we're
 * deadlocked, we can see what was going on when we grabbed the lock
 * in the first place, and (hopefully) why we didn't release it.
 *
 */

#define	CACHEFS_DEBUG_DEPTH		(16)
typedef struct cachefs_debug_info {
	char		*cdb_message;	/* arbitrary message */
	u_int		cdb_flags;	/* arbitrary flags */
	int		cdb_int;	/* arbitrary int */
	void		*cdb_pointer;	/* arbitrary pointer */
	u_int		cdb_count;	/* how many times called */

	cachefscache_t	*cdb_cachep;	/* relevant cachep (maybe undefined) */
	struct fscache	*cdb_fscp;	/* relevant fscache */
	struct cnode	*cdb_cnode;	/* relevant cnode */
	vnode_t		*cdb_frontvp;	/* relevant front vnode */
	vnode_t		*cdb_backvp;	/* relevant back vnode */

	kthread_id_t	cdb_thread;	/* thread who called */
	hrtime_t	cdb_timestamp;	/* when */
	int		cdb_depth;	/* depth of saved stack */
	u_int		cdb_stack[CACHEFS_DEBUG_DEPTH]; /* stack trace */
	struct cachefs_debug_info *cdb_next; /* pointer to next */
} cachefs_debug_info_t;

/*
 * cachefs function prototypes
 */
#if defined(_KERNEL) && defined(__STDC__)
extern int cachefs_getcookie(vnode_t *, struct fid *, struct vattr *,
		cred_t *);
cachefscache_t *cachefs_cache_create(void);
void cachefs_cache_destroy(cachefscache_t *cachep);
int cachefs_cache_activate_ro(cachefscache_t *cachep, vnode_t *cdvp);
void cachefs_cache_activate_rw(cachefscache_t *cachep);
void cachefs_cache_dirty(struct cachefscache *cachep, int lockit);
int cachefs_cache_rssync(struct cachefscache *cachep);
void cachefs_cache_sync(struct cachefscache *cachep);
u_int cachefs_cache_unique(cachefscache_t *cachep);
void cachefs_do_req(struct cachefs_req *);

/* cachefs_cnode.c */
void cachefs_remhash(struct cnode *);
void cachefs_remfree(struct cnode *);
int cfind(ino_t, struct fid *, struct fscache *, struct cnode **);
int makecachefsnode(ino_t, struct fscache *, struct fid *,
    vnode_t *, cred_t *, int, struct cnode **);
void cachefs_enable_caching(struct fscache *);
void cachefs_addfree(struct cnode *);

/* cachefs_fscache.c */
void fscache_destroy(fscache_t *);

/* cachefs_subr.c */
int cachefs_sync_metadata(cnode_t *, cred_t *);
int cachefs_cnode_cnt(int);
int cachefs_getbackvp(struct fscache *, struct fid *, struct cnode *);
int cachefs_getfrontfile(cnode_t *, struct vattr *, cred_t *);
void cachefs_removefrontfile(struct cachefs_metadata *, ino_t, filegrp_t *);
void cachefs_nocache(cnode_t *);
void cachefs_inval_object(cnode_t *, cred_t *);
void make_ascii_name(int, char *);
int cachefs_async_halt(struct cachefs_workq *, int);
int cachefs_check_allocmap(cnode_t *cp, u_int off);
void cachefs_update_allocmap(cnode_t *, u_int, u_int);
int cachefs_cachesymlink(struct cnode *cp, cred_t *cr);
void cachefs_cluster_allocmap(struct cnode *, int, int *, int *, int);
int cachefs_populate(cnode_t *, u_int, int, cred_t *);
int cachefs_stats_kstat_snapshot(kstat_t *, void *, int);
cachefs_debug_info_t *cachefs_debug_save(cachefs_debug_info_t *, int,
    char *, u_int, int, void *, cachefscache_t *, struct fscache *,
    struct cnode *);
void cachefs_debug_show(cachefs_debug_info_t *);


/* cachefs_resource.c */
void cachefs_lru_remove(struct cachefscache *, int);
void cachefs_lru_free(struct cachefscache *, u_int);
void cachefs_active_remove(struct cachefscache *, u_int);
void cachefs_active_add(struct cachefscache *, u_int);
int cachefs_lru_local(struct cachefscache *, int, int);
int cachefs_allocblocks(cachefscache_t *, int, cred_t *);
void cachefs_freeblocks(cachefscache_t *, int);
void cachefs_freefile(cachefscache_t *);
void cachefs_lru_add(struct cachefscache *, u_int);
int cachefs_allocfile(cachefscache_t *);
int cachefs_lru_alloc(struct cachefscache *, struct fscache *, u_int *, int);
int cachefs_lru_attrc(struct cachefscache *, int, int);
void cachefs_garbage_collect_thread(cachefscache_t *);
void cachefs_move_active_to_lru(cachefscache_t *);

/* cachefs_dir.c */
int cachefs_filldir(struct cnode *, int, cred_t *);
int cachefs_dirlook(struct cnode *, char *, struct fid *, u_int *, u_int *,
    ino_t *, cred_t *);
void cachefs_dirent_mod(struct cnode *, u_int, struct fid *, ino_t *);
int cachefs_direnter(struct cnode *, char *, struct fid *, u_int, ino_t,
    off_t, cred_t *, int);
int cachefs_rmdirent(struct cnode *, char *, cred_t *);
int cachefs_read_dir(struct cnode *, struct uio *, int *, cred_t *);

/* cachefs_log.c */
int cachefs_log_kstat_snapshot(kstat_t *, void *, int);
void cachefs_log_process_queue(cachefscache_t *, int);
int cachefs_log_logfile_open(cachefscache_t *, char *);
struct cachefs_log_cookie
	*cachefs_log_create_cookie(struct cachefs_log_control *);
void cachefs_log_error(cachefscache_t *, int, int);
void cachefs_log_destroy_cookie(struct cachefs_log_cookie *);

void cachefs_log_mount(cachefscache_t *, int, struct vfs *,
    struct cachefsoptions *, char *, enum uio_seg, char *);
void cachefs_log_umount(cachefscache_t *, int, struct vfs *);
void cachefs_log_getpage(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long, u_int, u_int);
void cachefs_log_readdir(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long, off_t, int);
void cachefs_log_readlink(cachefscache_t *, int, struct vfs *,
    fid_t *, ino_t, long, size_t);
void cachefs_log_remove(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long);
void cachefs_log_rmdir(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long);
void cachefs_log_truncate(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long, size_t);
void cachefs_log_putpage(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long, u_int, u_int);
void cachefs_log_create(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long);
void cachefs_log_mkdir(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long);
void cachefs_log_rename(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    int, long);
void cachefs_log_symlink(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long, size_t);
void cachefs_log_populate(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    u_int, int);
void cachefs_log_csymlink(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    int);
void cachefs_log_filldir(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    int);
void cachefs_log_mdcreate(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    u_int);
void cachefs_log_gpfront(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long, u_int, u_int);
void cachefs_log_rfdir(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    long);
void cachefs_log_ualloc(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    u_int, u_int);
void cachefs_log_calloc(cachefscache_t *, int, struct vfs *, fid_t *, ino_t,
    u_int, u_int);
void cachefs_log_nocache(cachefscache_t *, int, struct vfs *, fid_t *, ino_t);

/* cachefs_vnops.c */
int cachefs_initstate(cnode_t *, int, int, cred_t *);
int cachefs_fsync(struct vnode *, int, cred_t *);
void cachefs_inactive(register struct vnode *, cred_t *);
int cachefs_lookup_back(struct vnode *, char *, struct vnode **,
    u_int, cred_t *);
struct vnodeops *cachefs_getvnodeops(void);


/* cachefs_vfsops.c */
void cachefs_kstat_mount(struct fscache *, char *, char *, char *, char *);
void cachefs_kstat_umount(int);
int cachefs_kstat_key_update(kstat_t *, int);
int cachefs_kstat_key_snapshot(kstat_t *, void *, int);

extern void cachefs_workq_init(struct cachefs_workq *);
extern int cachefs_addqueue(struct cachefs_req *, struct cachefs_workq *);


extern caddr_t cachefs_kmem_alloc(int, int);
extern caddr_t cachefs_kmem_zalloc(int, int);
extern void cachefs_kmem_free(caddr_t, int);
extern char *cachefs_strdup(char *);

#endif /* defined (_KERNEL) && defined (__STDC__) */



#define	C_LRU_MAXENTS	0x4000		/* Whatever */



#ifdef DEBUG
#define	CFSDEBUG_ALL		0xffffffff
#define	CFSDEBUG_NONE		0x0
#define	CFSDEBUG_GENERAL	0x1
#define	CFSDEBUG_SUBR		0x2
#define	CFSDEBUG_CNODE		0x4
#define	CFSDEBUG_DIR		0x8
#define	CFSDEBUG_STRICT		0x10
#define	CFSDEBUG_VOPS		0x20
#define	CFSDEBUG_VFSOP		0x40
#define	CFSDEBUG_RESOURCE	0x80
#define	CFSDEBUG_CHEAT		0x100

extern int cachefsdebug;

#define	CFS_DEBUG(N)    if (cachefsdebug & (N))
#define	CFSDEBUG
#endif /* DEBUG */

#define	C_ISVDEV(t) ((t == VBLK) || (t == VCHR) || (t == VFIFO))

/*
 * ioctls.
 */
#include <sys/ioccom.h>
#define	_FIOPIN		_IO('f', 72)		/* pin file in cache */
#define	_FIOUNPIN	_IO('f', 73)		/* unpin file in cache */
#define	_FIOCOD		_IO('f', 78)		/* consistency on demand */
#define	_FIOBOINFO	_IO('f', 79)		/* get bootobj info */
#define	_FIOCNVTMNT	_IO('f', 80)		/* convert a mount */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FS_CACHEFS_FS_H */
