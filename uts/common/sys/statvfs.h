/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_STATVFS_H
#define	_SYS_STATVFS_H

#pragma ident	"@(#)statvfs.h	1.9	95/02/24 SMI"	/* SVr4.0 1.10 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Structure returned by statvfs(2).
 */

#define	FSTYPSZ	16

typedef struct statvfs {
	u_long	f_bsize;	/* preferred file system block size */
	u_long	f_frsize;	/* fundamental file system block size */
	u_long	f_blocks;	/* total # of blocks of f_frsize on fs */
	u_long	f_bfree;	/* total # of free blocks of f_frsize */
	u_long	f_bavail;	/* # of free blocks avail to non-superuser */
	u_long	f_files;	/* total # of file nodes (inodes) */
	u_long	f_ffree;	/* total # of free file nodes */
	u_long	f_favail;	/* # of free nodes avail to non-superuser */
	u_long	f_fsid;		/* file system id (dev for now) */
	char	f_basetype[FSTYPSZ]; /* target fs type name, null-terminated */
	u_long	f_flag;		/* bit-mask of flags */
	u_long	f_namemax;	/* maximum file name length */
	char	f_fstr[32];	/* filesystem-specific string */
	u_long	f_filler[16];	/* reserved for future expansion */
} statvfs_t;

/*
 * Flag definitions.
 */

#define	ST_RDONLY	0x01	/* read-only file system */
#define	ST_NOSUID	0x02	/* does not support setuid/setgid semantics */
#define	ST_NOTRUNC	0x04	/* does not truncate long file names */

#if defined(__STDC__) && !defined(_KERNEL)
int statvfs(const char *, struct statvfs *);
int fstatvfs(int, struct statvfs *);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_STATVFS_H */
