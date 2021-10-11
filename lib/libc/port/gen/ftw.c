/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ftw.c	1.11	93/10/30 SMI"	/* SVr4.0 1.6.1.11	*/

/*LINTLIBRARY*/

#ifdef __STDC__
	#pragma weak ftw = _ftw
#endif

#include "synonyms.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <string.h>
#include <stdlib.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#ifdef _REENTRANT
static mutex_t ftw_lock = DEFAULTMUTEX;
#endif _REENTRANT

int
_ftw(path, fn, depth)
	const char *path;
	int	(*fn)();
	int	depth;
{
	int retval;

	_mutex_lock(&ftw_lock);
	retval = _xftw(_XFTWVER, path, fn, depth);
	_mutex_unlock(&ftw_lock);
	return (retval);
}
