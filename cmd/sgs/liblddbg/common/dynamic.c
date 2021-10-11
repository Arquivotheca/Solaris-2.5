/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)dynamic.c	1.7	94/10/06 SMI"

/* LINTLIBRARY */

#include	<link.h>
#include	<stdio.h>
#include	"_debug.h"

/*
 * Print out the dynamic section entries.
 */
void
Elf_dyn_print(Dyn * dyn, const char * names)
{
	const char *	name;
	char		index[10];
	int		ndx;

	dbg_print("     index  tag           value");

	for (ndx = 1; dyn->d_tag != DT_NULL; ndx++, dyn++) {
		/*
		 * Print the information numerically, and if possible
		 * as a string.
		 */
		if (names && ((dyn->d_tag == DT_NEEDED) ||
		    (dyn->d_tag == DT_SONAME) ||
		    (dyn->d_tag == DT_FILTER) ||
		    (dyn->d_tag == DT_AUXILIARY) ||
		    (dyn->d_tag == DT_RPATH) ||
		    (dyn->d_tag == DT_USED)))
			name = names + dyn->d_un.d_ptr;
		else
			name = Str_empty;

		(void) sprintf(index, " [%d]", ndx);
		dbg_print("%10.10s  %-12.12s  %-10#x    %s", index,
			conv_dyntag_str(dyn->d_tag), dyn->d_un.d_val, name);
	}
}
