/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)dup2.c	1.10	92/11/30 SMI"	/* SVr4.0 1.8	*/

#ifdef __STDC__
	#pragma weak dup2 = _dup2
#endif
#include 	"synonyms.h"
#include	<limits.h>	/* Get definition for OPEN_MAX */
#include	<fcntl.h>
#include	<unistd.h>
#include	<ulimit.h>
#include	<errno.h>	/* Get definition for EBADF */

int
dup2(fildes, fildes2)
int	fildes,		/* file descriptor to be duplicated */
	fildes2;	/* desired file descriptor */
{
	int	tmperrno;	/* local work area */
	register int open_max;	/* max open files */

	if ((open_max = ulimit(UL_GDESLIM, 0)) < 0)
		open_max = OPEN_MAX;	/* take a guess */

	/* Be sure fildes is valid and open */
	if (fcntl(fildes, F_GETFL, 0) == -1) {
		errno = EBADF;
		return (-1);
	}

	/* Be sure fildes2 is in valid range */
	if (fildes2 < 0 || fildes2 >= open_max) {
		errno = EBADF;
		return (-1);
	}

	/* Check if file descriptors are equal */
	if (fildes == fildes2) {
		/* open and equal so no dup necessary */
		return (fildes2);
	}

	/* Close in case it was open for another file */
	/* Must save and restore errno in case file was not open */
	tmperrno = errno;
	close(fildes2);
	errno = tmperrno;

	/* Do the dup */
	return (fcntl(fildes, F_DUPFD, fildes2));
}

