#pragma ident	"@(#)madvise.c	1.3	93/04/28 SMI" 

/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ifdef __STDC__
	#pragma weak madvise = _madvise
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <sys/mman.h>

/*
 * Function to provide advise to vm system to optimize its
 * management of the memory resources of a particular application
 */
madvise(addr, len, advice)
	caddr_t	addr;
	size_t	len;
	int advice;
{
	return (memcntl(addr, len, MC_ADVISE, (caddr_t) advice, 0, 0));
}
