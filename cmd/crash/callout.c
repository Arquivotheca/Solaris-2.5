#ident	"@(#)callout.c	1.8	92/12/14 SMI"		/* SVr4.0 1.8 */

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

/*
 * This file contains code for the crash function callout.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/thread.h>
#include <sys/t_lock.h>
#include <sys/callo.h>
#include <sys/elf.h>
#include "crash.h"

/* get arguments for callout function */
int
getcallout()
{
	int c;
	Elf32_Sym *callout_state, *rt_callout_state;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	if (!(callout_state = symsrch("callout_state")))
		error("callout_state not found in symbol table\n");
	if (!(rt_callout_state = symsrch("rt_callout_state")))
		error("rt_callout_state not found in symbol table\n");

	fprintf(fp, "FUNCTION        ARGUMENT        TIME  ID\n");
	if (args[optind]) {
		longjmp(syn, 0);
	} else {
		prcallout(callout_state);
		prcallout(rt_callout_state);
	}
}

int
prcallout(callout_sym)
Elf32_Sym *callout_sym;
{
	callout_state_t cs, *csp;
	callout_t c, *cp;
	int i;
	char *name, *kobj_getsymname();

	csp = (callout_state_t *) callout_sym->st_value;
	kvm_read(kd, (u_long)csp, (char *)&cs, sizeof (cs));
	for (i = 0; i < CALLOUT_BUCKETS; i++) {
		cp = cs.cs_bucket[i].b_first;
		while (cp != (callout_t *)&csp->cs_bucket[i]) {
			if (kvm_read(kd, (u_long)cp, (char *)&c,
			    sizeof (c)) != sizeof (c))
				error("read error on callout table\n");
			if (!(name = kobj_getsymname((u_long)c.c_func)))
				error("%8x does not match in symbol table\n",
					c.c_func);
			else
				fprintf(fp, "%-15s", name);
			fprintf(fp, " %08lx  %10u  %08lx\n",
				c.c_arg,
				c.c_runtime,
				c.c_xid);
			cp = c.c_next;
		}
	}
}
