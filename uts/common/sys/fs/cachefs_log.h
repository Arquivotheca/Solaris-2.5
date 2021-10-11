/*
 * Copyright (c) 1994, Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_FS_CACHEFS_LOG_H
#define	_SYS_FS_CACHEFS_LOG_H

#pragma ident	"@(#)cachefs_log.h	1.10	94/09/28 SMI"

#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/fs/cachefs_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* constants, etc. */

#define	CACHEFS_LOG_MAGIC	32321
#define	CACHEFS_LOG_FILE_REV	    1

#define	CACHEFS_LOG_MOUNT		 1
#define	CACHEFS_LOG_UMOUNT		 2
#define	CACHEFS_LOG_GETPAGE		 3
#define	CACHEFS_LOG_READDIR		 4
#define	CACHEFS_LOG_READLINK		 5
#define	CACHEFS_LOG_REMOVE		 6
#define	CACHEFS_LOG_RMDIR		 7
#define	CACHEFS_LOG_TRUNCATE		 8
#define	CACHEFS_LOG_PUTPAGE		 9
#define	CACHEFS_LOG_CREATE		10
#define	CACHEFS_LOG_MKDIR		11
#define	CACHEFS_LOG_RENAME		12
#define	CACHEFS_LOG_SYMLINK		13
#define	CACHEFS_LOG_POPULATE		14
#define	CACHEFS_LOG_CSYMLINK		15
#define	CACHEFS_LOG_FILLDIR		16
#define	CACHEFS_LOG_MDCREATE		17
#define	CACHEFS_LOG_GPFRONT		18
#define	CACHEFS_LOG_RFDIR		19
#define	CACHEFS_LOG_UALLOC		20
#define	CACHEFS_LOG_CALLOC		21
#define	CACHEFS_LOG_NOCACHE		22
#define	CACHEFS_LOG_NUMRECS		22

/*
 * for communicating from user to kernel, or for storing state.
 */

typedef struct cachefs_log_control {
	int	lc_magic;
	char	lc_path[MAXPATHLEN];
	u_char	lc_which[(CACHEFS_LOG_NUMRECS / NBBY) + 1];
	void	*lc_cachep; /* really cachefscache_t * */
} cachefs_log_control_t;

/*
 * per-cachefscache information
 */

typedef struct cachefs_log_cookie {
	void		*cl_head;	/* head of records to be written */
	void		*cl_tail;	/* tail of records to be written */
	u_int		cl_size;	/* # of bytes to be written */

	struct vnode	*cl_logvp;	/* vnode for logfile */

	cachefs_log_control_t *cl_logctl; /* points at ksp->ks_data */

	int		cl_magic;	/* cheap sanity check */
} cachefs_log_cookie_t;

/* macros for determining which things we're logging + misc stuff */
#define	CACHEFS_LOG_LOGGING(cp, which)				\
	((cp != NULL) &&					\
	(cp->c_log != NULL) &&				\
	(cp->c_log_ctl->lc_which[which / NBBY] &	\
	(1 << (which % NBBY))))
#define	CACHEFS_LOG_SET(lc, which)	\
	(lc->lc_which[which / NBBY] |= (1 << (which % NBBY)))
#define	CACHEFS_LOG_CLEAR(lc, which)	\
	(lc->lc_which[which / NBBY] &= ~(1 << (which % NBBY)))
#define	CLPAD(sname, field)			\
	((sizeof (struct sname)) -		\
	(int) (&(((struct sname *)0)->field)) -	\
	(sizeof (((struct sname *)0)->field)))

struct cachefs_log_logfile_header {
	u_int lh_magic;
	u_int lh_revision;
	int lh_errno;
	u_int lh_blocks;
	u_int lh_files;
	u_int lh_maxbsize;
	u_int lh_pagesize;
};

/*
 * declarations of the logging records.
 *
 * note -- the first three fields must be int, int, and time_t,
 * corresponding to record type, error status, and timestamp.
 *
 * note -- the size of a trailing string should be large enough to
 * hold any necessary null-terminating bytes.  i.e. for one string,
 * say `char foo[1]'.  for two strings, null-separated, say `char
 * foo[2]'.
 */

struct cachefs_log_mount_record {
	int type;		/* == CACHEFS_LOG_MOUNT */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* vfs pointer -- unique while mounted */
	u_int flags;		/* opt_flags from cachefsoptions */
	u_int popsize;		/* opt_popsize from cachefsoptions */
	u_int fgsize;		/* opt_fgsize from cachefsoptions */
	u_short pathlen;	/* length of path */
	u_short cacheidlen;	/* length of cacheid */
	char path[2];		/* the path of the mountpoint, and cacheid */
};

