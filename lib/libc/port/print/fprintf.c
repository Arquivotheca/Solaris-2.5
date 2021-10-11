/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fprintf.c	1.17	95/08/30 SMI"	/* SVr4.0 1.14	*/

/*LINTLIBRARY*/

/* This function should not be defined weak, but there might be */
/* some program or libraries that may be interposing on this */
#ifdef __STDC__
#pragma weak fprintf = _fprintf
#endif

#include "synonyms.h"
#include "shlib.h"
#include <thread.h>
#include <synch.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <mtlib.h>
#include "print.h"

/*VARARGS2*/
int
#ifdef __STDC__
fprintf(FILE *iop, const char *format, ...)
#else
fprintf(iop, format, va_alist)
FILE *iop;
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
	/* Use F*LOCKFILE() macros because fprintf() is not async-safe. */
	FLOCKFILE(lk, iop);
	if (!(iop->_flag & _IOWRT)) {
		/* if no write flag */
		if (iop->_flag & _IORW) {
			/* if ok, cause read-write */
			iop->_flag |= _IOWRT;
		} else {
			/* else error */
			FUNLOCKFILE(lk);
			errno = EBADF;
			return (EOF);
		}
	}
	count = _doprnt(format, ap, iop);
	va_end(ap);
	retval = (FERROR(iop)? EOF: count);
	FUNLOCKFILE(lk);
	return (retval);
}
