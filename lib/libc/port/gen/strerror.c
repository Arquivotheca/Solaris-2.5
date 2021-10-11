/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strerror.c	1.10	92/07/14 SMI"	/* SVr4.0 1.4	*/

#pragma weak strerror = _strerror

/*LINTLIBRARY*/
#include "synonyms.h"
#include "_libc_gettext.h"
#include <string.h>
#include <stddef.h>

#ifdef __STDC__
extern const char _sys_errs[];
extern const int _sys_index[];
#else
extern char _sys_errs[];
extern int _sys_index[];
#endif
extern int _sys_num_err;

char *
_strerror(errnum)
int errnum;
{
	if (errnum < _sys_num_err && errnum >= 0)
		return (_libc_gettext((char *)&_sys_errs[_sys_index[errnum]]));
	else
		return (NULL);
}
