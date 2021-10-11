/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)perror.c	1.13	93/07/01 SMI"	/* SVr4.0 1.13	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * Print the error indicated
 * in the cerror cell.
 */
#include "synonyms.h"
#include "_libc_gettext.h"
#include <errno.h>

extern int _sys_num_err, strlen(), write();
#ifdef __STDC__
extern const char _sys_errs[];
extern const int _sys_index[];
#else
extern char _sys_errs[];
extern int _sys_index[];
#endif


void
perror(s)
const char	*s;
{
#ifdef __STDC__
	register const char *c;
#else
	register char *c;
#endif
	register int err = errno;

	if (err < _sys_num_err && err >= 0)
		c = _libc_gettext(&_sys_errs[_sys_index[err]]);
	else
		c = _libc_gettext("Unknown error");

	if (s && *s) {
		(void) write(2, s, strlen(s));
		(void) write(2, ": ", 2);
	}
	(void) write(2, c, strlen(c));
	(void) write(2, "\n", 1);
}
