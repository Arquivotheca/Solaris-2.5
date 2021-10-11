/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)map.c	1.11	94/08/10 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

static const char
	* _Dbg_decl =	NULL,
	* _Dbg_szsd =	"size-symbol declaration (@), symbol=%s; %s";

void
Dbg_map_version(const char * version, const char * name, int scope)
{
	const char *	str = "symbol scope definition ({})";
	const char *	scp;

	if (DBG_NOTCLASS(DBG_MAP | DBG_SYMBOLS))
		return;

	if (scope)
		scp = (const char *)"global";
	else
		scp = (const char *)"local";

	if (version)
		dbg_print("%s, %s; symbol=%s  (%s)", str, version, name, scp);
	else
		dbg_print("%s; symbol=%s  (%s)", str, name, scp);
}

void
Dbg_map_size_new(const char * name)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	dbg_print(_Dbg_szsd, name, (const char *)"adding");
}

void
Dbg_map_size_old(Sym_desc * sdp)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	dbg_print(_Dbg_szsd, sdp->sd_name, (const char *)"updating");

	if (DBG_NOTDETAIL())
		return;

	Elf_sym_table_entry((const char *)"updated", sdp->sd_sym,
	    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
	    conv_deftag_str(sdp->sd_ref));
}

/*
 * Provide for printing mapfile entered symbols when symbol debugging hasn't
 * been enabled.
 */
void
Dbg_map_symbol(Sym_desc * sdp)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;
	if (DBG_NOTDETAIL())
		return;

	if (DBG_NOTCLASS(DBG_SYMBOLS))
		Elf_sym_table_entry(Str_entered, sdp->sd_sym,
		    sdp->sd_aux ? sdp->sd_aux->sa_verndx : 0, NULL,
		    conv_deftag_str(sdp->sd_ref));
}

void
Dbg_map_dash(const char * name, Sdf_desc * sdf)
{
	const char *	str;
	const char *	type;

	if (DBG_NOTCLASS(DBG_MAP))
		return;

	if (sdf->sdf_flags & FLG_SDF_USED)
		type = "USED";
	else
		type = "NEEDED";

	if (sdf->sdf_flags & FLG_SDF_SONAME)
		str = "library control definition (-), %s; %s=%s";
	else
		str = "library control definition (-), %s; %s";

	dbg_print(str, name, type, sdf->sdf_soname);
}

void
Dbg_map_sort_orig(Sg_desc * sgp)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print("map sort_seg_list(): original=%s",
		(sgp->sg_name ? (*sgp->sg_name ? sgp->sg_name : Str_null) :
		Str_null));
}

void
Dbg_map_sort_fini(Sg_desc * sgp)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print("map sort_seg_list(): sorted=%s",
		(sgp->sg_name ? (*sgp->sg_name ? sgp->sg_name : Str_null) :
		Str_null));
}

void
Dbg_map_parse(const char * file)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	dbg_print(Str_empty);
	dbg_print("map file=%s", file);
}

void
Dbg_map_equal(Boolean new)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	if (new)
		_Dbg_decl = "segment declaration (=), segment added:";
	else
		_Dbg_decl = "segment declaration (=), segment updated:";
}

void
Dbg_map_ent(Boolean new, Ent_desc * enp)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	dbg_print("mapping directive (:), entrance criteria added:");
	_Dbg_ent_entry(enp);
	if (new)
		_Dbg_decl = "implicit segment declaration (:), segment added:";
}

void
Dbg_map_atsign(Boolean new)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	if (new)
		_Dbg_decl = "implicit segment declaration (@), segment added:";
	else
		_Dbg_decl = "size-symbol declaration (@), segment updated:";
}

void
Dbg_map_pipe(Sg_desc * sgp, const char * sec_name, const Word ndx)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	dbg_print("map section ordering, segment: %s section: %s index: %d",
	    sgp->sg_name, sec_name, ndx);
}

void
Dbg_map_seg(Sg_desc * sgp)
{
	if (DBG_NOTCLASS(DBG_MAP))
		return;

	if (_Dbg_decl) {
		dbg_print("%s", _Dbg_decl);
		_Dbg_sg_desc_entry(0, sgp);
		dbg_print(Str_empty);
		_Dbg_decl = NULL;
	}
}
