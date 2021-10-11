/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)args.c	1.3	92/09/04 SMI"

/* LINTLIBRARY */

#include	"_debug.h"

void
Dbg_args_flags(int ndx, int c)
{
	if (DBG_NOTCLASS(DBG_ARGS))
		return;

	dbg_print("arg[%d]\tflag=-%c", ndx, c);
}

void
Dbg_args_files(int ndx, char * file)
{
	if (DBG_NOTCLASS(DBG_ARGS))
		return;

	dbg_print("arg[%d]\tfile=%s", ndx, file);
}
