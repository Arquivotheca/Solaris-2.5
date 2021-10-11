#ident	"@(#)mutex.c	1.3	93/05/28 SMI"

/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */


/*
 * This file contains code for the crash functions: mutextable.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/elf.h>
#include "crash.h"


static void prmutextable();
static void prmutexentry();

Elf32_Sym *Mutex_Tbl;

getmutextable()
{
	int slot = -1;
	int c;

	if (!Mutex_Tbl)
		if ((Mutex_Tbl = symsrch("mutex_init_table")) == NULL)
			error("mutex_init_table not found in symbol table\n");
	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	if (args[optind]) {
		do {
			if ((slot = strcon(args[optind++], 'd')) == -1)
				continue;
			prmutextable(slot);
		} while (args[optind]);
	} else prmutextable(slot);
	return (0);
}

struct mutex_init_table {
	kmutex_t *addr;
	char	*name;
};

static void
prmutextable(slot)
int slot;
{
	struct mutex_init_table mut;
	int i = 0;

	if (slot != -1) {
		readmem(Mutex_Tbl->st_value + (slot * sizeof (mut)), 1, -1,
			(char *)&mut, sizeof (mut), "mutex_init_table entry");
		if (mut.addr == NULL)
			return;
		else prmutexentry(&mut);
	} else {
		do {
			readmem(Mutex_Tbl->st_value + (i * sizeof (mut)), 1, -1,
					(char *)&mut, sizeof (mut),
					"mutex_init_table entry");
			if (mut.addr == NULL)
				return;
			else prmutexentry(&mut);
			i++;
		} while (1);
	}
}

static void
prmutexentry(mut)
struct mutex_init_table *mut;
{
	kmutex_t mu;
	char buf[256];


		readmem((unsigned)mut->name, 1, -1, buf, sizeof (buf),
						"mutex name");
		fprintf(fp, "mutex name %s\n", buf);
		readmem((unsigned)mut->addr, 1, -1, (char *)&mu,
						sizeof (mu), "mutex entry");
		prmutex(&mu);
}
