/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_STAT_H
#define	_SYS_STAT_H

#pragma ident	"@(#)stat.h	1.26	95/08/14 SMI"	/* SVr4 11.29	*/

#include <sys/feature_tests.h>

#include <sys/time.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	_ST_FSTYPSZ 16		/* array size for file system type name */

/*
 * stat structure, used by stat(2) and fstat(2)
 */

#if defined(_KERNEL)

	/* SVID stat struct (SVr4.x) */

struct	o_stat {
	o_dev_t	st_dev;
	o_ino_t	st_ino;
	o_mode_t st_mode;
	o_nlink_t st_nlink;
	o_uid_t	st_uid;
	o_gid_t	st_gid;
	o_dev_t	st_rdev;
	off_t	st_size;
	time_t	st_atime;
	time_t	st_mtime;
	time_t	st_ctime;
};

	/* Expanded stat structure */

struct	stat {
	dev_t	st_dev;
	long	st_pad1[3];	/* reserve for dev expansion, */
				/* sysid definition */
	ino_t	st_ino;
	mode_t	st_mode;
	nlink_t st_nlink;
	uid_t 	st_uid;
	gid_t 	st_gid;
	dev_t	st_rdev;
	long	st_pad2[2];
	off_t	st_size;
	long	st_pad3;	/* reserve pad for future off_t expansion */
	timestruc_t st_atime;
	timestruc_t st_mtime;
	timestruc_t st_ctime;
	long	st_blksize;
	long	st_blocks;
	char	st_fstype[_ST_FSTYPSZ];
	long	st_pad4[8];	/* expansion area */
};

#else /* !defined(_KERNEL) */

	/* user level 4.0 stat struct */

struct	stat {
	dev_t	st_dev;
	long	st_pad1[3];	/* reserved for network id */
	ino_t	st_ino;
	mode_t	st_mode;
	nlink_t st_nlink;
	uid_t 	st_uid;
	gid_t 	st_gid;
	dev_t	st_rdev;
	long	st_pad2[2];
	off_t	st_size;
	long	st_pad3;	/* future off_t expansion */
	timestruc_t st_atim;
	timestruc_t st_mtim;
	timestruc_t st_ctim;
	long	st_blksize;
	long	st_blocks;
	char	st_fstype[_ST_FSTYPSZ];
	long	st_pad4[8];	/* expansion area */
};
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
#define	st_atime	st_atim.tv_sec
#define	st_mtime	st_mtim.tv_sec
#define	st_ctime	st_ctim.tv_sec
#else
#define	st_atime	st_atim._tv_sec
#define	st_mtime	st_mtim._tv_sec
#define	st_ctime	st_ctim._tv_sec
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */

#endif	/* end defined(_KERNEL) */


/* MODE MASKS */

/* de facto standard definitions */

#define	S_IFMT		0xF000	/* type of file */
#define	S_IAMB		0x1FF	/* access mode bits */
#define	S_IFIFO		0x1000	/* fifo */
#define	S_IFCHR		0x2000	/* character special */
#define	S_IFDIR		0x4000	/* directory */
#define	S_IFNAM		0x5000  /* XENIX special named file */
#define	S_INSEM 	0x1	/* XENIX semaphore subtype of IFNAM */
#define	S_INSHD 	0x2	/* XENIX shared data subtype of IFNAM */
#define	S_IFBLK		0x6000	/* block special */
#define	S_IFREG		0x8000	/* regular */
#define	S_IFLNK		0xA000	/* symbolic link */
#define	S_IFSOCK	0xC000	/* socket */
#define	S_IFDOOR	0xD000	/* door */
#define	S_ISUID		0x800	/* set user id on execution */
#define	S_ISGID		0x400	/* set group id on execution */
#define	S_ISVTX		0x200	/* save swapped text even after use */
#define	S_IREAD		00400	/* read permission, owner */
#define	S_IWRITE	00200	/* write permission, owner */
#define	S_IEXEC		00100	/* execute/search permission, owner */
#define	S_ENFMT		S_ISGID	/* record locking enforcement flag */

/* the following macros are for POSIX conformance */

#define	S_IRWXU	00700		/* read, write, execute: owner */
#define	S_IRUSR	00400		/* read permission: owner */
#define	S_IWUSR	00200		/* write permission: owner */
#define	S_IXUSR	00100		/* execute permission: owner */
#define	S_IRWXG	00070		/* read, write, execute: group */
#define	S_IRGRP	00040		/* read permission: group */
#define	S_IWGRP	00020		/* write permission: group */
#define	S_IXGRP	00010		/* execute permission: group */
#define	S_IRWXO	00007		/* read, write, execute: other */
#define	S_IROTH	00004		/* read permission: other */
#define	S_IWOTH	00002		/* write permission: other */
#define	S_IXOTH	00001		/* execute permission: other */


#define	S_ISFIFO(mode)	(((mode)&0xF000) == 0x1000)
#define	S_ISCHR(mode)	(((mode)&0xF000) == 0x2000)
#define	S_ISDIR(mode)	(((mode)&0xF000) == 0x4000)
#define	S_ISBLK(mode)	(((mode)&0xF000) == 0x6000)
#define	S_ISREG(mode)	(((mode)&0xF000) == 0x8000)
#define	S_ISLNK(mode)	(((mode)&0xF000) == 0xa000)
#define	S_ISSOCK(mode)	(((mode)&0xF000) == 0xc000)
#define	S_ISDOOR(mode)	(((mode)&0xF000) == 0xd000)

/* POSIX.4 macros */
#define	S_TYPEISMQ(buf)		(0)
#define	S_TYPEISSEM(buf)	(0)
#define	S_TYPEISSHM(buf)	(0)

#if !defined(_KERNEL)
#if defined(__STDC__)

extern int fstat(int, struct stat *);
extern int stat(const char *, struct stat *);
#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
extern int lstat(const char *, struct stat *);
extern int mknod(const char *, mode_t, dev_t);
#endif /* (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE))... */
#if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) || \
	(_POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
extern int fchmod(int, mode_t);
#endif /* !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) || ... */

extern int chmod(const char *, mode_t);
extern int mkdir(const char *, mode_t);
extern int mkfifo(const char *, mode_t);
extern mode_t umask(mode_t);

#else	/* !__STDC__ */

extern int fstat(), stat();
#if !defined(_POSIX_C_SOURCE)
extern int mknod(), lstat(), fchmod();
#endif /* !defined(_POSIX_C_SOURCE) */

extern int chmod();
extern int mkdir();
extern int mkfifo();
extern mode_t umask();

#endif /* defined(__STDC__) */
#endif /* !defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_STAT_H */
