#ident	"@(#)pcfs.c	1.2	93/05/28 SMI"		/* SVr4.0 1.18.12.1 */

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
 * This file contains code for the crash functions:  pcfs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <nlist.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/vnode.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include "crash.h"

static void prpcfsnodes();

getpcfsnode()
{
	int c;
	long addr;
	char *heading = "FLAGS	VNODE	SIZE	SCLUSTER\n";
	struct pcnode *prpcfsnode();

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	fprintf(fp, "%s\n", heading);
	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			prpcfsnode(addr);
		} while (args[++optind]);
	} else {
		prpcfsnodes("pcfhead");
		prpcfsnodes("pcdhead");
	}
	return (0);
}


#define	NPCHASH	1		/* from fs/pc_node.h */

static void
prpcfsnodes(listname)
char *listname;
{
	struct nlist pc_sym;
	struct pchead pch;
	struct pcnode *pcp;
	struct pcnode *prpcfsnode();

	if (nl_getsym(listname, &pc_sym) == -1) {
		fprintf(fp, "Could not find %s in symbol table\n", listname);
		return;
	}

	readmem(pc_sym.n_value, 1, -1, (char *)&pch, sizeof (pch), listname);

	pcp = pch.pch_forw;
	while (pcp != NULL) {
		pcp = prpcfsnode(pcp);
	}
}

struct pcnode *
prpcfsnode(addr)
unsigned addr;
{
	struct pcnode pc;

	readmem(addr, 1, -1, (char *)&pc, sizeof (pc), "pcfs node");
	fprintf(fp, "%x	%x	%d	%x\n",
		pc.pc_flags, pc.pc_vn, pc.pc_size, pc.pc_scluster);
	return (pc.pc_forw);
}