struct cachefs_log_umount_record {
	int type;		/* == CACHEFS_LOG_UMOUNT */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* vfs pointer we're unmounting */
};

struct cachefs_log_getpage_record {
	int type;		/* == CACHEFS_LOG_GETPAGE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* file identifier */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
	u_int offset;		/* offset we're getting */
	u_int len;		/* how many bytes we're getting */
};

struct cachefs_log_readdir_record {
	int type;		/* == CACHEFS_LOG_READDIR */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* file identifier */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
	off_t offset;		/* offset into directory */
	int eof;		/* like `*eofp' in VOP_READDIR */
};

struct cachefs_log_readlink_record {
	int type;		/* == CACHEFS_LOG_READLINK */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* file identifier */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
	size_t length;		/* length of symlink */
};

struct cachefs_log_remove_record {
	int type;		/* == CACHEFS_LOG_REMOVE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of file being removed */
				/* (not the directory holding the file) */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
};

struct cachefs_log_rmdir_record {
	int type;		/* == CACHEFS_LOG_RMDIR */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of directory being removed */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
};

struct cachefs_log_truncate_record {
	int type;		/* == CACHEFS_LOG_TRUNCATE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* file being truncated */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
	size_t size;		/* new size */
};

struct cachefs_log_putpage_record {
	int type;		/* == CACHEFS_LOG_PUTPAGE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* file being written */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
	u_int offset;		/* offset */
	u_int len;		/* length */
};

struct cachefs_log_create_record {
	int type;		/* == CACHEFS_LOG_CREATE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of newly created file */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
};

struct cachefs_log_mkdir_record {
	int type;		/* == CACHEFS_LOG_MKDIR */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of newly created directory */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
};

struct cachefs_log_rename_record {
	int type;		/* == CACHEFS_LOG_RENAME */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t gone;		/* fid of file removed (may be undefined) */
	u_long fileno;		/* fileno */
	int removed;		/* nonzero if file was removed */
	long uid;		/* uid of credential */
};

struct cachefs_log_symlink_record {
	int type;		/* == CACHEFS_LOG_SYMLINK */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of newly created symlink */
	u_long fileno;		/* fileno */
	size_t size;		/* size of newly created symlink */
	long uid;		/* uid of credential */
};

struct cachefs_log_populate_record {
	int type;		/* == CACHEFS_LOG_POPULATE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of file being populated */
	u_long fileno;		/* fileno */
	u_int off;		/* offset */
	int size;		/* popsize */
};

struct cachefs_log_csymlink_record {
	int type;		/* == CACHEFS_LOG_CSYMLINK */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of symlink being cached */
	u_long fileno;		/* fileno */
	int size;		/* size of symlink being cached */
};

struct cachefs_log_filldir_record {
	int type;		/* == CACHEFS_LOG_FILLDIR */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of directory being filled */
	u_long fileno;		/* fileno */
	int size;		/* size of frontfile after filling */
};

struct cachefs_log_mdcreate_record {
	int type;		/* == CACHEFS_LOG_MDCREATE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of file for whom md slot is created */
	u_long fileno;		/* fileno */
	u_int count;		/* new number of entries in attrcache */
};

struct cachefs_log_gpfront_record {
	int type;		/* == CACHEFS_LOG_GPFRONT */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of file for whom md slot is created */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
	u_int off;		/* offset */
	u_int len;		/* length */
};

struct cachefs_log_rfdir_record {
	int type;		/* == CACHEFS_LOG_GPFRONT */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of directory */
	u_long fileno;		/* fileno */
	long uid;		/* uid of credential */
};

struct cachefs_log_ualloc_record {
	int type;		/* == CACHEFS_LOG_UALLOC */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of allocmap-updated file */
	u_long fileno;		/* fileno of allocmap-updated file */
	u_int off;		/* offset of new area */
	u_int len;		/* length of new area */
};

struct cachefs_log_calloc_record {
	int type;		/* == CACHEFS_LOG_CALLOC */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of allocmap-checked file */
	u_long fileno;		/* fileno of allocmap-checked file */
	u_int off;		/* offset of successful check_allocmap */
	u_int len;		/* length of successful check_allocmap */
};

struct cachefs_log_nocache_record {
	int type;		/* == CACHEFS_LOG_NOCACHE */
	int error;		/* errno */
	time_t time;		/* timestamp */
	caddr_t vfsp;		/* which filesystem */
	fid_t fid;		/* fid of file being nocached */
	u_long fileno;		/* fileno of file being nocached */
};

#ifdef __cplusplus
}
#endif


#endif /* _SYS_FS_CACHEFS_LOG_H */
