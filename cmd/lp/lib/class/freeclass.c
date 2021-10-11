/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)freeclass.c	1.3	90/03/01 SMI"	/* SVr4.0 1.5	*/
/* LINTLIBRARY */

#include "lp.h"
#include "class.h"

/**
 ** freeclass() - FREE SPACE USED BY CLASS STRUCTURE
 **/

void
#if	defined(__STDC__)
freeclass (
	CLASS *			clsbufp
)
#else
freeclass (clsbufp)
	CLASS			*clsbufp;
#endif
{
	if (!clsbufp)
		return;
	if (clsbufp->name)
		Free (clsbufp->name);
	freelist (clsbufp->members);
	return;
}
