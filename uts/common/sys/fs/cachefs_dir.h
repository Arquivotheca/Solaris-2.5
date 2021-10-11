/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_FS_CACHEFS_DIR_H
#define	_SYS_FS_CACHEFS_DIR_H

#pragma ident	"@(#)cachefs_dir.h	1.12	94/06/07 SMI"

#include <sys/types.h>
#include <sys/fs/cachefs_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct c_dirent {
	u_int		d_length;	/* entry length */
	u_int		d_flag;		/* entry flags */
	u_long		d_fileno;	/* file number */
	off_t		d_offset;	/* disk offset of this entry */
	struct fid 	d_cookie;	/* back fid */
	u_short		d_namelen;	/* name length, without null */
	char		d_name[1];	/* name */
};

#define	C_DIRSIZ(dp) \
	(((sizeof (struct c_dirent) - 1) + \
	((dp)->d_namelen + 1) + 3) & ~3)

#define	CDE_SIZE(NM)	((strlen(NM) + sizeof (struct c_dirent) + 3) & ~3)

/*
 * Various flags stored in c_dirent flag field.
 */
#define	CDE_VALID	0x1		/* entry is valid */
#define	CDE_COMPLETE	0x2		/* entry is complete */


#if defined(_KERNEL) && defined(__STDC__)
extern int	cachefs_dirlook(struct cnode *, char *,
			struct fid *, u_int *,
			u_int *, ino_t *, cred_t *);
extern int	cachefs_direnter(struct cnode *, char *,
			struct fid *, u_int,
			ino_t, off_t, cred_t *, int);
extern int	cachefs_rmdirent(struct cnode *, char *, cred_t *);
extern void	cachefs_dirent_mod(struct cnode *, u_int,
			struct fid *, ino_t *);
extern int	cachefs_read_dir(struct cnode *, struct uio *, int *, cred_t *);
extern int	cachefs_filldir(struct cnode *, int, cred_t *);
extern int	cachefs_dirempty(struct cnode *, cred_t *);

#endif /* defined(_KERNEL) && defined(__STDC__) */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FS_CACHEFS_DIR_H */
