/*
 * Copyright (c) 1989, 1990, 1991 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_FS_TMP_H
#define	_SYS_FS_TMP_H

#pragma ident	"@(#)tmp.h	1.15	94/01/21 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * tmpfs per-mount data structure.
 *
 * There is a linked list of these structures rooted at tmpfs_mountp.
 * Locking for this structure is as follows:
 * 	tm_next is protected by the tmpfs global mutex tmpfs_mutex
 *	all other fields are protected by the tm_contents lock
 * File renames on a particular file system are protected tm_renamelck.
 */
struct tmount {
	struct tmount	*tm_next;	/* linked list of tmounts */
	struct vfs	*tm_vfsp;	/* filesystem's vfs struct */
	struct tmpnode	*tm_rootnode;	/* root tmpnode */
	struct tmpimap	*tm_inomap;	/* tmpnode allocator maps */
	char 		*tm_mntpath;	/* name of tmpfs mount point */
	u_int		tm_filerefcnt;	/* count of referenced files */
	u_int		tm_anonmax;	/* file system max anon reservation */
	dev_t		tm_dev;		/* unique dev # of mounted `device' */
	kmutex_t	tm_contents;	/* lock for tmount structure */
	kmutex_t	tm_renamelck;	/* rename lock for this mount */
	long		tm_gen;		/* pseudo generation number for files */
					/*
					 * The following are resource counters
					 *  for this particular file system
					 */
	u_int		tm_direntries;	/* number of directory entries */
	u_int		tm_directories; /* number of directories */
	u_int		tm_files;	/* number of files (not directories) */
	u_int		tm_kmemspace;	/* bytes of kmem_alloc'd phys memory */
	u_int		tm_anonmem;	/* pages of reserved anon memory */
};

/*
 * Unique node-id's (the equivalent of inode numbers) are provided
 * and maintained through a bitmap in each mount structure.
 * The bit in the map associated with a particular number is set
 * when an id is allocated and cleared when freed.
 */
#define	TMPIMAPNODES	512
#define	TMPIMAPSIZE	TMPIMAPNODES/NBBY

struct tmpimap {
	u_char timap_bits[TMPIMAPSIZE];	/* bitmap of available index numbers */
	struct tmpimap *timap_next;	/* ptr to more index numbers */
};

struct tmpfs_args {
	u_int		anonmax;	/* maximum size for this file system */
	u_int		flags;		/* flags for this mount */
};

/*
 * File system independent to tmpfs conversion macros
 */
#define	VFSTOTM(vfsp)		((struct tmount *)(vfsp)->vfs_data)
#define	VTOTM(vp)		((struct tmount *)(vp)->v_vfsp->vfs_data)
#define	VTOTN(vp)		((struct tmpnode *)(vp)->v_data)
#define	TNTOV(tp)		((struct vnode *)&(tp)->tn_vnode)

/*
 * functions to manipulate bitmaps
 */
#define	TESTBIT(map, i)		(((map)[(i) >> 3] & (1 << ((i) % NBBY))))
#define	SETBIT(map, i)		(((map)[(i) >> 3] |= (1 << ((i) % NBBY))))
#define	CLEARBIT(map, i)	(((map)[(i) >> 3] &= ~(1 << ((i) % NBBY))))

/*
 * enums
 */
enum de_op	{ DE_CREATE, DE_MKDIR, DE_LINK, DE_RENAME }; /* direnter ops */
enum dr_op	{ DR_REMOVE, DR_RMDIR, DR_RENAME };	/* dirremove ops */

/*
 * tmpfs_mutex protects tmpfs global data (e.g. mount list).
 */
extern kmutex_t	tmpfs_mutex;

/*
 * tmpfs_minfree is the amount (in pages) of anonymous memory that tmpfs
 * leaves free for the rest of the system.  E.g. in a system with 32MB of
 * configured swap space, if 16MB were reserved (leaving 16MB free),
 * tmpfs could allocate up to 16MB - tmpfs_minfree.  The default value
 * for tmpfs_minfree is btopr(TMPMINFREE) but it can cautiously patched
 * to a different number of pages.
 * NB: If tmpfs allocates too much swap space, other processes will be
 * unable to execute.
 */
#define	TMPMINFREE	2 * 1024 * 1024	/* 2 Megabytes */

extern u_int	tmpfs_minfree;		/* Anonymous memory in pages */

/*
 * tmpfs can allocate only a certain percentage of kernel memory,
 * which is used for tmpnodes, directories, file names, etc.
 * This is statically set as TMPMAXPROCKMEM percent of physical memory.
 * The actual number of allocatable bytes can be patched in tmpfs_maxkmem.
 */
#define	TMPMAXPROCKMEM	4	/* 4 percent of physical memory */

extern struct vfsops tmp_vfsops;
extern int 	tmp_kmemspace;

extern u_int	tmpfs_maxkmem;	/* Allocatable kernel memory in bytes */
extern	void	tmp_timestamp();
extern	struct tmpnode *tmpnode_alloc();
extern	int	tmpnode_trunc();
extern	void	tmpnode_free();
extern	void	tmpnode_growmap();
extern	int	tdirlookup();
extern	int	tdirdelete();
extern	int	tdirinit();
extern	void	tdirtrunc();
extern	char	*tmp_memalloc();
extern	void	tmp_memfree();
extern	int	tmp_resv();
extern	int	tmp_taccess();
extern	void	tmpnode_rele();
extern	void	tmpnode_hold();
extern	void	tmpnode_rmanon();
extern long	tmp_imapalloc(struct tmount *);
extern void	tmp_imapfree(struct tmount *, ino_t);
extern	int	tdirenter(struct tmount *, struct tmpnode *, char *,
	enum de_op, struct tmpnode *, struct tmpnode *, struct vattr *,
	struct tmpnode **, struct cred *);

#ifndef lint
#define	TMPFSDEBUG 1
#endif

#ifdef TMPFSDEBUG
extern int tmpdebug;
extern int tmpcheck;
extern int tmp_findnode(struct tmount *, struct tmpnode *);
extern int tmp_findentry(struct tmount *, struct tmpnode *);
extern int tmpcheck;

#define	T_DEBUG	0x1
#define	T_DIR	0x2
#define	T_ALLOC	0x4

#define	TMP_PRINT(cond, fmt, arg1, arg2,  arg3, arg4, arg5) \
	if (tmpdebug & (cond))  \
		printf((fmt), (arg1), (arg2), (arg3), (arg4), (arg5))
#else
#define	TMP_PRINT(cond, fmt, arg1, arg2, arg3, arg4, arg5)
#endif	/* TMPFSDEBUG */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_TMP_H */
