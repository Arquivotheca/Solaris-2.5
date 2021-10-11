#ident	"@(#)map.c	1.5	93/11/12 SMI"		/* SVr4.0 1.4 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

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
 * This file contains code for the crash function map.
 */

#include <stdio.h>
#include <stdlib.h>
#include <nlist.h>
#include <sys/types.h>
#include <sys/map.h>
#include "crash.h"

static void prmap();

/* get arguments for map function */
int
getmap()
{
	struct nlist nl;
	int c;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind])
		do {
			if (!(nl_getsym(args[optind], &nl))) {
				fprintf(fp, "\n%s:\n", args[optind]);
				prmap(&nl);
			} else fprintf(fp, "%s not found in symbol table\n",
				args[optind]);
		} while (args[++optind]);
	else longjmp(syn, 0);
	return (0);
}

/* print map */
static void
prmap(name)
struct nlist *name;
{
	struct map mbuf;
	struct map_head mh;
	unsigned units = 0, seg = 0;
	char buf[100];
	int addr;

	readmem(name->n_value, 1, -1, (char *)&addr, sizeof (addr),
							"map address");
	readmem(addr, 1, -1, (char *)&mh, sizeof (struct map_head),
						"map table header");

	readmem((unsigned)mh.m_nam, 1, -1, buf, sizeof (buf), "map name");
	fprintf(fp, "MAPNAME: %s\n", buf);
	fprintf(fp, "FREE: %u\tWANT: %u\tSIZE: %u\n",
		mh.m_free,
		mh.m_want,
		mh.m_size);
	fprintf(fp, "m_lock: ");
	prmutex(&mh.m_lock);
	prcondvar(&mh.m_cv, "m_cv");

	fprintf(fp, "\nSIZE    ADDRESS\n");
	mbuf = mh.m_map[0];
	addr += sizeof (struct map_head);
	for (;;) {
		units += mbuf.m_size;
		if (!mbuf.m_size) {
			fprintf(fp, "TOTAL NUMBER OF SEGMENTS %u\n", seg);
			fprintf(fp, "TOTAL SIZE %u\n", units);
			return;
		}
		fprintf(fp, "%4u   %8x\n",
			mbuf.m_size,
			mbuf.m_addr);
		readmem(addr, 1, -1, (char *)&mbuf, sizeof (mbuf), "map entry");
		seg++;
		addr += sizeof (struct map);
	}
}
