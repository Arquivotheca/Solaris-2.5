/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 *
 * Map in a read-only page of zeroes at location zero, for stupid
 * programs that think a null pointer is as good as a null string.
 *
 * Use:
 *	LD_PRELOAD=0@0.so.1 program args ...
 *
 */
#pragma ident	"@(#)0@0.c	1.1	94/05/10 SMI"

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/fcntl.h>

#pragma	init(__zero_at_zero)

static void
__zero_at_zero()
{
	int fd;

	if ((fd = open("/dev/zero", O_RDWR)) < 0)
		return;
	(void) mmap(0, 1, PROT_READ, MAP_PRIVATE|MAP_FIXED, fd, 0);
	(void) close(fd);
}
