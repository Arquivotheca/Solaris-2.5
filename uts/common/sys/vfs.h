/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef _SYS_VFS_H
#define	_SYS_VFS_H

#pragma ident	"@(#)vfs.h	1.32	95/06/20 SMI"	/* SVr4.0 1.23 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/statvfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Data associated with mounted file systems.
 */

/*
 * File system identifier. Should be unique (at least per machine).
 */
typedef struct {
	long val[2];			/* file system id type */
} fsid_t;

/*
 * File identifier.  Should be unique per filesystem on a single
 * machine.  This is typically called by a stateless file server
 * in order to generate "file handles".
 *
 * Do not change the definition of struct fid ... fid_t without
 * letting the CacheFS group know about it!  They will have to do at
 * least two things, in the same change that changes this structure:
 *   1. change CFSVERSION in usr/src/uts/common/sys/fs/cachefs_fs.h
 *   2. put the old version # in the canupgrade array
 *       in cachfs_upgrade() in usr/src/cmd/fs.d/cachefs/fsck/fsck.c
 * This is necessary because CacheFS stores FIDs on disk.
 *
 * Many underlying file systems cast a struct fid into other
 * file system dependant structures which may require 4 byte alignment.
 * Because a fid starts with a short it may not be 4 byte aligned, the
 * fid_pad will force the alignment.
 */
#define	MAXFIDSZ	64
#define	OLD_MAXFIDSZ	16

typedef struct fid {
	union {
		long fid_pad;
		struct {
			u_short  len;	/* length of data in bytes */
			char	data[MAXFIDSZ]; /* data (variable len) */
		} _fid;
	} un;
} fid_t;
#define	fid_len		un._fid.len
#define	fid_data	un._fid.data

/*
 * Structure per mounted file system.  Each mounted file system has
 * an array of operations and an instance record.  The file systems
 * are kept on a singly linked list headed by "rootvfs" and terminated
 * by NULL.
 */
typedef struct vfs {
	struct vfs	*vfs_next;		/* next VFS in VFS list */
	struct vfsops	*vfs_op;		/* operations on VFS */
	struct vnode	*vfs_vnodecovered;	/* vnode mounted on */
	u_long		vfs_flag;		/* flags */
	u_long		vfs_bsize;		/* native block size */
	int		vfs_fstype;		/* file system type index */
	fsid_t		vfs_fsid;		/* file system id */
	caddr_t		vfs_data;		/* private data */
	dev_t		vfs_dev;		/* device of mounted VFS */
	u_long		vfs_bcount;		/* I/O count (accounting) */
	u_short		vfs_nsubmounts;		/* immediate sub-mount count */
	struct vfs	*vfs_list;		/* sync list pointer */
	struct vfs	*vfs_hash;		/* hash list pointer */
	kmutex_t	vfs_reflock;		/* mount/unmount/sync lock */
} vfs_t;

/*
 * VFS flags.
 */
#define	VFS_RDONLY	0x01		/* read-only vfs */
#define	VFS_NOSUID	0x08		/* setuid disallowed */
#define	VFS_REMOUNT	0x10		/* modify mount options only */
#define	VFS_NOTRUNC	0x20		/* does not truncate long file names */
#define	VFS_UNLINKABLE	0x40		/* unlink(2) can be applied to root */

/*
 * Argument structure for mount(2).
 */
struct mounta {
	char	*spec;
	char	*dir;
	int	flags;
	char	*fstype;
	char	*dataptr;
	int	datalen;
};

/*
 * Reasons for calling the vfs_mountroot() operation.
 */
enum whymountroot { ROOT_INIT, ROOT_REMOUNT, ROOT_UNMOUNT, ROOT_FRONTMOUNT,
	ROOT_BACKMOUNT};
typedef enum whymountroot whymountroot_t;

/*
 * Operations supported on virtual file system.
 */
/*
 * XXX:  Due to a bug in the current compilation system, which has
 * trouble mixing new (ansi) and old (K&R) style prototypes when a
 * short or char is a parameter, we do not prototype this vfsop:
 *	vfs_sync
 */
typedef struct vfsops {
	int	(*vfs_mount)(struct vfs *, struct vnode *, struct mounta *,
			struct cred *);
	int	(*vfs_unmount)(struct vfs *, struct cred *);
	int	(*vfs_root)(struct vfs *, struct vnode **);
	int	(*vfs_statvfs)(struct vfs *, struct statvfs *);
	int	(*vfs_sync)(struct vfs *, short, struct cred *);
	int	(*vfs_vget)(struct vfs *, struct vnode **, struct fid *);
	int	(*vfs_mountroot)(struct vfs *, enum whymountroot);
	int	(*vfs_swapvp)(struct vfs *, struct vnode **, char *);
} vfsops_t;

