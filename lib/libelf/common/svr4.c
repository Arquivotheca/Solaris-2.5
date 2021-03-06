/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)svr4.c	1.4	92/07/17 SMI" 	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/

#include "syn.h"
#include <sys/utsname.h>
#include "libelf.h"
#include "decl.h"


int
_elf_svr4()
{
	struct utsname	u;
	static int	vers = -1;

	if (vers == -1)
	{
#ifndef	SUNOS
		if (uname(&u) > 0)
			vers = 1;
		else
			vers = 0;
#else
		vers = 0;
#endif
	}
	return vers;
}
