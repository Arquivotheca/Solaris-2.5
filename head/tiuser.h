/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_TIUSER_H
#define	_TIUSER_H

#pragma ident	"@(#)tiuser.h	1.12	95/09/10 SMI"	/* SVr4.0 1.1	*/

/*
 * TLI user interface definitions.
 */

#include <sys/tiuser.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_REENTRANT) || defined(_TS_ERRNO) || \
	_POSIX_C_SOURCE - 0 >= 199506L
extern int	*__t_errno();
#define	t_errno	(*(__t_errno()))
#else
extern int t_errno;
#endif	/* defined(_REENTRANT) || defined(_TS_ERRNO) */

#ifdef	__cplusplus
}
#endif

#endif	/* _TIUSER_H */
