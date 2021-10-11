/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)close.c	1.4	95/01/23 SMI"

#ifdef __STDC__
#pragma weak close = aio_close
#endif

#include <unistd.h>
#include <sys/types.h>

extern int __uaio_ok;

aio_close(fd)
	int fd;
{
	if (__uaio_ok)
		aiocancel_all(fd);
	return (_close(fd));
}
