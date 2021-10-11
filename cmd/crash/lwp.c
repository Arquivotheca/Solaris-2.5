#ident	"@(#)lwp.c	1.8	94/10/14 SMI"

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
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/lwp.h>
#include "crash.h"

static void prlwp();

getlwp()
{
	long addr;
	int c;
	struct _kthread threadb;

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
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			prlwp(addr);
		} while (args[optind]);
	} else {
		readmem((unsigned)Curthread, 1, -1, (char *)&threadb,
					sizeof (threadb), "thread entry");
		prlwp(threadb.t_lwp);
	}
	return (0);
}

static void
prlwp(addr)
unsigned addr;
{
	int i;
	struct _klwp lwpb;

	readbuf(addr, 0, 0, -1, (char *)&lwpb, sizeof (lwpb), "lwp entry");
	fprintf(fp, "LWP:\n");
	fprintf(fp, "\toldcontext %x\tap   %x\teosys %d\n",
		lwpb.lwp_oldcontext,
		lwpb.lwp_ap,
		lwpb.lwp_eosys);
	fprintf(fp, "\tlwp_regs   %x\tqsav %x\n", lwpb.lwp_regs, lwpb.lwp_qsav);
	fprintf(fp, "\targs:\t");
	for (i = 0; i < MAXSYSARGS; i++) {
		fprintf(fp, "arg[%d]: %x\t", i, lwpb.lwp_arg[i]);
		if ((i % 3) == 2)
			fprintf(fp, "\n\t\t");
	}
	fprintf(fp,
"\n\tsysabort %d   asleep %d   cursig %d   curflt %d   curinfo  %x\n",
		lwpb.lwp_sysabort,
		lwpb.lwp_asleep,
		lwpb.lwp_cursig,
		lwpb.lwp_curflt,
		lwpb.lwp_curinfo);
	fprintf(fp, "thread %x   procp %x\n",
		lwpb.lwp_thread,
		lwpb.lwp_procp);
}
