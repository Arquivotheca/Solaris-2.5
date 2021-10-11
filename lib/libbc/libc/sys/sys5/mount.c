/*
 * Copyright (c) 1995 Sun Microsystems, Inc. All rights reserved.
 */

#ident	"@(#)mount.c	1.3	95/04/19 SMI"

#include <errno.h>

int
mount(spec, dir, rdonly)
char *spec;
char *dir;
int rdonly;
{
	int ret;

	if ((ret = _mount(spec, dir, rdonly)) != 0) {
		maperror(errno);
	}
	return (ret);
}
