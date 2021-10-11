#ident	"@(#)ts.c	1.5	94/06/10 SMI"		/* SVr4.0 1.2.2.3 */

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
 * This file contains code for the crash functions:  tsproc, tsdptbl
 */

#include <stdio.h>
#include <stdlib.h>
#include <nlist.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/ts.h>
#include "crash.h"

static void prtsproc();
static void prtsdptbl();

struct nlist Tsproc, Tsdptbl, Tsmaxumdpri;	/* namelist symbol */
tsdpent_t *tsdptbl;
struct tsproc tsbuf;
struct tsproc *tsp;

/* get arguments for tsproc function */
int
gettsproc()
{
	int c;

	char *tsprochdg = " TMLFT CPUPRI UPRILIM UPRI UMDPRI NICE FLAGS "
	    "DISPWAIT THREADP   NEXT    PREV\n";

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}


	if (nl_getsym("ts_plisthead", &Tsproc))
		error("ts_plisthead not found in symbol table\n");

	readmem((long)Tsproc.n_value, 1, -1, (char *)&tsbuf,
		sizeof (tsbuf), "tsproc table");

	fprintf(fp, "%s", tsprochdg);
	tsp = tsbuf.ts_next;

	for (; tsp != (tsproc_t *)Tsproc.n_value; tsp = tsbuf.ts_next) {
		readmem((long)tsp, 1, -1, (char *)&tsbuf,
			sizeof (tsbuf), "tsproc table");
		prtsproc();
	}
	return (0);
}




/* print the time sharing process table */
static void
prtsproc()
{


	fprintf(fp,
"  %3d    %2d      %2d    %2d    %2d    %2d   0x%2-x    %3d   %.8x %.8x %.8x\n",
		tsbuf.ts_timeleft, tsbuf.ts_cpupri, tsbuf.ts_uprilim,
		tsbuf.ts_upri, tsbuf.ts_umdpri, tsbuf.ts_nice,
		tsbuf.ts_flags, tsbuf.ts_dispwait, tsbuf.ts_tp,
		tsbuf.ts_next, tsbuf.ts_prev);
}


/* get arguments for tsdptbl function */

int
gettsdptbl()
{
	int slot = -1;
	long arg1 = -1;
	long arg2 = -1;
	int c;
	long ts_addr;
	short tsmaxumdpri;

	char *tsdptblhdg = "SLOT     GLOBAL PRIO     TIME QUANTUM     "
	    "TQEXP     SLPRET     MAXWAIT     LWAIT\n\n";

	if (nl_getsym("ts_dptbl", &Tsdptbl))
		error("ts_dptbl not found in symbol table\n");

	if (nl_getsym("ts_maxumdpri", &Tsmaxumdpri))
		error("ts_maxumdpri not found in symbol table\n");

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	readmem((long)Tsmaxumdpri.n_value, 1, -1, (char *)&tsmaxumdpri,
		sizeof (short), "ts_maxumdpri");

	tsdptbl = (tsdpent_t *)malloc((tsmaxumdpri + 1) * sizeof (tsdpent_t));

	readmem((long)Tsdptbl.n_value, 1, -1, (char *)&ts_addr,
			sizeof (ts_addr), "ts_dptbl address");

	readmem(ts_addr, 1, -1, (char *)tsdptbl,
	    (tsmaxumdpri + 1) * sizeof (tsdpent_t), "ts_dptbl");

	fprintf(fp, "%s", tsdptblhdg);

	if (args[optind]) {
		do {
			getargs(tsmaxumdpri + 1, &arg1, &arg2, 0);
			if (arg1 == -1)
				continue;
			if (arg2 != -1)
				for (slot = arg1; slot <= arg2; slot++)
					prtsdptbl(slot);
			else {
				if (arg1 < tsmaxumdpri + 1)
					slot = arg1;
				prtsdptbl(slot);
			}
			slot = arg1 = arg2 = -1;
		} while (args[++optind]);
	} else
		for (slot = 0; slot < tsmaxumdpri + 1; slot++)
			prtsdptbl(slot);

	free(tsdptbl);
	return (0);
}

/* print the time sharing dispatcher parameter table */
static void
prtsdptbl(slot)
int  slot;
{
	fprintf(fp,
"%3d         %4d         %10ld        %3d       %3d        %5d       %3d\n",
	    slot, tsdptbl[slot].ts_globpri, tsdptbl[slot].ts_quantum,
	    tsdptbl[slot].ts_tqexp, tsdptbl[slot].ts_slpret,
	    tsdptbl[slot].ts_maxwait, tsdptbl[slot].ts_lwait);
}
