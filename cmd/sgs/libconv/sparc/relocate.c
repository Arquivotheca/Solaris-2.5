/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)relocate.c	1.5	95/03/06 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for relocation types.
 */
#include	<stdio.h>
#include	"_libconv.h"

/*
 * The conv_reloc_type_string() routine is referenced from within the
 * run-time linker.  Because of this reloc_strings is kept in a single
 * large string array instead of a table of strings.  This is done to
 * reduce the number of output relocations that will be generated when
 * building ld.so.1.
 */

static const char *	reloc_strings =
	"R_SPARC_NONE\0"
	"R_SPARC_8\0"
	"R_SPARC_16\0"
	"R_SPARC_32\0"
	"R_SPARC_DISP8\0"
	"R_SPARC_DISP16\0"
	"R_SPARC_DISP32\0"
	"R_SPARC_WDISP30\0"
	"R_SPARC_WDISP22\0"
	"R_SPARC_HI22\0"
	"R_SPARC_22\0"
	"R_SPARC_13\0"
	"R_SPARC_LO10\0"
	"R_SPARC_GOT10\0"
	"R_SPARC_GOT13\0"
	"R_SPARC_GOT22\0"
	"R_SPARC_PC10\0"
	"R_SPARC_PC22\0"
	"R_SPARC_WPLT30\0"
	"R_SPARC_COPY\0"
	"R_SPARC_GLOB_DAT\0"
	"R_SPARC_JMP_SLOT\0"
	"R_SPARC_RELATIVE\0"
	"R_SPARC_UA32\0"
	"R_SPARC_PLT32\0"
	"R_SPARC_HIPLT22\0"
	"R_SPARC_LOPLT10\0"
	"R_SPARC_PCPLT32\0"
	"R_SPARC_PCPLT22\0"
	"R_SPARC_PCPLT10\0"
	"R_SPARC_10\0"
	"R_SPARC_11\0"
	"R_SPARC_64\0"
	"R_SPARC_OLO10\0"
	"R_SPARC_HH22\0"
	"R_SPARC_HM10\0"
	"R_SPARC_LM22\0"
	"R_SPARC_PC_HH22\0"
	"R_SPARC_PC_HM10\0"
	"R_SPARC_PC_LM22\0"
	"R_SPARC_WDISP16\0"
	"R_SPARC_WDISP19\0"
	"R_SPARC_GLOB_JMP\0"
	"R_SPARC_7\0"
	"R_SPARC_5\0"
	"R_SPARC_6\0"
	"\0";

const char *
conv_reloc_type_str(Word rtype)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	rels[R_SPARC_NUM] = { 0 };

	if (rels[0] == 0) {
		char *	ptr = (char *)reloc_strings;
		int	i;

		for (i = 0; i < R_SPARC_NUM; i++) {
			rels[i] = ptr;
			while (*ptr)
				ptr++;
			if (*++ptr == '\0')
				break;
		}
	}

	if (rtype >= R_SPARC_NUM) {
		(void) sprintf(string, format, (int)rtype);
		return ((const char *) string);
	} else
		return (rels[rtype]);
}
