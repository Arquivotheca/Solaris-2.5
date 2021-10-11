/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)data.c	1.2	94/11/09 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for Elf data buffer types.
 */
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_d_type_str(Elf_Type type)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	types[] = {
		"BYTE",		"ADDR",		"DYN",		"EHDR",
		"HALF",		"OFF",		"PHDR",		"RELA",
		"REL",		"SHDR",		"SWORD",	"SYM",
		"WORD",		"VDEF",		"VNEED",	"NUM"
		};

	if (type > ELF_T_NUM) {
		(void) sprintf(string, format, type);
		return ((const char *) string);
	} else
		return (types[type]);
}
