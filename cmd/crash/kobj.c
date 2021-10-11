/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */

#ident	"@(#)kobj.c	1.5	94/06/10 SMI"

#include <stdio.h>
#include <sys/elf.h>
#include <libelf.h>
#include <sys/elf_M32.h>
#include "crash.h"

/* Global Symbols */
int kobj_initted;
unsigned int kobj_symbol_info_adr;
unsigned int vdcurmod_adr;

struct kobj_symbol_info {
	struct kobj_symbol_info *next;
	unsigned int base1, len1;
	unsigned int base2, len2;
	unsigned int base3, len3;
	int nsyms;
	int symsize;
	char *symtbl;
	int string_size;
	char *strings;
	int hash_size;
	unsigned int *buckets;
	unsigned int *chains;
	int *byaddr;
	int version;
	int id;
};



static void
init_kobj()
{
	unsigned int val;
	Elf32_Sym *asym;

	kobj_initted = 1;

	/*
	 * the variable kobj_symbol_info should be in the base symbol
	 * table, and it is a pointer to a list of kobj_symbol_info
	 * structures for all the currently loaded modules
	 */
	if ((asym = symsrch("kobj_symbol_info")) == NULL)
		return;
	kobj_symbol_info_adr = asym->st_value;
	if ((asym = symsrch("vdcurmod")) == NULL)
		return;
	vdcurmod_adr = asym->st_value;
}

/* look for a symbol near value. */
char *
kobj_getsymname(value)
	unsigned int value;
{
	static char name[100];
	Elf32_Sym *sym;
	int si;
	struct kobj_symbol_info syminfo;
	int symadr;
	int syms_to_go;
	unsigned int bestval;
	/* this is static just to keep it off the stack */
	static char symbuf[1024];
	int syms_this_time;
	int i;
	int beststr;
	static Elf32_Sym symsave;

	if (kobj_initted == 0)
		init_kobj();

	if (kobj_symbol_info_adr == 0)
		return (0);

	/* first, find the module that covers this address */
	if (kvm_read(kd, kobj_symbol_info_adr, (char *)&si, sizeof (si)) < 0)
		return (0);
	while (si != 0) {
		if (kvm_read(kd, si, (char *)&syminfo, sizeof (syminfo)) < 0)
			return (0);

		if ((syminfo.base1 <= value &&
			value < syminfo.base1 + syminfo.len1) ||
			(syminfo.base2 <= value &&
			value < syminfo.base2 + syminfo.len2) ||
			(syminfo.base3 <= value &&
			value < syminfo.base3 + syminfo.len3))
			break;

		si = (int)syminfo.next;
	}

	if (si == 0)
		return (0);

	/*
	 * now go through the symbol table.  There is some hair here
	 * to do buffering ... I think it will be useful to have,
	 * but I'm only guessing.
	 */
	symadr = (int)syminfo.symtbl;
	syms_to_go = syminfo.nsyms;
	bestval = 0;
	while (syms_to_go > 0) {
		/*
		 * its ok if sizeof symbuf is not a multiple of
		 * syminfo.symsize ... we'll just ignore the
		 * extra bytes at the end
		 */
		if (kvm_read(kd, symadr, symbuf, sizeof (symbuf)) < 0)
			return (0);
		syms_this_time = sizeof (symbuf) / syminfo.symsize;
		if (syms_this_time > syms_to_go)
			syms_this_time = syms_to_go;
		for (i = 0, sym = (Elf32_Sym *)symbuf; i < syms_this_time;
			i++, sym =
				(Elf32_Sym *)((char *)sym + syminfo.symsize)) {
			if (sym->st_name == 0)
				continue;
			if (ELF32_ST_BIND(sym->st_info) == STB_GLOBAL ||
			    (ELF32_ST_BIND(sym->st_info) == STB_LOCAL &&
				(ELF32_ST_TYPE(sym->st_info) == STT_OBJECT ||
				    ELF32_ST_TYPE(sym->st_info) == STT_FUNC))) {
				if (sym->st_value <= value &&
				    sym->st_value > bestval) {
					symsave = *sym;
					bestval = sym->st_value;
					beststr = sym->st_name;
					if (bestval == value)
						break;
				}
			}
		}
		syms_to_go -= syms_this_time;
		symadr += syms_this_time * syminfo.symsize;
	}

	if (bestval == 0)
		return (0);

	if (kvm_read(kd, ((u_long)syminfo.strings)+beststr, name,
	    sizeof (name)) < 0)
		return (0);
	name[sizeof (name) - 1] = 0;
	return (name);
}
