/* @(#)msync.c 1.2 90/03/30 SMI */

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

/*
 * Function to synchronize address range with backing store.
 */

/*LINTLIBRARY*/
msync(addr, len, flags)
	caddr_t addr;
	u_int len;
	int flags;
{
	if ((int)len <= 0) {
		errno = EINVAL;
		return (-1);
	}
	return (mctl(addr, len, MC_SYNC, flags));
}
