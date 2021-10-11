/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)dynamic.c	1.6	94/10/06 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for .dynamic tag entries.
 */
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_dyntag_str(Sword tag)
{
	static const char *	tags[DT_MAXPOSTAGS] = {
		"NULL",		"NEEDED",	"PLTRELSZ",	"PLTGOT",
		"HASH",		"STRTAB",	"SYMTAB",	"RELA",
		"RELASZ",	"RELAENT",	"STRSZ",	"SYMENT",
		"INIT",		"FINI",		"SONAME",	"RPATH",
		"SYMBOLIC",	"REL",		"RELSZ",	"RELENT",
		"PLTREL",	"DEBUG",	"TEXTREL",	"JMPREL"
	};

	if (tag < DT_MAXPOSTAGS)
		return (tags[tag]);
	else {
		if (tag == DT_USED)
			return ((const char *)"USED");
		else if (tag == DT_FILTER)
			return ((const char *)"FILTER");
		else if (tag == DT_AUXILIARY)
			return ((const char *)"AUXILIARY");
		else if (tag == DT_VERDEF)
			return ((const char *)"VERDEF");
		else if (tag == DT_VERDEFNUM)
			return ((const char *)"VERDEFNUM");
		else if (tag == DT_VERNEED)
			return ((const char *)"VERNEED");
		else if (tag == DT_VERNEEDNUM)
			return ((const char *)"VERNEEDNUM");
		else
			return ((const char *)"(unknown)");
	}
}
