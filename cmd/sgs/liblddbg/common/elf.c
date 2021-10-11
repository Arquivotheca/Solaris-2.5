/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)elf.c	1.7	94/09/17 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

static const char
	* Format_etil =	"            addr type        size   offset al file",
	* Format_emem = "  %3s %#10x %-5s %#10x %#8x %2d %s";


void
Elf_elf_data(const char * str, Addr addr, Elf_Data * data,
	const char * file)
{
	dbg_print(Format_emem, str, addr,
		conv_d_type_str(data->d_type), data->d_size,
		data->d_off, data->d_align, file);
}

void
Elf_elf_data_title()
{
	dbg_print(Format_etil);
}

void
_Dbg_elf_data_in(Os_desc * osp, Is_desc * isp)
{
	Shdr *		shdr = osp->os_shdr;
	Elf_Data *	data = isp->is_indata;

	Elf_elf_data("in", shdr->sh_addr + data->d_off, data,
		(isp->is_file ? isp->is_file->ifl_name : Str_empty));
}

void
_Dbg_elf_data_out(Os_desc * osp)
{
	Shdr *		shdr = osp->os_shdr;
	Elf_Data *	data = osp->os_outdata;

	Elf_elf_data("out", shdr->sh_addr, data, Str_empty);
}

void
Elf_elf_header(Ehdr * ehdr)
{
	Byte *	byte =	&(ehdr->e_ident[0]);

	dbg_print(Str_empty);
	dbg_print("ELF Header");

	dbg_print("  ei_magic:   { 0x%x, %c, %c, %c }",
		byte[EI_MAG0],
		(byte[EI_MAG1] ? byte[EI_MAG1] : '0'),
		(byte[EI_MAG2] ? byte[EI_MAG2] : '0'),
		(byte[EI_MAG3] ? byte[EI_MAG3] : '0'));
	dbg_print("  ei_class:   %-14s  ei_data:      %s",
		conv_eclass_str(ehdr->e_ident[EI_CLASS]),
		conv_edata_str(ehdr->e_ident[EI_DATA]));
	dbg_print("  e_machine:  %-14s  e_version:    %s",
		conv_emach_str(ehdr->e_machine),
		conv_ever_str(ehdr->e_version));
	dbg_print("  e_type:     %s", conv_etype_str(ehdr->e_type));
	dbg_print("  e_flags:    %#14x", ehdr->e_flags);
	dbg_print("  e_entry:    %#14x  e_ehsize:     %2d  "
		"e_shstrndx:   %2d",
		ehdr->e_entry, ehdr->e_ehsize, ehdr->e_shstrndx);
	dbg_print("  e_shoff:    %#14x  e_shentsize:  %2d  "
		"e_shnum:      %2d",
		ehdr->e_shoff, ehdr->e_shentsize, ehdr->e_shnum);
	dbg_print("  e_phoff:    %#14x  e_phentsize:  %2d  "
		"e_phnum:      %2d",
		ehdr->e_phoff, ehdr->e_phentsize, ehdr->e_phnum);
}
