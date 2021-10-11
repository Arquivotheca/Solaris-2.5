/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)relocate.c	1.13	94/06/13 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

const char
	* _Fmt_rel6 = "  %3s %-16s %10#x %10#x  %-14.14s %s",
	* _Fmt_rel5 = "  %3s %-16s %10#x             %-14.14s %s";

void
Dbg_reloc_proc(Os_desc * osp, Is_desc * isp)
{
	if (DBG_NOTCLASS(DBG_RELOC))
		return;

	dbg_print(Str_empty);
	dbg_print("collecting input relocations: section=%s, file=%s",
		(osp->os_name ? osp->os_name : Str_null),
		((isp->is_file != NULL) ?
		isp->is_file->ifl_name : Str_null));
	if (DBG_NOTDETAIL())
		return;
	dbg_print(_Fmt_tle1);
}

void
Dbg_reloc_doactiverel()
{
	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(Str_empty);
	dbg_print("performing active relocations");
	dbg_print(_Fmt_tle2);
}

void
Dbg_reloc_doact(Word rtype, Word off, Word value, const char * sym,
    Os_desc * osp)
{
	const char *	sec;

	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;
	if (osp) {
		sec = osp->os_name;
		off += osp->os_shdr->sh_offset;
	} else
		sec = Str_empty;

	dbg_print(_Fmt_rel6, Str_empty,
	    conv_reloc_type_str(rtype), off, value, sec, sym);
}

void
Dbg_reloc_dooutrel()
{
	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(Str_empty);
	dbg_print("creating output relocations");
	dbg_print(_Fmt_tle1);
}

void
Dbg_reloc_apply(Word off, Word value, Os_desc * osp)
{
	const char *	name;

	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	/*
	 * If we've been called from ld(1) then the `osp' is specified.  In this
	 * case add the associated section header offset to the supplied offset,
	 * this allows us to print the relocation address as if it was the true
	 * offset within the output file.  If `osp' is null then we've been
	 * called from ld.so.1 so take the offset as is.  In this case we are
	 * also unaware of the actual section that's being relocated.
	 */
	if (osp) {
		name = osp->os_name ? osp->os_name : Str_empty;
		off += osp->os_shdr->sh_offset;
	} else
		name = Str_empty;

	/*
	 * Print the actual relocation being applied to the specified output
	 * section, the offset represents the actual relocation address, and the
	 * value is the new data being written to that address).
	 */
	dbg_print("\t\t\t      %10#x %10#x  %-14.14s", off, value, name);
}

void
Dbg_reloc_out(Rel * rel, const char * name, Os_desc * osp)
{
	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_reloc_entry(Str_empty, rel, osp->os_relosdesc->os_name, name);
}

/* VARARGS2 */
void
Dbg_reloc_in(Rel * rel, const char * name, Is_desc * isp)
{
	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	Elf_reloc_entry(" in", rel, (isp ? isp->is_name : Str_empty),
	    (name ? name : Str_empty));
}

/*
 * Print a output relocation structure(Rel_desc).
 */
void
Dbg_reloc_ors_entry(Rel_desc * orsp)
{
	const char *	os_name;

	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	if (orsp->rel_flags & FLG_REL_GOT)
		os_name = ".got";
	else if (orsp->rel_flags & FLG_REL_PLT)
		os_name = ".plt";
	else if (orsp->rel_flags & FLG_REL_BSS)
		os_name = ".bss";
	else
		os_name = orsp->rel_osdesc->os_name;

	dbg_print(_Fmt_rel5, (const char *)"out",
	    conv_reloc_type_str(orsp->rel_rtype), orsp->rel_roffset,
	    os_name, orsp->rel_sym->sd_name);
}

/*
 * Print a Active relocation structure (Rel_desc).
 */
void
Dbg_reloc_ars_entry(Rel_desc * arsp)
{
	const char *	os_name;

	if (DBG_NOTCLASS(DBG_RELOC))
		return;
	if (DBG_NOTDETAIL())
		return;

	if (arsp->rel_flags & FLG_REL_GOT)
		os_name = ".got";
	else
		os_name = arsp->rel_osdesc->os_name;

	dbg_print(_Fmt_rel5, (const char *)"act",
	    conv_reloc_type_str(arsp->rel_rtype), arsp->rel_roffset,
	    os_name, arsp->rel_sym->sd_name);
}

void
Dbg_reloc_run(const char * file)
{
	if (DBG_NOTCLASS(DBG_RELOC))
		return;

	dbg_print(Str_empty);
	dbg_print("relocation processing: file=%s", file);

	if (DBG_NOTDETAIL())
		return;
	_Dbg_reloc_run();
}
