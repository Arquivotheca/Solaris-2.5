/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)fork.c	1.5	95/01/23 SMI"

#ifdef __STDC__
#pragma weak fork = aio_fork
#endif

#include <unistd.h>
#include <sys/types.h>

extern int __uaio_ok, _kaio_ok;

aio_fork()
{
	int pid;

	if (__uaio_ok || _kaio_ok) {
		pid = fork1();
		if (pid == 0)
			_aio_forkinit();
		return (pid);
	}
	return (_fork());
}
