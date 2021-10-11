/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)strfmon.c	1.3	93/12/09 SMI"

/*LINTLIBRARY*/
#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "print.h"

#define	MAXINT 0x7fffffff

extern int _dostrfmon_unlocked();

/*VARARGS2*/
int
#ifdef __STDC__
strfmon(char *string, int cnt, const char *format, ...)
#else
strfmon(string, int cnt,  format, va_alist)
char *string;
char *format;
va_dcl
#endif
{
	register int count;
	FILE siop;
	va_list ap;

	siop._cnt = MAXINT;
	siop._base = siop._ptr = (unsigned char *)string;
	siop._flag = _IOREAD; /* distinguish dummy file descriptor */
#ifdef __STDC__
	va_start(ap, /* null */);
#else
	va_start(ap);
#endif
	count = _dostrfmon_unlocked(cnt, format, ap, &siop);
	va_end(ap);
	*siop._ptr = '\0'; /* plant terminating null character */
	return (count);
}
