/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ifndef _SYS_FCNTL_H
#define	_SYS_FCNTL_H

#pragma ident	"@(#)fcntl.h	1.31	95/01/19 SMI"	/* SVr4.0 11.38	*/

#include <sys/feature_tests.h>

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Flag values accessible to open(2) and fcntl(2)
 * (the first three can only be set by open).
 */
#define	O_RDONLY	0
#define	O_WRONLY	1
#define	O_RDWR		2
#if defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE)
#define	O_NDELAY	0x04	/* non-blocking I/O */
#endif /* defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) */
#define	O_APPEND	0x08	/* append (writes guaranteed at the end) */
#if defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) || \
	(_POSIX_C_SOURCE > 2) || defined(_XOPEN_SOURCE)
#define	O_SYNC		0x10	/* synchronized file update option */
#define	O_DSYNC		0x40	/* synchronized data update option */
#define	O_RSYNC		0x8000	/* synchronized file update option */
				/* defines read/write file integraty */
				/* we are taking last bit as no spece */
				/* left in first 8 bits */
#endif /* defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) ... */
#define	O_NONBLOCK	0x80	/* non-blocking I/O (POSIX) */
#ifdef SUN_SRC_COMPAT
#define	O_PRIV 		0x1000  /* Private access to file */
#endif /* SUN_SRC_COMPAT */

/*
 * Flag values accessible only to open(2).
 */
#define	O_CREAT		0x100	/* open with file create (uses third arg) */
#define	O_TRUNC		0x200	/* open with truncation */
#define	O_EXCL		0x400	/* exclusive open */
#define	O_NOCTTY	0x800	/* don't allocate controlling tty (POSIX) */

/* fcntl(2) requests */
#define	F_DUPFD		0	/* Duplicate fildes */
#define	F_GETFD		1	/* Get fildes flags */
#define	F_SETFD		2	/* Set fildes flags */
#define	F_GETFL		3	/* Get file flags */
#define	F_SETFL		4	/* Set file flags */
#ifdef SUN_SRC_COMPAT
#define	F_SETLK		6	/* Set file lock */
#define	F_SETLKW	7	/* Set file lock and wait */
#endif /* SUN_SRC_COMPAT */

/*
 * Applications that read /dev/mem must be built like the kernel.  A
 * new symbol "_KMEMUSER" is defined for this purpose.
 */
#if defined(_KERNEL) || defined(_KMEMUSER)
#define	F_O_GETLK	5	/* SVR3 Get file lock (need for rfs, across */
				/* the wire compatibility */
#endif	/* defined(_KERNEL) */

#define	F_SETLK		6	/* Set file lock */
#define	F_SETLKW	7	/* Set file lock and wait */

#define	F_CHKFL		8	/* Unused */

#define	F_ALLOCSP	10	/* Reserved */
#define	F_FREESP	11	/* Free file space */
#define	F_ISSTREAM	13	/* Is the file desc. a stream ? */
#define	F_GETLK		14	/* Get file lock */
#define	F_PRIV		15	/* Turn on private access to file */
#define	F_NPRIV		16	/* Turn off private access to file */
#define	F_QUOTACTL	17	/* UFS quota call */
#define	F_BLOCKS	18	/* Get number of BLKSIZE blocks allocated */
#define	F_BLKSIZE	19	/* Get optimal I/O block size */

/*
 * F_RSETLK, F_RGETLK, and F_RSETLKW are private to the lock manager;
 * applications should neither use them nor depend upon their existence.
 */
#define	F_RSETLK	20	/* Remote SETLK for NFS */
#define	F_RGETLK	21	/* Remote GETLK for NFS */
#define	F_RSETLKW	22	/* Remote SETLKW for NFS */

#define	F_GETOWN	23	/* Get owner (socket emulation) */
#define	F_SETOWN	24	/* Set owner (socket emulation) */

#ifdef C2_AUDIT
#define	F_REVOKE	25	/* C2 Security. Revoke access to file desc. */
#endif

#define	F_HASREMOTELOCKS 26	/* Does vp have NFS locks; private to lock */
				/* manager */

/*
 * File segment locking set data type - information passed to system by user.
 */
typedef struct flock {
	short	l_type;
	short	l_whence;
	off_t	l_start;
	off_t	l_len;		/* len == 0 means until end of file */
	long	l_sysid;
	pid_t	l_pid;
	long	l_pad[4];		/* reserve area */
} flock_t;

#if defined(_KERNEL) || defined(_KMEMUSER)
/* SVr3 flock type; needed for rfs across the wire compatibility */
typedef struct o_flock {
	short	l_type;
	short	l_whence;
	long	l_start;
	long	l_len;		/* len == 0 means until end of file */
	short	l_sysid;
	o_pid_t	l_pid;
} o_flock_t;
#endif	/* defined(_KERNEL) */

/*
 * File segment locking types.
 */
#define	F_RDLCK		01	/* Read lock */
#define	F_WRLCK		02	/* Write lock */
#define	F_UNLCK		03	/* Remove lock(s) */
#define	F_UNLKSYS	04	/* remove remote locks for a given system */

/*
 * POSIX constants
 */

#define	O_ACCMODE	3	/* Mask for file access modes */
#define	FD_CLOEXEC	1	/* close on exec flag */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FCNTL_H */
