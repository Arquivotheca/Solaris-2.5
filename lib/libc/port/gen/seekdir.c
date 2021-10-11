/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)seekdir.c	1.14	93/08/20 SMI"	/* SVr4.0 1.9	*/

/*
	seekdir -- C library extension routine

*/

#ifdef __STDC__
	#pragma weak seekdir = _seekdir
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include	<dirent.h>
#include	<thread.h>
#include	<synch.h>
#include	<mtlib.h>

extern long	lseek(), telldir();

#define NULL	0

#ifdef _REENTRANT
extern mutex_t	_dirent_lock;
#endif	/* _REENTRANT */

void
seekdir(dirp, loc)
register DIR	*dirp;		/* stream from opendir() */
long		loc;		/* position from telldir() */
{
	struct dirent *dp;
	long off = 0;

	_mutex_lock(&_dirent_lock);
	if (lseek(dirp->dd_fd, 0, 1) != 0) {
		dp = (struct dirent *)&dirp->dd_buf[dirp->dd_loc];
		off = dp->d_off;
	}
	lseek(dirp->dd_fd, 0, 1);
	if (off != loc) {
		dirp->dd_loc = 0;
		lseek(dirp->dd_fd, loc, 0);
		dirp->dd_size = 0;

		/*
		 * Save seek offset in d_off field, in case telldir 
		 * follows seekdir with no intervening call to readdir
		 */
		((struct dirent *) &dirp->dd_buf[0])->d_off = loc;
	}
	_mutex_unlock(&_dirent_lock);
}
