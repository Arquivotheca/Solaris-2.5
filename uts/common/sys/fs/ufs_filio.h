/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_FS_UFS_FILIO_H
#define	_SYS_FS_UFS_FILIO_H

#pragma ident	"@(#)ufs_filio.h	1.7	93/03/12 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * _FIOAI
 *
 * struct for _FIOAI ioctl():
 *	Input:
 *		fai_off	  - byte offset in file
 *		fai_size  - byte range (0 == EOF)
 *		fai_num   - number of entries in fai_daddr
 *		fai_daddr - array of daddr_t's
 *	Output:
 *		fai_off	  - resultant offset in file
 *		fai_size  - unchanged
 *		fai_num   - number of entries returned in fai_daddr
 *		fai_daddr - array of daddr_t's (0 entry == hole)
 *
 * Allocation information is returned in DEV_BSIZE multiples.  fai_off
 * is rounded down to a DEV_BSIZE multiple, and fai_size is rounded up.
 */

struct fioai {
	off_t	fai_off;	/* byte offset in file */
	size_t	fai_size;	/* byte range */
	u_long	fai_num;	/* # entries in array */
	daddr_t	*fai_daddr;	/* array of daddr_t's */
};

#define	_FIOAI_HOLE	((daddr_t)(-1))

/*
 * _FIOIO
 *
 * struct for _FIOIO ioctl():
 *	Input:
 *		fio_ino	- inode number
 *		fio_gen	- generation number
 *	Output:
 *		fio_fd	- readonly file descriptor
 *
 */

struct fioio {
	ino_t	fio_ino;	/* input : inode number */
	long	fio_gen;	/* input : generation number */
	int	fio_fd;		/* output: readonly file descriptor */
};

#if defined(_KERNEL) && defined(__STDC__)

extern	int	ufs_fioai(struct vnode *, struct fioai *, struct cred *);
extern	int	ufs_fiosatime(struct vnode *, struct timeval *, struct cred *);
extern	int	ufs_fiosdio(struct vnode *, u_long *, struct cred *);
extern	int	ufs_fiogdio(struct vnode *, u_long *, struct cred *);
extern	int	ufs_fioio(struct vnode *, struct fioio *, struct cred *);

#endif	/* defined(_KERNEL) && defined(__STDC__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_UFS_FILIO_H */
