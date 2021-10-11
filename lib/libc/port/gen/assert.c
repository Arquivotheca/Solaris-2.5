/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)assert.c	1.9	92/07/14 SMI"	/* SVr4.0 1.4.1.7	*/

/*LINTLIBRARY*/
/*
 *	called from "assert" macro; prints without printf or stdio.
 */

#ifdef __STDC__
	#pragma weak _assert = __assert
#endif
#include "synonyms.h"
#include "_libc_gettext.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


void
_assert(assertion, filename, line_num)
char *assertion;
char *filename;
int line_num;
{
	char buf[512];

	sprintf(buf,
		_libc_gettext("Assertion failed: %s, file %s, line %d\n"),
		assertion, filename, line_num);
	write(2, buf, strlen(buf));
	(void) abort();
}
