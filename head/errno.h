/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _ERRNO_H
#define	_ERRNO_H

#pragma ident	"@(#)errno.h	1.13	95/09/10 SMI"	/* SVr4.0 1.4.1.5 */

/*
 * Error codes
 */

#include <sys/errno.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if (defined(_REENTRANT) || defined(_TS_ERRNO) || \
	_POSIX_C_SOURCE - 0 >= 199506L) && !(defined(lint) || defined(__lint))
extern int *___errno();
#define	errno (*(___errno()))
#else
extern int errno;
#endif	/* defined(_REENTRANT) || defined(_TS_ERRNO) */

#ifdef	__cplusplus
}
#endif

#endif	/* _ERRNO_H */
