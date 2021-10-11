/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getlogin.c	1.11	95/08/28 SMI"	/* SVr4.0 1.16  */

/*LINTLIBRARY*/
#ifdef __STDC__
#pragma weak getlogin = _getlogin
#ifdef _REENTRANT
#pragma weak getlogin_r = _getlogin_r
#endif /* _REENTRANT */
#endif
#include "synonyms.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include "utmp.h"
#include <unistd.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#define	NULL 0

extern long lseek();
extern int open(), read(), close(), ttyslot();

/*
 * POSIX.1c Draft-6 version of the function getlogin_r.
 * It was implemented by Solaris 2.3.
 */
char *
_getlogin_r(char *answer, int namelen)
{
	register me, uf;
	struct utmp ubuf;

	if (namelen < sizeof (ubuf.ut_user)+1) {
		errno = ERANGE;
		return (NULL);
	}

	if ((me = ttyslot()) < 0)
		return (NULL);
	if ((uf = open((const char *)UTMP_FILE, 0)) < 0)
		return (NULL);
	(void) lseek(uf, (long)(me * sizeof (ubuf)), 0);
	if (read(uf, (char *)&ubuf, sizeof (ubuf)) != sizeof (ubuf)) {
		(void) close(uf);
		return (NULL);
	}
	(void) close(uf);
	if (ubuf.ut_user[0] == '\0')
		return (NULL);
	strncpy(&answer[0], &ubuf.ut_user[0], sizeof (ubuf.ut_user));
	answer[sizeof (ubuf.ut_user)] = '\0';
	return (&answer[0]);
}

/*
 * POSIX.1c standard version of the function getlogin_r.
 * User gets it via static getlogin_r from the header file.
 */
int
__posix_getlogin_r(char *name, int namelen)
{
	int nerrno = 0;
	int oerrno = errno;

	errno = 0;
	if (_getlogin_r(name, namelen) == NULL) {
		if (errno == 0)
			nerrno = EINVAL;
		else
			nerrno = errno;
	}
	errno = oerrno;
	return (nerrno);
}

char *
getlogin()
{
	register me, uf;
	struct utmp ubuf;
	static char *answer = 0;

	if (!answer && ((answer = malloc(sizeof (ubuf.ut_user)+1)) == NULL)) {
		errno = ENOMEM;
		return (NULL);
	}
	return (_getlogin_r(answer, sizeof (ubuf.ut_user)+1));
}
