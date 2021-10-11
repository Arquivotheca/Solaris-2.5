/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)open.c	1.5	95/08/27 SMI"

#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

#include "chkpath.h"

int
open(char *path, int flags, int mode)
{
	return (bc_open(path, flags, mode));
}
	
int
bc_open(char *path, int flags, int mode)
{
	CHKNULL(path);
	if (flags & FNDELAY) {
		flags &= ~FNDELAY;
		flags |= O_NONBLOCK;
	}
	return (open_com(path, flags, mode));
}
