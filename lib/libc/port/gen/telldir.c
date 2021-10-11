/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)telldir.c	1.13	93/08/20 SMI"	/* SVr4.0 1.6	*/

/*
	telldir -- C library extension routine

*/

#ifdef __STDC__
	#pragma weak telldir = _telldir
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include 	<dirent.h>
#include	<thread.h>
#include	<synch.h>
#include	<mtlib.h>

extern long	lseek();

#ifdef _REENTRANT
extern mutex_t	_dirent_lock;
#endif	/* _REENTRANT */

long
telldir(dirp)
DIR	*dirp;			/* stream from opendir() */
{
	struct dirent *dp;
	long off = 0;

	_mutex_lock(&_dirent_lock);
	/* if at beginning of dir, return 0 */
	if (lseek(dirp->dd_fd, 0, 1) != 0) {
		dp = (struct dirent *)&dirp->dd_buf[dirp->dd_loc];
		off = dp->d_off;
	}
	_mutex_unlock(&_dirent_lock);
	return (off);
}
