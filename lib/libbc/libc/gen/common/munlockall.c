/* @(#)munlockall.c 1.2 90/03/30 SMI */

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/mman.h>

/*
 * Function to unlock address space from memory.
 */

/*LINTLIBRARY*/
munlockall()
{

	return (mctl(0, 0, MC_UNLOCKAS, 0));
}
