/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fdetach.c	1.7	92/07/14 SMI"	/* SVr4.0 1.2	*/

/*
 * Detach a STREAMS-based file descriptor from an object in the
 * file system name space.
 */

#ifdef __STDC__
	#pragma weak fdetach = _fdetach
#endif
#include "synonyms.h"

int
fdetach(path)
#ifdef __STDC__
	const char *path;
#else
	char *path;
#endif	/* __STDC__ */
{
	extern int umount();

	return (umount(path));
}
