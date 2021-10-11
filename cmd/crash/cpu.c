#ident	"@(#)cpu.c	1.7	94/11/29 SMI"

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
#include <sys/types.h>
#include <sys/cpuvar.h>
#include "crash.h"


getcpu()
{
	unsigned long addr = -1;
	int c;

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
			prcpu(addr);
		} while (args[optind]);
	} else longjmp(syn, 0);
}

prcpu(addr)
unsigned addr;
{
	int i;
	struct cpu cpub;

	readbuf(addr, 0, 0, -1, (char *)&cpub,
					sizeof (cpub), "cpu entry");
	fprintf(fp, "CPU:\n");
	fprintf(fp, "\tid %8-d\tflags %8-x\tthread %8-x\tidle_thread %8-x\n",
		cpub.cpu_id,
		cpub.cpu_flags,
		cpub.cpu_thread,
		cpub.cpu_idle_thread);
	fprintf(fp, "\tlwp %8-x\tcallo %8-x\tfpu %8-x\n",
		cpub.cpu_lwp,
		cpub.cpu_callo,
		cpub.cpu_fpowner);
	fprintf(fp,
		"\trunrun %8-x\tkprunrun %x\tdispthread %8-x\ton_intr %8-d\n",
		cpub.cpu_runrun,
		cpub.cpu_kprunrun,
		cpub.cpu_dispthread,
		cpub.cpu_on_intr);
	fprintf(fp, "\tintr_stack %8-x\tintr_thread %8-x\tintr_actv %8-d\n",
		cpub.cpu_intr_stack,
		cpub.cpu_intr_thread,
		cpub.cpu_intr_actv);
	fprintf(fp, "\tbase_spl %8-d\n",
		cpub.cpu_base_spl);
}
