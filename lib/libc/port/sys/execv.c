/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)execv.c	1.9	92/07/14 SMI"	/* SVr4.0 1.6.1.5	*/

/*
 *	execv(file, argv)
 *
 *	where argv is a vector argv[0] ... argv[x], NULL
 *	last vector element must be NULL
 *	environment passed automatically
 */
#ifdef __STDC__
	#pragma weak execv = _execv
#endif
#include "synonyms.h"

extern int execve();


execv(file, argv)
	char	*file;
	char	**argv;
{
	extern	char	**environ;

	return(execve(file, argv, environ));
}
