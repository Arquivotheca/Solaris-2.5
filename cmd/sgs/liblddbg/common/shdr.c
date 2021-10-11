/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)shdr.c	1.4	92/09/04 SMI"

/* LINTLIBRARY */

#include	"_debug.h"

/*
 * Print out a single `section header' entry.
 */
void
Elf_shdr_entry(Shdr * shdr)
{
	dbg_print("    sh_addr:      %#-10x  sh_flags:   %s",
		shdr->sh_addr, conv_secflg_str(shdr->sh_flags));
	dbg_print("    sh_size:      %#-10x  sh_type:    %s",
		shdr->sh_size, conv_sectyp_str(shdr->sh_type));
	dbg_print("    sh_offset:    %#-10x  sh_entsize: %#x",
		shdr->sh_offset, shdr->sh_entsize);
	dbg_print("    sh_link:      %#-10d  sh_info:    %d",
		shdr->sh_link, shdr->sh_info);
	dbg_print("    sh_addralign: %#-10x", shdr->sh_addralign);
}
