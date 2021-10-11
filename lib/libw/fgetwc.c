/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* Copyright (c) 1991, Sun Microsystems, Inc. */

#pragma ident	"@(#)fgetwc.c	1.13	94/04/22 SMI"
/*
 * Fgetwc transforms the next character in EUC from the named input
 * "iop" into the wide character, and returns it as wint_t.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <wchar.h>
#include <widec.h>
#include "mtlibw.h"

#pragma weak fgetwc = _fgetwc
wint_t
#ifdef _REENTRANT
_fgetwc_unlocked(FILE *iop)
#else
_fgetwc(FILE *iop)
#endif /* _REENTRANT */
{
	register c, length;
	register wint_t intcode, mask;

	if ((c = GETC(iop)) == EOF)
		return (WEOF);

	if (isascii(c))		/* ASCII code */
		return ((wint_t)c);

	/* See note in mbtowc.c for description of wchar_t bit assignment. */

	intcode = 0;
	mask = 0;
	if (c == SS2) {
		if ((length = eucw2) == 0)
			goto lab1;
		mask = WCHAR_CS2;
		goto lab2;
	} else if (c == SS3) {
		if ((length = eucw3) == 0)
			goto lab1;
		mask = WCHAR_CS3;
		goto lab2;
	}

lab1:
	if ((c <= UCHAR_MAX) && iscntrl(c))
		return ((wint_t)c);
	length = eucw1 - 1;
	mask = WCHAR_CS1;
	intcode = c & WCHAR_S_MASK;
lab2:
	if (length < 0)		/* codeset 1 is not defined? */
		return ((wint_t)c);
	while (length--) {
		c = GETC(iop);
		if (c == EOF || isascii(c) ||
		    ((c <= UCHAR_MAX) && iscntrl(c))) {
			UNGETC(c, iop);
			errno = EILSEQ;
			return (WEOF); /* Illegal EUC sequence. */
		}
		intcode = (intcode << WCHAR_SHIFT) | (c & WCHAR_S_MASK);
	}
	return ((wint_t)(intcode|mask));
}

#pragma weak getwc = _getwc
wint_t
_getwc(FILE *iop)
{
	return (fgetwc(iop));
}

#ifdef _REENTRANT
wint_t
_fgetwc(FILE *iop)
{
	wint_t result;

	flockfile(iop);
	result = _fgetwc_unlocked(iop);
	funlockfile(iop);
	return (result);
}
#endif /* _REENTRANT */
