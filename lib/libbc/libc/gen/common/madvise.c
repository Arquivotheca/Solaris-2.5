/* @(#)madvise.c 1.2 90/03/30 SMI */

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

/*
 * Function to provide advise to vm system to optimize it's
 *   characteristics for a particular application
 */

/*LINTLIBRARY*/
madvise(addr, len, advice)
	caddr_t addr;
	u_int len;
	int advice;
{
	if (len == 0) {
		errno = EINVAL;
		return (-1);
	}
	return (mctl(addr, len, MC_ADVISE, advice));
}

/*
 * This is only here so programs that use vadvise will not fail
 * because it is not in the bcp libc.
 */
int
vadvise(int param) {
	return (0);
}
