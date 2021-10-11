/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _AIO_H
#define	_AIO_H

#pragma ident	"@(#)aio.h	1.10	94/12/06 SMI"

#include <sys/feature_tests.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/siginfo.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if	(_POSIX_C_SOURCE - 0 > 0) && (_POSIX_C_SOURCE - 0 <= 2)
#error	"POSIX Asynchronous I/O is not supported in POSIX.1-1990"
#endif
typedef struct aiocb {
	int	aio_fildes;		/* file descriptor		*/
#if	defined(__STDC__)
	volatile void	*aio_buf;	/* buffer location		*/
#else
	void	*aio_buf;		/* buffer location		*/
#endif
	size_t	aio_nbytes;		/* length of transfer		*/
	off_t	aio_offset;		/* file offset			*/
	int	aio_reqprio;		/* request priority offset	*/
	struct sigevent	aio_sigevent;	/* signal number and offset	*/
	int	aio_lio_opcode;		/* listio operation		*/
	ssize_t	aio__return;		/* operation result value	*/
	int	aio__error;		/* operation error code		*/
	int	aio__pad[2];		/* extension padding		*/
}	aiocb_t;

/*
 * return values for aiocancel()
 */
#define	AIO_CANCELED	0
#define	AIO_ALLDONE	1
#define	AIO_NOTCANCELED	2

/*
 * mode values for lio_listio()
 */
#define	LIO_NOWAIT	0
#define	LIO_WAIT	1

/*
 * listio operation codes
 */
#define	LIO_NOP		0
#define	LIO_READ	1
#define	LIO_WRITE	2

/*
 * function prototypes
 */
#if	defined(__STDC__)
#include <sys/time.h>
extern int	aio_read(struct aiocb *aiocbp);
extern int	aio_write(struct aiocb *aiocbp);
extern int	lio_listio(int mode, struct aiocb * const list[], int nent,
			struct sigevent *sig);
extern int	aio_error(const struct aiocb *aiocbp);
extern int	aio_return(struct aiocb *aiocbp);
extern int	aio_cancel(int fildes, struct aiocb *aiocbp);
extern int	aio_suspend(const struct aiocb * const list[], int nent,
			const struct timespec *timeout);
extern int	aio_fsync(int op, struct aiocb *aiocbp);
#else
extern int	aio_read();
extern int	aio_write();
extern int	lio_listio();
extern int	aio_error();
extern int	aio_return();
extern int	aio_cancel();
extern int	aio_suspend();
extern int	aio_fsync();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _AIO_H */
