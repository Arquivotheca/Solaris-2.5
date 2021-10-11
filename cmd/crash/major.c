#ident	"@(#)major.c	1.3	93/05/28 SMI"		/* SVr4.0 1.3.1.1 */

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
 * This file contains code for the crash function major.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/elf.h>
#include "crash.h"

#define	MAJSIZE	256		/* size of MAJOR table */

Elf32_Sym *Major;		/* namelist symbol pointer */

static void prmajor();

/* get arguments for major function */
int
getmaj()
{
	int slot = -1;
	int c;
	unsigned char majbuf[MAJSIZE];		/* buffer for MAJOR table */

	if (!Major)
		if ((Major = symsrch("MAJOR")) == NULL)
			error("MAJOR not found in symbol table\n");
	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	fprintf(fp, "MAJOR TABLE SIZE = %d\n", MAJSIZE);
	readmem(Major->st_value, 1, -1, (char *)majbuf, MAJSIZE, "MAJOR Table");

	if (args[optind]) {
		do {
			if ((slot = strcon(args[optind++], 'd')) == -1)
				continue;
			if ((slot < 0) || (slot >= MAJSIZE))
				error("%d is out of range\n", slot);
			prmajor(slot, majbuf);
		} while (args[optind]);
	} else prmajor(slot, majbuf);
	return (0);
}

/* print MAJOR table */
static void
prmajor(slot, buf)
int slot;
unsigned char *buf;
{
	int i;

	if (slot == -1) {
		for (i = 0; i < MAJSIZE; i++) {
			if (!(i & 3))
				fprintf(fp, "\n");
			fprintf(fp, "[%3d]: %3d\t", i, buf[i]);
		}
		fprintf(fp, "\n");
	} else fprintf(fp, "[%3d]: %3d\n", slot, buf[slot]);
}
