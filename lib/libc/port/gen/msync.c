/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)msync.c	1.7	93/04/28 SMI"	/* SVr4.0 1.3	*/

#ifdef __STDC__
	#pragma weak msync = _msync
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <sys/mman.h>

msync(addr, len, flags)
caddr_t	addr;
size_t	len;
int flags;
{
	return (memcntl(addr, len, MC_SYNC, (caddr_t) flags, 0, 0));
}
