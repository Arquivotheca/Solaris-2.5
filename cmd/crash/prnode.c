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

#ident	"@(#)prnode.c	1.5	94/06/10 SMI"		/* SVr4.0 1.3.4.1 */

/*
 * This file contains code for the crash function:  prnode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <nlist.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/vnode.h>
#include <sys/poll.h>
#include <sys/proc/prdata.h>
#include "crash.h"

static struct nlist Prrootnode;	/* namelist symbol pointers */

static void prprnode();

/* get arguments for prnode function */
int
getprnode()
{
	int slot = -1;
	int full = 0;
	int lock = 0;
	int phys = 0;
	long addr = -1;
	long arg1 = -1;
	long arg2 = -1;
	int c;
	char *heading = "SLOT   PROC       THREAD  MODE   WRITER   FLAGS\n"
	    "      VNEXT  TYPE  OPENS   PHEAD\n";

	if (nl_getsym("prrootnode", &Prrootnode))
		error("prrootnode not found in symbol table\n");
	optind = 1;
	while ((c = getopt(argcnt, args, "eflpw:")) != EOF) {
		switch (c) {
			case 'e' :
					break;
			case 'f' :	full = 1;
					break;
			case 'l' :	lock = 1;
					break;
			case 'p' :	phys = 1;
					break;
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
					break;
		}
	}

	if (!full)
		fprintf(fp, "%s", heading);
	if (args[optind]) {
		do {
			getargs(vbuf.v_proc, &arg1, &arg2, phys);
			if (arg1 == -1)
				continue;
			if (arg2 != -1)
				for (slot = arg1; slot <= arg2; slot++)
					prprnode(full, slot, phys, addr,
						heading, lock);
			else {
				if ((unsigned long)arg1 < vbuf.v_proc)
					slot = arg1;
				else addr = arg1;
				prprnode(full, slot, phys, addr,
					heading, lock);
			}
			slot = addr = arg1 = arg2 = -1;
		} while (args[++optind]);
	} else for (slot = 0; slot < vbuf.v_proc; slot++)
		prprnode(full, slot, phys, addr, heading, lock);
	return (0);
}



/* print prnode */
static void
prprnode(full, slot, phys, addr, heading, lock)
int full, slot, phys;
long addr;
char *heading;
int lock;
{
	struct proc pbuf;
	struct vnode vnbuf;
	struct prnode prnbuf;
	proc_t *procaddr;
	proc_t *slot_to_proc();

	if (addr != -1) {
		readbuf(addr, 0, phys, -1, (char *)&prnbuf, sizeof (prnbuf),
				"prnode");
	} else {
		procaddr = slot_to_proc(slot);
		if (procaddr)
			readbuf((unsigned)procaddr, 0, phys, -1, (char *)&pbuf,
				sizeof (pbuf), "proc table");
		else
			return;
		if (pbuf.p_trace == 0)
			return;
		readmem((unsigned)pbuf.p_trace, 1, -1, (char *)&vnbuf,
			sizeof (vnbuf), "vnode");
		readmem((unsigned)vnbuf.v_data, 1, -1, (char *)&prnbuf,
			sizeof (prnbuf), "prnode");
	}

	if (full)
		fprintf(fp, "%s", heading);
	if (slot == -1)
		fprintf(fp, "  - ");
	else fprintf(fp, "%4d", slot);

	fprintf(fp, "  %8x %8x   %s  %4d ",
		prnbuf.pr_proc, prnbuf.pr_thread,
		(prnbuf.pr_mode & 0600) ? "rw" : "  ",
		prnbuf.pr_writers);

	fprintf(fp, "      %s%s\n",
		prnbuf.pr_flags & PREXCL ? " excl" : "",
		prnbuf.pr_flags & PRINVAL ? " inval" : "");

	fprintf(fp, "  %8x    %d    %d    %8x\n",
		prnbuf.pr_vnext, prnbuf.pr_type,
		prnbuf.pr_opens, prnbuf.pr_pollhead);

	if (!full)
		return;
	/* print vnode info */
	fprintf(fp, "\nVNODE :\n");
	fprintf(fp, "VCNT VFSMNTED   VFSP   STREAMP VTYPE   RDEV VDATA    ");
	fprintf(fp, "   VFILOCKS   VFLAG \n");
	prvnode(&prnbuf.pr_vnode, lock);
	fprintf(fp, "\n");
}
