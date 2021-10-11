/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)syms.c	1.15	94/08/18 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

/*
 * Print out a single `symbol table node' entry.
 */
void
Elf_sym_table_title(const char * index, const char * name)
{
	dbg_print("%10.10s    value       size     type bind oth "
		"shndx       %s", index, name);
}

void
Elf_sym_table_entry(const char * prestr, Sym * sym, int verndx,
	const char * sec, const char * poststr)
{
	dbg_print("%10.10s  0x%8.8x 0x%8.8x  %4s %4s %-3d %-11.11s %s",
		prestr,
		sym->st_value, sym->st_size,
		conv_info_type_str(ELF_ST_TYPE(sym->st_info)),
		conv_info_bind_str(ELF_ST_BIND(sym->st_info)),
		verndx, sec ? sec : conv_shndx_str(sym->st_shndx),
		poststr);
}

void
Dbg_syms_ar_title(const char * file, int found)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print(Str_empty);
	dbg_print("symbol table processing; input file=%s  "
		"[ archive ] %s", file, found ? Str_again : Str_empty);
}

void
Dbg_syms_ar_entry(int ndx, Elf_Arsym * arsym)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("archive[%d]=%s", ndx, arsym->as_name);
}

void
Dbg_syms_ar_checking(int ndx, Elf_Arsym * arsym, const char * name)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("archive[%d]=%s  (%s) checking for tentative override",
		ndx, arsym->as_name, name);
}

void
Dbg_syms_ar_resolve(int ndx, Elf_Arsym * arsym, const char * name)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("archive[%d]=%s  (%s) resolves undefined or tentative "
		"symbol", ndx, arsym->as_name, name);
}

void
Dbg_syms_spec_title()
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print(Str_empty);
	dbg_print("symbol table processing; building special symbols");
}


void
Dbg_syms_entered(Sym * sym, Sym_desc * sdp)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry(Str_entered, sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    conv_deftag_str(sdp->sd_ref));
}

void
Dbg_syms_process(Ifl_desc * ifl)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print(Str_empty);
	dbg_print("symbol table processing; input file=%s  [ %s ]",
		ifl->ifl_name, conv_etype_str(ifl->ifl_ehdr->e_type));
}

void
Dbg_syms_entry(int ndx, Sym_desc * sdp)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("symbol[%d]=%s", ndx, sdp->sd_name);
}

void
Dbg_syms_global(int ndx, const char * name)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("symbol[%d]=%s  (global); adding", ndx, name);
}

void
Dbg_syms_sec_title()
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(Str_empty);
	dbg_print("symbol table processing; "
		"determining section symbol's index");
}

void
Dbg_syms_sec_entry(int ndx, Sg_desc * sgp, Os_desc * osp)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print("symbol[%d]=%s  (section); segment=%s", ndx, osp->os_name,
		(*sgp->sg_name ? sgp->sg_name : Str_null));
}

void
Dbg_syms_up_title()
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(Str_empty);
	dbg_print("symbol table processing; final update");
	Elf_sym_table_title(Str_empty, Str_empty);
}

void
Dbg_syms_old(Sym_desc *	sdp)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry("old", sdp->sd_sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL, sdp->sd_name);
}

void
Dbg_syms_new(Sym * sym, Sym_desc * sdp)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry("new", sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    conv_deftag_str(sdp->sd_ref));
}

static const char * Str_symup = "symbol=%s; updated";

void
Dbg_syms_updated(Sym_desc * sdp, const char * name)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print(Str_symup, name);

	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry(" ", sdp->sd_sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    conv_deftag_str(sdp->sd_ref));
}

void
Dbg_syms_created(const char * name)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("symbol=%s;  creating", name);
}

void
Dbg_syms_resolving1(int ndx, const char * name, int row, int col)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("symbol[%d]=%s  (global); resolving [%d][%d]",
		ndx, name, row, col);
}

void
Dbg_syms_resolving2(Sym * osym, Sym * nsym, Sym_desc * sdp, Ifl_desc * ifl)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry("old", osym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    sdp->sd_file->ifl_name);
	Elf_sym_table_entry("new", nsym, 0, NULL, ifl->ifl_name);
}

void
Dbg_syms_resolved(Sym_desc * sdp)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry("resolved", sdp->sd_sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    conv_deftag_str(sdp->sd_ref));
}

void
Dbg_syms_nl()
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print(Str_empty);
}

static Boolean	symbol_title = TRUE;

static void
_Dbg_syms_reloc_title()
{
	dbg_print(Str_empty);
	dbg_print("symbol table processing; "
		"assigning to bss (possible copy relocations)");

	symbol_title = FALSE;
}
void
Dbg_syms_reloc(Sym_desc * sdp, Boolean copy)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	if (symbol_title)
		_Dbg_syms_reloc_title();
	dbg_print(Str_symup, sdp->sd_name);

	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry(copy ? (const char *)"copy rel" : Str_empty,
	    sdp->sd_sym, sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    conv_deftag_str(sdp->sd_ref));
}

void
Dbg_syms_lookup_aout(const char * name)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("symbol=%s;  (original AOUT name)", name);
}

void
Dbg_syms_lookup(const char * name, const char * file, const char * type)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print("symbol=%s;  lookup in file=%s  [ %s ]", name, file, type);
}

void
Dbg_syms_dlsym(const char * file, const char * name, int next)
{
	if (DBG_NOTCLASS(DBG_SYMBOLS))
		return;

	dbg_print(Str_empty);
	dbg_print("symbol=%s;  dlsym() starting at file=%s %s", name, file,
		(next ? (const char *)"[ RTLD_NEXT ]" : Str_empty));
}

void
Dbg_syms_reduce(Sym_desc * sdp)
{
	static Boolean	sym_reduce_title = TRUE;

	if (DBG_NOTCLASS(DBG_SYMBOLS | DBG_VERSIONS))
		return;

	if (sym_reduce_title) {
		sym_reduce_title = FALSE;
		dbg_print(Str_empty);
		dbg_print("symbol table processing; reducing global symbols");
	}

	dbg_print("symbol=%s;  reducing", sdp->sd_name);

	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry("local", sdp->sd_sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    sdp->sd_file->ifl_name);
}
