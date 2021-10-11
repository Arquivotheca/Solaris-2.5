/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 * Copyright (c) 1988 by Nihon Sun Microsystems K.K.
 */

#ident	"@(#)wsdup.c	1.5	92/07/14 SMI"

/*
 *	string duplication
 *	returns pointer to a new string which is the duplicate of string
 *	pointed to by s1
 *	NULL is returned if new string can't be created
 */

#include <stdlib.h>
#include <widec.h>

#ifndef NULL
#define NULL	(wchar_t *)0
#endif

wchar_t *
wsdup(const wchar_t * s1)
{  
	wchar_t * s2;

	s2 = (wchar_t *)malloc((unsigned) (wslen(s1)+1) * sizeof (wchar_t)) ;
	return (s2==NULL ? NULL : wscpy(s2,s1) );
}