#define	VFS_MOUNT(vfsp, mvp, uap, cr) \
	(*(vfsp)->vfs_op->vfs_mount)(vfsp, mvp, uap, cr)
#define	VFS_UNMOUNT(vfsp, cr)	(*(vfsp)->vfs_op->vfs_unmount)(vfsp, cr)
#define	VFS_ROOT(vfsp, vpp)	(*(vfsp)->vfs_op->vfs_root)(vfsp, vpp)
#define	VFS_STATVFS(vfsp, sp)	(*(vfsp)->vfs_op->vfs_statvfs)(vfsp, sp)
#define	VFS_SYNC(vfsp, flag, cr) \
	(*(vfsp)->vfs_op->vfs_sync)(vfsp, flag, cr)
#define	VFS_VGET(vfsp, vpp, fidp) \
	(*(vfsp)->vfs_op->vfs_vget)(vfsp, vpp, fidp)
#define	VFS_MOUNTROOT(vfsp, init) \
	(*(vfsp)->vfs_op->vfs_mountroot)(vfsp, init)
#define	VFS_SWAPVP(vfsp, vpp, nm) \
	(*(vfsp)->vfs_op->vfs_swapvp)(vfsp, vpp, nm)

/*
 * Filesystem type switch table.
 */
typedef struct vfssw {
	char		*vsw_name;	/* type name string */
	int		(*vsw_init)();	/* init routine */
	struct vfsops	*vsw_vfsops;	/* filesystem operations vector */
	long		vsw_flag;	/* flags */
} vfssw_t;

#if defined(_KERNEL)
/*
 * Public operations.
 */
struct umounta;
struct statvfsa;
struct fstatvfsa;

int	rootconf(void);
int	dounmount(struct vfs *, cred_t *);
int	vfs_lock(struct vfs *);
void	vfs_lock_wait(struct vfs *);
void	vfs_unlock(struct vfs *);
void	sync(void);
void	vfs_sync(int);
void	vfs_mountroot(void);
void	vfs_add(vnode_t *, struct vfs *, int);
void	vfs_remove(struct vfs *);
void	vfs_syncall(void);
void	vfsinit(void);
void	vfs_unmountall(void);
struct vfs *getvfs(fsid_t *);
struct vfs *vfs_devsearch(dev_t);
struct vfs *vfs_opssearch(struct vfsops *);
struct vfssw *allocate_vfssw(char *);
struct vfssw *vfs_getvfssw(char *);
struct vfssw *vfs_getvfsswbyname(char *);
u_long	vf_to_stf(u_long);

#define	VFSHASH(dev, fstyp) (((int)dev + (int)fstyp) & (vfshsz - 1))

/*
 * Globals.
 */
extern struct vfs *rootvfs;		/* ptr to root vfs structure */
extern struct vfs **rvfs_head;		/* root hash vfs structures */
extern struct vfssw vfssw[];		/* table of filesystem types */
extern krwlock_t vfssw_lock;
extern char rootfstype[];		/* name of root fstype */
extern int nfstype;			/* # of elements in vfssw array */

extern kmutex_t	vfslist;		/* protects vfs linked list */
extern kmutex_t	vfstlock;		/* vfs temp lock */
extern kmutex_t *rvfs_lock;		/* protects vfs hash list */
extern int vfshsz;			/* # of elements in rvfs_head array */

#endif /* defined(_KERNEL) */

#define	VFS_INIT(vfsp, op, data)	{ \
	(vfsp)->vfs_next = (struct vfs *)0; \
	(vfsp)->vfs_op = (op); \
	(vfsp)->vfs_flag = 0; \
	(vfsp)->vfs_data = (data); \
	(vfsp)->vfs_nsubmounts = 0; \
	mutex_init(&(vfsp)->vfs_reflock, "mnt/sync lock", \
		MUTEX_DEFAULT, NULL); \
}

#define	VFS_INSTALLED(vfsswp)		((vfsswp)->vsw_vfsops)
#define	ALLOCATED_VFSSW(vswp)		((vswp)->vsw_name[0] != '\0')
#define	RLOCK_VFSSW()			(rw_enter(&vfssw_lock, RW_READER))
#define	RUNLOCK_VFSSW()			(rw_exit(&vfssw_lock))
#define	WLOCK_VFSSW()			(rw_enter(&vfssw_lock, RW_WRITER))
#define	WUNLOCK_VFSSW()			(rw_exit(&vfssw_lock))
#define	VFSSW_LOCKED()			(RW_LOCK_HELD(&vfssw_lock))
#define	VFSSW_WRITE_LOCKED()		(RW_WRITE_HELD(&vfssw_lock))
/*
 * VFS_SYNC flags.
 */
#define	SYNC_ATTR	0x01		/* sync attributes only */
#define	SYNC_CLOSE	0x02		/* close open file */
#define	SYNC_ALL	0x04		/* force to sync all fs */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VFS_H */
