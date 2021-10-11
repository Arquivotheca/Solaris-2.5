/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)closedir.c	1.13	93/08/20 SMI"	/* SVr4.0 1.10	*/

/*
	closedir -- C library extension routine

*/

#ifdef __STDC__
	#pragma weak closedir = _closedir
#endif
#include "synonyms.h"
#include "shlib.h"
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>


int
closedir( dirp )
register DIR	*dirp;		/* stream from opendir() */
{
	register int 	tmp_fd = dirp->dd_fd;

	free((char *)dirp);
	return(close( tmp_fd ));
}
