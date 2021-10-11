/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)symbols.c	1.1	92/09/03 SMI"

/* LINTLIBRARY */

/*
 * String conversion routines for symbol attributes.
 */
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_info_type_str(unsigned char type)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	infotypes[STT_NUM] = {
		"NOTY",			"OBJT",
		"FUNC",			"SECT",
		"FILE"
		};

	if (type >= STT_NUM) {
		(void) sprintf(string, format, type);
		return ((const char *) string);
	} else
		return (infotypes[type]);
}

const char *
conv_info_bind_str(unsigned char bind)
{
	static char	string[STRSIZE] = { '\0' };
	static const char *	infobinds[STB_NUM] = {
		"LOCL",			"GLOB",
		"WEAK"
		};

	if (bind >= STB_NUM) {
		(void) sprintf(string, format, bind);
		return ((const char *) string);
	} else
		return (infobinds[bind]);
}

const char *
conv_shndx_str(Half shndx)
{
	static	char	string[STRSIZE] = { '\0' };

	if (shndx == SHN_UNDEF)
		return ("UNDEF");
	else if (shndx == SHN_ABS)
		return ("ABS");
	else if (shndx == SHN_COMMON)
		return ("COMMON");
	else {
		(void) sprintf(string, "%d", shndx);
		return ((const char *) string);
	}
}
