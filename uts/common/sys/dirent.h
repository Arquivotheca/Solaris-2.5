/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_DIRENT_H
#define	_SYS_DIRENT_H

#pragma ident	"@(#)dirent.h	1.15	94/07/26 SMI"	/* SVr4.0 11.11 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * File-system independent directory entry.
 */
struct dirent {
	ino_t		d_ino;		/* "inode number" of entry */
	off_t		d_off;		/* offset of disk directory entry */
	unsigned short	d_reclen;	/* length of this record */
	char		d_name[1];	/* name of file */
};

typedef	struct	dirent	dirent_t;

#if (!defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)) || \
	defined(__EXTENSIONS__)
#if !defined(_KERNEL)
#if defined(__STDC__)
int getdents(int, struct dirent *, unsigned);
#else
int getdents();
#endif
#endif /* !defined(_KERNEL) */
#endif /* (!defined(_POSIX_C_SOURCE)  && !defined(_XOPEN_SOURCE)) ... */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DIRENT_H */
