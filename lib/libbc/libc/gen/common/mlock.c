/* @(#)mlock.c 1.2 90/03/30 SMI */

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

/*
 * Function to lock address range in memory.
 */

/*LINTLIBRARY*/
mlock(addr, len)
	caddr_t addr;
	u_int len;
{
	if((int)len <= 0) {
		errno = EINVAL;
		return(-1);
	}
	return (mctl(addr, len, MC_LOCK, 0));
}
