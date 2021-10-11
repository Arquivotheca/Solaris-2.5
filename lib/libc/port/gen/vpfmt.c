/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)vpfmt.c	1.2	93/12/01 SMI"

/* vpfmt() - format and print (variable argument list)
 */
#include "synonyms.h"
#include <pfmt.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <thread.h>
#include "pfmt_data.h"

vpfmt(stream, flag, format, args)
FILE *stream;
long flag;
const char *format;
va_list args;
{
	return (__pfmt_print(stream, flag, format, NULL, NULL, args));
}
