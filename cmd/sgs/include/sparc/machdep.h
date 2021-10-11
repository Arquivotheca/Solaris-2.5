/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)machdep.h	1.21	95/03/06 SMI"

/*
 * Global include file for all sgs SPARC machine dependent macros, constants
 * and declarations.
 */
#ifndef	MACHDEP_DOT_H
#define	MACHDEP_DOT_H

#include	<link.h>
#include	<sys/elf_SPARC.h>


#ifndef	_ASM
/*
 * Structure used to build the reloc_table[]
 */
typedef struct {
	unsigned int	re_mask;	/* mask to apply to reloc */
	unsigned char	re_flags;	/* relocation attributes */
	unsigned char	re_fsize;	/* field size (in # bytes) */
	unsigned char	re_bshift;	/* # of bits to shift */
	unsigned char	re_sigbits;	/* # of significant bits */
} reloc_entry;

#endif

/*
 * Elf header information.
 */
#define	M_MACH			EM_SPARC
#define	M_MACHPLUS		EM_SPARC32PLUS
#define	M_CLASS			ELFCLASS32
#define	M_DATA			ELFDATA2MSB
#define	M_FLAGSPLUS_MASK	EF_SPARC_32PLUS_MASK
#define	M_FLAGSPLUS		EF_SPARC_32PLUS

/*
 * Page boundary Macros: truncate to previous page boundary and round to
 * next page boundary (refer to generic macros in ../sgs.h also).
 */
#define	M_PTRUNC(X)	((X) & ~(syspagsz - 1))
#define	M_PROUND(X)	(((X) + syspagsz - 1) & ~(syspagsz - 1))

/*
 * Segment boundary macros: truncate to previous segment boundary and round
 * to next page boundary.
 */
#ifndef	M_SEGSIZE
#define	M_SEGSIZE	ELF_SPARC_MAXPGSZ
#endif
#define	M_STRUNC(X)	((X) & ~(M_SEGSIZE - 1))
#define	M_SROUND(X)	(((X) + M_SEGSIZE - 1) & ~(M_SEGSIZE - 1))


/*
 * Instruction encodings.
 */
#define	M_SAVESP64	0x9de3bfc0	/* save %sp, -64, %sp */
#define	M_CALL		0x40000000
#define	M_JMPL		0x81c06000	/* jmpl %g1 + simm13, %g0 */
#define	M_SETHIG0	0x01000000	/* sethi %hi(val), %g0 */
#define	M_SETHIG1	0x03000000	/* sethi %hi(val), %g1 */
#define	M_NOP		0x01000000	/* sethi 0, %o0 (nop) */
#define	M_BA_A		0x30800000	/* ba,a */
#define	M_MOV07TOG1	0x8210000f	/* mov %o7, %g1 */


/*
 * Plt and Got information; the first few .got and .plt entries are reserved
 *	PLT[0]	jump to dynamic linker
 *	GOT[0]	address of _DYNAMIC
 */
#define	M_PLT_XRTLD	0		/* plt index for jump to rtld */
#define	M_PLT_XNumber	4		/* reserved no. of plt entries */
#define	M_PLT_ENTSIZE	12		/* plt entry size in bytes */
#define	M_PLT_INSSIZE	4		/* single plt instruction size */

#define	M_GOT_XDYNAMIC	0		/* got index for _DYNAMIC */
#define	M_GOT_XNumber	1		/* reserved no. of got entries */
#define	M_GOT_ENTSIZE	4		/* got entry size in bytes */
#define	M_GOT_MAXSMALL	2048		/* maximum no. of small gots */
					/* transition flags for got sizing */
#define	M_GOT_LARGE	(Word)(-M_GOT_MAXSMALL - 1)
#define	M_GOT_SMALL	(Word)(-M_GOT_MAXSMALL - 2)


/*
 * Other machine dependent entities
 */
#define	M_SEGM_ORIGIN	(Addr)0x10000	/* default first segment offset */
#define	M_SEGM_ALIGN	ELF_SPARC_MAXPGSZ
#define	M_WORD_ALIGN	4


/*
 * Make machine class dependent data types transparent to the common code
 */
#define	Word		Elf32_Word
#define	Sword		Elf32_Sword
#define	Half		Elf32_Half
#define	Addr		Elf32_Addr
#define	Off		Elf32_Off
#define	Byte		unsigned char

