/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rename.c	1.8	94/03/02 SMI"	/* SVr4.0 1.10	*/

/*LINTLIBRARY*/
#include "synonyms.h"
#include <sys/types.h>
#include <sys/stat.h>

extern int link(), unlink(), uname(), _rename();

int
remove(filename)
#ifdef __STDC__
const char *filename;
#else
char *filename;
#endif
{
	struct stat	statb;

	/*
	 * If filename is not a directory, call unlink(filename)
	 * Otherwise, call rmdir(filename)
	 */

	if (lstat(filename, &statb) != 0)
		return (-1);
	if ((statb.st_mode & S_IFMT) != S_IFDIR)
		return (unlink(filename));
	return (rmdir(filename));
}

int
rename(old, new)
#ifdef __STDC__
const char *old;
const char *new;
#else
char *old;
char *new;
#endif
{
	return (_rename(old, new));
}
