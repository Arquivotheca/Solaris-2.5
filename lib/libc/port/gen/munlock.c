/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)munlock.c	1.6	92/07/14 SMI"	/* SVr4.0 1.3	*/

#ifdef __STDC__
	#pragma weak munlock = _munlock
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <sys/mman.h>

/*
 * Function to unlock address range from memory.
 */

/*LINTLIBRARY*/
munlock(addr, len)
	caddr_t addr;
	size_t len;
{

	return (memcntl(addr, len, MC_UNLOCK, 0, 0, 0));
}
