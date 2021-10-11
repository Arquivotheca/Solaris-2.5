/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

/*
 * Autofs mount info - one per mount
 */

#ifndef	_SYS_FS_AUTOFS_H
#define	_SYS_FS_AUTOFS_H

#pragma ident	"@(#)autofs.h	1.12	95/02/22 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#undef	MAXNAMLEN
#define	MAXNAMLEN	255

struct auto_args {
	struct netbuf	addr;		/* daemon address */
	char		*path;		/* autofs mountpoint */
	char		*opts;		/* default mount options */
	char		*map;		/* name of map */
	int		mount_to;	/* time in sec the fs is to remain */
					/* mounted after last reference */
	int 		rpc_to;		/* timeout for rpc calls */
	int		direct;		/* 1 = direct mount */
};

#ifdef	_KERNEL

struct auto_callargs {
	vnode_t		*ac_vp;		/* vnode */
	char		*ac_name;	/* name of path to mount */
	cred_t		*ac_cred;
	int		ac_thr_exit;	/* thread exit on completion? */
};

struct autoinfo {
	struct vfs	*ai_mountvfs;	/* vfs */
	struct vnode	*ai_rootvp;	/* root vnode */
	struct knetconfig ai_knconf;	/* netconfig */
	struct netbuf	 ai_addr;	/* daemon address */
	char		*ai_path;	/* autofs mountpoint */
	int		 ai_pathlen;	/* autofs mountpoint len */
	char		*ai_opts;	/* default mount options */
	int		 ai_optslen;	/* default mount options len */
	char		*ai_map;	/* name of map */
	int		 ai_maplen;	/* name of map len */
	int		 ai_direct;	/* 1 = direct mount */
	int		 ai_refcnt;	/* reference count */
	int		 ai_mount_to;
	int		 ai_rpc_to;
	char		 ai_current[MAXNAMLEN + 1];
};

/*
 * The autonode is the "inode" for autofs.  It contains
 * all the information necessary to handle automatic mounting.
 */
typedef struct autonode {
	char		an_name[MAXNAMLEN + 1];
	u_short		an_mode;	/* file mode bits */
	uid_t		an_uid;
	gid_t		an_gid;
	u_short		an_nodeid;	/* unique id */
	u_short		an_offset;	/* offset within parent dir */
	u_short		an_waiters;
	u_int		an_mntflags;	/* mount flags */
	u_int		an_size;	/* size of directory */
	struct vnode	an_vnode;	/* place hldr vnode for file */
	long		an_ref_time;    /* time last referred */
	int		an_error;	/* mount error */
	struct autonode *an_parent;	/* parent dir for .. lookup */
	struct autonode *an_next;	/* next sibling */
	struct autonode *an_dirents;	/* autonode list under this */
	krwlock_t	an_rwlock;	/* serialize access for lookup */
					/* and enter */
	kcondvar_t	an_cv_mount;	/* wakeup address */
	kcondvar_t	an_cv_umount;	/* wakeup address */
	kmutex_t	an_lock;	/* protection */
} autonode_t;

/*
 * Mount flags
 */

#define	MF_MNTPNT		0x001		/* A mountpoint */
#define	MF_INPROG		0x002		/* mount in progress */
#define	MF_WAITING_MOUNT	0x004		/* thread waiting */
#define	MF_WAITING_UMOUNT	0x008
#define	MF_MOUNTED		0x010		/* mount taken place */
#define	MF_UNMOUNTING		0x020		/* unmount in prog */
#define	MF_DONTMOUNT		0x040		/* unmount failed, so don't */
						/* try another mount */
#define	MF_CHECKED		0x080		/* checked by unmount thread */
/*
 * Convert between vfs/autoinfo & vnode/autonode
 */
#define	antovn(ap)	(&((ap)->an_vnode))
#define	vntoan(vp)	((struct autonode *) ((vp)->v_data))
#define	vfstoai(vfsp)	((struct autoinfo *) ((vfsp)->vfs_data))

extern kmutex_t autonode_list_lock;
extern struct autonode *autonode_list;
extern kmutex_t autonode_count_lock;
extern int anode_cnt, makeautonode_count, freeautonode_count;

extern int	do_mount(vnode_t *, char *, cred_t *);
extern int	force_remount(autonode_t *, char *, cred_t *);
extern int	autodir_lookup(vnode_t *, char *, vnode_t **, cred_t *, int);
extern int	auto_direnter(autonode_t *, autonode_t *);
extern void	do_unmount(void);
extern vnode_t	*makeautonode(vtype_t, vfs_t *, cred_t *);
extern void	freeautonode(autonode_t *anp);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_AUTOFS_H */
