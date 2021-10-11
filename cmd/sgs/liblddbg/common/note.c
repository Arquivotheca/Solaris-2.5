/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)note.c	1.4	92/10/19 SMI"

/* LINTLIBRARY */

#include	"_debug.h"

/*
 * Print out a single `note' entry.
 */
void
Elf_note_entry(long * np)
{
	long	namesz, descsz;
	int	cnt;

	namesz = *np++;
	descsz = *np++;

	dbg_print("    type:         %#-8x", *np++);
	if (namesz) {
		char *	name = (char *)np;

		dbg_print(Str_empty);
		dbg_print("    %s", name);
		name += (namesz + (sizeof (long) - 1)) &
			~(sizeof (long) - 1);
		/* LINTED */
		np = (long *)name;
	}
	if (descsz) {
		for (cnt = 1; descsz; np++, cnt++, descsz -= sizeof (long))
			dbg_print("    desc[%d]:      %#x  (%d)", cnt,
				*np, *np);
	}
}
