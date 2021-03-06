/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getkey.c	1.2	90/03/09 SMI"	/* SVr4.0 1.1.1.4	*/

#include "sys/types.h"
#include "time.h"
#include "stdlib.h"

#include "lpsched.h"

long
#if	defined(__STDC__)
getkey (
	void
)
#else
getkey ()
#endif
{
	ENTRY ("getkey")

	static int		started = 0;

	if (!started) {
		srand48 (time((time_t *)0));
		started = 1;
	}
	return (lrand48());
}
