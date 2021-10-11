/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)aio.c	1.4	94/12/06 SMI"

#include "synonyms.h"
#include <aio.h>
#include <errno.h>

int
aio_cancel(int fildes, struct aiocb *aiocbp)
{
	errno = ENOSYS;
	return (-1);
}

#undef aio_error

int
aio_error(const struct aiocb *aiocbp)
{
	errno = ENOSYS;
	return (-1);
}

int
aio_fsync(int op, struct aiocb *aiocbp)
{
	errno = ENOSYS;
	return (-1);
}

int
aio_read(struct aiocb *aiocbp)
{
	errno = ENOSYS;
	return (-1);
}

#undef aio_return

ssize_t
aio_return(struct aiocb *aiocbp)
{
	errno = ENOSYS;
	return (-1);
}

int
aio_suspend(const struct aiocb * const list[], int nent,
    const struct timespec *timeout)
{
	errno = ENOSYS;
	return (-1);
}

int
aio_write(struct aiocb *aiocbp)
{
	errno = ENOSYS;
	return (-1);
}

int
lio_listio(int mode, struct aiocb * const list[], int nent,
	struct sigevent *sig)
{
	errno = ENOSYS;
	return (-1);
}
