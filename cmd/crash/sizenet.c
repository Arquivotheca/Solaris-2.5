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

#ident	"@(#)sizenet.c	1.10	95/03/02 SMI"		/* SVr4.0 1.7.4.2 */

/*
 * This file contains code for the crash function size.  The
 * Streams tables and sizes are listed here to allow growth and not
 * overrun the compiler symbol table.
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/poll.h>
#include <sys/systm.h>		/* for rval_t -- sigh! */
#include <sys/stream.h>
#include <sys/strsubr.h>

extern FILE *fp;

struct sizenetable {
	char *name;
	char *symbol;
	unsigned size;
};

struct sizenetable sizntab[] = {
	"datab", "dblock", sizeof (struct datab),
	"dblk", "dblock", sizeof (struct datab),
	"dblock", "dblock", sizeof (struct datab),
	"linkblk", "linkblk", sizeof (struct linkblk),
	"mblk", "mblock", sizeof (struct msgb),
	"mblock", "mblock", sizeof (struct msgb),
	"msgb", "mblock", sizeof (struct msgb),
	"queue", "queue", sizeof (struct queue),
	"stdata", "streams", sizeof (struct stdata),
	"streams", "streams", sizeof (struct stdata),
	NULL, NULL, NULL
};


/* get size from size table */
unsigned
getsizenetab(name)
char *name;
{
	unsigned size = 0;
	struct sizenetable *st;

	for (st = sizntab; st->name; st++) {
		if (!(strcmp(st->name, name))) {
			size = st->size;
			break;
		}
	}
	return (size);
}

/* print size */
int
prsizenet(name)
char *name;
{
	struct sizenetable *st;
	int i;

	if (strcmp("", name) == 0) {
		for (st = sizntab, i = 0; st->name; st++, i++) {
			if (!(i & 3))
				fprintf(fp, "\n");
			fprintf(fp, "%-15s", st->name);
		}
		fprintf(fp, "\n");
	}
}

/* get symbol name and size */
int
getnetsym(name, symbol, size)
char *name;
char *symbol;
unsigned *size;
{
	struct sizenetable *st;

	for (st = sizntab; st->name; st++)
		if (!(strcmp(st->name, name))) {
			strcpy(symbol, st->symbol);
			*size = st->size;
			break;
		}
}
