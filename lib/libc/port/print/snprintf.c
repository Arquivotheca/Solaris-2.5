/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)snprintf.c	1.1	95/09/12 SMI"	/* SVr4.0 3.30	*/

/*LINTLIBRARY*/
#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <stdarg.h>
#include <values.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "print.h"

extern int _doprnt();

/*VARARGS2*/
int
#ifdef __STDC__
__snprintf(char *string, size_t n, const char *format, ...)
#else
__snprintf(string, n, format, va_alist)
char *string;
char *format;
size_t n;
va_dcl
#endif
{
	register int count;
	FILE siop;
	va_list ap;

	if (n == 0)
		return (0);

	/* MAXINT is treated as unlimited - n is unsigned, _cnt is signed */
	if (n >= MAXINT)
		siop._cnt = MAXINT;
	else
		siop._cnt = n - 1;
	siop._base = siop._ptr = (unsigned char *)string;
	siop._flag = _IOREAD; /* distinguish dummy file descriptor */
#ifdef __STDC__
	va_start(ap, /* null */);
#else
	va_start(ap);
#endif
	count = _doprnt(format, ap, &siop);
	va_end(ap);
	*siop._ptr = '\0'; /* plant terminating null character */
	return (count);
}