#define	Ehdr		Elf32_Ehdr
#define	Shdr		Elf32_Shdr
#define	Sym		Elf32_Sym
#define	Rel		Elf32_Rela
#define	Phdr		Elf32_Phdr
#define	Dyn		Elf32_Dyn
#define	Boot		Elf32_Boot
#define	Verdef		Elf32_Verdef
#define	Verdaux		Elf32_Verdaux
#define	Verneed		Elf32_Verneed
#define	Vernaux		Elf32_Vernaux
#define	Versym		Elf32_Versym


/*
 * Make machine class dependent functions transparent to the common code
 */
#define	ELF_R_TYPE	ELF32_R_TYPE
#define	ELF_R_INFO	ELF32_R_INFO
#define	ELF_R_SYM	ELF32_R_SYM
#define	ELF_ST_BIND	ELF32_ST_BIND
#define	ELF_ST_TYPE	ELF32_ST_TYPE
#define	ELF_ST_INFO	ELF32_ST_INFO
#define	elf_fsize	elf32_fsize
#define	elf_getehdr	elf32_getehdr
#define	elf_getphdr	elf32_getphdr
#define	elf_newehdr	elf32_newehdr
#define	elf_newphdr	elf32_newphdr
#define	elf_getshdr	elf32_getshdr


/*
 * Make relocation types transparent to the common code
 */
#define	M_REL_DT_TYPE	DT_RELA		/* .dynamic entry */
#define	M_REL_DT_SIZE	DT_RELASZ	/* .dynamic entry */
#define	M_REL_DT_ENT	DT_RELAENT	/* .dynamic entry */
#define	M_REL_SHT_TYPE	SHT_RELA	/* section header type */
#define	M_REL_ELF_TYPE	ELF_T_RELA	/* data buffer type */
#define	M_REL_PREFIX	".rela"		/* section name prefix */
#define	M_REL_TYPE	"rela"		/* mapfile section type */
#define	M_REL_STAB	".rela.stab"	/* stab name prefix and */
#define	M_REL_STABSZ	10		/* 	its string size */

/*
 * Make common relocation types transparent to the common code
 */
#define	M_R_NONE	R_SPARC_NONE
#define	M_R_GLOB_DAT	R_SPARC_GLOB_DAT
#define	M_R_COPY	R_SPARC_COPY
#define	M_R_RELATIVE	R_SPARC_RELATIVE
#define	M_R_JMP_SLOT	R_SPARC_JMP_SLOT


/*
 * Make plt section information transparent to the common code.
 */
#define	M_PLT_SHF_FLAGS	(SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR)


/*
 * Define a set of identifies for special sections.  These allow the sections
 * to be ordered within the output file image.
 *
 *  o	null identifies indicate that this section does not need to be added
 *	to the output image (ie. shared object sections or sections we're
 *	going to recreate (sym tables, string tables, relocations, etc.));
 *
 *  o	any user defined section will be first in the associated segment.
 *
 *  o	the hash, dynsym, dynstr and rel's are grouped together as these
 *	will all be accessed first by ld.so.1 to perform relocations.
 *
 *  o	the got, dynamic, and plt are grouped together as these may also be
 *	accessed first by ld.so.1 to perform relocations, fill in DT_DEBUG
 *	(executables only), and .plt[0].
 *
 *  o	unknown sections (stabs and all that crap) go at the end.
 *
 * Note that .bss is given the largest identifier.  This insures that if any
 * unknown sections become associated to the same segment as the .bss, the
 * .bss section is always the last section in the segment.
 */
#define	M_ID_NULL	0x00
#define	M_ID_USER	0x01

#define	M_ID_INTERP	0x02			/* SHF_ALLOC */
#define	M_ID_HASH	0x03
#define	M_ID_DYNSYM	0x04
#define	M_ID_DYNSTR	0x05
#define	M_ID_VERSION	0x06
#define	M_ID_REL	0x07
#define	M_ID_TEXT	0x08			/* SHF_ALLOC + SHF_EXECINSTR */
#define	M_ID_DATA	0x09

#define	M_ID_GOT	0x02			/* SHF_ALLOC + SHF_WRITE */
#define	M_ID_DYNAMIC	0x03
#define	M_ID_PLT	0x04
#define	M_ID_BSS	0xff

#define	M_ID_SYMTAB	0x02
#define	M_ID_STRTAB	0x03
#define	M_ID_NOTE	0x04

#define	M_ID_UNKNOWN	0xfe

#endif
