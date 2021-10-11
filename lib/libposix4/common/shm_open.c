/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)shm_open.c	1.3	93/04/13 SMI"

#include "synonyms.h"
#include <sys/mman.h>
#include <errno.h>

int
shm_open(const char *name, int oflag, mode_t mode)
{
	errno = ENOSYS;
	return (-1);
}
