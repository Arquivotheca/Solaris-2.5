/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)wslen.c 1.1	94/10/12 SMI"

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wslen.c	1.6	93/12/02 SMI" /* from JAE2.0 1.0 */

/*
 * Returns the number of non-NULL characters in s.
 */

#include <stdlib.h>
#include <wchar.h>


size_t
__wcslen(const wchar_t *s)
{
	const wchar_t *s0 = s + 1;

	while (*s++)
		;
	return (s - s0);
}

size_t
__wslen(const wchar_t *s)
{
	return (__wcslen(s));
}
