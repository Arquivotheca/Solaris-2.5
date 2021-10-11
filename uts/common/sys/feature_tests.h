/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_FEATURE_TESTS_H
#define	_SYS_FEATURE_TESTS_H

#pragma ident	"@(#)feature_tests.h	1.7	94/12/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * 	Values of _POSIX_C_SOURCE
 *
 *		undefined	not a POSIX compilation
 *			1	POSIX.1-1990 compilation
 *			2	POSIX.2-1992 compilation
 *		  199309L	POSIX.1b-1993 compilation
 */
#if	defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE)
#define	_POSIX_C_SOURCE	1
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FEATURE_TESTS_H */
