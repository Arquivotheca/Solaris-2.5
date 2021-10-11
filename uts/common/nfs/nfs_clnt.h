/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		PROPRIETARY NOTICE (Combined)
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
 *	(c) 1986,1987,1988,1989,1990,1991  Sun Microsystems, Inc.
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

#ifndef	_NFS_NFS_CLNT_H
#define	_NFS_NFS_CLNT_H

#pragma ident	"@(#)nfs_clnt.h	1.40	95/05/18 SMI"
/* SVr4.0 1.8	*/
/*	nfs_clnt.h 2.28 88/08/19 SMI	*/

#include <sys/kstat.h>
#include <vm/page.h>
#include <sys/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	HOSTNAMESZ	32
#define	ACREGMIN	3	/* min secs to hold cached file attr */
#define	ACREGMAX	60	/* max secs to hold cached file attr */
#define	ACDIRMIN	30	/* min secs to hold cached dir attr */
#define	ACDIRMAX	60	/* max secs to hold cached dir attr */
#define	ACMINMAX	3600	/* 1 hr is longest min timeout */
#define	ACMAXMAX	36000	/* 10 hr is longest max timeout */

#define	NFS_CALLTYPES	3	/* Lookups, Reads, Writes */

/*
 * Fake errno passed back from rfscall to indicate transfer size adjustment
 */
#define	ENFS_TRYAGAIN	999

/*
 * The NFS specific async_reqs structure.
 */

enum iotype { NFS_READ_AHEAD, NFS_PUTAPAGE, NFS_PAGEIO, NFS_READDIR };

struct nfs_async_read_req {
	void (*readahead)();		/* pointer to readahead function */
	u_int blkoff;			/* offset in file */
	struct seg *seg;		/* segment to do i/o to */
	caddr_t addr;			/* address to do i/o to */
};

struct nfs_pageio_req {
	int (*pageio)();		/* pointer to pageio function */
	page_t *pp;			/* page list */
	u_int io_off;			/* offset in file */
	u_int io_len;			/* size of request */
	int flags;
};

struct nfs_readdir_req {
	int (*readdir)();		/* pointer to readdir function */
	struct rddir_cache *rdc;	/* pointer to cache entry to fill */
};

struct nfs_async_reqs {
	struct nfs_async_reqs *a_next;	/* pointer to next arg struct */
#ifdef DEBUG
	kthread_t *a_queuer;		/* thread id of queueing thread */
#endif
	struct vnode *a_vp;		/* vnode pointer */
	struct cred *a_cred;		/* cred pointer */
	enum iotype a_io;		/* i/o type */
	union {
		struct nfs_async_read_req a_read_args;
		struct nfs_pageio_req a_pageio_args;
		struct nfs_readdir_req a_readdir_args;
	} a_args;
};

#define	a_nfs_readahead a_args.a_read_args.readahead
#define	a_nfs_blkoff a_args.a_read_args.blkoff
#define	a_nfs_seg a_args.a_read_args.seg
#define	a_nfs_addr a_args.a_read_args.addr

#define	a_nfs_putapage a_args.a_pageio_args.pageio
#define	a_nfs_pageio a_args.a_pageio_args.pageio
#define	a_nfs_pp a_args.a_pageio_args.pp
#define	a_nfs_off a_args.a_pageio_args.io_off
#define	a_nfs_len a_args.a_pageio_args.io_len
#define	a_nfs_flags a_args.a_pageio_args.flags

#define	a_nfs_readdir a_args.a_readdir_args.readdir
#define	a_nfs_rdc a_args.a_readdir_args.rdc

/*
 * NFS private data per mounted file system
 *	The mi_lock mutex protects the following fields:
 *		mi_printed
 *		mi_down
 *		mi_stsize
 *		mi_curread
 *		mi_curwrite
 *		mi_timers
 */
