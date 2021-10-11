#ident	"@(#)vm.c	1.6	94/10/14 SMI"

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * This file contains code for the crash functions related to
 * low-level machine-independent virtual memory.
 */

#include <stdio.h>
#include <sys/types.h>
#include <vm/hat.h>
#include "crash.h"

static void prhment();

gethment()
{
	int c;
	long addr;
	int all = 0;

	optind = 1;
	while ((c = getopt(argcnt, args, "aw:")) != EOF) {
		switch (c) {
			case 'a' :	all = 1;
					break;
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	fprintf(fp, "PAGE      HAT   PROT   NOCONSIST   NCPREF  NOSYNC   ");
	fprintf(fp, "VALID   NEXT\n");

	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind++], 'h')) == -1)
				continue;
			prhment(all, addr);
		} while (args[optind]);
	} else longjmp(syn, 0);
	return (0);
}

static void
prhment(all, addr)
int all;
unsigned long addr;
{
	struct hment hbuf;

	do {
		readmem(addr, 1, -1, (char *)&hbuf, sizeof (hbuf),
			"hment entry");
		fprintf(fp,
"%8-x  %4-d  %2-x     %1x           %1x       %1x        %1x       %8-x\n",
			hbuf.hme_page, hbuf.hme_hat, hbuf.hme_prot,
			hbuf.hme_noconsist, hbuf.hme_ncpref, hbuf.hme_nosync,
			hbuf.hme_valid, hbuf.hme_next);
		addr = (unsigned)hbuf.hme_next;
	} while (all && (addr != NULL));
}
