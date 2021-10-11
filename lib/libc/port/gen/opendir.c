/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)opendir.c	1.15	93/08/20 SMI"	/* SVr4.0 1.13	*/

/*
	opendir -- C library extension routine

*/

#ifdef __STDC__
	#pragma weak opendir = _opendir
#endif
#include	"synonyms.h"
#include	"shlib.h"
#include	<sys/types.h>
#include	<dirent.h>
#include   	<sys/fcntl.h>
#include	<sys/stat.h>
#include	<sys/errno.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<thread.h>
#include	<synch.h>
#include        <mtlib.h>

extern int	open(), close(), fstat();

#define NULL	0

#ifdef _REENTRANT
mutex_t	_dirent_lock = DEFAULTMUTEX;
#endif	/* _REENTRANT */

DIR *
opendir(filename)
const char *filename;
{
	register DIR	*dirp = NULL;	/* -> malloc'ed storage */
	register int	fd;		/* file descriptor for read */
	struct stat	sbuf;		/* result of fstat() */

	if ((fd = open(filename, O_RDONLY | O_NDELAY)) < 0)
		return NULL;
	/* POSIX mandated behavior
	 * close on exec if using file descriptor 
 	 */
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) 
		return NULL;
	if ((fstat(fd, &sbuf) < 0)
	  || ((sbuf.st_mode & S_IFMT) != S_IFDIR)
	  || ((dirp = (DIR *)malloc(sizeof(DIR) + DIRBUF)) == NULL)) {
		if ((sbuf.st_mode & S_IFMT) != S_IFDIR)
			errno = ENOTDIR;
		if (dirp)
			free(dirp); 	/* free malloc'ed space, if needed */
		(void)close( fd );
		return NULL;		/* bad luck today */
	}
	dirp->dd_buf = (char *)dirp + sizeof(DIR);
	dirp->dd_fd = fd;
	dirp->dd_loc = dirp->dd_size = 0;	/* refill needed */
	return dirp;
}
