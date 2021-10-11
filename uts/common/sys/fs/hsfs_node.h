/*
 * High Sierra filesystem structure definitions
 * Copyright (c) 1989, 1990 by Sun Microsystem, Inc.
 */

#ifndef	_SYS_FS_HSFS_NODE_H
#define	_SYS_FS_HSFS_NODE_H

#pragma ident	"@(#)hsfs_node.h	1.22	95/02/26 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

struct	hs_direntry {
	u_int		ext_lbn;	/* LBN of start of extent */
	u_int		ext_size;    	/* no. of data bytes in extent */
	struct timeval	cdate;		/* creation date */
	struct timeval	mdate;		/* last modification date */
	struct timeval	adate;		/* last access date */
	enum vtype	type;		/* file type */
	mode_t		mode;		/* mode and type of file (UNIX) */
	u_int		nlink;		/* no. of links to file */
	uid_t		uid;		/* owner's user id */
	gid_t		gid;		/* owner's group id */
	dev_t		r_dev;		/* major/minor device numbers */
	u_int		xar_prot :1;	/* 1 if protection in XAR */
	u_char		xar_len;	/* no. of Logical blocks in XAR */
	u_char		intlf_sz;	/* intleaving size */
	u_char		intlf_sk;	/* intleaving skip factor */
	u_short		sym_link_flag;	/* flags for sym link */
	char		*sym_link; 	/* path of sym link for readlink() */
};

struct	ptable {
	u_char	filler[7];		/* filler */
	u_char	dname_len;		/* length of directory name */
	u_char	dname[HS_DIR_NAMELEN+1];	/* directory name */
};

struct ptable_idx {
	struct ptable_idx *idx_pptbl_idx; /* parent's path table index entry */
	struct ptable	*idx_mptbl;	/* path table entry for myself */
	u_short	idx_nochild;		/* no. of children */
	u_short idx_childid;		/* directory no of first child */
};

/*
 * hsnode structure:
 *
 * hs_offset, hs_ptbl_idx, base  apply to VDIR type only
 *
 * nodeid uniquely identifies an hsnode.
 * For directories it is the disk address of
 * the data extent of the dir (the directory itself,
 * ".", and ".." all point to same data extent).
 * For non-directories, it is the disk address of the
 * directory entry for the file; note that this does
 * not permit hard links, as it assumes a single dir
 * entry per file.
 */

struct  hsnode {
	struct hsnode	*hs_hash;	/* next hsnode in hash list */
	struct hsnode	*hs_freef;	/* next hsnode in free list */
	struct hsnode	*hs_freeb;	/* previous hsnode in free list */
	struct vnode	hs_vnode;	/* the real vnode for the file */
	struct hs_direntry hs_dirent;	/* the directory entry for this file */
	u_long		hs_nodeid;	/* "inode" number for hsnode */
	u_int		hs_dir_lbn;	/* LBN of directory entry */
	u_int		hs_dir_off;	/* offset in LBN of directory entry */
	struct ptable_idx	*hs_ptbl_idx;	/* path table index */
	u_int		hs_offset;	/* start offset in dir for searching */
	long		hs_mapcnt;	/* mappings to file pages */
	u_long		hs_vcode;	/* version code */
	u_int		hs_flags;	/* (see below) */
	kmutex_t	hs_contents_lock;	/* protects hsnode contents */
						/* 	except hs_offset */
};

/* hs_flags */
#define	HREF	1			/* hsnode is referenced */

struct  hsfid {
	u_short		hf_len;		/* length of fid */
	u_short		hf_dir_off;	/* offset in LBN of directory entry */
	u_long		hf_dir_lbn;	/* LBN of directory */
};


/*
 * All of the fields in the hs_volume are read-only once they have been
 * initialized.
 */
struct	hs_volume {
	u_long		vol_size; 	/* no. of Logical blocks in Volume */
	u_int		lbn_size;	/* no. of bytes in a block */
	u_int		lbn_shift;	/* shift to convert lbn to bytes */
	u_int		lbn_secshift;	/* shift to convert lbn to sec */
	u_int		lbn_maxoffset;	/* max lbn-relative offset and mask */
	u_char		file_struct_ver; /* version of directory structure */
	uid_t		vol_uid;	/* uid of volume */
	gid_t		vol_gid;	/* gid of volume */
	u_int		vol_prot;	/* protection (mode) of volume */
	struct timeval	cre_date;	/* volume creation time */
	struct timeval	mod_date;	/* volume modification time */
	struct	hs_direntry root_dir;	/* dir entry for Root Directory */
	u_short		ptbl_len;	/* number of bytes in Path Table */
	u_int		ptbl_lbn;	/* logical block no of Path Table */
	u_short		vol_set_size;	/* number of CD in this vol set */
	u_short		vol_set_seq;	/* the sequence number of this CD */
	char		vol_id[32];		/* volume id in PVD */
};

/*
 * hsnode incore table
 *
 */
#define	HS_HASHSIZE	32		/* hsnode hash table size */
#define	HS_HSTABLESIZE	16384		/* hsnode incore table size */

