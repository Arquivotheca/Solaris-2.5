/* @(#)munlock.c 1.2 90/03/30 SMI */

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/mman.h>

/*
 * Function to unlock address range from memory.
 */

/*LINTLIBRARY*/
munlock(addr, len)
	caddr_t addr;
	u_int len;
{

	return (mctl(addr, len, MC_UNLOCK, 0));
}
