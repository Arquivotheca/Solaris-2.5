/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)vsprintf.c	1.11	93/10/01 SMI"	/* SVr4.0 1.6.1.4	*/

/*LINTLIBRARY*/
#include "synonyms.h"
#include <stdio.h>
#include <stdarg.h>
#include <values.h>
#include <synch.h>
#include <thread.h>
#include <mtlib.h>
#include "print.h"

extern int _doprnt();

/*VARARGS2*/
int
vsprintf(string, format, ap)
char *string;
#ifdef __STDC__
const char *format;
#else
char *format;
#endif
va_list ap;
{
	register int count;
	FILE siop;

	siop._cnt = MAXINT;
	siop._base = siop._ptr = (unsigned char *)string;
	siop._flag = _IOREAD; /* distinguish dummy file descriptor */
	count = _doprnt(format, ap, &siop);
	*siop._ptr = '\0'; /* plant terminating null character */
	return (count);
}
