/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)getwd.c	1.2	1.2 SMI"

/*LINTLIBRARY*/
#include "synonyms.h"

#include <sys/param.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

char *
getwd(char *pathname)
{
	char *c;

	/*
	 * XXX Should use pathconf() here
	 */
	if ((c = getcwd(pathname, MAXPATHLEN + 1)) == NULL) {
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
