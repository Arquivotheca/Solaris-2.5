/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_UIO_H
#define	_SYS_UIO_H

#pragma ident	"@(#)uio.h	1.21	94/04/22 SMI"	/* SVr4.0 1.6 */

#include <sys/feature_tests.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * I/O parameter information.  A uio structure describes the I/O which
 * is to be performed by an operation.  Typically the data movement will
 * be performed by a routine such as uiomove(), which updates the uio
 * structure to reflect what was done.
 */

typedef struct iovec {
	caddr_t	iov_base;
	int	iov_len;
} iovec_t;

/*
 * Segment flag values.
 */
typedef enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_USERISPACE } uio_seg_t;

typedef struct uio {
	iovec_t	*uio_iov;	/* pointer to array of iovecs */
	int	uio_iovcnt;	/* number of iovecs */
	lloff_t	_uio_offset;	/* file offset */
	uio_seg_t uio_segflg;	/* address space (kernel or user) */
	short	uio_fmode;	/* file mode flags */
	lldaddr_t _uio_limit;	/* u-limit (maximum "block" offset) */
	int	uio_resid;	/* residual count */
} uio_t;

#define	uio_loffset	_uio_offset._f
#define	uio_offset	_uio_offset._p._l

#define	uio_llimit	_uio_limit._f
#define	uio_limit	_uio_limit._p._l

/*
 * I/O direction.
 */
typedef enum uio_rw { UIO_READ, UIO_WRITE } uio_rw_t;

#if	defined(_KERNEL)
#ifdef __STDC__

int	uiomove(caddr_t, long, enum uio_rw, uio_t *);
int	ureadc(int, uio_t *);	/* should be errno_t in future */
int	uwritec(struct uio *);
int	uiomvuio(uio_t *, uio_t *);
void	uioskip(uio_t *, size_t);
int	uiodup(uio_t *, uio_t *, iovec_t *, int);
int	uioipcopyin(caddr_t, long, uio_t *, unsigned short *);
int	uioipcopyout(caddr_t, long, uio_t *, unsigned short *);

#else

int	uiomove();
int	ureadc();	/* should be errno_t in future */
int	uwritec();
int	uiomvuio();
void	uioskip();
int	uiodup();
int	uioipcopyin();
int	uioipcopyout();

#endif /* __STDC__ */
#else	/* defined(_KERNEL) */

#if	!defined(_POSIX_C_SOURCE)
#if	defined(__STDC__)
extern ssize_t readv(int, struct iovec *, int);
extern ssize_t writev(int, const struct iovec *, int);
#else	/* defined(__STDC__) */
extern ssize_t readv();
extern ssize_t writev();
#endif	/* defined(__STDC__) */
#endif	/* !defined(_POSIX_C_SOURCE) */

#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UIO_H */
