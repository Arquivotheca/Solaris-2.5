/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)deftag.c	1.1	92/09/03 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for `Sym_desc' types.
 */
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_deftag_str(Symref ref)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	reference[] = {
		"REF_DYN_SEEN",		"REF_DYN_NEED",
		"REF_REL_NEED"
		};

	if (ref > REF_REL_NEED) {
		(void) sprintf(string, format, ref);
		return ((const char *) string);
	} else
		return (reference[ref]);
}
