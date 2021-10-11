/*	Copyright (c) 1988 AT&T	*/
/*	All Rights Reserved	*/
/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fsync.c	1.3	93/03/10	SMI"
/*
 * fsync(int fd)
 *
 */
#ifdef __STDC__
	#pragma weak fsync = _fsync
#endif
#include "synonyms.h"
#include "sys/file.h"

extern int __fdsync();


fsync(fd)
	int fd;
{

	return (__fdsync(fd, FSYNC));
}
