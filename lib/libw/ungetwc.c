/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)ungetwc.c	1.13	94/05/02 SMI"	/* from JAE2.0 1.0 */

/*
 * Ungetwc saves the process code c into the one character buffer
 * associated with an input stream "iop". That character, c,
 * will be returned by the next getwc call on that stream.
 */
/* #include "shlib.h" */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <widec.h>
#include <limits.h>

#pragma weak ungetwc=_ungetwc

wint_t
_ungetwc(wint_t wc, FILE *iop)
{
	char mbs[MB_LEN_MAX];
	register unsigned char *p;
	register int n;

	if (wc == WEOF)
		return (WEOF);

	flockfile(iop);
	if ((iop->_flag & _IOREAD) == 0 || iop->_ptr <= iop->_base)
	{
		if (iop->_base != NULL && iop->_ptr == iop->_base &&
		    iop->_cnt == 0)
			++iop->_ptr;
		else {
		  	funlockfile(iop);
			return (WEOF);
		}
	}

	n=_wctomb(mbs, (wchar_t)wc);
	if (n<=0) {
	  	funlockfile(iop);
		return (WEOF);
	}
	p=(unsigned char *)(mbs+n-1); /* p points the last byte */
	while (n--) {
		*--(iop)->_ptr = (*p--);
		++(iop)->_cnt;
	}
	iop->_flag &= (unsigned short)~_IOEOF;
 	funlockfile(iop);
	return (wc);
}