typedef struct mntinfo {
	kmutex_t	mi_lock;	/* protects mntinfo fields */
	struct knetconfig *mi_knetconfig;	/* bound TLI fd */
	struct netbuf	mi_addr;	/* server's address */
	struct netbuf	mi_syncaddr;	/* AUTH_DES time sync addr */
	struct vnode	*mi_rootvp;	/* root vnode */
	u_int		mi_flags;	/* see below */
	long		mi_tsize;	/* transfer size (bytes) */
					/* really read size */
	long		mi_stsize;	/* server's max transfer size (bytes) */
					/* really write size */
	int		mi_timeo;	/* inital timeout in 10th sec */
	int		mi_retrans;	/* times to retry request */
	char		mi_hostname[HOSTNAMESZ];	/* server's hostname */
	char		*mi_netname;	/* server's netname */
	int		mi_netnamelen;	/* length of netname */
	int		mi_authflavor;	/* authentication type */
	u_int		mi_acregmin;	/* min secs to hold cached file attr */
	u_int		mi_acregmax;	/* max secs to hold cached file attr */
	u_int		mi_acdirmin;	/* min secs to hold cached dir attr */
	u_int		mi_acdirmax;	/* max secs to hold cached dir attr */
	/*
	 * Extra fields for congestion control, one per NFS call type,
	 * plus one global one.
	 */
	struct rpc_timers mi_timers[NFS_CALLTYPES+1];
	long		mi_curread;	/* current read size */
	long		mi_curwrite;	/* current write size */
	/*
	 * async I/O management
	 */
	struct nfs_async_reqs *mi_async_reqs; /* head of async request list */
	struct nfs_async_reqs *mi_async_tail; /* tail of async request list */
	kcondvar_t	mi_async_reqs_cv;
	u_short		mi_threads;	/* number of active async threads */
	u_short		mi_max_threads;	/* max number of async threads */
	kcondvar_t	mi_async_cv;
	u_int		mi_async_count;	/* number of entries on async list */
	kmutex_t	mi_async_lock;	/* lock to protect async list */
	/*
	 * Other stuff
	 */
	struct pathcnf *mi_pathconf;	/* static pathconf kludge */
	u_long		mi_prog;	/* RPC program number */
	u_long		mi_vers;	/* RPC program version number */
	char		**mi_rfsnames;	/* mapping to proc names */
	kstat_named_t	*mi_reqs;	/* count of requests */
	char		*mi_call_type;	/* dynamic retrans call types */
	char		*mi_timer_type;	/* dynamic retrans timer types */
	clock_t		mi_printftime;	/* last error printf time */
	/*
	 * ACL entries
	 */
	char		**mi_aclnames;	/* mapping to proc names */
	kstat_named_t	*mi_aclreqs;	/* count of acl requests */
	char		*mi_acl_call_type; /* dynamic retrans call types */
	char		*mi_acl_timer_type; /* dynamic retrans timer types */
} mntinfo_t;

/*
 * vfs pointer to mount info
 */
#define	VFTOMI(vfsp)	((mntinfo_t *)((vfsp)->vfs_data))

/*
 * vnode pointer to mount info
 */
#define	VTOMI(vp)	((mntinfo_t *)(((vp)->v_vfsp)->vfs_data))

/*
 * The values for mi_flags.
 */
#define	MI_HARD		0x1		/* hard or soft mount */
#define	MI_PRINTED	0x2		/* not responding message printed */
#define	MI_INT		0x4		/* interrupts allowed on hard mount */
#define	MI_DOWN		0x8		/* server is down */
#define	MI_NOAC		0x10		/* don't cache attributes */
#define	MI_NOCTO	0x20		/* no close-to-open consistency */
#define	MI_DYNAMIC	0x40		/* dynamic transfer size adjustment */
#define	MI_LLOCK	0x80		/* local locking only (no lockmgr) */
#define	MI_GRPID	0x100		/* System V group id inheritance */
#define	MI_RPCTIMESYNC	0x200		/* RPC time sync */
#define	MI_LINK		0x400		/* server supports link */
#define	MI_SYMLINK	0x800		/* server supports symlink */
#define	MI_READDIR	0x1000		/* use readdir instead of readdirplus */
#define	MI_ACL		0x2000		/* server supports NFS_ACL */
#define	MI_TRYANON	0x4000		/* try anonymous access if AUTH_DES */
					/* or AUTH_KERB failed. i.e. try */
					/* AUTH_NONE */

/*
 * Mark cached attributes as timed out
 */
#define	PURGE_ATTRCACHE(vp)	VTOR(vp)->r_attrtime = hrestime.tv_sec

/*
 * Is the attribute cache valid?
 */
#define	ATTRCACHE_VALID(vp)	hrestime.tv_sec < VTOR(vp)->r_attrtime

/*
 * If returned error is ESTALE flush all caches.
 */
#define	PURGE_STALE_FH(errno, vp, cr)				\
	if ((errno) == ESTALE) {				\
		struct rnode *rp = VTOR(vp);			\
		mutex_enter(&rp->r_statelock);			\
		rp->r_flags |= RDONTWRITE;			\
		if (!rp->r_error)				\
			rp->r_error = error;			\
		mutex_exit(&rp->r_statelock);			\
		if ((vp)->v_pages != NULL)			\
			nfs_invalidate_pages((vp), 0, (cr));	\
		nfs_purge_caches((vp), (cr));			\
	}

/*
 * Is cache valid?
 * Swap is always valid, if no attributes (attrtime == 0) or
 * if mtime matches cached mtime it is valid
 * NOTE: mtime is now a timestruc_t.
 * Caller should have the rnode r_protect mutex
 */
#define	CACHE_VALID(rp, mtime, fsize)				\
	((RTOV(rp)->v_flag & VISSWAP) == VISSWAP ||		\
	(((mtime).tv_sec == (rp)->r_attr.va_mtime.tv_sec &&	\
	(mtime).tv_nsec == (rp)->r_attr.va_mtime.tv_nsec) &&	\
	((fsize) == (rp)->r_attr.va_size)))

#ifdef _KERNEL
extern void	nfs_free_mi(mntinfo_t *);
extern void	purge_authtab(mntinfo_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _NFS_NFS_CLNT_H */
