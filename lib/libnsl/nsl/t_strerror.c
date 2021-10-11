/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ident	"@(#)t_strerror.c	1.1	93/07/13 SMI"	/* SVr4.0 1.2	*/

#pragma weak t_strerror = _t_strerror

/*LINTLIBRARY*/
#include <stdio.h>
#include <stddef.h>
#include <libintl.h>
#include <tiuser.h>

static const char __nsl_dom[]  = "SUNW_OST_NETNSL";

char *
_t_strerror(errnum)
int errnum;
{
	static char buf[BUFSIZ];


	/*
	 *	For the moment we simply index into the t_errlist[] array.
	 *	When the array fills (we cannot allow it to expand in size
	 *	or binary compatibility will be broken), this code will need
	 *	modification.  See the comment in _errlst.c.
	 */
	if (errnum < t_nerr && errnum >= 0)
		return (dgettext(__nsl_dom, t_errlist[errnum]));
	else {
		sprintf(buf, "%d: %s", errnum,
			dgettext(__nsl_dom, "error unknown"));
		return (buf);
	}
}
