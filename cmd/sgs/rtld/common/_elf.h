/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */
#ifndef	__ELF_DOT_H
#define	__ELF_DOT_H

#pragma ident	"@(#)_elf.h	1.16	95/03/03 SMI"

#include	<sys/types.h>
#include	<elf.h>
#include	"_rtld.h"

/*
 * Common extern functions for ELF file class.
 */
extern	int		elf_reloc(Rt_map *, int);
extern	void		elf_plt_init(unsigned long *, Rt_map *);
#if	defined(__sparc) || defined(__i386)
extern	void		elf_plt_write(unsigned long *, unsigned long *);
#else
#error Unknown architecture!
#endif

extern	int		elf_set_prot(Rt_map *, int);
extern	Rt_map *	elf_obj_file(Lm_list *, const char *);
extern	Rt_map *	elf_obj_fini(Lm_list *, Rt_map *);
extern	int		elf_copy_reloc(char *, Sym *, Rt_map *, void *, Sym *,
				Rt_map *, const void *);

/*
 * Private data for an ELF file class.
 */
typedef struct _rt_elf_private {
	void *		e_symtab;	/* symbol table */
	unsigned long *	e_hash;		/* hash table */
	char *		e_strtab;	/* string table */
	void *		e_reloc;	/* relocation table */
	unsigned long *	e_pltgot;	/* addrs for procedure linkage table */
	void *		e_dynplt;	/* dynamic plt table - used by prof */
	void *		e_jmprel;	/* plt relocations */
	unsigned long	e_pltrelsize;	/* size of PLT relocation entries */
	unsigned long	e_relsz;	/* size of relocs */
	unsigned long	e_relent;	/* size of base reloc entry */
	unsigned long	e_syment;	/* size of symtab entry */
	int		e_symbolic;	/* compiled Bsymbolic? */
	unsigned long	e_entry;	/* entry point for file */
	void *		e_phdr;		/* program header of object */
	unsigned short	e_phnum;	/* number of segments */
	unsigned short	e_phentsize;	/* size of phdr entry */
	Verneed *	e_verneed;	/* versions needed by this image and */
	int		e_verneednum;	/*	their associated count */
	Verdef *	e_verdef;	/* versions defined by this image and */
	int		e_verdefnum;	/*	their associated count */
} Rt_elfp;

/*
 * Macros for getting to linker ELF private data.
 */
#define	ELFPRV(X)	((X)->rt_priv)
#define	SYMTAB(X)	(((Rt_elfp *)(X)->rt_priv)->e_symtab)
#define	HASH(X)		(((Rt_elfp *)(X)->rt_priv)->e_hash)
#define	STRTAB(X)	(((Rt_elfp *)(X)->rt_priv)->e_strtab)
#define	REL(X)		(((Rt_elfp *)(X)->rt_priv)->e_reloc)
#define	PLTGOT(X)	(((Rt_elfp *)(X)->rt_priv)->e_pltgot)
#define	DYNPLT(X)	(((Rt_elfp *)(X)->rt_priv)->e_dynplt)
#define	JMPREL(X)	(((Rt_elfp *)(X)->rt_priv)->e_jmprel)
#define	PLTRELSZ(X)	(((Rt_elfp *)(X)->rt_priv)->e_pltrelsize)
#define	RELSZ(X)	(((Rt_elfp *)(X)->rt_priv)->e_relsz)
#define	RELENT(X)	(((Rt_elfp *)(X)->rt_priv)->e_relent)
#define	SYMENT(X)	(((Rt_elfp *)(X)->rt_priv)->e_syment)
#define	SYMBOLIC(X)	(((Rt_elfp *)(X)->rt_priv)->e_symbolic)
#define	ENTRY(X)	(((Rt_elfp *)(X)->rt_priv)->e_entry)
#define	PHDR(X)		(((Rt_elfp *)(X)->rt_priv)->e_phdr)
#define	PHNUM(X)	(((Rt_elfp *)(X)->rt_priv)->e_phnum)
#define	PHSZ(X)		(((Rt_elfp *)(X)->rt_priv)->e_phentsize)
#define	VERNEED(X)	(((Rt_elfp *)(X)->rt_priv)->e_verneed)
#define	VERNEEDNUM(X)	(((Rt_elfp *)(X)->rt_priv)->e_verneednum)
#define	VERDEF(X)	(((Rt_elfp *)(X)->rt_priv)->e_verdef)
#define	VERDEFNUM(X)	(((Rt_elfp *)(X)->rt_priv)->e_verdefnum)

#endif
