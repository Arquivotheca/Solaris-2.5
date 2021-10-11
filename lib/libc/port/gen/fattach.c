/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fattach.c	1.11	94/11/08 SMI"	/* SVr4.0 1.4	*/

/*
 * Attach a STREAMS or door based file descriptor to an object in the file
 * system name space.
 */
#ifdef __STDC__
	#pragma weak fattach = _fattach
#endif
#include "synonyms.h"
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <sys/vnode.h>
#include <sys/door.h>
#include <sys/fs/namenode.h>
#include <sys/mount.h>

int
fattach(fildes, path)
	int fildes;
#ifdef __STDC__
	const char *path;
#else
	char *path;
#endif	/* __STDC__ */
{
	struct namefd  namefdp;
	struct door_info dinfo;
	int	s;

	/* Only STREAMS and doors allowed to be mounted */
	if ((s = isastream(fildes)) == 1 || _door_info(fildes, &dinfo) == 0) {
		namefdp.fd = fildes;
		return (mount((char *)NULL, path, MS_DATA,
		    (const char *)"namefs", (char *)&namefdp,
		    sizeof (struct namefd)));
	} else if (s == 0) {
		/* Not a STREAM */
		errno = EINVAL;
		return (-1);
	} else {
		/* errno already set */
		return (-1);
	}
}
