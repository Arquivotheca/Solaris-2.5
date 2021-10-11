/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)strcpy.c	1.9	93/05/30 SMI"

/* from S5R4 1.10 subr.c */

#include <sys/systm.h>

/*
 * Copy s2 to s1. s1 must be large enough.
 */
char *
strcpy(s1, s2)
	char *s1;
	const char *s2;
{
	size_t len;

	return (knstrcpy(s1, s2, &len));
}
