/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mctl.c	1.4	93/12/26 SMI"	/* SVr4.0 1.2	*/

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice 

Notice of copyright on this source code product does not indicate 
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
	          All rights reserved.
********************************************************************/ 

#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>

/*
 * Memory control function.
 */

/*LINTLIBRARY*/
mctl(addr, len, function, arg)
	caddr_t addr;
	size_t len;
	int function;
	int arg;
{

	if ((int)len < 0) {
		errno = EINVAL;
		return(-1);
	}
	return (memcntl(addr, len, function, (caddr_t) arg, 0, 0));
}
