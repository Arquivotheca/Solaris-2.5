/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ident	"@(#)wcswidth.c	1.3	94/01/06 SMI"

#include	<stdlib.h>
#include 	<wchar.h>
#include 	<widec.h>
#include	<euc.h>
#include	<ctype.h>

#ifndef WNULL
#define	WNULL	(wchar_t *)0
#endif

#pragma weak wcswidth = _wcswidth
#pragma weak wcwidth = _wcwidth

int
_wcswidth(const wchar_t *pwcs, size_t n)
{
	int	col = 0;

	while (*pwcs && n) {
		if (iswprint(*pwcs) == 0)
			return (-1);
		switch (wcsetno(*pwcs)) {
		case	0:
			col += 1;
			break;
		case	1:
			col += scrw1;
			break;
		case	2:
			col += scrw2;
			break;
		case	3:
			col += scrw3;
			break;
		}
		pwcs++;
		n--;
	}
	return (col);
}

int
_wcwidth(wchar_t wc)
{
	if (wc) {
		if (iswprint(wc) == 0)
			return (-1);
		switch (wcsetno(wc)) {
		case	0:
			return (1);
		case	1:
			return (scrw1);
		case	2:
			return (scrw2);
		case	3:
			return (scrw3);
		}
	}
	return (0);
}
