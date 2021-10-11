/*	Copyright (c) 1984 AT&T	*/
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

#pragma	ident	"@(#)vtop.c	1.7	94/06/10 SMI"	/* SVr4.0 1.5.1.1 */

/*
 * This file contains code for the crash functions:  vtop and mode, as well as
 * the virtual to physical offset conversion routine vtop.
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/vmparam.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <vm/as.h>
#include <sys/elf.h>
#include "crash.h"

/*
 * XXX	DANGER, DANGER Will Robinson
 * XXX	This is an internal interface which belongs to libkvm!
 */
extern u_longlong_t kvm_physaddr(kvm_t *, struct as *, u_int);

static void prvtop(long, int);
static void prmode(char *);

/* virtual to physical offset address translation */
longlong_t
vtop(long vaddr, int slot)
{
	struct proc procbuf;
	struct as asbuf;

	readbuf(-1, (unsigned)slottab[slot].p, 0, -1,
	    (char *)&procbuf, sizeof (procbuf), "proc table");
	if (!procbuf.p_stat)
		return (-1LL);
	if (procbuf.p_as == NULL)
		return (-1LL);
	if (vaddr > KERNELBASE)
		procbuf.p_as = (struct as *)(symsrch("kas")->st_value);
	readmem((unsigned)procbuf.p_as, 1, -1, (char *)&asbuf,
	    sizeof (asbuf), "address space");
	return (kvm_physaddr(kd, &asbuf, vaddr));
}

/* get arguments for vtop function */
int
getvtop(void)
{
	int proc = Procslot;
	Elf32_Sym *sp;
	long addr;
	int c;

	optind = 1;
	while ((c = getopt(argcnt, args, "w:s:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			case 's' :	proc = setproc();
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind]) {
		fprintf(fp, " VIRTUAL  PHYSICAL\n");
		do {
			if (*args[optind] == '(') {
				if ((addr = eval(++args[optind])) == -1)
					continue;
				prvtop(addr, proc);
			} else if (sp = symsrch(args[optind]))
				prvtop((long)sp->st_value, proc);
			else if (isasymbol(args[optind]))
				error("%s not found in symbol table\n",
					args[optind]);
			else {
				if ((addr = strcon(args[optind], 'h')) == -1)
					continue;
				prvtop(addr, proc);
			}
		} while (args[++optind]);
	}
	else
		longjmp(syn, 0);
	return (0);
}

/* print vtop information */
static void
prvtop(long addr, int proc)
{
	longlong_t paddr;

	paddr = vtop(addr, proc);
	if (paddr == -1LL)
		fprintf(fp, "%8x not mapped\n", addr);
	else
		fprintf(fp, "%8x %16llx\n", addr, paddr);
}

/* get arguments for mode function */
int
getmode(void)
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
	if (args[optind])
		prmode(args[optind]);
	else
		prmode("s");
	return (0);
}

/* print mode information */
static void
prmode(char *mode)
{
	switch (*mode) {
		case 'p' :  Virtmode = 0;
			    break;
		case 'v' :  Virtmode = 1;
			    break;
		case 's' :  break;
		default  :  longjmp(syn, 0);
	}
	if (Virtmode)
		fprintf(fp, "Mode = virtual\n");
	else
		fprintf(fp, "Mode = physical\n");
}
