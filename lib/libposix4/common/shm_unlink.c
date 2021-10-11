/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)shm_unlink.c	1.3	93/04/13 SMI"

#include "synonyms.h"
#include <errno.h>

int
shm_unlink(const char *name)
{
	errno = ENOSYS;
	return (-1);
}
