/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1992, Sun Microsystems Inc.
 */

#ident	"@(#)wstod.c	1.12	94/02/08 SMI"

/*
 * This file is based on /usr/src/lib/libc/sparc/gen/strtod.c and
 * /usr/src/lib/libc/sparc/fp/string_decim.c
 */
/*LINTLIBRARY*/

#include <errno.h>
#include <stdio.h>
#include <values.h>
#include <floatingpoint.h>
#include <stddef.h>
#include <wctype.h>
#include "base_conversion.h"	/* from usr/src/lib/libc/sparc/fp */
#include <locale.h>

static void wstring_to_decimal(const wchar_t **, int, int,
	    decimal_record *, enum decimal_string_form *, const wchar_t **);

#pragma weak wcstod = _wcstod
#pragma weak wstod = _wstod

double
_wcstod(const wchar_t *cp, const wchar_t **ptr)
{
	double		x;
	decimal_mode	mr;
	decimal_record	dr;
	fp_exception_field_type fs;
	enum decimal_string_form form;
	const wchar_t	*pechar;

	wstring_to_decimal(&cp, MAXINT, 0, &dr, &form, &pechar);
	if (ptr != (const wchar_t **) NULL)
		*ptr = cp;
	if (form == invalid_form)
		return (0.0);	/* Shameful kluge for SVID's sake. */
#if defined(i386)
	mr.rd = __xgetRD();
#elif defined(sparc)
	mr.rd = _QgetRD();
#else
#error Unknown architecture!
#endif
	decimal_to_double(&x, &mr, &dr, &fs);
	if (fs & (1 << fp_overflow)) {	/* Overflow. */
		errno = ERANGE;
	}
	if (fs & (1 << fp_underflow)) {	/* underflow */
		errno = ERANGE;
	}
	return (x);
}

double
_wstod(const wchar_t *cp, const wchar_t **ptr)
{
	return (_wcstod(cp, ptr));
}

static void
wstring_to_decimal(
	const wchar_t	**ppc,
	int		nmax,
	int		fortran_conventions,
	decimal_record	*pd,
	enum decimal_string_form *pform,
	const wchar_t	**pechar)

{
	register const wchar_t *cp = *ppc;
	register int    current;
	register int    nread = 1;	/* Number of characters read so far. */
	const wchar_t	*cp0 = cp;
	const wchar_t	*good = cp - 1;	/* End of known good token. */

	current = *cp;

#define	ATEOF 0			/* A string is never at EOF.	 */
#define	CURRENT current
#define	ISSPACE iswspace
#define	NEXT \
	if (nread < nmax) \
		{cp++; current = *cp; nread++; } \
	else \
		{current = NULL; };	/* Increment input character and cp. */

#include "char_to_decimal.h"	/* from usr/src/lib/libc/sparc/fp */
#undef CURRENT
#undef NEXT
}
