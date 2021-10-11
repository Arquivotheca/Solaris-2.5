/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		Copyright (C) 1986,1991  Sun Microsystems, Inc
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

#ident	"@(#)base.c	1.3	94/06/10 SMI"		/* SVr4.0 1.2 */

/*
 * This file contains code for the crash function base.
 */

#include <stdio.h>
#include "crash.h"

/* get arguments for function base */
int
getbase()
{
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
			prnum(args[optind++]);
		} while (args[optind]);
	} else
		longjmp(syn, 0);
}


/* print results of function base */
static int
prnum(string)
char *string;
{
	int i;
	long num;

	if (*string == '(')
		num = eval(++string);
	else num = strcon(string, NULL);
	if (num == -1)
		return;
	fprintf(fp, "hex: %x\n", num);
	fprintf(fp, "decimal: %d\n", num);
	fprintf(fp, "octal: %o\n", num);
	if (num == 0)
		fprintf(fp, "binary: 0\n");
	else {
		fprintf(fp, "binary: ");
		for (i = 0; num >= 0 && i < 32; i++, num <<= 1)
			;
		for (; i < 32; i++, num <<= 1)
			num < 0 ? fprintf(fp, "1") : fprintf(fp, "0");
		fprintf(fp, "\n");
	}
}
