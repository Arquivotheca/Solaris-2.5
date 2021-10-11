/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)entry.c	1.8	94/06/21 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

/*
 * Print out a single `entry descriptor' entry.
 */
void
_Dbg_ent_entry(Ent_desc * enp)
{
	Listnode *	lnp;
	char *		cp;

	dbg_print("  ec_name:       %-8s  ec_attrmask:  %s",
		(enp->ec_name ? enp->ec_name : Str_null),
		conv_secflg_str(enp->ec_attrmask));
	dbg_print("  ec_segment:    %-8s  ec_attrbits:  %s",
		(enp->ec_segment->sg_name ? enp->ec_segment->sg_name :
		Str_null), conv_secflg_str(enp->ec_attrbits));
	dbg_print("  ec_ndx:        %-8d  ec_type:      %s",
	    enp->ec_ndx, conv_sectyp_str(enp->ec_type));
	if (enp->ec_files.head) {
		dbg_print("  ec_files:");
		for (LIST_TRAVERSE(&(enp->ec_files), lnp, cp))
			dbg_print("    %s", cp);
	}
}


/*
 * Print out all `entrance descriptor' entries.
 */
void
Dbg_ent_print(List * len, Boolean dmode)
{
	Listnode *	lnp;
	Ent_desc *	enp;
	int		ndx = 1;

	if (DBG_NOTCLASS(DBG_ENTRY))
		return;

	dbg_print(Str_empty);
	dbg_print("%s Entrance Descriptor List (available)",
		(dmode ? (const char *)"Dynamic" : (const char *)"Static"));
	for (LIST_TRAVERSE(len, lnp, enp)) {
		dbg_print("entrance descriptor[%d]", ndx);
		_Dbg_ent_entry(enp);
		ndx++;
	}
}
