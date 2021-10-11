/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)thread.c	1.5	94/06/10 SMI"		/* SVr4.0 1.2.1.1 */

/*
 * This file contains code for the crash functions:  class, claddr
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/elf.h>
#include <sys/thread.h>
#include <sys/lwp.h>
#include "crash.h"

static unsigned prthread();

Elf32_Sym *Thread;

/* get arguments for class function */
int
getthread()
{
	int all = 0;
	int full = 0;
	int phys = 0;
	int c;
	unsigned addr = -1;
	unsigned oaddr, prthread();
	char *heading =
	    "LINK     FLAG   SCHEDFLAG   STATE  PRI   WCHAN0   WCHAN    INTR"
	    "     CRED\n   TID   LWP       FORW   BACK       PROCP\n";

	optind = 1;
	while ((c = getopt(argcnt, args, "efpw:")) != EOF) {
		switch (c) {
			case 'e' :	all = 1;
					break;
			case 'f' :	full = 1;
					break;
			case 'p' :	phys = 1;
					break;
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	if (!full)
		fprintf(fp, "%s", heading);

	if (args[optind]) {
		do {
			if ((addr = strcon(args[optind], 'h')) == -1)
				continue;
			prthread(addr, phys, all, full, heading);
		} while (args[++optind]);
	} else if (all) {
		if (!Thread)
			if (!(Thread = symsrch("allthreads")))
				error("thread not found in symbol table\n");

		readmem(Thread->st_value, 1, -1, (char *)&addr, sizeof (addr),
				"address of head of thread list");
		oaddr = addr;
		do {
			addr = prthread(addr, phys, 0, full, heading);
		} while (addr != oaddr);
	} else
		prthread(Curthread, 0, all, full, heading);
	return (0);
}

/* print class table  */
static unsigned
prthread(addr, phys, all, full, heading)
unsigned addr;
int phys, all, full;
char *heading;
{
	kthread_t tb;
	unsigned orig;

	orig = addr;
	do {
		if (full)
			fprintf(fp, "\n%s", heading);

		readbuf(addr, 0, phys, -1, (char *)&tb,
						sizeof (tb), "thread entry");

		fprintf(fp,
" %8-x  %4-x   %4-x       %4-x    %4-x  %8-x  %8-x %8-x %8-x\n",
			tb.t_link,
			tb.t_flag,
			tb.t_schedflag,
			tb.t_state,
			tb.t_pri,
			tb.t_wchan0,
			tb.t_wchan,
			tb.t_intr,
			tb.t_cred);
		fprintf(fp, "    %x  %8-x  %8-x  %8-x  %8-x\n",
			tb.t_tid,
			tb.t_lwp,
			tb.t_forw,
			tb.t_next,
			tb.t_procp);
		if (full) {
			fprintf(fp,
"\tPC        SP        CID     CTX       CPU        SIG     HOLD\n");
			fprintf(fp,
"\t%8-x  %8-x  %d       %8-x  %8-x   %x       %x\n",
				tb.t_pc, tb.t_sp, tb.t_cid, tb.t_ctx,
				tb.t_cpu, tb.t_sig, tb.t_hold);
		}
		if (all)
			addr = (unsigned) tb.t_forw;
	} while (all && addr != orig);
	return ((unsigned) tb.t_next);
}

int
getdefthread()
{
	int c;
	int thread = -1;
	int reset = 0;
	int change = 0;

	optind = 1;
	while ((c = getopt(argcnt, args, "cprw:")) != EOF) {
		switch (c) {
			case 'w' :	redirect();
					break;
			case 'c' :	change = 1;
					break;
			case 'r' :	reset = 1;
					break;
			default  :	longjmp(syn, 0);
		}
	}
	if (args[optind])
		if ((thread = strcon(args[optind], 'h')) == -1)
			error("\n");
	prdefthread(thread, change, reset);
	return (0);
}

/* print results of defproc function */
int
prdefthread(thread, change, reset)
unsigned thread, change, reset;
{
	struct _kthread t;
	extern kthread_id_t getcurthread();

	if (change)
		Curthread = getcurthread();
	else if (thread != -1)
		Curthread = (kthread_id_t)thread;
	if (reset) {
		readmem((unsigned)Curthread, 1, -1, (char *)&t,
					sizeof (t), "thread struct");
		Procslot = proc_to_slot((long)t.t_procp);
	}
	fprintf(fp, "Current Thread = %x\n", Curthread);
}
