/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)getwd.c	1.6	1.6 SMI"

/*LINTLIBRARY*/

#include <sys/param.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

extern char *_getcwd(char *, size_t);

char *
getwd(char *pathname)
{
	char *c;

	/*
	 * XXX Should use pathconf() here
	 */
	if ((c = _getcwd(pathname, MAXPATHLEN + 1)) == NULL) {
		if (errno == EACCES)
			(void) strcpy(pathname,
				"getwd: a parent directory cannot be read");
		else if (errno == ERANGE)
			(void) strcpy(pathname, "getwd: buffer too small");
		else
			(void) strcpy(pathname, "getwd: failure occurred");
		return (0);
	}

	return (c);
}
