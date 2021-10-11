/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#ident	"@(#)_relocate.c	1.4	94/06/13 SMI"

/* LINTLIBRARY */

#include	"_debug.h"

const char
	* _Fmt_tle1 = "\t\ttype\t\t  offset     addend  section        symbol",
	* _Fmt_tle2 = "\t\ttype\t\t  offset      value  section        symbol";

/*
 * Print a relocation entry. This can either be an input or output
 * relocation record, and specifies the relocation section for which is
 * associated.
 */
void
Elf_reloc_entry(const char * prestr, Rel * rel, const char * sec,
	const char * name)
{
	dbg_print(_Fmt_rel6, prestr,
	    conv_reloc_type_str(ELF_R_TYPE(rel->r_info)), rel->r_offset,
	    rel->r_addend, sec, name);
}

void
_Dbg_reloc_run()
{
	dbg_print("\t\ttype\t\t  offset     addend                 symbol");
	dbg_print("\t\t\t\t              value");
}
