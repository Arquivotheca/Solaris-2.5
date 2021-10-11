/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)printf.c	1.14	95/08/30 SMI"	/* SVr4.0 1.14	*/

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

extern int _doprnt();

/*VARARGS1*/
int
#ifdef __STDC__
printf(const char *format, ...)
#else
printf(format, va_alist)
char *format;
va_dcl
#endif
{
	register int count, retval;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT
	va_list ap;

#ifdef __STDC__
	va_start(ap, /* null */);
#else
	va_start(ap);
#endif
	/* Use F*LOCKFILE() macros because printf() is not async-safe. */
	FLOCKFILE(lk, stdout);
	if (!(stdout->_flag & _IOWRT)) {
		/* if no write flag */
		if (stdout->_flag & _IORW) {
			/* if ok, cause read-write */
			stdout->_flag |= _IOWRT;
		} else {
			/* else error */
			FUNLOCKFILE(lk);
			errno = EBADF;
			return (EOF);
		}
	}
	count = _doprnt(format, ap, stdout);
	va_end(ap);
	retval = (FERROR(stdout)? EOF: count);
	FUNLOCKFILE(lk);
	return (retval);
}
