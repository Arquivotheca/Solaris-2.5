/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* Copyright (c) 1991, Sun Microsystems, Inc. */

#pragma ident	"@(#)fputwc.c	1.12	94/01/06 SMI"
/*
 * Fputwc transforms the wide character c into the EUC,
 * and writes it onto the output stream "iop".
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <wchar.h>
#include <widec.h>
#include <limits.h>
#include <synch.h>
#include <thread.h>
#include "mtlibw.h"

#pragma weak fputwc = _fputwc
wint_t
_fputwc(wint_t wc, FILE *iop)
{
	char mbs[MB_LEN_MAX];
	register unsigned char *p;
	register int n;

	if (wc==WEOF) return (WEOF);
	n=_wctomb(mbs, (wchar_t)wc);
	if (n<=0) return (WEOF);
	p=(unsigned char *)mbs;
	flockfile(iop);
	while (n--){
		if (PUTC((*p++), iop) == EOF) {
			funlockfile(iop);
		  	return (WEOF);
		}
	}
	funlockfile(iop);
	return (wc);
}

#pragma weak putwc = _putwc
wint_t
_putwc(wint_t wc, FILE *iop)
{
	return(fputwc(wc, iop));
}
