/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)getws.c	1.10	94/04/05 SMI" 	/* from JAE2.0 1.0 */

/*
 * Getws reads EUC characters from stdin, converts them to process
 * codes, and places them in the array pointed to by "s". Getws
 * reads until a new-line character is read or an EOF.
 */

/* #include "shlib.h" */
#include <stdio.h>
#include <stdlib.h>
#include <widec.h>

#ifdef _REENTRANT
#define	FGETWC _fgetwc_unlocked
#else
#define	FGETWC fgetwc
#endif /* _REENTRANT */

wchar_t *
getws(ptr)
wchar_t *ptr;
{
	wchar_t *ptr0 = ptr;
	register c;

	flockfile(stdin);
	for (;;) {
		if ((c = FGETWC(stdin)) == EOF) {
			if (ptr == ptr0) { /* no data */
				funlockfile(stdin);
				return (NULL);
			}
			break; /* no more data */
		}
		if (c == '\n') /* new line character */
			break;
		*ptr++ = c;
	}
	*ptr = 0;
	funlockfile(stdin);
	return (ptr0);
}
