/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_FS_UFS_INODE_H
#define	_SYS_FS_UFS_INODE_H

#pragma ident	"@(#)ufs_inode.h	2.92	95/10/15 SMI"
/* SVr4.0 1.16	*/

#include <sys/isa_defs.h>
#include <sys/fbuf.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
#include <sys/cred.h>
#include <sys/time.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_lockfs.h>
#include <sys/fs/ufs_trans.h>
#include <sys/kstat.h>
#include <sys/fs/ufs_acl.h>
#include <sys/fs/ufs_panic.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The I node is the focus of all local file activity in UNIX.
 * There is a unique inode allocated for each active file,
 * each current directory, each mounted-on file, each mapping,
 * and the root.  An inode is `named' by its dev/inumber pair.
 * Data in icommon is read in from permanent inode on volume.
 *
 * Each inode has 5 locks associated with it:
 *	i_rwlock:	Serializes ufs_write and ufs_setattr request
 *			and allows ufs_read requests to proceed in parallel.
 *			Serializes reads/updates to directories.
 *	i_contents:	Protects almost all of the fields in the inode
 *			except for those list below.
 *	i_tlock:	Protects just the i_atime, i_mtime, i_ctime,
 *			i_delayoff, i_delaylen, i_nextrio, i_writes
 *			and i_flag fields.
 *	ih_lock:	Protects inode hash chain buckets
 *	ifree_lock:	Protects inode freelist
 *
 * Lock ordering:
 *	i_rwlock > i_contents > i_tlock
 *	i_contents > i_tlock
 *	ih_lock > i_contents > i_tlock
 *
 * Unfortunately, performance considerations have required that we be more
 * intelligent about using i_tlock when updating i_flag.  Ideally, we would
 * have simply separated out sevaral of the bits in i_flag into their own
 * ints to avoid problems.  But, instead, we have implemented the following
 * rules:
 *
 *	o You can update any i_flag field while holding the writer-contents.
 *	  You can only call ITIMES_NOLOCK while holding the writer-contents.
 *
 *	o You can update IACC in i_flag while holding the reader-contents lock.
 *	  For a directory, holding the reader-rw_lock is sufficient for setting
 *	  IACC.  In either case, you must call ITIMES to update the times.
 *
 *	o Races with IREF are avoided by holding the reader contents lock
 *	  and by holding i_tlock in ufs_rmidle, ufs_putapage, and ufs_getpage.
 *	  And by holding the writer-contents in ufs_iinactive.
 *
 *	o The callers are no longer required to handle the calls to ITIMES
 *	  and ITIMES_NOLOCK.  The functions that set the i_flag bits are
 *	  responsible for managing those calls.  The exceptions are the
 *	  bmap routines.
 *
 * SVR4 Extended Fundamental Type (EFT) support:
 * 	The inode structure has been enhanced to support
 *	32-bit user-id, 32-bit group-id, and 32-bit device number.
 *	Standard SVR4 ufs also supports 32-bit mode field.  For the reason
 *	of backward compatibility with the previous ufs disk format,
 *	32-bit mode field is not supported.
 *
 *	The current inode structure is 100% backward compatible with
 *	the previous inode structure if no user-id or group-id exceeds
 *	USHRT_MAX, and no major or minor number of a device number
 *	stored in an inode exceeds 255.
 */

#define	UID_LONG  (o_uid_t)65535
				/* flag value to indicate uid is 32-bit long */
#define	GID_LONG  (o_uid_t)65535
				/* flag value to indicate gid is 32-bit long */

#define	NDADDR	12		/* direct addresses in inode */
#define	NIADDR	3		/* indirect addresses in inode */
#define	FSL_SIZE (NDADDR+NIADDR-1) * sizeof (daddr_t)
				/* max fast symbolic name length is 56 */

#define	i_fs	i_ufsvfs->vfs_bufp->b_un.b_fs
#define	i_vfs	i_vnode.v_vfsp

struct 	icommon {
	o_mode_t ic_smode;	/*  0: mode and type of file */
	short	ic_nlink;	/*  2: number of links to file */
	o_uid_t	ic_suid;	/*  4: owner's user id */
	o_gid_t	ic_sgid;	/*  6: owner's group id */
	quad	ic_size;	/*  8: number of bytes in file */
#ifdef _KERNEL
	struct timeval ic_atime; /* 16: time last accessed */
	struct timeval ic_mtime; /* 24: time last modified */
	struct timeval ic_ctime; /* 32: last time inode changed */
#else
	time_t	ic_atime;	/* 16: time last accessed */
	long	ic_atspare;
	time_t	ic_mtime;	/* 24: time last modified */
	long	ic_mtspare;
	time_t	ic_ctime;	/* 32: last time inode changed */
	long	ic_ctspare;
#endif
	daddr_t	ic_db[NDADDR];	/* 40: disk block addresses */
	daddr_t	ic_ib[NIADDR];	/* 88: indirect blocks */
	long	ic_flags;	/* 100: status, currently unused */
	long	ic_blocks;	/* 104: blocks actually held */
	long	ic_gen;		/* 108: generation number */
	long	ic_shadow;	/* 112: shadow inode */
	uid_t	ic_uid;		/* 116: long EFT version of uid */
	gid_t	ic_gid;		/* 120: long EFT version of gid */
	ulong	ic_oeftflag;	/* 124: reserved */
};

struct inode {
	struct	inode *i_chain[2];	/* must be first */
	struct	vnode i_vnode;	/* vnode associated with this inode */
	struct	vnode *i_devvp;	/* vnode for block I/O */
	u_short	i_flag;
	dev_t	i_dev;		/* device where inode resides */
	ino_t	i_number;	/* i number, 1-to-1 with device address */
	off_t	i_diroff;	/* offset in dir, where we found last entry */
	struct	ufsvfs *i_ufsvfs; /* incore fs associated with inode */
	struct	dquot *i_dquot;	/* quota structure controlling this file */
	krwlock_t i_rwlock;	/* serializes write/setattr requests */
	krwlock_t i_contents;	/* protects (most of) inode contents */
	kmutex_t i_tlock;	/* protects time fields, i_flag */
	daddr_t	i_nextr;	/*					*/
				/* next byte read offset (read-ahead)	*/
				/*   No lock required			*/
				/*					*/
	struct inode *i_freef;	/* free list forward */
	struct inode *i_freeb;	/* free list back */
	u_long	i_vcode;	/* version code attribute */
	long	i_mapcnt;	/* mappings to file pages */
	int	*i_map;		/* block list for the corresponding file */
	long	i_rdev;		/* INCORE rdev from i_oldrdev by ufs_iget */
	long	i_delaylen;	/* delayed writes, units=bytes */
	long	i_delayoff;	/* where we started delaying */
	long	i_nextrio;	/* where to start the next clust */
	long	i_writes;	/* number of outstanding bytes in write q */
	kcondvar_t i_wrcv;	/* sleep/wakeup for write throttle */
	kthread_id_t i_owner;	/* prevent races in bmap_write/ufs_itrunc */
	offset_t i_doff;	/* dinode byte offset in file system */
	si_t *i_ufs_acl;	/* pointer to acl entry */
	struct 	icommon	i_ic;
};

struct dinode {
	union {
		struct	icommon di_icom;
		char	di_size[128];
	} di_un;
};

#define	i_mode		i_ic.ic_smode
#define	i_nlink		i_ic.ic_nlink
#define	i_uid		i_ic.ic_uid
#define	i_gid		i_ic.ic_gid
#define	i_smode		i_ic.ic_smode
#define	i_suid		i_ic.ic_suid
#define	i_sgid		i_ic.ic_sgid

#ifdef _LITTLE_ENDIAN
#define	i_size		i_ic.ic_size.val[0]
#define	i_size_o	i_ic.ic_size.val[1]
#else
#define	i_size		i_ic.ic_size.val[1]
#define	i_size_o	i_ic.ic_size.val[0]
#endif
#define	i_db		i_ic.ic_db
#define	i_ib		i_ic.ic_ib

#define	i_atime		i_ic.ic_atime
#define	i_mtime		i_ic.ic_mtime
#define	i_ctime		i_ic.ic_ctime

#define	i_shadow	i_ic.ic_shadow
#define	i_blocks	i_ic.ic_blocks
#ifdef _LITTLE_ENDIAN
/*
 * Originally done on x86, but carried on to all other little
 * architectures, which provides for file system compatibility.
 */
#define	i_ordev		i_ic.ic_db[1]	/* USL SVR4 compatibility */
#else
#define	i_ordev		i_ic.ic_db[0]	/* was i_oldrdev */
#endif
#define	i_gen		i_ic.ic_gen
#define	i_forw		i_chain[0]
#define	i_back		i_chain[1]

#if defined(DEBUG)
#define	i_cksum		i_ic.ic_oeftflag
#define	di_cksum	di_ic.ic_oeftflag
#endif /* DEBUG */

/* EFT transition aids - obsolete */
#define	oEFT_MAGIC	0x90909090
#define	di_oeftflag	di_ic.ic_oeftflag

#define	di_ic		di_un.di_icom
#define	di_mode		di_ic.ic_smode
#define	di_nlink	di_ic.ic_nlink
#define	di_uid		di_ic.ic_uid
#define	di_gid		di_ic.ic_gid
#define	di_smode	di_ic.ic_smode
#define	di_suid		di_ic.ic_suid
#define	di_sgid		di_ic.ic_sgid

#ifdef _LITTLE_ENDIAN
#define	di_size		di_ic.ic_size.val[0]
#else
#define	di_size		di_ic.ic_size.val[1]
#endif
#define	di_db		di_ic.ic_db
#define	di_ib		di_ic.ic_ib

#define	di_atime	di_ic.ic_atime
#define	di_mtime	di_ic.ic_mtime
#define	di_ctime	di_ic.ic_ctime

#ifdef _LITTLE_ENDIAN
#define	di_ordev	di_ic.ic_db[1]
#else
#define	di_ordev	di_ic.ic_db[0]
#endif
#define	di_shadow	di_ic.ic_shadow
#define	di_blocks	di_ic.ic_blocks
#define	di_gen		di_ic.ic_gen

/* flags */
#define	IUPD		0x0001		/* file has been modified */
#define	IACC		0x0002		/* inode access time to be updated */
#define	IMOD		0x0004		/* inode has been modified */
#define	ICHG		0x0008		/* inode has been changed */
#define	INOACC		0x0010		/* no access time update in getpage */
#define	IMODTIME	0x0020		/* mod time already set */
#define	IREF		0x0040		/* inode is being referenced */
#define	ISYNC		0x0080		/* do all allocation synchronously */
#define	IFASTSYMLNK	0x0100		/* fast symbolic link */
#define	IMODACC		0x0200		/* only access time changed; */
					/*   filesystem won't become active */
#define	IATTCHG		0x0400		/* only size/blocks have changed */
#define	IBDWRITE	0x0800		/* the inode has been scheduled for */
					/* write operation asynchrously */
#define	IBAD		0x1000		/* invalid inode contents */

/* modes */
#define	IFMT		0170000		/* type of file */
#define	IFIFO		0010000		/* named pipe (fifo) */
#define	IFCHR		0020000		/* character special */
#define	IFDIR		0040000		/* directory */
#define	IFBLK		0060000		/* block special */
#define	IFREG		0100000		/* regular */
#define	IFLNK		0120000		/* symbolic link */
#define	IFSHAD		0130000		/* shadow indode */
#define	IFSOCK		0140000		/* socket */

#define	ISUID		04000		/* set user id on execution */
#define	ISGID		02000		/* set group id on execution */
#define	ISVTX		01000		/* save swapped text even after use */
#define	IREAD		0400		/* read, write, execute permissions */
#define	IWRITE		0200
#define	IEXEC		0100

/* specify how the inode info is written in ufs_syncip() */
#define	I_SYNC		1		/* wait for the inode written to disk */
#define	I_DSYNC		2		/* wait for the inode written to disk */
					/* only if IATTCHG is set */
#define	I_ASYNC		0		/* don't wait for the inode written */

/* flags passed to ufs_itrunc(), indirtrunc(), and free() */
#define	I_FREE	0x00000001		/* inode is being freed */
#define	I_DIR	0x00000002		/* inode is a directory */
#define	I_IBLK	0x00000004		/* indirect block */
#define	I_CHEAP	0x00000008		/* cheap free */
#define	I_SHAD	0x00000010		/* inode is a shadow inode */
#define	I_QUOTA	0x00000020		/* quota file */

/*
 * Statistics on inodes
 * Not protected by locks
 */
struct instats {
	kstat_named_t in_size;		/* current cache size */
	kstat_named_t in_maxsize;	/* maximum cache size */
	kstat_named_t in_hits;		/* cache hits */
	kstat_named_t in_misses;	/* cache misses */
	kstat_named_t in_malloc;	/* kmem_alloce'd */
	kstat_named_t in_mfree;		/* kmem_free'd */
	kstat_named_t in_maxreached;	/* Largest size reached by cache */
	kstat_named_t in_frfront;	/* # put at front of freelist */
	kstat_named_t in_frback;	/* # put at back of freelist */
	kstat_named_t in_qfree;		/* q's to free thread */
	kstat_named_t in_scan;		/* # inodes scanned */
};

#ifdef _KERNEL
extern int	ufs_ninode;		/* high-water mark for inode cache */
extern int	ufs_idle_max;

extern struct vnodeops ufs_vnodeops;	/* vnode operations for ufs */

/*
 * Convert between inode pointers and vnode pointers
 */
#define	VTOI(VP)	((struct inode *)(VP)->v_data)
#define	ITOV(IP)	((struct vnode *)&(IP)->i_vnode)

/*
 * convert to fs
 */
#define	ITOF(IP)	((struct fs *)(IP)->i_fs)

/*
 * Convert between vnode types and inode formats
 */
extern enum vtype	iftovt_tab[];

#ifdef notneeded

/* Look at sys/mode.h and os/vnode.c */

extern int		vttoif_tab[];

#endif

/*
 * Mark an inode with the current (unique) timestamp.
 */
struct timeval iuniqtime;

#define	ITIMES_NOLOCK(ip) ufs_itimes_nolock(ip)

#define	ITIMES(ip) { \
	mutex_enter(&(ip)->i_tlock); \
	ITIMES_NOLOCK(ip); \
	mutex_exit(&(ip)->i_tlock); \
}

/*
 * Allocate the specified block in the inode
 * and make sure any in-core pages are initialized.
 */
#define	BMAPALLOC(ip, off, size, cr) \
	bmap_write((ip), (off), (daddr_t *)NULL, (int *)0, (size), 0, cr)

#define	ESAME	(-1)		/* trying to rename linked files (special) */

/*
 * Check that file is owned by current user or user is su.
 */
#define	OWNER(CR, IP) (((CR)->cr_uid == (IP)->i_uid)? 0: (suser(CR)? 0: EPERM))

#define	UFS_HOLE	(daddr_t)-1	/* value used when no block allocated */

/*
 * enums
 */
enum de_op   { DE_CREATE, DE_MKDIR, DE_LINK, DE_RENAME };  /* direnter ops */
enum dr_op   { DR_REMOVE, DR_RMDIR, DR_RENAME };	   /* dirremove ops */

/*
 * This overlays the fid structure (see vfs.h)
 */
struct ufid {
	u_short	ufid_len;
	ino_t	ufid_ino;
	long	ufid_gen;
};

/*
 * each ufs thread (see ufs_thread.c) is managed by this struct
 */
struct ufs_q {
	union uq_head {
		void		*_uq_generic;	/* first entry on q */
		struct inode	*_uq_i;
		ufs_failure_t	*_uq_uf;
	} _uq_head;
	long		uq_ne;		/* # of entries/failures found */
	long		uq_maxne;	/* thread runs when ne == maxne */
	u_short		uq_flags;	/* flags */
	kcondvar_t	uq_cv;		/* for sleep/wakeup */
	kthread_id_t	uq_threadp;	/* thread managing this q */
	kmutex_t	uq_mutex;	/* protects this struct */
};

#define	uq_head		_uq_head._uq_generic
#define	uq_ihead	_uq_head._uq_i
#define	uq_ufhead	_uq_head._uq_uf

/*
 * uq_flags
 */
#define	UQ_EXIT		(0x0001)	/* q server exits at its convenience */
#define	UQ_WAIT		(0x0002)	/* thread is waiting on q server */
#define	UQ_SUSPEND	(0x0004)	/* request for suspension */
#define	UQ_SUSPENDED	(0x0008)	/* thread has suspended itself */
#define	UQ_EXISTS	(0x0010)	/* thread exists */

/*
 * global list of idle inodes (pseudo LRU list)
 */
extern struct ufs_q	ufs_idle_q;	/* used by global ufs idle thread */
extern struct ufs_q	ufs_hlock;	/* used by global ufs hlock thread */

/*
 * UFS VFS private data.
 */
struct ufsvfs {
	struct ufsvfs	*vfs_next;	/* forcible unmount list	*/
	struct vnode	*vfs_root;	/* root vnode			*/
	struct buf	*vfs_bufp;	/* buffer containing superblock */
	struct vnode	*vfs_devvp;	/* block device vnode		*/
	struct inode	*vfs_qinod;	/* QUOTA: pointer to quota file */
	u_short		vfs_qflags;	/* QUOTA: filesystem flags	*/
	u_long		vfs_btimelimit;	/* QUOTA: block time limit	*/
	u_long		vfs_ftimelimit;	/* QUOTA: file time limit	*/
	/*
	 * some fs local threads
	 */
	struct ufs_q	vfs_delete;	/* delayed inode delete */
	struct ufs_q	vfs_reclaim;	/* reclaim open, deleted files */
	/*
	 * These are copied from the super block at mount time.
	 */
	long		vfs_nrpos;	/* # rotational positions */
	long		vfs_npsect;	/* # sectors/track including spares */
	long		vfs_interleave;	/* hardware sector interleave */
	long		vfs_trackskew;	/* sector 0 skew, per track */
	/*
	 * This lock protects cg's and super block pointed at by
	 * vfs_bufp->b_fs.  Locks contents of fs and cg's and contents
	 * of vfs_dio.
	 */
	kmutex_t	vfs_lock;
	struct ulockfs	vfs_ulockfs;	/* ufs lockfs support */
	u_long		vfs_dio;	/* delayed io (_FIODIO) */
	u_long		vfs_nointr;	/* disallow lockfs interrupts */
	u_long		vfs_nosetsec;	/* disallow ufs_setsecattr */
	u_long		vfs_syncdir;	/* synchronous local directory ops */
	struct ufstrans *vfs_trans;	/* transaction stuff */
	u_long		vfs_domatamap;	/* transaction stuff */
	u_long		vfs_maxacl;	/* transaction stuff - max acl size */
	u_long		vfs_smallwrite;	/* transaction stuff - 8K log space  */
	u_long		vfs_maxwrite;	/* transaction stuff - maximum write */
	u_long		vfs_dirsize;	/* logspace for directory creation */
	/*
	 * Some useful constants
	 */
	int	vfs_nindirshift;	/* calc. from fs_nindir */
	int	vfs_nindiroffset;	/* calc. from fs_ninidr */
	int	vfs_rdclustsz;		/* bytes in read cluster */
	int	vfs_wrclustsz;		/* bytes in write cluster */

	vfs_ufsfx_t	vfs_fsfx;	/* lock/fix-on-panic support */
};

#define	vfs_fs	vfs_bufp->b_un.b_fs

/* inohsz is guaranteed to be a power of 2 */
#define	INOHASH(dev, ino)	(hash2ints((int)dev, (int)ino) & (inohsz - 1))

union ihead {
	union	ihead	*ih_head[2];
	struct	inode	*ih_chain[2];
};

extern	union	ihead	*ihead;
extern  kmutex_t	*ih_lock;
extern  int	*ih_ne;
extern	int	inohsz;

#endif /* _KERNEL */

/*
 * ufs function prototypes
 */
#if defined(_KERNEL)

extern	int	ufs_iget(struct vfs *, ino_t, struct inode **, cred_t *);
extern	void	ufs_iinactive(struct inode *);
extern	void	ufs_iupdat(struct inode *, int);
extern	void	ufs_iput(struct inode *);
extern	void	ufs_rmidle(struct inode *);
extern	int	ufs_itrunc(struct inode *, u_long, int, cred_t *);
extern	int	ufs_iaccess(struct inode *, int, cred_t *);
extern  int	rwip(struct inode *, struct uio *, enum uio_rw, int,
		struct cred *);

extern void	ufs_imark(struct inode *);
extern void	ufs_itimes_nolock(struct inode *);

extern	int	ufs_dirlook(struct inode *, char *, struct inode **,
    cred_t *, int);
extern	int	ufs_direnter(struct inode *, char *, enum de_op, struct inode *,
    struct inode *, struct vattr *, struct inode **, cred_t *);
extern	int	ufs_dirremove(struct inode *, char *, struct inode *,
    struct vnode *, enum dr_op, cred_t *);

extern	void	sbupdate(struct vfs *);

extern	int	ufs_ialloc(struct inode *, ino_t, mode_t, struct inode **,
    cred_t *);
extern	void	ufs_ifree(struct inode *, ino_t, mode_t);
extern	void	free(struct inode *, daddr_t, off_t, int);
extern	int	alloc(struct inode *, daddr_t, int, daddr_t *, cred_t *);
extern	int	realloccg(struct inode *, daddr_t, daddr_t, int, int,
    daddr_t *, cred_t *);
extern	int	ufs_freesp(struct vnode *, struct flock *, int, cred_t *);
extern	ino_t	dirpref(struct ufsvfs *);
extern	daddr_t	blkpref(struct inode *, daddr_t, int, daddr_t *);

extern	int	ufs_rdwri(enum uio_rw, int, struct inode *, caddr_t, int, off_t,
    enum uio_seg, int *, cred_t *);

extern	int	bmap_read(struct inode *, u_int, daddr_t *, int *);
extern	int	bmap_write(struct inode *, u_int, daddr_t *, int *, int,
    int, struct cred *);
extern	int	bmap_has_holes(struct inode *);

extern	void	ufs_sbwrite(struct ufsvfs *);
extern	void	ufs_update(int);
extern	int	ufs_getsummaryinfo(dev_t, struct ufsvfs *, struct fs *);
extern	int	ufs_syncip(struct inode *, int, int);
extern	int	ufs_sync_indir(struct inode *);
extern	int	ufs_indirblk_sync(struct inode *, long);
extern	int	ufs_badblock(struct inode *, daddr_t);
extern	int	ufs_indir_badblock(struct inode *, daddr_t *);
extern	void	ufs_notclean(struct ufsvfs *);
extern	void	ufs_checkclean(struct vfs *);
extern	int	isblock(struct fs *, u_char *, daddr_t);
extern	void	setblock(struct fs *, u_char *, daddr_t);
extern	void	clrblock(struct fs *, u_char *, daddr_t);
extern	int	isclrblock(struct fs *, u_char *, daddr_t);
extern	void	fragacct(struct fs *, int, long *, int);
extern	int	skpc(char, u_int, char *);
extern	int	scanc(u_int, u_char *, u_char *, u_char);
extern	int	ufs_fbwrite(struct fbuf *, struct inode *);
extern	int	ufs_fbiwrite(struct fbuf *, struct inode *, daddr_t, long);
extern	int	ufs_putapage(struct vnode *, struct page *, u_int *, u_int *,
    int, struct cred *);

/*
 * special stuff
 */
extern	void	ufs_setreclaim(struct inode *);
extern	int	ufs_scan_inodes(int, int (*)(struct inode *, void *), void *);
extern	int	ufs_sync_inode(struct inode *, void *);

/*
 * quota
 */
extern	int	chkiq(struct ufsvfs *, struct inode *, uid_t, int,
			struct cred *);

/*
 * ufs thread stuff
 */
extern	void	ufs_thread_delete(struct vfs *);
extern	void	ufs_delete_drain(struct vfs *, int, int);
extern	void	ufs_delete(struct ufsvfs *, struct inode *, int);
extern	void	ufs_thread_idle(void);
extern	void	ufs_idle_drain(struct vfs *);
extern	void	ufs_thread_reclaim(struct vfs *);
extern	void	ufs_thread_init(struct ufs_q *, long);
extern	void	ufs_thread_start(struct ufs_q *, void (*)(), struct vfs *);
extern	void	ufs_thread_exit(struct ufs_q *);
extern	void	ufs_thread_suspend(struct ufs_q *);
extern	void	ufs_thread_continue(struct ufs_q *);
extern	void	ufs_thread_hlock(void *);
/*
 * ufs lockfs stuff
 */
struct seg;
extern int ufs_reconcile_fs(struct vfs *, struct ufsvfs *, int);
extern int ufs_quiesce(struct ulockfs *);
extern int ufs_flush(struct vfs *);
extern int ufs_fiolfs(struct vnode *, struct lockfs *);
extern int ufs__fiolfs(struct vnode *, struct lockfs *, int);
extern int ufs_fiolfss(struct vnode *, struct lockfs *);
extern int ufs_fioffs(struct vnode *, char *, struct cred *);
extern int ufs_check_lockfs(struct ufsvfs *, struct ulockfs *, u_long);
extern int ufs_lockfs_begin(struct ufsvfs *, struct ulockfs **, u_long);
extern int ufs_lockfs_begin_getpage(struct ufsvfs *, struct ulockfs **,
		struct seg *, int, u_int *);
extern void ufs_lockfs_end(struct ulockfs *);
/*
 * ufs acl stuff
 */
extern int ufs_si_inherit(struct inode *, struct inode *, o_mode_t, cred_t *);
extern void si_cache_init(void);
extern int ufs_si_load(struct inode *, cred_t *);
extern void ufs_si_del(struct inode *);
extern int ufs_acl_access(struct inode *, int, cred_t *);
extern void ufs_si_cache_flush(dev_t);
extern int ufs_si_free(si_t *, struct vfs *, cred_t *);
extern int ufs_acl_setattr(struct inode *, struct vattr *, cred_t *);
extern int ufs_acl_get(struct inode *, vsecattr_t *, int, cred_t *);
extern int ufs_acl_set(struct inode *, vsecattr_t *, int, cred_t *);

#if defined(DEBUG)
extern	int	ufs_ivalid(struct inode *);
extern	int	ufs_cksum(char *, int, void *, struct fs *);
extern	ulong	ufs_icksum(struct inode *);
extern	void	ufs_cgcheck(char *, struct ufsvfs *, struct cg *);
extern	void	ufs_debug_icache_linecheck(int hno);
extern	void	ufs_debug_icache_check(void);

#define	CK_INO	2	/* do inode checksums */

#define	SET	0	/* for sequence check/set routines */
#define	CHK	1

#define	set_icksum(ip)		(ip)->i_cksum = ufs_icksum(ip)

#define	UDBG_OFF		0x000	/* no ufs debugging */
#define	UFS_DEBUG_PANIC		0x001	/* if any problem found, panic */
#define	UDBG_ICKSUM		0x002	/* do inode checksumming */
#define	UDBG_IVALID		0x004	/* do inode validity checking */
#define	UDBG_ICKCACHE		0x008	/* do inode cache checking */
#define	UFS_DEBUG_CGCHECK	0x010	/* do cylinder group sanity checking */

#define	UFS_DEBUG_ICACHE_CHECK()	ufs_debug_icache_check()
#define	UFS_DEBUG_ICACHE_LINECHECK(H)	ufs_debug_icache_linecheck((H))
#define	UFS_DEBUG_ICACHE_ZALLOC(SZ, F)	(kmem_zalloc((SZ), (F)))
#define	UFS_DEBUG_ICACHE_INC(H)		(ih_ne[(H)]++)
#define	UFS_DEBUG_ICACHE_DEC(H)		(--ih_ne[(H)])

extern int ufs_debug;

#else	/* DEBUG */

#define	UFS_DEBUG_ICACHE_CHECK()
#define	UFS_DEBUG_ICACHE_LINECHECK(H)
#define	UFS_DEBUG_ICACHE_ZALLOC(SZ, F)	0
#define	UFS_DEBUG_ICACHE_INC(H)
#define	UFS_DEBUG_ICACHE_DEC(H)

#endif /* DEBUG */

#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_UFS_INODE_H */
