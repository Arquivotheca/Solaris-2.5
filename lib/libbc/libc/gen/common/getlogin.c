#pragma ident	"@(#)getlogin.c	1.1	92/07/20 SMI"

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#include <pwd.h>
#include <stdio.h>

char *
getlogin()
{
	char *lgn;

	if ((lgn = (char *)_getlogin()) == NULL) {
		struct passwd *pwd;
		if ((pwd = (struct passwd *)_getpwuid(_getuid())) != NULL)
			return (pwd->pw_name);
	}
	return (lgn);
}
