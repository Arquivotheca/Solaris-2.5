/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)sections.c	1.3	94/02/23 SMI"

/* LINTLIBRARY */

/*
 * String conversion routines for section attributes.
 */
#include	<string.h>
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_sectyp_str(unsigned int type)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	secs[] = {
		"[ SHT_NULL ]",		"[ SHT_PROGBITS ]",
		"[ SHT_SYMTAB ]",	"[ SHT_STRTAB ]",
		"[ SHT_RELA ]",		"[ SHT_HASH ]",
		"[ SHT_DYNAMIC ]",	"[ SHT_NOTE ]",
		"[ SHT_NOBITS ]",	"[ SHT_REL ]",
		"[ SHT_SHLIB ]",	"[ SHT_DYNSYM ]",
		"[ SHT_NUM ]",
		};

	if (type > SHT_NUM) {
		if (type == (unsigned int)SHT_SUNW_verdef)
			return ((const char *)"[ SHT_SUNW_verdef ]");
		else if (type == (unsigned int)SHT_SUNW_verneed)
			return ((const char *)"[ SHT_SUNW_verneed ]");
		else if (type == (unsigned int)SHT_SUNW_versym)
			return ((const char *)"[ SHT_SUNW_versym ]");
		else {
			(void) sprintf(string, format, type);
			return ((const char *) string);
		}
	} else
		return (secs[type]);
}

const char *
conv_secflg_str(unsigned int flags)
{
	static	char	string[40] = { '\0' };

	if (flags == 0)
		return ("0");
	else {
		(void) strcpy(string, "[");
		if (flags & SHF_WRITE)
			(void) strcat(string, " SHF_WRITE ");
		if (flags & SHF_ALLOC)
			(void) strcat(string, " SHF_ALLOC ");
		if (flags & SHF_EXECINSTR)
			(void) strcat(string, " SHF_EXECINSTR ");
		(void) strcat(string, "]");

		return ((const char *) string);
	}
}
