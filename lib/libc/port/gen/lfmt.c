/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)lfmt.c	1.2	93/12/01 SMI"

/* lfmt() - format, print and log */

#include "synonyms.h"
#include <pfmt.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <thread.h>
#include "pfmt_data.h"

int
#ifdef __STDC__
lfmt(FILE *stream, long flag, const char *format, ...)
#else
lfmt(stream, flag, format, va_alist)
FILE *stream;
long flag;
const char *format;
va_dcl
#endif
{
	int ret;
	va_list args;
	const char *text, *sev;
#ifdef __STDC__
	va_start(args,);
#else
	va_start(args);
#endif

	if ((ret = __pfmt_print(stream, flag, format, &text, &sev, args)) < 0)
		return (ret);

	ret = __lfmt_log(text, sev, args, flag, ret);

	return (ret);
}
