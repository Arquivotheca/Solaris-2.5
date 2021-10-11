/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)version.c	1.3	94/07/06 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for version flag entries.
 */
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_verflg_str(Half flags)
{
	/*
	 * Presently we only know about a weak flag
	 */
	if (flags & VER_FLG_WEAK)
		return ((const char *)"[ WEAK ]");
	else if (flags & VER_FLG_BASE)
		return ((const char *)"[ BASE ]");
	else
		return ("");
}
