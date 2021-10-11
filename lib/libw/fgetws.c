/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)fgetws.c	1.10	94/04/05 SMI"	/* from JAE2.0 1.0 */

/*
 * Fgetws reads EUC characters from the "iop", converts
 * them to process codes, and places them in the wchar_t
 * array pointed to by "ptr". Fgetws reads until n-1 process
 * codes are transferred to "ptr", or EOF.
 */

/* #include "shlib.h" */
#include <stdlib.h>
#include <stdio.h>
#include <widec.h>

#ifdef _REENTRANT
#define	FGETWC _fgetwc_unlocked
#else
#define	FGETWC fgetwc
#endif /* _REENTRANT */

wchar_t *
fgetws(ptr, size, iop)
register wchar_t *ptr;
register int  size;
register FILE *iop;
{
	register wchar_t *ptr0 = ptr;
	register c;

	flockfile(iop);
	for (size--; size > 0; size--) { /* until size-1 */
		if ((c = FGETWC(iop)) == EOF) {
			if (ptr == ptr0) { /* no data */
				funlockfile(iop);
				return (NULL);
			}
			break; /* no more data */
		}
		*ptr++ = c;
		if (c == '\n')   /* new line character */
			break;
	}
	*ptr = 0;
	funlockfile(iop);
	return (ptr0);
}
