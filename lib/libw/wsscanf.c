/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)wsscanf.c	1.8	93/04/20 SMI"

/*LINTLIBRARY*/
#include <stdio.h>
#include <varargs.h>
#include <stdlib.h>
#include <stdlib.h>
#include <widec.h>

/*
 * 	wsscanf -- this function will read wchar_t characters from
 *		    wchar_t string according to the conversion format.
 *		    Note that the performance degrades if the intermediate
 *		    result of conversion exceeds 1024 bytes due to the
 *		    use of malloc() on each call.
 *		    We should implement wchar_t version of doscan()
 *		    for better performance.
 */
#define	MAXINSTR	1024

/*VARARGS2*/
int
wsscanf(va_alist)
va_dcl
{
	va_list		args;
	wchar_t		*string;
	char		*format;
	int		i;
	char		stackbuf[MAXINSTR];
	char		*tempstring=stackbuf;
	int		malloced=0;

	va_start(args);
	string = va_arg(args, wchar_t *);
	format = va_arg(args, char *);
	i=wcstombs(tempstring, string, MAXINSTR);
	if (i<0){
		return (-1);
	}else if (i==MAXINSTR){ /* The buffer was too small.  Malloc it. */
		tempstring=malloc(malloced=MB_CUR_MAX*wslen(string)+1);
		if (tempstring==0) return (-1);
		i=wcstombs(tempstring, string, malloced); /* Try again. */
		if (i<0){
			free (tempstring);
			return (-1);
		}
	}

	i = _vsscanf(tempstring, format, args);
	va_end(args);
	if (malloced) free(tempstring);
	return (i);
}




extern int _doscan();

/*VARARGS2*/
int
_vsscanf(str, fmt, ap)
register char *str;
char *fmt;
va_list ap;
{
	FILE strbuf;

	strbuf._flag = _IOREAD|_IOWRT;
	strbuf._ptr = strbuf._base = (unsigned char*)str;
	strbuf._cnt = strlen(str);
	strbuf._file = _NFILE;
	return (_doscan(&strbuf, fmt, ap));
}
