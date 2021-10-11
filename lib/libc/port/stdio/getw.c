/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getw.c	1.9	92/10/01 SMI"	/* SVr4.0 1.11	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * The intent here is to provide a means to make the order of
 * bytes in an io-stream correspond to the order of the bytes
 * in the memory while doing the io a `word' at a time.
 */
#ifdef __STDC__
	#pragma weak getw = _getw
#endif
#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

/* Read sizeof(int) characters from stream and return these in an int */
int
getw(stream)
	register FILE *stream;
{
	int w;
	register char *s = (char *)&w;
	register int i = sizeof(int);
	int ret;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	FLOCKFILE(lk, stream);
	while (--i >= 0 && !(stream->_flag & (_IOERR | _IOEOF)))
		*s++ = GETC(stream);
	ret = ((stream->_flag & (_IOERR | _IOEOF)) ? EOF : w);
	FUNLOCKFILE(lk);
	return (ret);
}
