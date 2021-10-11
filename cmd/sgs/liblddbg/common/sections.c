/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)sections.c	1.8	93/01/04 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

void
Dbg_sec_in(Is_desc * isp)
{
	if (DBG_NOTCLASS(DBG_SECTIONS))
		return;

	dbg_print("section=%s; input from file=%s",
		isp->is_name, ((isp->is_file != NULL) ?
		(char *)(isp->is_file->ifl_name) : Str_null));
}

void
Dbg_sec_added(Os_desc * osp, Sg_desc * sgp)
{
	if (DBG_NOTCLASS(DBG_SECTIONS))
		return;

	dbg_print("section=%s; added to segment=%s",
		osp->os_name, (sgp->sg_name ? (*sgp->sg_name ?
		sgp->sg_name : Str_null) : Str_null));
}

void
Dbg_sec_created(Os_desc * osp, Sg_desc * sgp)
{
	if (DBG_NOTCLASS(DBG_SECTIONS))
		return;

	dbg_print("section=%s; added to segment=%s (created)",
		osp->os_name, (sgp->sg_name ? (*sgp->sg_name ?
		sgp->sg_name : Str_null) : Str_null));
}
