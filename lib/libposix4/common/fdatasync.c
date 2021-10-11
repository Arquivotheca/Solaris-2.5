/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)fdatasync.c	1.7	93/04/13 SMI"

#include "synonyms.h"
#include <sys/fcntl.h>

extern int __fdsync(int fd, mode_t mode);

int
fdatasync(int fd)
{
	return (__fdsync(fd, O_DSYNC));
}
