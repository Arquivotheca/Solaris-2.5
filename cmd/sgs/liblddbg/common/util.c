/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)util.c	1.8	93/07/16 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

/*
 * Print out the input file list.
 */
void
_Dbg_ifl_print(List * lfl)
{
	Listnode *	lnp;
	Ifl_desc *	ifl;
	Half		sec;

	dbg_print("Input File List");
	for (LIST_TRAVERSE(lfl, lnp, ifl)) {
		dbg_print("input file=%s  (%d)", ifl->ifl_name,
			ifl->ifl_ehdr->e_shnum);
		for (sec = 0; sec < ifl->ifl_ehdr->e_shnum; sec++) {
			if (ifl->ifl_isdesc[sec] == (Is_desc *)0)
				dbg_print("  section[%d]=0", sec);
			else
				dbg_print("  section[%d]=%s", sec,
					ifl->ifl_isdesc[sec]->is_name);
		}
	}
}

/*
 * Generic new line generator.
 */
void
Dbg_util_nl()
{
	dbg_print(Str_empty);
}

/*
 * If any run-time linker debugging is being carried out always indicate the
 * fact and specify the point at which we transfer control to the main program.
 */
void
Dbg_util_call_main(const char * name)
{
	dbg_print(Str_empty);
	dbg_print("transferring control: %s", name);
	dbg_print(Str_empty);
}

void
Dbg_util_call_init(const char * name)
{
	dbg_print(Str_empty);
	dbg_print("calling init: %s", name);
	dbg_print(Str_empty);
}

void
Dbg_util_call_fini(const char * name)
{
	dbg_print(Str_empty);
	dbg_print("calling fini: %s", name);
	dbg_print(Str_empty);
}

void
Dbg_util_str(const char * name)
{
	dbg_print(Str_empty);
	dbg_print("%s", name);
	dbg_print(Str_empty);
}
