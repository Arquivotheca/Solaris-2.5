/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)vprintf.c	1.13	95/08/30 SMI"	/* SVr4.0 1.7.1.4	*/

/*LINTLIBRARY*/
#include "synonyms.h"
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
vprintf(format, ap)
#ifdef __STDC__
const char *format;
#else
char *format;
#endif
va_list ap;
{
	register int count, retval;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT
	/* Use F*LOCKFILE() macros because vprintf() is not async-safe. */
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
	retval = (FERROR(stdout)? EOF: count);
	FUNLOCKFILE(lk);
	return (retval);
}