struct hstable {
	struct  vfs *hs_vfsp;		/* point back to vfs */
	int	hs_tablesize;		/* size of the hstable */
	krwlock_t hshash_lock;		/* protect hash table */
	kmutex_t hsfree_lock;		/* protect free list */
	struct	hsnode	*hshash[HS_HASHSIZE];	/* head of hash lists */
	struct	hsnode	*hsfree_f;		/* first entry of free list */
	struct	hsnode	*hsfree_b;		/* last entry of free list */
	int	hs_nohsnode;		/* no. of hsnode in table */
	struct	hsnode	hs_node[1];		/* should be much more */
};

/*
 * High Sierra filesystem structure.
 * There is one of these for each mounted High Sierra filesystem.
 */
enum hs_vol_type {
	HS_VOL_TYPE_HS = 0, HS_VOL_TYPE_ISO = 1
};
#define	HSFS_MAGIC 0x03095500
struct hsfs {
	struct hsfs	*hsfs_next;	/* ptr to next entry in linked list */
	long		hsfs_magic;	/* should be HSFS_MAGIC */
	struct vfs	*hsfs_vfs;	/* vfs for this fs */
	struct vnode	*hsfs_rootvp;	/* vnode for root of filesystem */
	struct vnode	*hsfs_devvp;	/* device mounted on */
	enum hs_vol_type hsfs_vol_type; /* 0 hsfs 1 iso 2 hsfs+sun 3 iso+sun */
	struct hs_volume hsfs_vol;	/* File Structure Volume Descriptor */
	struct ptable	*hsfs_ptbl;	/* pointer to incore Path Table */
	int		hsfs_ptbl_size;	/* size of incore path table */
	struct ptable_idx *hsfs_ptbl_idx; /* pointer to path table index */
	int		hsfs_ptbl_idx_size;	/* no. of path table index */
	struct hstable	*hsfs_hstbl;	/* pointer to incore hsnode table */
	u_long		ext_impl_bits;	/* ext. information bits */
	u_short		sua_offset;	/* the SUA offset */
	u_short		hsfs_namemax;	/* maximum file name length */
	u_long		hsfs_err_flags;	/* ways in which fs is non-conformant */
	char		*hsfs_fsmnt;	/* name mounted on */
	u_long		hsfs_flags;	/* hsfs-specific mount flags */
};

/*
 * Error types: bit offsets into hsfs_err_flags.
 * Also serves as index into hsfs_error[], so must be
 * kept in sync with that data structure.
 */
#define	HSFS_ERR_TRAILING_JUNK	0
#define	HSFS_ERR_LOWER_CASE_NM	1
#define	HSFS_ERR_BAD_ROOT_DIR	2
#define	HSFS_ERR_UNSUP_TYPE	3
#define	HSFS_ERR_BAD_FILE_LEN	4

#define	HSFS_HAVE_LOWER_CASE(fsp) \
	((fsp)->hsfs_err_flags & (1 << HSFS_ERR_LOWER_CASE_NM))


/*
 * File system parameter macros
 */
#define	hs_blksize(HSFS, HSP, OFF)	/* file system block size */ \
	((HSP)->hs_vn.v_flag & VROOT ? \
	    ((OFF) >= \
		((HSFS)->hsfs_rdirsec & ~((HSFS)->hsfs_spcl - 1))*HS_SECSIZE ?\
		((HSFS)->hsfs_rdirsec & ((HSFS)->hsfs_spcl - 1))*HS_SECSIZE :\
		(HSFS)->hsfs_clsize): \
	    (HSFS)->hsfs_clsize)
#define	hs_blkoff(OFF)		/* offset within block */ \
	((OFF) & (HS_SECSIZE - 1))

/*
 * Conversion macros
 */
#define	VFS_TO_HSFS(VFSP)	((struct hsfs *)(VFSP)->vfs_data)
#define	HSFS_TO_VFS(FSP)	((FSP)->hsfs_vfs)

#define	VTOH(VP)		((struct hsnode *)(VP)->v_data)
#define	HTOV(HP)		(&((HP)->hs_vnode))

/*
 * Convert between Logical Block Number and Sector Number.
 */
#define	LBN_TO_SEC(lbn, vfsp)	((lbn)>>((struct hsfs *)((vfsp)->vfs_data))->  \
				hsfs_vol.lbn_secshift)

#define	SEC_TO_LBN(sec, vfsp)	((sec)<<((struct hsfs *)((vfsp)->vfs_data))->  \
				hsfs_vol.lbn_secshift)

#define	LBN_TO_BYTE(lbn, vfsp)	((lbn)<<((struct hsfs *)((vfsp)->vfs_data))->  \
				hsfs_vol.lbn_shift)
#define	BYTE_TO_LBN(boff, vfsp)	((boff)>>((struct hsfs *)((vfsp)->vfs_data))-> \
				hsfs_vol.lbn_shift)
#define	BYTE_TO_LBN(boff, vfsp)	((boff)>>((struct hsfs *)((vfsp)->vfs_data))-> \
				hsfs_vol.lbn_shift)

/*
 * Create a nodeid.
 * We construct the nodeid from the location of the directory
 * entry which points to the file.  We divide by 32 to
 * compress the range of nodeids; we know that the minimum size
 * for an ISO9660 dirent is 34, so we will never have adjacent
 * dirents with the same nodeid.
 */
#define	HSFS_MIN_DL_SHFT	5
#define	MAKE_NODEID(lbn, off, vfsp) \
		((LBN_TO_BYTE((lbn), (vfsp)) + (off)) >> HSFS_MIN_DL_SHFT)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_HSFS_NODE_H */
