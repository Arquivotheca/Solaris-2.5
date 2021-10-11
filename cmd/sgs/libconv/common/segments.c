/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)segments.c	1.3	93/01/04 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for segment flags.
 */
#include	<string.h>
#include	<stdio.h>
#include	"_libconv.h"
#include	"libld.h"

const char *
conv_segaflg_str(unsigned int flags)
{
	static	char	string[110] = { '\0' };

	if (flags == 0)
		return ("0");
	else {
		(void) strcpy(string, "[");
		if (flags & FLG_SG_TYPE)
			(void) strcat(string, " FLG_SG_TYPE ");
		if (flags & FLG_SG_VADDR)
			(void) strcat(string, " FLG_SG_VADDR ");
		if (flags & FLG_SG_PADDR)
			(void) strcat(string, " FLG_SG_PADDR ");
		if (flags & FLG_SG_ALIGN)
			(void) strcat(string, " FLG_SG_ALIGN ");
		if (flags & FLG_SG_LENGTH)
			(void) strcat(string, " FLG_SG_LENGTH ");
		if (flags & FLG_SG_FLAGS)
			(void) strcat(string, " FLG_SG_FLAGS ");
		if (flags & FLG_SG_ORDER)
			(void) strcat(string, " FLG_SG_ORDER ");
		(void) strcat(string, "]");

		return ((const char *) string);
	}
}
