/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)vsnprintf.c	1.1	95/09/12 SMI"	/* SVr4.0 3.30	*/

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
__vsnprintf(string, n, format, ap)
char *string;
#ifdef __STDC__
const char *format;
#else
char *format;
#endif
size_t n;
va_list ap;
{
	register int count;
	FILE siop;

	if (n == 0)
		return (0);

	/* MAXINT is treated as unlimited - n is unsigned, _cnt is signed */
	if (n >= MAXINT)
		siop._cnt = MAXINT;
	else
		siop._cnt = n - 1;
	siop._base = siop._ptr = (unsigned char *)string;
	siop._flag = _IOREAD; /* distinguish dummy file descriptor */
	count = _doprnt(format, ap, &siop);
	*siop._ptr = '\0'; /* plant terminating null character */
	return (count);
}
