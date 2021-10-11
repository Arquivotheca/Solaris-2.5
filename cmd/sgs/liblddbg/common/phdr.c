/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)phdr.c	1.3	92/09/04 SMI"

/* LINTLIBRARY */

#include	"_debug.h"

/*
 * Print out a single `program header' entry.
 */
void
Elf_phdr_entry(Phdr * phdr)
{
	dbg_print("    p_vaddr:      %#-10x  p_flags:    %s",
		phdr->p_vaddr, conv_phdrflg_str(phdr->p_flags));
	dbg_print("    p_paddr:      %#-10x  p_type:     %s",
		phdr->p_paddr, conv_phdrtyp_str(phdr->p_type));
	dbg_print("    p_filesz:     %#-10x  p_memsz:    %#x",
		phdr->p_filesz, phdr->p_memsz);
	dbg_print("    p_offset:     %#-10x  p_align:    %#x",
		phdr->p_offset, phdr->p_align);
}
