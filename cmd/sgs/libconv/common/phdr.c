/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)phdr.c	1.1	92/09/03 SMI"

/* LINTLIBRARY */

/*
 * String conversion routines for program header attributes.
 */
#include	<string.h>
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_phdrtyp_str(unsigned type)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	phdrs[] = {
		"[ PT_NULL ]",		"[ PT_LOAD ]",
		"[ PT_DYNAMIC ]",	"[ PT_INTERP ]",
		"[ PT_NOTE ]",		"[ PT_SHLIB ]",
		"[ PT_PHDR ]",		"[ PT_NUM ]"
		};

	if (type > PT_NUM) {
		(void) sprintf(string, format, type);
		return ((const char *) string);
	} else
		return (phdrs[type]);
}

const char *
conv_phdrflg_str(unsigned int flags)
{
	static	char	string[22] = { '\0' };

	if (flags == 0)
		return ("0");
	else {
		(void) strcpy(string, "[");
		if (flags & PF_X)
			(void) strcat(string, " PF_X ");
		if (flags & PF_W)
			(void) strcat(string, " PF_W ");
		if (flags & PF_R)
			(void) strcat(string, " PF_R ");
		(void) strcat(string, "]");

		return ((const char *) string);
	}
}
