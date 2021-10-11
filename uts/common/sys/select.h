/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_SELECT_H
#define	_SYS_SELECT_H

#pragma ident	"@(#)select.h	1.10	92/07/14 SMI"	/* SVr4.0 1.2 */

#ifndef _KERNEL
#include <sys/time.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Select uses bit masks of file descriptors in longs.
 * These macros manipulate such bit fields.
 * FD_SETSIZE may be defined by the user, but the default here
 * should be >= NOFILE (param.h).
 */
#ifndef	FD_SETSIZE
#define	FD_SETSIZE	1024
#endif

#ifndef NBBY		/* number of bits per byte */
#define	NBBY 8
#endif

typedef	long	fd_mask;
#define	NFDBITS	(sizeof (fd_mask) * NBBY)	/* bits per mask */
#ifndef	howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif

typedef	struct fd_set {
	fd_mask	fds_bits[howmany(FD_SETSIZE, NFDBITS)];
} fd_set;

#define	FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= \
			    ((unsigned)1 << ((n) % NFDBITS)))

#define	FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= \
			    ~((unsigned)1 << ((n) % NFDBITS)))

#define	FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & \
			    ((unsigned)1 << ((n) % NFDBITS)))

#ifdef _KERNEL
#define	FD_ZERO(p)	bzero((char *)(p), sizeof (*(p)))
#else
#define	FD_ZERO(p)	memset((char *)(p), 0, sizeof (*(p)))
#endif /* _KERNEL */

#ifndef	_KERNEL
#ifdef	__STDC__
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#else
extern int select();
#endif	/* __STDC__ */
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SELECT_H */
