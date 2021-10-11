/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)pfmt.c	1.2	93/12/01 SMI"

#include "synonyms.h"
#include <pfmt.h>
#include <stdio.h>
#include <stdarg.h>

/* pfmt() - format and print */

int
#ifdef __STDC__
pfmt(FILE *stream, long flag, const char *format, ...)
#else
pfmt(stream, flag, format, va_alist)
FILE *stream;
long flag;
const char *format;
va_dcl
#endif
{
	va_list args;

#ifdef __STDC__
	va_start(args,);
#else
	va_start(args);
#endif
	return (__pfmt_print(stream, flag, format, NULL, NULL, args));
}
