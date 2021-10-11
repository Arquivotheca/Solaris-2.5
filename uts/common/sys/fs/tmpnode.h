/*
 * Copyright (c) 1989, 1990, 1991 by Sun Microsystems, Inc.
 */

#ifndef _SYS_FS_TMPNODE_H
#define	_SYS_FS_TMPNODE_H

#pragma ident	"@(#)tmpnode.h	1.15	94/11/16 SMI"
/*  tmpnode.h 1.8 89/10/10 SMI */

#include <sys/t_lock.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * tmpnode is the file system dependent node for tmpfs.
 *
 *	tn_rwlock protects access of the directory list at tn_dir
 *	as well as syncronizing read and writes to the tmpnode
 *
 *	tn_contents protects growing, shrinking, reading and writing
 *	the file along with tn_rwlock (see below).
 *
 *	tn_tlock protects updates to
 *		tn_[amc]time, tn_flags and tn_nlink
 *
 *	tm_contents in the tmount filesystem data structure protects
 *	tn_forw and tn_back which are used to maintain a linked
 *	list of all tmpfs files associated with that file system
 *
 *	The anon array represents the secondary store for tmpfs.
 * 	To grow or shrink the file or fill in holes requires
 *	manipulation of the anon array. These operations are protected
 *	by a combination of tn_rwlock and tn_contents. Growing or shrinking
 * 	the array requires the write lock on tn_rwlock and tn_contents.
 *	Filling in a slot in the array requires the write lock on tn_contents.
 *	Reading the array requires the read lock on tn_contents.
 *
 *	The ordering of the locking is:
 *	tn_rwlock -> tn_contents -> page locks on pages in file
 *
 *	tn_tlock doesn't require any tmpnode locks
 */

struct tmpnode {
	u_int		tn_flags;		/* see T flags below */
	struct tmpnode	*tn_back;		/* linked list of tmpnodes */
	struct tmpnode	*tn_forw;		/* linked list of tmpnodes */
	union {
		struct tdirent	*un_dir;	/* pointer to directory list */
		char 		*un_symlink;	/* pointer to symlink */
		struct {
			struct	anon **un_anon;	/* anon backing for file */
			u_int un_size;		/* size repres. by array */
		} un_anonstruct;
	} un_tmpnode;
	struct vnode 	tn_vnode;		/* vnode for this tmpnode */
	struct vattr	tn_attr;		/* attributes */
	long 		tn_gen;			/* pseudo gen number for tfid */
	krwlock_t	tn_contents;		/* vm side -serialize mods */
	krwlock_t	tn_rwlock;		/* rw,trunc size - serialize */
						/* mods and directory updates */
	kmutex_t	tn_tlock;		/* time, flag, and nlink lock */
};

#define	tn_dir		un_tmpnode.un_dir
#define	tn_symlink	un_tmpnode.un_symlink
#define	tn_anon		un_tmpnode.un_anonstruct.un_anon
#define	tn_asize	un_tmpnode.un_anonstruct.un_size

/*
 * Attributes
 */
#define	tn_mask		tn_attr.va_mask
#define	tn_type		tn_attr.va_type
#define	tn_mode		tn_attr.va_mode
#define	tn_uid		tn_attr.va_uid
#define	tn_gid		tn_attr.va_gid
#define	tn_fsid		tn_attr.va_fsid
#define	tn_nodeid	tn_attr.va_nodeid
#define	tn_nlink	tn_attr.va_nlink
#define	tn_size		tn_attr.va_size
#define	tn_atime	tn_attr.va_atime
#define	tn_mtime	tn_attr.va_mtime
#define	tn_ctime	tn_attr.va_ctime
#define	tn_rdev		tn_attr.va_rdev
#define	tn_blksize	tn_attr.va_blksize
#define	tn_nblocks	tn_attr.va_nblocks
#define	tn_vcode	tn_attr.va_vcode

/*
 * tn_flags
 */
#define	TREF		0x001		/* tmpnode referenced */
#define	TUPD		0x002		/* file accessed */
#define	TACC		0x004		/* file modified */
#define	TCHG		0x008		/* file changed */
#define	TMAP		0x010		/* file was mapped beyond EOF */
#define	TFREE		0x020		/* tmpnode freed, but still has HELD */

#define	tmp_accessed(t)	tmp_timestamp((t), (((t)->tn_flags)|TACC))
#define	tmp_modified(t)	tmp_timestamp((t), (((t)->tn_flags)|TACC|TUPD))
#define	tmp_created(t)	tmp_timestamp((t), (((t)->tn_flags)|TACC|TUPD|TCHG))

/*
 * tmpfs directories are made up of a linked list of tdirent structures
 * hanging off directory tmpnodes.  File names are not fixed length,
 * but are null terminated.
 */
struct tdirent {
	struct tmpnode	*td_tmpnode;		/* tnode for this file */
	struct tdirent	*td_next;		/* next directory entry */
	struct tdirent	*td_prev;		/* prev directory entry */
	u_int		td_offset;		/* "offset" of dir entry */
	u_int		td_hash;		/* a hash of td_name */
	struct tdirent	*td_link;		/* linked via the hash table */
	struct tmpnode	*td_parent;		/* parent, dir we are in */
	char		*td_name;		/* must be null terminated */
						/* max length is MAXNAMELEN */
};

/*
 * tfid overlays the fid structure (for VFS_VGET)
 */
struct tfid {
	u_short	tfid_len;
	ino_t	tfid_ino;
	long	tfid_gen;
};

#define	ESAME	(-1)		/* trying to rename linked files (special) */

extern int	tmp_files;
extern struct vnodeops tmp_vnodeops;
extern int	tmpfsinit();
extern int	tmpfsfstype;
extern dev_t	tmpdev;


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_TMPNODE_H */
