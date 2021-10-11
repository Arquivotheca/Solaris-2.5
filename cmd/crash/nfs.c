#ident	"@(#)nfs.c	1.4	93/05/28 SMI"

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

#include <stdio.h>
#include <stdlib.h>
#include <nlist.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <sys/t_lock.h>
#include <sys/vnode.h>
#include <nfs/nfs.h>
#include <nfs/rnode.h>
#include "crash.h"

static void prallnfsnodes();
static rnode_t *prnfsnode();

getnfsnode()
{
	int c;
	int lock = 0;
	int full = 0;
	long addr;
	char *heading = "FH       FLAGS    ERROR    SIZE   NEXTR     CRED\n";

	optind = 1;
	while ((c = getopt(argcnt, args, "flw:")) != EOF) {
		switch (c) {
			case 'f' :	full = 1;
					break;
			case 'l' :	lock = 1;
					break;
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	if (!full && !lock)
		fprintf(fp, "%s", heading);
	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			prnfsnode(heading, addr, full, lock);
		} while (args[++optind]);
	} else
		prallnfsnodes(heading, full, lock);
	return (0);
}


#define	RTABLESIZE	64		/* from nfs/nfs_subr.c */

static void
prallnfsnodes(heading, full, lock)
char *heading;
int full, lock;
{
	int i;
	struct nlist rtbl_sym;
	rnode_t *rn, *rtable[RTABLESIZE];

	if (nl_getsym("rtable", &rtbl_sym) == -1) {
		fprintf(fp, "Could not find rtable in symbol table\n");
		return;
	}

	readmem(rtbl_sym.n_value, 1, -1, (char *)rtable, sizeof (rtable),
								"rtable");

	for (i = 0; i < RTABLESIZE; i++) {
		rn = rtable[i];
		while (rn != NULL) {
			rn = prnfsnode(heading, rn, full, lock);
		}
	}
}

static rnode_t *
prnfsnode(heading, addr, full, lock)
char *heading;
unsigned addr;
int full, lock;
{
	rnode_t rnode;

	if (full || lock)
		fprintf(fp, "%s", heading);
	readmem(addr, 1, -1, (char *)&rnode, sizeof (rnode), "nfs remote node");
	fprintf(fp, "%8-x %8-x %8-d %6-d %8-x %8-x\n",
		rnode.r_fh, rnode.r_flags, rnode.r_error,
		rnode.r_size, rnode.r_nextr, rnode.r_cred);
	if (lock) {
		fprintf(fp, "r_rwlock:");
		prrwlock(&rnode.r_rwlock);
		fprintf(fp, "r_statelock:");
		prmutex(&rnode.r_statelock);
		fprintf(fp, "\n");
	}
	if (full) {
		/* print vnode info */
		fprintf(fp, "\nVNODE :\n");
		fprintf(fp,
"VCNT VFSMNTED   VFSP   STREAMP VTYPE   RDEV VDATA    ");
		fprintf(fp, "   VFILOCKS   VFLAG \n");
		prvnode(&rnode.r_vnode, lock);
		fprintf(fp, "\n");
	}
	return (rnode.r_hash);
}
