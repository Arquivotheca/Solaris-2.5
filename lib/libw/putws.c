/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)putws.c	1.10	92/12/16 SMI" 	/* from JAE2.0 1.0 */

/*
 * Putws transforms process codes in wchar_t array pointed to by
 * "ptr" into a byte string in EUC, and writes the string followed
 * by a new-line character to stdout.
 */
/* #include "shlib.h" */
#include <stdio.h>
#include <stdlib.h>
#include <widec.h>

int
putws(const wchar_t *ptr)
{
	register wchar_t *ptr0 = (wchar_t *)ptr;

	flockfile(stdout);
	for ( ; *ptr; ptr++) { /* putwc till NULL */
		if (putwc(*ptr, stdout) == EOF) {
			funlockfile(stdout);
			return(EOF);
		}
	}
	putwc('\n', stdout); /* append a new line */
	funlockfile(stdout);

	if (fflush(stdout))  /* flush line */
		return(EOF);
	return(ptr - ptr0);
}
