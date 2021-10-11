/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_KSYMS_H
#define	_SYS_KSYMS_H

#pragma ident	"@(#)ksyms.h	1.7	95/09/13 SMI"

#include <sys/types.h>
#include <sys/elf.h>
#include <sys/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * WARNING
 *
 * This file is obsolete and will be removed in a future release
 * of Solaris.  DO NOT include this file in your programs!
 */
#define	KSYMS_IMAGES	10		/* number of ELF images */
#define	KSYMS_NUMCLONES	20		/* number of clone devices */
#define	SHSTRTABLE	"\0.symtab\0.strtab\0.shstrtab\0"

struct ksyms_headers {
	Elf32_Ehdr	elf_hdr;	/* Elf file header */
	Elf32_Phdr	text_phdr;	/* text program header */
	Elf32_Phdr	data_phdr;	/* data program header */
	Elf32_Shdr	null_shdr;	/* first shdr is null */
	Elf32_Shdr	sym_shdr;	/* symtab shdr */
	Elf32_Shdr	str_shdr;	/* strtab shdr */
	Elf32_Shdr	shstr_shdr;	/* shstrtab shdr */
	char		shstr_raw[sizeof (SHSTRTABLE)];
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KSYMS_H */
