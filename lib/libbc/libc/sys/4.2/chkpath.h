/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)chkpath.h	1.5	95/08/27 SMI" 

#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/errno.h>

#define	 CHKNULL(p)					\
	if ((p) == (char *)0 || (p) == (char *) -1) {	\
		errno = EFAULT;				\
		return (-1);				\
	} else if (*(p) == 0) {				\
		p = ".";				\
	}

extern int      syscall();
extern          errno;
