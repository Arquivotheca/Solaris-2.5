/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_FCNTL_H
#define	_FCNTL_H

#pragma ident	"@(#)fcntl.h	1.9	93/10/12 SMI"	/* SVr4.0 1.6.1.7 */

#if defined(__EXTENSIONS__) || \
	(defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))
#include <sys/stat.h>
#endif
#include <sys/types.h>
#include <sys/fcntl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__EXTENSIONS__) || \
	(defined(_XOPEN_SOURCE) && (_XOPEN_VERSION - 0 == 4))

/* Symbolic constants for the "lseek" routine. */

#ifndef	SEEK_SET
#define	SEEK_SET	0	/* Set file pointer to "offset" */
#endif

#ifndef	SEEK_CUR
#define	SEEK_CUR	1	/* Set file pointer to current plus "offset" */
#endif

#ifndef	SEEK_END
#define	SEEK_END	2	/* Set file pointer to EOF plus "offset" */
#endif

#endif /* defined(__EXTENSIONS__) || (defined(_XOPEN_SOURCE) ... */

#if defined(__STDC__)

extern int fcntl(int, int, ...);
extern int open(const char *, int, ...);
extern int creat(const char *, mode_t);

#else

extern int fcntl();
extern int open();
extern int creat();

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _FCNTL_H */
