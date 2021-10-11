/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)readdir_r.c	1.9	95/08/28 SMI"	/* SVr4.0 1.11  */

/*
	readdir_r -- C library extension routine

*/
#ifdef __STDC__
#pragma weak readdir_r = _readdir_r
#endif
#include	"synonyms.h"
#include	<sys/types.h>
#include	<dirent.h>
#include	<thread.h>
#include	<synch.h>
#include	<mtlib.h>
#include	<errno.h>

extern char	*strncpy();
extern int	_getdents(), strlen();

#define	NULL 0

#ifdef _REENTRANT
extern mutex_t	_dirent_lock;
#endif	/* _REENTRANT */

/*
 * POSIX.1c Draft-6 version of the function readdir_r.
 * It was implemented by Solaris 2.3.
 */
struct dirent *
_readdir_r(register DIR *dirp, struct dirent *result)
{
	register struct dirent	*dp;	/* -> directory data */
	int saveloc = 0;

	_mutex_lock(&_dirent_lock);
	if (dirp->dd_size != 0) {
		dp = (struct dirent *)&dirp->dd_buf[dirp->dd_loc];
		saveloc = dirp->dd_loc;   /* save for possible EOF */
		dirp->dd_loc += dp->d_reclen;
	}

	if (dirp->dd_loc >= dirp->dd_size)
		dirp->dd_loc = dirp->dd_size = 0;

	if (dirp->dd_size == 0 &&	/* refill buffer */
	    (dirp->dd_size = _getdents(dirp->dd_fd,
			(struct dirent *)dirp->dd_buf, DIRBUF)) <= 0) {
		if (dirp->dd_size == 0)	/* This means EOF */
			dirp->dd_loc = saveloc;  /* EOF so save for telldir */
		_mutex_unlock(&_dirent_lock);
		return (NULL);	/* error or EOF */
	}

	dp = (struct dirent *)&dirp->dd_buf[dirp->dd_loc];
	memcpy(result, dp, dp->d_reclen);
	_mutex_unlock(&_dirent_lock);
	return ((struct dirent *)result);
}

/*
 * POSIX.1c standard version of the thr function readdir_r.
 * User gets it via static readdir_r from header file.
 */

int
__posix_readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result)
{
	int nerrno = 0;
	int oerrno = errno;

	errno = 0;
	if ((*result = _readdir_r(dirp, entry)) == NULL) {
		if (errno == 0)
			nerrno = EINVAL;
		else
			nerrno = errno;
	}
	errno = oerrno;
	return (nerrno);
}
